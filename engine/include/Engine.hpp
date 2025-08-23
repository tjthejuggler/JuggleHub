#pragma once

#include "juggler.pb.h"
#include "../src/modules/ModuleBase.hpp"
#include "../src/modules/UdpBallColorModule.hpp"
#include <memory>
#include <queue>
#include <mutex>
#include <string>
#include <atomic>
#include <zmq.hpp>
#include <librealsense2/rs.hpp>

class Engine {
public:
    enum class OutputFormat {
        DEFAULT,
        SIMPLE,
        LEGACY
    };

    Engine(const std::string& config_file, OutputFormat format = OutputFormat::DEFAULT);
    ~Engine();

    void run();
    void stop();

private:
    void processCommands();
    void sendCommand(const juggler::v1::CommandRequest& command);
    std::unique_ptr<ModuleBase> create_module(const juggler::v1::CommandRequest& command);

    OutputFormat output_format_;

    // Thread-safe queue for commands
    std::queue<juggler::v1::CommandRequest> command_queue_;
    std::mutex command_queue_mutex_;

    std::atomic<bool> running_;
    std::unique_ptr<ModuleBase> active_module_;
    std::unique_ptr<UdpBallColorModule> color_module_;

    // ZMQ
    zmq::context_t zmq_context_;
    zmq::socket_t zmq_publisher_;
    zmq::socket_t zmq_commander_;

    // RealSense
    rs2::pipeline pipe_;
    rs2::config rs_config_;
    rs2::align align_to_color_;
};