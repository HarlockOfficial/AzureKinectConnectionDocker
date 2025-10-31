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
    if (!device.getCalibration(&calibration_)) {
        std::cerr << "BodyTracker: failed to get calibration\n";
        return false;
    }
    if (config) cfg = *config;
    if (K4A_RESULT_SUCCEEDED != k4abt_tracker_create(&calibration_, cfg, &tracker_)) {
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
                // position (x,y,z), orientation (x,y,z,w), confidence in 3D coordinate system relative to the kinect
                auto &p = skeleton.joints[j].position;
                auto &o = skeleton.joints[j].orientation;
                float xp = p.v[0], yp = p.v[1], zp = p.v[2];
                float xo = o.v[0], yo = o.v[1], zo = o.v[2], wo = o.v[3];
                float conf3d= static_cast<float>(skeleton.joints[j].confidence_level);
                joints.push_back(xp); joints.push_back(yp); joints.push_back(zp);
                joints.push_back(xo); joints.push_back(yo); joints.push_back(zo); joints.push_back(wo);
                joints.push_back(conf3d);
                // position (x, y) in image space
                k4a_float2_t p2d;
                int conf2d;
                k4a_result_t result;
                result = k4a_calibration_3d_to_2d(&calibration_, &skeleton.joints[j].position,
                            K4A_CALIBRATION_TYPE_DEPTH, K4A_CALIBRATION_TYPE_COLOR, &p2d, &conf2d);
                if (result == K4A_RESULT_FAILED) {
                    joints.push_back(-1.0f); joints.push_back(-1.0f);
                    joints.push_back(-1.0f);
                } else {
                    joints.push_back(p2d.v[0]); joints.push_back(p2d.v[1]);
                    joints.push_back(static_cast<float>(conf2d));
                }
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
