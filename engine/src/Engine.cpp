#include "Engine.hpp"
#include "BallTracker.hpp"
#include "modules/UdpBallColorModule.hpp"
#include "modules/PositionToRgbModule.hpp"
#include "BallTracker.hpp"
#include "DNNTracker.hpp" // Include the DNNTracker header
#include <iostream>
#include <thread>
#include <chrono>
#include <iomanip>
#include <filesystem>
#include <opencv2/imgcodecs.hpp>
#include <deque>

namespace fs = std::filesystem;

Engine::Engine(const std::string& config_file, const std::string& device_name, OutputFormat format, bool use_dnn_tracker, bool verbose)
    : running_(false),
      output_format_(format),
      use_dnn_tracker_(use_dnn_tracker), // Initialize the flag
      verbose_(verbose),
      zmq_context_(1),
      zmq_publisher_(zmq_context_, ZMQ_PUB),
      zmq_commander_(zmq_context_, ZMQ_REP),
      align_to_color_(RS2_STREAM_COLOR),
      color_module_(std::make_unique<UdpBallColorModule>()),
      frame_counter_(0),
      continuous_recording_(false) {
   // Bind ZMQ sockets
   zmq_publisher_.bind("tcp://127.0.0.1:5555");
    zmq_commander_.bind("tcp://127.0.0.1:5565");

    // Initialize DNNTracker if enabled
    // In your Engine's setup/initialization function
    try {
        // This assumes your models are in JuggleHub/engine/models/
        dnn_tracker_ = std::make_shared<DNNTracker>("engine/models/yolo11n.xml", device_name);
    } catch (const std::exception& e) {
        std::cerr << "FATAL ERROR: Failed to initialize DNNTracker: " << e.what() << std::endl;
        // Exit or handle the critical failure appropriately
        return; // or exit(1);
    }

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
    rs2::pipeline_profile profile = pipe_.start(rs_config_);
    if (verbose_) {
        std::cout << "Camera started." << std::endl;
        auto stream = profile.get_stream(RS2_STREAM_COLOR).as<rs2::video_stream_profile>();
        std::cout << "Color stream: " << stream.width() << "x" << stream.height() << " @" << stream.fps() << "fps" << std::endl;
    }

    // Initialize the old BallTracker only if DNN tracking is not enabled
    if (use_dnn_tracker_) {
        settings_module_ = std::make_unique<juggler::modules::UdpBallSettingsModule>(dnn_tracker_);
    } else {
        ball_tracker_ = std::make_shared<juggler::BallTracker>("ball_settings.json");
        settings_module_ = std::make_unique<juggler::modules::UdpBallSettingsModule>(ball_tracker_);
    }
    settings_module_->setup();

    while (running_) {
        rs2::frameset frames = pipe_.wait_for_frames();
        auto aligned_frames = align_to_color_.process(frames);
        auto color_frame = aligned_frames.get_color_frame();
        auto depth_frame = aligned_frames.get_depth_frame();

        if (!color_frame || !depth_frame) {
            continue;
        }

        cv::Mat color_image(cv::Size(640, 480), CV_8UC3, (void*)color_frame.get_data(), cv::Mat::AUTO_STEP);
        
        // Add to frame buffer
        {
            std::lock_guard<std::mutex> lock(frame_buffer_mutex_);
            frame_buffer_.push_back(color_image.clone());
            if (frame_buffer_.size() > 150) {
                frame_buffer_.pop_front();
            }
        }
        
        // Add to continuous recording buffer if recording
        if (continuous_recording_) {
            std::lock_guard<std::mutex> lock(continuous_frame_buffer_mutex_);
            continuous_frame_buffer_.push_back(color_image.clone());
            // No size limit for continuous recording - capture all frames from start to stop
            // Memory usage will be monitored and user should stop recording when needed
        }
        
        // This is a simplified FrameData creation.
        // In a real application, this would be much more complex.
        juggler::v1::FrameData frame_data;
        frame_data.set_timestamp_us(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
        frame_data.set_frame_number(frame_counter_++);

        // Encode color image to JPEG and send as bytes
        std::vector<uchar> buf;
        cv::imencode(".jpg", color_image, buf);
        frame_data.set_color_image_b64(buf.data(), buf.size());

        // Get camera intrinsics
        auto intrinsics = depth_frame.get_profile().as<rs2::video_stream_profile>().get_intrinsics();

        // --- BAll TRACKING CODE ---
        std::vector<TrackedObject> tracked_objects;
        if (use_dnn_tracker_) {
            if (!dnn_tracker_) return; // Safety check

            auto [tracker_results, raw_detections] = dnn_tracker_->update(color_image);
            tracked_objects = tracker_results;

            // Populate raw detections in protobuf
            for (const auto& det : raw_detections) {
                auto* raw_det_pb = frame_data.add_raw_detections();
                raw_det_pb->set_x(det.box.x);
                raw_det_pb->set_y(det.box.y);
                raw_det_pb->set_width(det.box.width);
                raw_det_pb->set_height(det.box.height);
                raw_det_pb->set_confidence(det.confidence);
            }

            if (verbose_) {
                std::cout << "DNNTracker update returned " << tracked_objects.size() << " objects and " << raw_detections.size() << " raw detections." << std::endl;
            }
        } else {
            auto detections = ball_tracker_->detectBalls(color_image, depth_frame, intrinsics);
            // ... (code to populate frame_data from detections)
        }

        // Now, convert the 2D results to 3D and populate your Protobuf message
        // This part uses your existing RealSense knowledge

        for (const auto& obj : tracked_objects) {
            // Center of the bounding box
            float pixel_x = obj.box.x + obj.box.width / 2.0f;
            float pixel_y = obj.box.y + obj.box.height / 2.0f;

            // Ensure pixel is within frame bounds before querying depth
            if (pixel_x >= 0 && pixel_y >= 0 && pixel_x < depth_frame.get_width() && pixel_y < depth_frame.get_height()) {
                float depth_in_meters = depth_frame.get_distance(pixel_x, pixel_y);

                if (depth_in_meters > 0) { // Only process valid depth readings
                    float point[3];
                    rs2_deproject_pixel_to_point(point, &intrinsics, (const float[2]){pixel_x, pixel_y}, depth_in_meters);
                    
                    // point[0] is X, point[1] is Y, point[2] is Z
                    auto* ball = frame_data.add_balls();
                    ball->set_id(obj.id);
                    auto* pos = ball->mutable_position();
                    pos->set_x(point[0]);
                    pos->set_y(point[1]);
                    pos->set_z(point[2]);

                    // Populate the new bounding box field
                    auto* bbox = ball->mutable_bounding_box_2d();
                    bbox->set_x(obj.box.x);
                    bbox->set_y(obj.box.y);
                    bbox->set_width(obj.box.width);
                    bbox->set_height(obj.box.height);
                }
            }
        }
        // --- END NEW DNN TRACKING CODE ---

        // Update active module
        if (active_module_) {
            active_module_->update(frame_data, [this](const juggler::v1::CommandRequest& command) {
                sendCommand(command);
            });
        }

        // Publish FrameData
        std::string serialized_data;
        if (verbose_) {
            std::cout << "DEBUG: C++ sending " << frame_data.balls_size() << " balls." << std::endl;
        }
        frame_data.SerializeToString(&serialized_data);

        if (verbose_) {
            std::cout << "Serialized FrameData size: " << serialized_data.size() << " bytes" << std::endl;
        }

        zmq::message_t message(serialized_data.size());
        memcpy(message.data(), serialized_data.c_str(), serialized_data.size());
        zmq_publisher_.send(message, zmq::send_flags::dontwait);
    }

    command_thread.join();
}

void Engine::stop() {
    running_ = false;
    if (settings_module_) {
        settings_module_->cleanup();
    }
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
                case juggler::v1::CommandRequest::CONFIGURE_MODULE:
                    if (active_module_) {
                        active_module_->processCommand(command); // Forward config command to active module
                        response.set_message("Module configuration sent to " + command.module_name());
                    } else {
                        response.set_success(false);
                        response.set_message("No active module to configure.");
                    }
                    break;
                case juggler::v1::CommandRequest::RECORD_START:
                    saveRecording();
                    response.set_message("Recording saved");
                    break;
                case juggler::v1::CommandRequest::RECORD_CONTINUOUS_START:
                    startContinuousRecording();
                    response.set_message("Continuous recording started");
                    break;
                case juggler::v1::CommandRequest::RECORD_CONTINUOUS_STOP:
                    stopContinuousRecording();
                    response.set_message("Continuous recording stopped and saved");
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

void Engine::saveRecording() {
    std::cout << "DEBUG: saveRecording() called." << std::endl;
    std::lock_guard<std::mutex> lock(frame_buffer_mutex_);
    
    if (frame_buffer_.empty()) {
        std::cout << "DEBUG: Frame buffer is empty. Nothing to save." << std::endl;
        return;
    }

    // Create a timestamped directory
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::tm buf;
    localtime_r(&in_time_t, &buf);
    std::stringstream ss;
    ss << "rs455_" << std::put_time(&buf, "%Y-%m-%d_%H-%M-%S");
    fs::path data_dir = "engine/data/1_raw_recordings";
    fs::path recording_dir = data_dir / ss.str();
    
    std::cout << "DEBUG: Attempting to create directory: " << recording_dir << std::endl;

    try {
        if (fs::create_directories(recording_dir)) {
            std::cout << "DEBUG: Successfully created directory." << std::endl;
        } else {
            std::cout << "DEBUG: Directory already existed or failed to create." << std::endl;
        }
        
        int frame_num = 0;
        for (const auto& frame : frame_buffer_) {
            std::string filename = ss.str() + "_frame_" + std::to_string(frame_num++) + ".jpg";
            fs::path filepath = recording_dir / filename;
            bool success = cv::imwrite(filepath.string(), frame);
            if (!success) {
                std::cerr << "Error: Failed to save frame to " << filepath << std::endl;
            }
        }
        
        std::cout << "Saved " << frame_buffer_.size() << " frames to " << recording_dir << std::endl;
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Error creating directory or saving frames: " << e.what() << std::endl;
    }
}

void Engine::startContinuousRecording() {
    std::cout << "DEBUG: startContinuousRecording() called." << std::endl;
    
    if (continuous_recording_) {
        std::cout << "DEBUG: Continuous recording already active." << std::endl;
        return;
    }
    
    // Clear the continuous buffer and start recording
    {
        std::lock_guard<std::mutex> lock(continuous_frame_buffer_mutex_);
        continuous_frame_buffer_.clear();
    }
    
    // Create session name with timestamp
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::tm buf;
    localtime_r(&in_time_t, &buf);
    std::stringstream ss;
    ss << "continuous_" << std::put_time(&buf, "%Y-%m-%d_%H-%M-%S");
    continuous_recording_session_ = ss.str();
    
    continuous_recording_ = true;
    std::cout << "Continuous recording started: " << continuous_recording_session_ << std::endl;
}

void Engine::stopContinuousRecording() {
    std::cout << "DEBUG: stopContinuousRecording() called." << std::endl;
    
    if (!continuous_recording_) {
        std::cout << "DEBUG: No continuous recording active." << std::endl;
        return;
    }
    
    continuous_recording_ = false;
    
    std::lock_guard<std::mutex> lock(continuous_frame_buffer_mutex_);
    
    if (continuous_frame_buffer_.empty()) {
        std::cout << "DEBUG: Continuous frame buffer is empty. Nothing to save." << std::endl;
        return;
    }
    
    // Create directory for continuous recording
    fs::path data_dir = "engine/data/1_raw_recordings";
    fs::path recording_dir = data_dir / continuous_recording_session_;
    
    std::cout << "DEBUG: Attempting to create directory: " << recording_dir << std::endl;
    
    try {
        if (fs::create_directories(recording_dir)) {
            std::cout << "DEBUG: Successfully created directory." << std::endl;
        } else {
            std::cout << "DEBUG: Directory already existed or failed to create." << std::endl;
        }
        
        int frame_num = 0;
        for (const auto& frame : continuous_frame_buffer_) {
            std::string filename = continuous_recording_session_ + "_frame_" + std::to_string(frame_num++) + ".jpg";
            fs::path filepath = recording_dir / filename;
            bool success = cv::imwrite(filepath.string(), frame);
            if (!success) {
                std::cerr << "Error: Failed to save frame to " << filepath << std::endl;
            }
        }
        
        std::cout << "Saved " << continuous_frame_buffer_.size() << " frames to " << recording_dir << std::endl;
        continuous_frame_buffer_.clear();
        
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Error creating directory or saving frames: " << e.what() << std::endl;
    }
}