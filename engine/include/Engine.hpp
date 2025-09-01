#pragma once

#include "juggler.pb.h"
#include "../src/modules/ModuleBase.hpp"
#include "../src/modules/UdpBallColorModule.hpp"
#include "../src/modules/UdpBallSettingsModule.hpp"
#include "DNNTracker.hpp" // Include the new DNNTracker
#include <memory>
#include <queue>
#include <mutex>
#include <string>
#include <deque>
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

    Engine(const std::string& config_file, const std::string& device_name = "CPU", OutputFormat format = OutputFormat::DEFAULT, bool use_dnn_tracker = true, bool verbose = false);
    ~Engine();

    void run();
    void stop();

private:
    void processCommands();
    void sendCommand(const juggler::v1::CommandRequest& command);
    void saveRecording();
    std::unique_ptr<ModuleBase> create_module(const juggler::v1::CommandRequest& command);

    OutputFormat output_format_;

    // Thread-safe queue for commands
    std::queue<juggler::v1::CommandRequest> command_queue_;
    std::mutex command_queue_mutex_;

    std::atomic<bool> running_;
    std::unique_ptr<ModuleBase> active_module_;
    std::unique_ptr<UdpBallColorModule> color_module_;
    std::unique_ptr<juggler::modules::UdpBallSettingsModule> settings_module_;
    std::shared_ptr<juggler::BallTracker> ball_tracker_;
    std::shared_ptr<DNNTracker> dnn_tracker_; // Use shared_ptr to pass to other modules
    bool use_dnn_tracker_; // Flag to switch between old/new tracker
    bool verbose_;

    // ZMQ
    zmq::context_t zmq_context_;
    zmq::socket_t zmq_publisher_;
    zmq::socket_t zmq_commander_;

    // RealSense
    rs2::pipeline pipe_;
    rs2::config rs_config_;
    rs2::align align_to_color_;

    // Frame buffer for recording
    std::deque<cv::Mat> frame_buffer_;
    std::mutex frame_buffer_mutex_;
    uint32_t frame_counter_;
};