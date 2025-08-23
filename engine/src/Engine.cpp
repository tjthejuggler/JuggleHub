#include "Engine.hpp"
#include "BallTracker.hpp"
#include "modules/UdpBallColorModule.hpp"
#include "modules/PositionToRgbModule.hpp"
#include "BallTracker.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <iomanip>

Engine::Engine(const std::string& config_file, OutputFormat format)
    : running_(false),
      output_format_(format),
      zmq_context_(1),
      zmq_publisher_(zmq_context_, ZMQ_PUB),
      zmq_commander_(zmq_context_, ZMQ_REP),
      align_to_color_(RS2_STREAM_COLOR),
      color_module_(std::make_unique<UdpBallColorModule>()) {
    // Bind ZMQ sockets
    zmq_publisher_.bind("tcp://127.0.0.1:5555");
    zmq_commander_.bind("tcp://127.0.0.1:5565");

    // Setup the default color module
    color_module_->setup();
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

        // Detect balls and populate frame_data
        auto intrinsics = depth_frame.get_profile().as<rs2::video_stream_profile>().get_intrinsics();
        auto detections = ball_tracker.detectBalls(color_image, depth_frame, intrinsics);
        
        // Console output for streaming ball detection data
        if (!detections.empty()) {
            switch (output_format_) {
                case OutputFormat::SIMPLE:
                    for (const auto& det : detections) {
                        std::cout << frame_data.timestamp_us() << ","
                                  << det.color_name << ","
                                  << det.world_x << "," << det.world_y << "," << det.world_z << ","
                                  << (int)det.center.x << "," << (int)det.center.y << ","
                                  << det.confidence << std::endl;
                    }
                    break;
                case OutputFormat::LEGACY:
                    for (const auto& det : detections) {
                        std::cout << det.color_name << ","
                                  << det.world_x << "," << det.world_y << "," << det.world_z << ","
                                  << frame_data.timestamp_us() << std::endl;
                    }
                    break;
                case OutputFormat::DEFAULT:
                default:
                    std::cout << "=== Ball Detections (Frame " << frame_data.timestamp_us() << ") ===" << std::endl;
                    for (const auto& det : detections) {
                        std::cout << "Ball: " << det.color_name
                                << " | Position: (" << std::fixed << std::setprecision(3)
                                << det.world_x << ", " << det.world_y << ", " << det.world_z << ")"
                                << " | 2D: (" << (int)det.center.x << ", " << (int)det.center.y << ")"
                                << " | Confidence: " << det.confidence << std::endl;
                    }
                    std::cout << "Total balls detected: " << detections.size() << std::endl;
                    std::cout << std::endl;
                    break;
            }

            for (const auto& det : detections) {
                auto* ball = frame_data.add_balls();
                ball->set_color_name(det.color_name);
                auto* pos = ball->mutable_position_3d();
                pos->set_x(det.world_x);
                pos->set_y(det.world_y);
                pos->set_z(det.world_z);
            }
        }

        // Update active module
        if (active_module_) {
            active_module_->update(frame_data, [this](const juggler::v1::CommandRequest& command) {
                sendCommand(command);
            });
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
        // Handle external ZMQ commands
        zmq::message_t request;
        auto result = zmq_commander_.recv(request, zmq::recv_flags::dontwait);
        
        if (result) {
            juggler::v1::CommandRequest command;
            command.ParseFromArray(request.data(), request.size());
            
            std::cout << "Received external command: " << command.type() << std::endl;

            juggler::v1::CommandResponse response;
            response.set_success(true);

            switch (command.type()) {
                case juggler::v1::CommandRequest::LOAD_MODULE:
                    active_module_ = create_module(command);
                    if (active_module_) {
                        active_module_->setup();
                        response.set_message(command.module_name() + " loaded");
                    } else {
                        response.set_success(false);
                        response.set_message("Unknown module: " + command.module_name());
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
                default:
                    response.set_success(false);
                    response.set_message("Unknown command");
                    break;
            }

            // Send response back via ZMQ
            std::string serialized_response;
            response.SerializeToString(&serialized_response);
            zmq::message_t reply(serialized_response.size());
            memcpy(reply.data(), serialized_response.c_str(), serialized_response.size());
            zmq_commander_.send(reply, zmq::send_flags::none);
        }

        // Handle internal commands from modules (like color commands)
        juggler::v1::CommandRequest internal_command;
        bool command_found = false;
        {
            std::lock_guard<std::mutex> lock(command_queue_mutex_);
            if (!command_queue_.empty()) {
                internal_command = command_queue_.front();
                command_queue_.pop();
                command_found = true;
            }
        }

        if (command_found) {
            std::cout << "Processing internal command: " << internal_command.type() << std::endl;

            switch (internal_command.type()) {
                case juggler::v1::CommandRequest::SEND_COLOR_COMMAND:
                    if (color_module_) {
                        color_module_->processCommand(internal_command);
                    }
                    break;
                default:
                    // Other internal commands can be handled here
                    break;
            }
        }

        if (!result && !command_found) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

void Engine::sendCommand(const juggler::v1::CommandRequest& command) {
    std::lock_guard<std::mutex> lock(command_queue_mutex_);
    command_queue_.push(command);
}

std::unique_ptr<ModuleBase> Engine::create_module(const juggler::v1::CommandRequest& command) {
    const std::string& module_name = command.module_name();
    if (module_name == "UdpBallColorModule") {
        return std::make_unique<UdpBallColorModule>();
    }
    if (module_name == "PositionToRgbModule") {
        return std::make_unique<PositionToRgbModule>();
    }
    return nullptr;
}