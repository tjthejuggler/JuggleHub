#pragma once

#include "juggler.pb.h"
#include "../src/modules/ModuleBase.hpp"
#include <memory>
#include <string>
#include <atomic>
#include <zmq.hpp>
#include <librealsense2/rs.hpp>

class Engine {
public:
    Engine(const std::string& config_file);
    ~Engine();

    void run();
    void stop();

private:
    void processCommands();

    std::atomic<bool> running_;
    std::unique_ptr<ModuleBase> active_module_;

    // ZMQ
    zmq::context_t zmq_context_;
    zmq::socket_t zmq_publisher_;
    zmq::socket_t zmq_commander_;

    // RealSense
    rs2::pipeline pipe_;
    rs2::config rs_config_;
    rs2::align align_to_color_;
};