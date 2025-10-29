#include "ZmqPublisher.h"
#include <iostream>

ZmqPublisher::ZmqPublisher() : ctx_(1), sock_(ctx_, ZMQ_PUB) {}
ZmqPublisher::~ZmqPublisher() { close(); }

bool ZmqPublisher::bind(const std::string &addr) {
    try {
        sock_.bind(addr);
    } catch (const zmq::error_t &e) {
        std::cerr << "ZMQ bind failed: " << e.what() << "\n";
        return false;
    }
    return true;
}

bool ZmqPublisher::publishFrame(const std::string &header_json,
                                const uint8_t *color_buf, size_t color_bytes,
                                const uint8_t *depth_buf, size_t depth_bytes,
                                const std::string &bodies_json,
                                const std::vector<uint8_t> &imu_binary) {
    try {
        // topic
        zmq::message_t topic_msg("frame", 5);
        sock_.send(topic_msg, zmq::send_flags::sndmore);

        // header
        zmq::message_t header_msg(header_json.data(), header_json.size());
        sock_.send(header_msg, zmq::send_flags::sndmore);

        // color
        zmq::message_t color_msg(color_bytes ? (void*)color_buf : nullptr, color_bytes);
        sock_.send(color_msg, zmq::send_flags::sndmore);

        // depth
        zmq::message_t depth_msg(depth_bytes ? (void*)depth_buf : nullptr, depth_bytes);
        sock_.send(depth_msg, zmq::send_flags::sndmore);

        // bodies
        zmq::message_t bodies_msg(bodies_json.data(), bodies_json.size());
        sock_.send(bodies_msg, zmq::send_flags::sndmore);

        // imu
        zmq::message_t imu_msg(imu_binary.data(), imu_binary.size());
        sock_.send(imu_msg, zmq::send_flags::none);
    } catch (const zmq::error_t &e) {
        std::cerr << "ZMQ publish error: " << e.what() << "\n";
        return false;
    }
    return true;
}

void ZmqPublisher::close() {
    try { sock_.close(); } catch(...) {}
}
