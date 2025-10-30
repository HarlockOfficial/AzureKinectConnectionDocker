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

using json = nlohmann::json;

static std::atomic<bool> running{true};
static void sigint_handler(int) { running = false; }

#define TIMEOUT_MS 50

int main() {
    signal(SIGINT, sigint_handler);
    signal(SIGTERM, sigint_handler);

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
    bool imu_ok = device.startIMU();

    BodyTracker tracker;
    if (!tracker.create(device)) {
        std::cerr << "Body tracker not created, continuing without it\n";
    }

    ZmqPublisher pub;
    if (!pub.bind("tcp://0.0.0.0:5555")) {
        return -1;
    } else {
        std::cerr << "Publisher bound to tcp://0.0.0.0:5555\n";
    }
    
    uint64_t seq = 0;
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
        if (tracker.create(device)) {
            tracker.enqueue(capture, TIMEOUT_MS);
            bodies_json = tracker.popResult(TIMEOUT_MS);
        }

        // IMU drain
        std::vector<IMUSample> imu = device.drainIMU();
        // pack imu binary: [uint64 ts][6 floats] repeated
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

        pub.publishFrame(header.dump(),
                         color_buf, color_bytes,
                         depth_buf, depth_bytes,
                         bodies_json,
                         imu_bin);

        if (color) k4a_image_release(color);
        if (depth) k4a_image_release(depth);
        if (capture) k4a_capture_release(capture);
    }

    pub.close();
    tracker.destroy();
    device.stopIMU();
    device.stopCameras();
    device.close();
    return 0;
}
