#pragma once
#include <k4a/k4a.h>
#include <vector>
#include <cstdint>

struct IMUSample {
    uint64_t acc_timestamp_usec;
    uint64_t gyro_timestamp_usec;
    float ax, ay, az;
    float gx, gy, gz;
};

class K4ADevice {
public:
    K4ADevice();
    ~K4ADevice();

    bool open(int index = 0);
    bool startCameras();
    bool startIMU();
    bool getCapture(k4a_capture_t &capture, int timeout_ms = K4A_WAIT_INFINITE);
    // Non-blocking drain of IMU samples
    std::vector<IMUSample> drainIMU();
    void stopCameras();
    void stopIMU();
    void close();
    bool getCalibration(k4a_calibration_t* calib);
private:
    k4a_device_t device_;
    bool imu_started_;
};
