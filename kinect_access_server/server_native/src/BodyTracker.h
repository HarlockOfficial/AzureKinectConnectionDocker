#pragma once
#include <k4abt.h>
#include <k4a/k4a.h>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

#include "K4ADevice.h"

class BodyTracker {
public:
    BodyTracker();
    ~BodyTracker();

    bool create(K4ADevice& device, const k4abt_tracker_configuration_t *config = nullptr);
    bool enqueue(k4a_capture_t capture, int32_t timeout_ms = 0);
    // popResult returns a JSON string representing bodies (compact)
    std::string popResult(int32_t timeout_ms = 0);
    void destroy();

private:
    k4abt_tracker_t tracker_;
    bool created_;
    K4ADevice device_;
    k4a_calibration_t calibration_;
};
