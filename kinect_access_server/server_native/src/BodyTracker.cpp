#include "BodyTracker.h"
#include <iostream>

#include "K4ADevice.h"

using json = nlohmann::json;

BodyTracker::BodyTracker() : tracker_(nullptr), created_(false) {}

BodyTracker::~BodyTracker() { destroy(); }

bool BodyTracker::create(K4ADevice& device, const k4abt_tracker_configuration_t* config) {
    if (created_) {
        return created_;
    }
    k4abt_tracker_configuration_t cfg = K4ABT_TRACKER_CONFIG_DEFAULT;
    k4a_calibration_t calibration;
    if (!device.getCalibration(&calibration)) {
        std::cerr << "BodyTracker: failed to get calibration\n";
        return false;
    }
    if (config) cfg = *config;
    if (K4A_RESULT_SUCCEEDED != k4abt_tracker_create(&calibration, cfg, &tracker_)) {
        std::cerr << "BodyTracker: failed to create\n";
        return false;
    }
    created_ = true;
    return true;
}

bool BodyTracker::enqueue(k4a_capture_t capture, int32_t timeout_ms) {
    if (!created_) return false;
    k4a_wait_result_t result = k4abt_tracker_enqueue_capture(tracker_, capture, timeout_ms);
    if (K4A_WAIT_RESULT_SUCCEEDED != result) {
        std::cerr << "BodyTracker: enqueue failed: " << result << "\n";
        return false;
    }
    return true;
}

std::string BodyTracker::popResult(int32_t timeout_ms) {
    if (!created_) return "{}";
    k4abt_frame_t body_frame = nullptr;
    if (K4A_WAIT_RESULT_SUCCEEDED != k4abt_tracker_pop_result(tracker_, &body_frame, timeout_ms)) {
        return "{}";
    }
    json out = json::array();
    uint32_t num_bodies = k4abt_frame_get_num_bodies(body_frame);
    for (uint32_t i = 0; i < num_bodies; ++i) {
        k4abt_skeleton_t skeleton;
        if (K4A_RESULT_SUCCEEDED == k4abt_frame_get_body_skeleton(body_frame, i, &skeleton)) {
            json bj;
            bj["id"] = i;
            std::vector<float> joints;
            for (int j = 0; j < K4ABT_JOINT_COUNT; ++j) {
                auto &p = skeleton.joints[j].position;
                float x = p.v[0], y = p.v[1], z = p.v[2];
                float conf = static_cast<float>(skeleton.joints[j].confidence_level);
                joints.push_back(x); joints.push_back(y); joints.push_back(z); joints.push_back(conf);
            }
            bj["joints"] = joints;
            out.push_back(bj);
        }
    }
    k4abt_frame_release(body_frame);
    return out.dump();
}

void BodyTracker::destroy() {
    if (tracker_) {
        k4abt_tracker_destroy(tracker_);
        tracker_ = nullptr;
        created_ = false;
    }
}
