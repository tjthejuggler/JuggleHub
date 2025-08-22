#include "Engine.hpp"
#include "BallTracker.hpp"
#include "modules/UdpBallColorModule.hpp"
#include "BallTracker.hpp"
#include <iostream>
#include <thread>
#include <chrono>

Engine::Engine(const std::string& config_file)
    : running_(false),
      zmq_context_(1),
      zmq_publisher_(zmq_context_, ZMQ_PUB),
      zmq_commander_(zmq_context_, ZMQ_REP),
      align_to_color_(RS2_STREAM_COLOR) {
    // Bind ZMQ sockets
    zmq_publisher_.bind("tcp://127.0.0.1:5555");
    zmq_commander_.bind("tcp://127.0.0.1:5565");
}

Engine::~Engine() {
    stop();
}

void Engine::run() {
    running_ = true;

    // Start command processing thread
    std::thread command_thread(&Engine::processCommands, this);

    // Initialize camera
    rs_config_.enable_stream(RS2_STREAM_COLOR, 640, 480, RS2_FORMAT_BGR8, 30);
    rs_config_.enable_stream(RS2_STREAM_DEPTH, 640, 480, RS2_FORMAT_Z16, 30);
    pipe_.start(rs_config_);

    juggler::BallTracker ball_tracker("ball_settings.json");

    while (running_) {
        rs2::frameset frames = pipe_.wait_for_frames();
        auto aligned_frames = align_to_color_.process(frames);
        auto color_frame = aligned_frames.get_color_frame();
        auto depth_frame = aligned_frames.get_depth_frame();

        if (!color_frame || !depth_frame) {
            continue;
        }

        cv::Mat color_image(cv::Size(640, 480), CV_8UC3, (void*)color_frame.get_data(), cv::Mat::AUTO_STEP);
        
        // This is a simplified FrameData creation.
        // In a real application, this would be much more complex.
        juggler::v1::FrameData frame_data;
        frame_data.set_timestamp_us(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

        // Update active module
        if (active_module_) {
            active_module_->update(frame_data);
        }

        // Publish FrameData
        std::string serialized_data;
        frame_data.SerializeToString(&serialized_data);
        zmq::message_t message(serialized_data.size());
        memcpy(message.data(), serialized_data.c_str(), serialized_data.size());
        zmq_publisher_.send(message, zmq::send_flags::dontwait);
    }

    command_thread.join();
}

void Engine::stop() {
    running_ = false;
}

void Engine::processCommands() {
    while (running_) {
        zmq::message_t request;
        auto result = zmq_commander_.recv(request, zmq::recv_flags::dontwait);
        if (!result) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        juggler::v1::CommandRequest command;
        command.ParseFromArray(request.data(), request.size());

        std::cout << "Received command: " << command.type() << std::endl;

        juggler::v1::CommandResponse response;
        response.set_success(true);

        switch (command.type()) {
            case juggler::v1::CommandRequest::LOAD_MODULE:
                if (command.module_name() == "UdpBallColorModule") {
                    active_module_ = std::make_unique<UdpBallColorModule>();
                    active_module_->setup();
                    response.set_message("UdpBallColorModule loaded");
                } else {
                    response.set_success(false);
                    response.set_message("Unknown module");
                }
                break;
            case juggler::v1::CommandRequest::UNLOAD_MODULE:
                if (active_module_) {
                    active_module_->cleanup();
                    active_module_.reset();
                    response.set_message("Module unloaded");
                } else {
                    response.set_success(false);
                    response.set_message("No active module");
                }
                break;
            case juggler::v1::CommandRequest::SEND_COLOR_COMMAND:
                if (active_module_) {
                    active_module_->processCommand(command);
                    response.set_message("Color command sent");
                } else {
                    response.set_success(false);
                    response.set_message("No active module");
                }
                break;
            default:
                response.set_success(false);
                response.set_message("Unknown command");
                break;
        }

        std::string serialized_response;
        response.SerializeToString(&serialized_response);
        zmq::message_t reply(serialized_response.size());
        memcpy(reply.data(), serialized_response.c_str(), serialized_response.size());
        zmq_commander_.send(reply, zmq::send_flags::none);
    }
}