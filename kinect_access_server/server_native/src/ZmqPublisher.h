#pragma once
#include <string>
#include <vector>
#include <zmq.hpp>

class ZmqPublisher {
public:
    ZmqPublisher();
    ~ZmqPublisher();
    bool bind(const std::string &addr);
    // Send multipart: topic, header (string), color buffer, depth buffer, bodies (string), imu binary
    bool publishFrame(const std::string &header_json,
                      const uint8_t *color_buf, size_t color_bytes,
                      const uint8_t *depth_buf, size_t depth_bytes,
                      const std::string &bodies_json,
                      const std::vector<uint8_t> &imu_binary);
    void close();
private:
    zmq::context_t ctx_;
    zmq::socket_t sock_;
};
