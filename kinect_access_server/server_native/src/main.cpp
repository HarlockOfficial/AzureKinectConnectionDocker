#include "K4ADevice.h"
#include "BodyTracker.h"
#include "ZmqPublisher.h"
#include "utils.h"
#include "CheckKinect.h"
#include <iostream>
#include <signal.h>
#include <vector>
#include <nlohmann/json.hpp>
#include <atomic>
#include <cstring>
#include <thread>
#include <algorithm>

extern "C" {
    #include <soundio/soundio.h>
}

using json = nlohmann::json;

static std::atomic<bool> running{true};
static void sigint_handler(int) { running = false; }

#define TIMEOUT_MS 50

// Audio settings
static const int AUDIO_SAMPLE_RATE = 48000;
static const int AUDIO_CHANNEL_COUNT = 7;   // Kinect 7-channel mic array
static const int AUDIO_FRAMES_PER_BUFFER = 256;

struct AudioCaptureData {
    ZmqPublisher* pub;
    uint64_t seq;
};

static void audio_read_callback(struct SoundIoInStream* instream,
                                int frame_count_min,
                                int frame_count_max)
{
    AudioCaptureData* acd = (AudioCaptureData*) instream->userdata;
    struct SoundIoChannelArea *areas;
    int err;
    int frame_count = std::min(frame_count_max, AUDIO_FRAMES_PER_BUFFER);
    if (frame_count < frame_count_min) {
        frame_count = frame_count_min;
    }

    if ((err = soundio_instream_begin_read(instream, &areas, &frame_count))) {
        std::cerr << "soundio_instream_begin_read error: " << soundio_strerror(err) << "\n";
        running = false;
        return;
    }

    if (frame_count > 0) {
        // buffer for interleaved float32 samples, one frame has AUDIO_CHANNEL_COUNT channels
        std::vector<float> buffer(frame_count * AUDIO_CHANNEL_COUNT);
        for (int frame = 0; frame < frame_count; ++frame) {
            for (int chan = 0; chan < AUDIO_CHANNEL_COUNT; ++chan) {
                float* ptr = (float*)((uint8_t*)areas[chan].ptr + areas[chan].step * frame);
                buffer[frame * AUDIO_CHANNEL_COUNT + chan] = *ptr;
            }
        }

        // Prepare header for audio
        json header;
        header["seq"] = acd->seq++;
        header["ts_ms"] = now_ms();
        header["audio_sample_rate"] = AUDIO_SAMPLE_RATE;
        header["audio_channels"] = AUDIO_CHANNEL_COUNT;
        header["audio_frames"] = frame_count;

        // Send audio via your ZmqPublisher
        acd->pub->publishAudio(header.dump(), buffer.data(), buffer.size() * sizeof(float));
        // Note: you’ll need publishAudio method in ZmqPublisher (not just publishFrame)
    }

    if ((err = soundio_instream_end_read(instream))) {
        std::cerr << "soundio_instream_end_read error: " << soundio_strerror(err) << "\n";
        running = false;
        return;
    }
}

int main() {
    signal(SIGINT, sigint_handler);
    signal(SIGTERM, sigint_handler);

    // Kinect init
    uint32_t count = k4a_device_get_installed_count();
    std::cerr << "Currently found: " << count << " Kinect devices\n";

    int check_rc = check_kinect();
    if (check_rc != 0) {
        std::cerr << "Kinect device check failed with code: " << check_rc << "\n";
        return check_rc;
    }

    K4ADevice device;
    if (!device.open(0)) return -1;
    if (!device.startCameras()) return -1;
    device.startIMU();

    BodyTracker tracker;
    if (!tracker.create(device)) {
        std::cerr << "Body tracker not created, continuing without it\n";
    }

    ZmqPublisher pub;
    if (!pub.bind("tcp://0.0.0.0:5555")) {
        return -1;
    }

    // Audio capture init
    struct SoundIo *soundio = soundio_create();
    if (!soundio) {
        std::cerr << "Failed to create soundio\n";
        return -1;
    }
    int err;
    if ((err = soundio_connect(soundio))) {
        std::cerr << "soundio_connect error: " << soundio_strerror(err) << "\n";
        soundio_destroy(soundio);
        return -1;
    }
    soundio_flush_events(soundio);

    int device_count = soundio_input_device_count(soundio);
    int input_index = soundio_default_input_device_index(soundio);

    for (int i = 0; i < device_count; ++i) {
        struct SoundIoDevice *dev = soundio_get_input_device(soundio, i);
        // std::cout << "Device " << i << ": " << dev->name
        //           << " (" << dev->probe_error << ")\n";
        // std::cout << "  Channels: " << dev->current_layout.channel_count<< "\n";
        // std::cout << "  Sample rates: ";
        // for (int j = 0; j < dev->sample_rate_count; ++j) {
        //     std::cout << dev->sample_rates[j].min << "-"
        //               << dev->sample_rates[j].max << " Hz, ";
        // }
        // std::cout << "\n";
        if (std::string(dev->name).find("Kinect") != std::string::npos &&
            dev->current_layout.channel_count == AUDIO_CHANNEL_COUNT &&
            std::string(dev->name).find("Default Audio Device") != std::string::npos) {
            input_index = i;
        }
        soundio_device_unref(dev);
    }

    if (input_index < 0) {
        std::cerr << "No input device found\n";
        soundio_destroy(soundio);
        return -1;
    }
    struct SoundIoDevice *device_audio = soundio_get_input_device(soundio, input_index);
    std::cout << "Using audio input device: " << device_audio->name << "\n";

    struct SoundIoInStream *instream = soundio_instream_create(device_audio);
    instream->format = SoundIoFormatFloat32NE;             // use float32
    instream->sample_rate = AUDIO_SAMPLE_RATE;
    instream->layout = device_audio->current_layout;
    instream->software_latency = 0.1;                    // 100 ms

    AudioCaptureData acd{ &pub, 0 };
    instream->userdata = &acd;
    instream->read_callback = audio_read_callback;

    if ((err = soundio_instream_open(instream))) {
        std::cerr << "soundio_instream_open error: " << soundio_strerror(err) << "\n";
        soundio_device_unref(device_audio);
        soundio_destroy(soundio);
        return -1;
    }
    if ((err = soundio_instream_start(instream))) {
        std::cerr << "soundio_instream_start error: " << soundio_strerror(err) << "\n";
        soundio_instream_destroy(instream);
        soundio_device_unref(device_audio);
        soundio_destroy(soundio);
        return -1;
    }

    // Calibration & Kinect header preparation (same as you already have)
    k4a_calibration_t calibration;
    device.getCalibration(&calibration);
    json calibration_json;
    calibration_json["depth_camera_calibration"]["extrinsics"]["rotation"] = calibration.depth_camera_calibration.extrinsics.rotation;
    calibration_json["depth_camera_calibration"]["extrinsics"]["translation"] = calibration.depth_camera_calibration.extrinsics.translation;
    calibration_json["depth_camera_calibration"]["intrinsics"]["type"] = calibration.depth_camera_calibration.intrinsics.type;
    calibration_json["depth_camera_calibration"]["intrinsics"]["parameter_count"] = calibration.depth_camera_calibration.intrinsics.parameter_count;
    calibration_json["depth_camera_calibration"]["intrinsics"]["parameters"] = calibration.depth_camera_calibration.intrinsics.parameters.v;
    calibration_json["depth_camera_calibration"]["resolution_width"] = calibration.depth_camera_calibration.resolution_width;
    calibration_json["depth_camera_calibration"]["resolution_height"] = calibration.depth_camera_calibration.resolution_height;
    calibration_json["depth_camera_calibration"]["metric_radius"] = calibration.depth_camera_calibration.metric_radius;

    calibration_json["color_camera_calibration"]["extrinsics"]["rotation"] = calibration.color_camera_calibration.extrinsics.rotation;
    calibration_json["color_camera_calibration"]["extrinsics"]["translation"] = calibration.color_camera_calibration.extrinsics.translation;
    calibration_json["color_camera_calibration"]["intrinsics"]["type"] = calibration.color_camera_calibration.intrinsics.type;
    calibration_json["color_camera_calibration"]["intrinsics"]["parameter_count"] = calibration.color_camera_calibration.intrinsics.parameter_count;
    calibration_json["color_camera_calibration"]["intrinsics"]["parameters"] = calibration.color_camera_calibration.intrinsics.parameters.v;
    calibration_json["color_camera_calibration"]["resolution_width"] = calibration.color_camera_calibration.resolution_width;
    calibration_json["color_camera_calibration"]["resolution_height"] = calibration.color_camera_calibration.resolution_height;
    calibration_json["color_camera_calibration"]["metric_radius"] = calibration.color_camera_calibration.metric_radius;

    for (int i = 0; i < K4A_CALIBRATION_TYPE_NUM; ++i) {
        for (int j = 0; j < K4A_CALIBRATION_TYPE_NUM; ++j) {
            calibration_json["extrinsics"][i][j]["rotation"] = calibration.extrinsics[i][j].rotation;
            calibration_json["extrinsics"][i][j]["translation"] = calibration.extrinsics[i][j].translation;
        }
    }

    calibration_json["depth_mode"] = calibration.depth_mode;
    calibration_json["color_resolution"] = calibration.color_resolution;


    uint64_t seq = 0;

    // Use a thread for soundio events
    std::thread audio_thread([&]() {
        while (running) {
            soundio_wait_events(soundio);
        }
    });

    // Main loop for Kinect data
    while (running) {
        k4a_capture_t capture = nullptr;
        if (!device.getCapture(capture, TIMEOUT_MS)) {
            continue;
        }

        k4a_image_t color = k4a_capture_get_color_image(capture);
        k4a_image_t depth = k4a_capture_get_depth_image(capture);

        uint8_t *color_buf = nullptr; size_t color_bytes = 0;
        uint8_t *depth_buf = nullptr; size_t depth_bytes = 0;
        int color_w = 0, color_h = 0, depth_w = 0, depth_h = 0;

        if (color) {
            color_buf = k4a_image_get_buffer(color);
            color_bytes = k4a_image_get_size(color);
            color_w = k4a_image_get_width_pixels(color);
            color_h = k4a_image_get_height_pixels(color);
        }
        if (depth) {
            depth_buf = k4a_image_get_buffer(depth);
            depth_bytes = k4a_image_get_size(depth);
            depth_w = k4a_image_get_width_pixels(depth);
            depth_h = k4a_image_get_height_pixels(depth);
        }

        // body tracker enqueue + pop
        std::string bodies_json = "[]";
        tracker.enqueue(capture, TIMEOUT_MS);
        bodies_json = tracker.popResult(TIMEOUT_MS);

        // IMU drain
        std::vector<IMUSample> imu = device.drainIMU();
        std::vector<uint8_t> imu_bin;
        imu_bin.reserve(imu.size() * (2*sizeof(uint64_t) + 6*sizeof(float)));
        for (auto &s : imu) {
            uint64_t ts_a = s.acc_timestamp_usec;
            uint64_t ts_g = s.gyro_timestamp_usec;
            float a[6] = {s.ax, s.ay, s.az, s.gx, s.gy, s.gz};
            size_t old = imu_bin.size();
            imu_bin.resize(old + sizeof(uint64_t) + 6*sizeof(float));
            std::memcpy(imu_bin.data()+old, &ts_a, sizeof(uint64_t));
            std::memcpy(imu_bin.data()+old+sizeof(uint64_t), &ts_g, sizeof(uint64_t));
            std::memcpy(imu_bin.data()+old+2*sizeof(uint64_t), &a, 6*sizeof(float));
        }

        // header
        json header;
        header["seq"] = seq++;
        header["ts_ms"] = now_ms();
        header["color_w"] = color_w; header["color_h"] = color_h; header["color_bytes"] = color_bytes;
        header["depth_w"] = depth_w; header["depth_h"] = depth_h; header["depth_bytes"] = depth_bytes;
        header["imu_count"] = imu.size();
        header["bodies_bytes"] = bodies_json.size();
        header["calibration"] = calibration_json;

        pub.publishFrame(header.dump(),
                         color_buf, color_bytes,
                         depth_buf, depth_bytes,
                         bodies_json,
                         imu_bin);

        if (color) k4a_image_release(color);
        if (depth) k4a_image_release(depth);
        if (capture) k4a_capture_release(capture);
    }

    // Cleanup
    audio_thread.join();
    soundio_instream_destroy(instream);
    soundio_device_unref(device_audio);
    soundio_destroy(soundio);

    pub.close();
    tracker.destroy();
    device.stopIMU();
    device.stopCameras();
    device.close();
    return 0;
}
