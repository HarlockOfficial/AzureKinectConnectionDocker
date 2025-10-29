#include "K4ADevice.h"
#include <iostream>

K4ADevice::K4ADevice() : device_(nullptr), imu_started_(false) {}

K4ADevice::~K4ADevice() {
    close();
}

bool K4ADevice::open(int index) {
    if (K4A_RESULT_SUCCEEDED != k4a_device_open(index, &device_)) {
        std::cerr << "K4A: failed to open device\n";
        return false;
    }
    return true;
}

bool K4ADevice::startCameras() {
    if (!device_) return false;
    k4a_device_configuration_t config = K4A_DEVICE_CONFIG_INIT_DISABLE_ALL;
    config.color_format = K4A_IMAGE_FORMAT_COLOR_BGRA32;
    config.color_resolution = K4A_COLOR_RESOLUTION_720P;
    config.depth_mode = K4A_DEPTH_MODE_NFOV_UNBINNED;
    config.camera_fps = K4A_FRAMES_PER_SECOND_30;
    config.synchronized_images_only = true;
    if (K4A_RESULT_SUCCEEDED != k4a_device_start_cameras(device_, &config)) {
        std::cerr << "K4A: failed to start cameras\n";
        return false;
    }
    return true;
}

bool K4ADevice::startIMU() {
    if (!device_) return false;
    if (K4A_RESULT_SUCCEEDED == k4a_device_start_imu(device_)) {
        imu_started_ = true;
        return true;
    }
    return false;
}

bool K4ADevice::getCapture(k4a_capture_t &capture, int timeout_ms) {
    if (!device_) return false;
    if (k4a_device_get_capture(device_, &capture, timeout_ms) == K4A_WAIT_RESULT_SUCCEEDED) {
        return true;
    }
    return false;
}

std::vector<IMUSample> K4ADevice::drainIMU() {
    std::vector<IMUSample> out;
    if (!device_ || !imu_started_) return out;
    k4a_imu_sample_t sample;
    while (k4a_device_get_imu_sample(device_, &sample, 0) == K4A_WAIT_RESULT_SUCCEEDED) {
        IMUSample s;
        s.acc_timestamp_usec = sample.acc_timestamp_usec;
        s.gyro_timestamp_usec = sample.gyro_timestamp_usec;
        s.ax = sample.acc_sample.xyz.x; s.ay = sample.acc_sample.xyz.y; s.az = sample.acc_sample.xyz.z;
        s.gx = sample.gyro_sample.xyz.x; s.gy = sample.gyro_sample.xyz.y; s.gz = sample.gyro_sample.xyz.z;
        out.push_back(s);
    }
    return out;
}

void K4ADevice::stopCameras() {
    if (device_) k4a_device_stop_cameras(device_);
}

void K4ADevice::stopIMU() {
    if (device_ && imu_started_) {
        k4a_device_stop_imu(device_);
        imu_started_ = false;
    }
}

void K4ADevice::close() {
    if (device_) {
        stopIMU();
        stopCameras();
        k4a_device_close(device_);
        device_ = nullptr;
    }
}

bool K4ADevice::getCalibration(k4a_calibration_t* calib) {
    if (device_) {
        k4a_device_get_calibration(device_, K4A_DEPTH_MODE_NFOV_UNBINNED, K4A_COLOR_RESOLUTION_720P, calib);
        return true;
    }
    return false;
}