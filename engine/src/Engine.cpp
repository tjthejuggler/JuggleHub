#include "Engine.hpp"
#include "DebugLog.hpp"
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
#include <fstream>
#include <sstream>
#include <librealsense2/rs_advanced_mode.hpp>

namespace fs = std::filesystem;

Engine::Engine(const std::string& camera_settings_path, const std::string& device_name, const std::string& model_name, const std::string& pose_model_name, OutputFormat format, bool use_dnn_tracker, bool verbose)
    : camera_settings_path_(camera_settings_path),
      running_(false),
      output_format_(format),
      use_dnn_tracker_(use_dnn_tracker),
      verbose_(verbose),
      zmq_context_(1),
      zmq_publisher_(zmq_context_, ZMQ_PUB),
      zmq_commander_(zmq_context_, ZMQ_REP),
      align_to_color_(RS2_STREAM_COLOR),
      color_module_(std::make_unique<UdpBallColorModule>()),
      frame_counter_(0),
      continuous_recording_(false),
      camera_running_(false),
      ir_projector_active_(false),
      camera_width_(640),
      camera_height_(480),
      camera_fps_(60),
      record_with_yolo_boxes_(false),
      record_with_bytetrack_boxes_(false) {
   DEBUG_LOG("[LOG] Engine constructor called.");
   DEBUG_LOG("[LOG] Initial camera settings: ", camera_width_, "x", camera_height_, " @ ", camera_fps_, " FPS");
   // Bind ZMQ sockets
   zmq_publisher_.bind("tcp://127.0.0.1:5555");
    zmq_commander_.bind("tcp://127.0.0.1:5565");

    // Initialize DNNTracker if enabled
    // In your Engine's setup/initialization function
    try {
        // This assumes your models are in JuggleHub/engine/models/
        const std::string ball_model_path = "engine/models/" + model_name + ".xml";
        const std::string pose_model_path = "engine/models/" + pose_model_name + ".xml";
        dnn_tracker_ = std::make_shared<DNNTracker>(ball_model_path, pose_model_path, device_name);
    } catch (const std::exception& e) {
        ERROR_LOG("FATAL ERROR: Failed to initialize DNNTracker: ", e.what());
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
    DEBUG_LOG("[LOG] Engine::run() called. Starting main loop.");

    // Start command processing thread
    std::thread command_thread(&Engine::processCommands, this);
    
    // Initialize and start the camera with default settings.
    DEBUG_LOG("[LOG] Calling initializeCamera() from run().");
    initializeCamera();
    DEBUG_LOG("[LOG] Performing initial camera start from run().");
    startCamera();

    // Initialize the old BallTracker only if DNN tracking is not enabled
    if (use_dnn_tracker_) {
        settings_module_ = std::make_unique<juggler::modules::UdpBallSettingsModule>(dnn_tracker_);
    } else {
        ball_tracker_ = std::make_shared<juggler::BallTracker>("ball_settings.json");
        settings_module_ = std::make_unique<juggler::modules::UdpBallSettingsModule>(ball_tracker_);
    }
    settings_module_->setup();

    while (running_) {
        // Skip frame processing if camera is stopped
        if (!camera_running_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        rs2::frameset frames;
        try {
            frames = pipe_.wait_for_frames(1000); // 1 second timeout
        } catch (const rs2::error& e) {
            DEBUG_LOG("Camera frame timeout or error: ", e.what());
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        auto aligned_frames = align_to_color_.process(frames);
        auto color_frame = aligned_frames.get_color_frame();
        auto depth_frame = aligned_frames.get_depth_frame();

        if (!color_frame || !depth_frame) {
            DEBUG_LOG("[LOG] Dropping frame: missing color or depth.");
            continue;
        }

        DEBUG_LOG("[LOG] Frame ", frame_counter_, ": Received color and depth frames.");

        cv::Mat color_image(cv::Size(color_frame.get_width(), color_frame.get_height()), CV_8UC3, (void*)color_frame.get_data(), cv::Mat::AUTO_STEP);
        cv::Mat depth_image(cv::Size(depth_frame.get_width(), depth_frame.get_height()), CV_16UC1, (void*)depth_frame.get_data(), cv::Mat::AUTO_STEP);
        last_depth_frame_ = depth_image.clone();
        last_color_frame_ = color_image.clone();
        
        // This block will be updated later to include detections
        
        juggler::v1::FrameData frame_data;
        frame_data.set_timestamp_us(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
        frame_data.set_frame_number(frame_counter_++);

        std::vector<uchar> buf;
        cv::imencode(".jpg", color_image, buf);
        DEBUG_LOG("[LOG] Frame ", frame_data.frame_number(), ": Encoded color image to JPG, size: ", buf.size(), " bytes.");
        frame_data.set_color_image_b64(buf.data(), buf.size());
        frame_data.set_ir_projector_active(ir_projector_active_);

        // --- BALL TRACKING CODE ---
        std::vector<TrackedObject> tracked_objects;
        std::vector<juggler::ColorTrackedBall> color_tracked_balls;
        
        if (use_dnn_tracker_) {
            if (!dnn_tracker_) return; // Safety check

            auto [tracker_results, tracked_hands] = dnn_tracker_->update(color_image, depth_image, camera_intrinsics_);
            tracked_objects = tracker_results;
            
            // Get the raw detections, unmatched detections, and color-tracked balls from DNNTracker
            last_raw_detections_ = dnn_tracker_->get_last_raw_detections();
            auto unmatched_detections = dnn_tracker_->get_unmatched_detections();
            color_tracked_balls = dnn_tracker_->get_color_tracked_balls();
            
            DEBUG_LOG("[LOG] Frame ", frame_data.frame_number(), ": DNNTracker returned ",
                      tracked_objects.size(), " tracked objects, ",
                      last_raw_detections_.size(), " raw detections, and ",
                      unmatched_detections.size(), " unmatched detections.");

            for (const auto& hand_obj : tracked_hands) {
                auto* hand = frame_data.add_hands();
                hand->set_id(hand_obj.id);
                
                // Set side field for Python compatibility
                hand->set_side(hand_obj.id == 0 ? "left" : "right");
                
                // Set wrist_pos_3d
                auto* pos = hand->mutable_wrist_pos_3d();
                pos->set_x(hand_obj.wrist_pos_3d.x);
                pos->set_y(hand_obj.wrist_pos_3d.y);
                pos->set_z(hand_obj.wrist_pos_3d.z);
                
                // Set position_3d (alias for compatibility)
                auto* pos_3d = hand->mutable_position_3d();
                pos_3d->set_x(hand_obj.wrist_pos_3d.x);
                pos_3d->set_y(hand_obj.wrist_pos_3d.y);
                pos_3d->set_z(hand_obj.wrist_pos_3d.z);
                
                hand->set_confidence(hand_obj.confidence);
                hand->set_is_visible(true);

                // Project wrist position to 2D for hand circle rendering
                cv::Point2f wrist_2d = DNNTracker::project_3d_to_2d(hand_obj.wrist_pos_3d, camera_intrinsics_);
                auto* hand_pos_2d = hand->mutable_position_2d();
                hand_pos_2d->set_x(wrist_2d.x);
                hand_pos_2d->set_y(wrist_2d.y);

                // Add keypoints with proper 2D projection
                for (const auto& kp : hand_obj.keypoints) {
                    auto* keypoint = hand->add_keypoints();
                    
                    // Project 3D keypoint to 2D screen coordinates
                    cv::Point2f kp_2d = DNNTracker::project_3d_to_2d(kp, camera_intrinsics_);
                    auto* pos_2d = keypoint->mutable_pos_2d();
                    pos_2d->set_x(kp_2d.x);
                    pos_2d->set_y(kp_2d.y);
                    
                    // Also store 3D position
                    auto* pos_3d = keypoint->mutable_pos_3d();
                    pos_3d->set_x(kp.x);
                    pos_3d->set_y(kp.y);
                    pos_3d->set_z(kp.z);
                    
                    // Set confidence (assuming all keypoints have same confidence as hand for now)
                    keypoint->set_confidence(hand_obj.confidence);
                }
                
                DEBUG_LOG("[LOG] Hand ", hand_obj.id, " with ", hand_obj.keypoints.size(),
                          " keypoints added to frame_data");
            }

            // Populate raw detections in protobuf
            for (const auto& det : last_raw_detections_) {
                auto* raw_det_pb = frame_data.add_raw_detections();
                raw_det_pb->set_x(det.box.x);
                raw_det_pb->set_y(det.box.y);
                raw_det_pb->set_width(det.box.width);
                raw_det_pb->set_height(det.box.height);
                raw_det_pb->set_confidence(det.confidence);
                raw_det_pb->set_class_id(det.class_id);
            }

            // Populate unmatched detections in protobuf
            for (const auto& det : unmatched_detections) {
                auto* unmatched_det_pb = frame_data.add_unmatched_detections();
                unmatched_det_pb->set_x(det.box.x);
                unmatched_det_pb->set_y(det.box.y);
                unmatched_det_pb->set_width(det.box.width);
                unmatched_det_pb->set_height(det.box.height);
                unmatched_det_pb->set_confidence(det.confidence);
                unmatched_det_pb->set_class_id(det.class_id);
            }

            last_tracked_objects_ = tracked_objects;

            // Add to frame buffers
            RecordingFrame rec_frame = {color_image.clone(), last_raw_detections_, tracked_objects, tracked_hands};
            {
                std::lock_guard<std::mutex> lock(frame_buffer_mutex_);
                frame_buffer_.push_back(rec_frame);
                if (frame_buffer_.size() > 150) {
                    frame_buffer_.pop_front();
                }
            }
            if (continuous_recording_) {
                std::lock_guard<std::mutex> lock(continuous_frame_buffer_mutex_);
                continuous_frame_buffer_.push_back(rec_frame);
            }
 
             DEBUG_LOG("DNNTracker update returned ", tracked_objects.size(), " objects and ", last_raw_detections_.size(), " raw detections.");
        } else {
             auto rs_intrinsics = depth_frame.get_profile().as<rs2::video_stream_profile>().get_intrinsics();
             auto detections = ball_tracker_->detectBalls(color_image, depth_frame, rs_intrinsics);
            // ... (code to populate frame_data from detections)
        }

        // Helper to map our internal status to the protobuf enum
        auto to_proto_status = [](TrackerStatus status) {
            switch (status) {
                case TrackerStatus::TRACKED: return juggler::v1::Ball::Status::Ball_Status_TRACKED;
                case TrackerStatus::PREDICTED: return juggler::v1::Ball::Status::Ball_Status_PREDICTED;
                case TrackerStatus::OCCLUDED: return juggler::v1::Ball::Status::Ball_Status_OCCLUDED;
                default: return juggler::v1::Ball::Status::Ball_Status_PREDICTED; // Default case
            }
        };

        for (const auto& obj : tracked_objects) {
             if (obj.world_pos.z <= 0 && obj.status != TrackerStatus::OCCLUDED) continue;

            if (obj.class_name == "ball") {
                auto* ball = frame_data.add_balls();
                ball->set_id(obj.id);
                ball->set_logical_id(obj.logical_id);
                ball->set_status(to_proto_status(obj.status));

                auto* pos = ball->mutable_position();
                pos->set_x(obj.world_pos.x);
                pos->set_y(obj.world_pos.y);
                pos->set_z(obj.world_pos.z);

                auto* bbox = ball->mutable_bounding_box_2d();
                bbox->set_x(obj.box.x);
                bbox->set_y(obj.box.y);
                bbox->set_width(obj.box.width);
                bbox->set_height(obj.box.height);

                ball->set_class_name(obj.class_name);
                
                cv::Point2f projected_pos = DNNTracker::project_3d_to_2d(obj.world_pos, camera_intrinsics_);
                auto* proj_pos_2d = ball->mutable_projected_pos_2d();
                proj_pos_2d->set_x(projected_pos.x);
                proj_pos_2d->set_y(projected_pos.y);

            } else if (obj.class_name == "hand") {
                auto* hand = frame_data.add_hands();
                // For now, just sending position and side.
                auto* pos = hand->mutable_wrist_pos_3d();
                pos->set_x(obj.world_pos.x);
                pos->set_y(obj.world_pos.y);
                pos->set_z(obj.world_pos.z);
                hand->set_is_visible(obj.status == TrackerStatus::TRACKED);
                hand->set_id(obj.is_left ? 0 : 1);
            }
        }

        // Add color-tracked balls to frame data
        for (const auto& color_ball : color_tracked_balls) {
            if (!color_ball.is_active) continue;
            
            auto* ct_ball = frame_data.add_color_tracked_balls();
            ct_ball->set_logical_id(color_ball.logical_id);
            ct_ball->set_color_name(color_ball.color_name);
            
            auto* pixel_pos = ct_ball->mutable_pixel_pos();
            pixel_pos->set_x(color_ball.pixel_pos.x);
            pixel_pos->set_y(color_ball.pixel_pos.y);
            
            auto* world_pos = ct_ball->mutable_world_pos();
            world_pos->set_x(color_ball.world_pos.x);
            world_pos->set_y(color_ball.world_pos.y);
            world_pos->set_z(color_ball.world_pos.z);
            
            ct_ball->set_is_active(color_ball.is_active);
            ct_ball->set_associated_wrist_id(color_ball.associated_wrist_id);
            ct_ball->set_frames_since_seen(color_ball.frames_since_seen);
        }

        if (active_module_) {
            active_module_->update(frame_data, [this](const juggler::v1::CommandRequest& command) {
                sendCommand(command);
            });
        }

        // Publish FrameData
        std::string serialized_data;
        DEBUG_LOG("DEBUG: C++ sending ", frame_data.balls_size(), " balls.");
        frame_data.SerializeToString(&serialized_data);

        DEBUG_LOG("Serialized FrameData size: ", serialized_data.size(), " bytes");

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
            
            DEBUG_LOG("Received external command: ", command.type());

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
                    record_with_yolo_boxes_ = command.record_with_yolo_boxes();
                    record_with_bytetrack_boxes_ = command.record_with_bytetrack_boxes();
                    saveRecording();
                    response.set_message("Recording saved");
                    break;
                case juggler::v1::CommandRequest::RECORD_CONTINUOUS_START:
                    record_with_yolo_boxes_ = command.record_with_yolo_boxes();
                    record_with_bytetrack_boxes_ = command.record_with_bytetrack_boxes();
                    startContinuousRecording();
                    response.set_message("Continuous recording started");
                    break;
                case juggler::v1::CommandRequest::RECORD_CONTINUOUS_STOP:
                    stopContinuousRecording();
                    response.set_message("Continuous recording stopped and saved");
                    break;
                case juggler::v1::CommandRequest::CAMERA_STOP:
                    stopCamera();
                    response.set_message("Camera feed stopped");
                    break;
                case juggler::v1::CommandRequest::CAMERA_START:
                    DEBUG_LOG("[LOG] CAMERA_START command received.");
                    if (!command.camera_settings_file().empty()) {
                        // Check if camera parameters are provided from UI
                        if (command.camera_width() > 0 && command.camera_height() > 0 && command.camera_fps() > 0) {
                            DEBUG_LOG("[LOG] Calling startCameraWithSettings with new resolution.");
                            // Use UI-provided parameters
                            startCameraWithSettings(command.camera_settings_file(), command.camera_width(), command.camera_height(), command.camera_fps());
                            response.set_message("Camera started with settings: " + command.camera_settings_file() +
                                               " at " + std::to_string(command.camera_width()) + "x" + std::to_string(command.camera_height()) +
                                               " @ " + std::to_string(command.camera_fps()) + " FPS");
                        } else {
                            DEBUG_LOG("[LOG] Calling startCameraWithSettings with settings file only.");
                            // Use original method without parameters (backward compatibility)
                            startCameraWithSettings(command.camera_settings_file());
                            response.set_message("Camera started with settings: " + command.camera_settings_file());
                        }
                    } else {
                        DEBUG_LOG("[LOG] Calling startCamera() with current settings.");
                        startCamera();
                        response.set_message("Camera started with current settings");
                    }
                    break;
                case juggler::v1::CommandRequest::CALIBRATE_OBJECT:
                    if (dnn_tracker_ && !last_depth_frame_.empty()) {
                        cv::Point2f pixel_coords(command.calibration_pixel_pos().x(), command.calibration_pixel_pos().y());
                        dnn_tracker_->calibrate_object(command.logical_id_to_calibrate(), pixel_coords, last_depth_frame_, camera_intrinsics_);
                        response.set_message("Calibration command sent to tracker.");
                    } else {
                        response.set_success(false);
                        response.set_message("Tracker not ready for calibration.");
                    }
                    break;
                case juggler::v1::CommandRequest::SET_POSE_MODEL_ENABLED:
                    if (dnn_tracker_) {
                        dnn_tracker_->update_setting("pose_model_enabled", command.pose_model_enabled() ? "true" : "false");
                        response.set_message("Pose model enabled set to " + std::string(command.pose_model_enabled() ? "true" : "false"));
                    } else {
                        response.set_success(false);
                        response.set_message("DNNTracker not initialized.");
                    }
                    break;
                case juggler::v1::CommandRequest::CALIBRATE_COLOR:
                    if (dnn_tracker_ && !last_color_frame_.empty()) {
                        cv::Point click_point(command.click_x(), command.click_y());
                        dnn_tracker_->calibrate_color(command.color_name(), click_point);
                        response.set_message("Color profile '" + command.color_name() + "' calibrated successfully");
                    } else {
                        response.set_success(false);
                        response.set_message("Tracker not ready for color calibration");
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
            DEBUG_LOG("Processing internal command: ", internal_command.type());

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
    DEBUG_LOG("DEBUG: saveRecording() called.");
    std::lock_guard<std::mutex> lock(frame_buffer_mutex_);
    
    if (frame_buffer_.empty()) {
        DEBUG_LOG("DEBUG: Frame buffer is empty. Nothing to save.");
        return;
    }

    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::tm buf;
    localtime_r(&in_time_t, &buf);
    std::stringstream ss;
    ss << "rs455_" << std::put_time(&buf, "%Y-%m-%d_%H-%M-%S");
    fs::path data_dir = "engine/data/1_raw_recordings";
    fs::path recording_dir = data_dir / ss.str();
    
    fs::path recording_dir_no_boxes = recording_dir / "no_boxes";

    DEBUG_LOG("DEBUG: Attempting to create directory: ", recording_dir_no_boxes.string());

    try {
        fs::create_directories(recording_dir_no_boxes);
        
        int frame_num = 0;
        for (const auto& rec_frame : frame_buffer_) {
            std::string filename = ss.str() + "_frame_" + std::to_string(frame_num++) + ".jpg";
            fs::path filepath = recording_dir_no_boxes / filename;
            cv::imwrite(filepath.string(), rec_frame.frame);
        }
        
        INFO_LOG("Saved ", frame_buffer_.size(), " frames to ", recording_dir_no_boxes.string());

        if (record_with_yolo_boxes_ || record_with_bytetrack_boxes_) {
            fs::path recording_dir_with_boxes = recording_dir / "with_boxes";
            fs::create_directories(recording_dir_with_boxes);

            int frame_num_boxes = 0;
            for (const auto& rec_frame : frame_buffer_) {
                cv::Mat frame_with_boxes = rec_frame.frame.clone();
                if (record_with_yolo_boxes_) {
                    for (const auto& det : rec_frame.raw_detections) {
                        cv::rectangle(frame_with_boxes, det.box, cv::Scalar(0, 0, 255), 2); // Red for YOLO
                    }
                }
                if (record_with_bytetrack_boxes_) {
                    for (const auto& obj : rec_frame.tracked_objects) {
                        cv::rectangle(frame_with_boxes, obj.box, cv::Scalar(0, 165, 255), 2); // Orange for ByteTrack
                    }
                }
                std::string filename = ss.str() + "_frame_" + std::to_string(frame_num_boxes++) + "_boxes.jpg";
                fs::path filepath = recording_dir_with_boxes / filename;
                cv::imwrite(filepath.string(), frame_with_boxes);
            }
            INFO_LOG("Saved ", frame_buffer_.size(), " frames with bounding boxes to ", recording_dir_with_boxes.string());
        }

    } catch (const fs::filesystem_error& e) {
        ERROR_LOG("Error creating directory or saving frames: ", e.what());
    }
}

void Engine::startContinuousRecording() {
    DEBUG_LOG("DEBUG: startContinuousRecording() called.");

    if (continuous_recording_) {
        DEBUG_LOG("DEBUG: Continuous recording already active.");
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
    INFO_LOG("Continuous recording started: ", continuous_recording_session_);
}

void Engine::stopContinuousRecording() {
    DEBUG_LOG("DEBUG: stopContinuousRecording() called.");

    if (!continuous_recording_) {
        DEBUG_LOG("DEBUG: No continuous recording active.");
        return;
    }
    
    continuous_recording_ = false;
    
    std::lock_guard<std::mutex> lock(continuous_frame_buffer_mutex_);
    
    if (continuous_frame_buffer_.empty()) {
        DEBUG_LOG("DEBUG: Continuous frame buffer is empty. Nothing to save.");
        return;
    }
    
    fs::path data_dir = "engine/data/1_raw_recordings";
    fs::path recording_dir = data_dir / continuous_recording_session_;
    fs::path recording_dir_no_boxes = recording_dir / "no_boxes";

    DEBUG_LOG("DEBUG: Attempting to create directory: ", recording_dir_no_boxes.string());
    
    try {
        fs::create_directories(recording_dir_no_boxes);
        
        int frame_num = 0;
        for (const auto& rec_frame : continuous_frame_buffer_) {
            std::string filename = continuous_recording_session_ + "_frame_" + std::to_string(frame_num++) + ".jpg";
            fs::path filepath = recording_dir_no_boxes / filename;
            cv::imwrite(filepath.string(), rec_frame.frame);
        }
        
        INFO_LOG("Saved ", continuous_frame_buffer_.size(), " frames to ", recording_dir_no_boxes.string());

        if (record_with_yolo_boxes_ || record_with_bytetrack_boxes_) {
            fs::path recording_dir_with_boxes = recording_dir / "with_boxes";
            fs::create_directories(recording_dir_with_boxes);

            int frame_num_boxes = 0;
            for (const auto& rec_frame : continuous_frame_buffer_) {
                cv::Mat frame_with_boxes = rec_frame.frame.clone();
                if (record_with_yolo_boxes_) {
                    for (const auto& det : rec_frame.raw_detections) {
                        cv::rectangle(frame_with_boxes, det.box, cv::Scalar(0, 0, 255), 2); // Red for YOLO
                    }
                }
                if (record_with_bytetrack_boxes_) {
                    for (const auto& obj : rec_frame.tracked_objects) {
                        cv::rectangle(frame_with_boxes, obj.box, cv::Scalar(0, 165, 255), 2); // Orange for ByteTrack
                    }
                }
                std::string filename = continuous_recording_session_ + "_frame_" + std::to_string(frame_num_boxes++) + "_boxes.jpg";
                fs::path filepath = recording_dir_with_boxes / filename;
                cv::imwrite(filepath.string(), frame_with_boxes);
            }
            INFO_LOG("Saved ", continuous_frame_buffer_.size(), " frames with bounding boxes to ", recording_dir_with_boxes.string());
        }

        continuous_frame_buffer_.clear();
        
    } catch (const fs::filesystem_error& e) {
        ERROR_LOG("Error creating directory or saving frames: ", e.what());
    }
}

void Engine::initializeCamera() {
    DEBUG_LOG("[LOG] Engine::initializeCamera() called.");
    // Load camera settings from JSON file first
    if (!camera_settings_path_.empty()) {
        DEBUG_LOG("[LOG] Loading camera settings from: ", camera_settings_path_);
        loadCameraSettingsFromJson(camera_settings_path_);
    } else {
        DEBUG_LOG("[LOG] No camera settings path provided.");
    }

    // Configure camera streams but do not start them
     DEBUG_LOG("[LOG] Configuring camera streams: ",
               camera_width_, "x", camera_height_, " @ ", camera_fps_, " FPS");
    rs_config_.enable_stream(RS2_STREAM_COLOR, camera_width_, camera_height_, RS2_FORMAT_BGR8, camera_fps_);
    rs_config_.enable_stream(RS2_STREAM_DEPTH, camera_width_, camera_height_, RS2_FORMAT_Z16, camera_fps_);
    
    DEBUG_LOG("[LOG] Camera configured.");
}

void Engine::loadCameraSettingsFromJson(const std::string& json_path) {
    try {
        std::ifstream file(json_path);
        if (!file.is_open()) {
            throw std::runtime_error("Could not open camera settings file: " + json_path);
        }
        
        // Read the entire JSON file content into a string
        std::stringstream buffer;
        buffer << file.rdbuf();
        json_content_ = buffer.str();
        
        INFO_LOG("Loaded camera settings from: ", json_path);
    } catch (const std::exception& e) {
        ERROR_LOG("Error loading camera settings: ", e.what());
        throw;
    }
}

void Engine::applyCameraSettings() {
    DEBUG_LOG("[LOG] Engine::applyCameraSettings() called.");
    if (json_content_.empty()) {
        DEBUG_LOG("[LOG] No JSON content, skipping settings application.");
        return;
    }

    try {
        if (!camera_running_) {
            DEBUG_LOG("[LOG] Camera not running, settings will be applied on start.");
            return;
        }

        DEBUG_LOG("[LOG] Applying camera settings from JSON...");
        
        auto profile = pipe_.get_active_profile();
        rs2::device dev = profile.get_device();

        if (dev.is<rs2::serializable_device>()) {
            rs2::serializable_device serializable_dev = dev.as<rs2::serializable_device>();
            serializable_dev.load_json(json_content_);
            DEBUG_LOG("[LOG] Camera settings applied successfully.");
        } else {
            DEBUG_LOG("[LOG] Device does not support advanced settings.");
        }
    } catch (const rs2::error& e) {
        ERROR_LOG("[ERROR] RealSense error in applyCameraSettings: ", e.what());
    } catch (const std::exception& e) {
        ERROR_LOG("[ERROR] General error in applyCameraSettings: ", e.what());
    }
}

void Engine::stopCamera() {
    DEBUG_LOG("[LOG] Engine::stopCamera() called.");
    if (!camera_running_) {
        DEBUG_LOG("[LOG] Camera already stopped.");
        return;
    }

    DEBUG_LOG("[LOG] Attempting to stop camera...");

    try {
        pipe_.stop();
        INFO_LOG("[LOG] Camera stopped successfully.");
        camera_running_ = false;
        ir_projector_active_ = false;
    } catch (const rs2::error& e) {
        ERROR_LOG("[ERROR] Error stopping camera: ", e.what());
    }
}

void Engine::startCamera() {
    DEBUG_LOG("[LOG] Engine::startCamera() called.");
    if (camera_running_) {
        DEBUG_LOG("[LOG] Camera is already running.");
        return;
    }

    DEBUG_LOG("[LOG] Attempting to start camera pipeline...");

    try {
        rs2::pipeline_profile profile = pipe_.start(rs_config_);
        camera_running_ = true;
        INFO_LOG("[LOG] Camera pipeline started successfully.");

        // --- Store Camera Intrinsics ---
        auto stream = profile.get_stream(RS2_STREAM_DEPTH).as<rs2::video_stream_profile>();
        auto intrinsics = stream.get_intrinsics();
        camera_intrinsics_.fx = intrinsics.fx;
        camera_intrinsics_.fy = intrinsics.fy;
        camera_intrinsics_.ppx = intrinsics.ppx;
        camera_intrinsics_.ppy = intrinsics.ppy;
        DEBUG_LOG("[LOG] Stored camera intrinsics: fx=", camera_intrinsics_.fx,
                  ", fy=", camera_intrinsics_.fy, ", ppx=", camera_intrinsics_.ppx,
                  ", ppy=", camera_intrinsics_.ppy);

        // Wait for a moment to ensure the device is ready.
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        // Apply advanced settings from JSON now that the pipeline is active.
        applyCameraSettings();

        // Programmatically enable the IR projector
        try {
            auto sensor = profile.get_device().first<rs2::depth_sensor>();
            if (sensor.supports(RS2_OPTION_EMITTER_ENABLED)) {
                sensor.set_option(RS2_OPTION_EMITTER_ENABLED, 1.f); // 1.f is for ON
                ir_projector_active_ = true;
                DEBUG_LOG("[LOG] IR Emitter enabled programmatically.");
            }
        } catch (const rs2::error& e) {
            WARN_LOG("[WARNING] Could not set IR emitter option: ", e.what());
            ir_projector_active_ = false;
        }

    } catch (const rs2::error& e) {
        ERROR_LOG("[ERROR] Error starting camera: ", e.what());
        camera_running_ = false;
    }
}

void Engine::startCameraWithSettings(const std::string& settings_file) {
    DEBUG_LOG("[LOG] startCameraWithSettings(settings_file) called.");
    startCameraWithSettings(settings_file, camera_width_, camera_height_, camera_fps_);
}

void Engine::startCameraWithSettings(const std::string& settings_file, uint32_t width, uint32_t height, uint32_t fps) {
    INFO_LOG("Reconfiguring camera with settings: ", settings_file,
             " at ", width, "x", height, " @ ", fps, " FPS");

    // Stop the pipeline completely.
    if (camera_running_) {
        stopCamera();
    }

    // Create a new configuration object.
    rs2::config new_config;

    // Update member variables for resolution and FPS.
    camera_width_ = width;
    camera_height_ = height;
    camera_fps_ = fps;
    camera_settings_path_ = settings_file;

    // Enable the streams on the new configuration.
    new_config.enable_stream(RS2_STREAM_COLOR, camera_width_, camera_height_, RS2_FORMAT_BGR8, camera_fps_);
    new_config.enable_stream(RS2_STREAM_DEPTH, camera_width_, camera_height_, RS2_FORMAT_Z16, camera_fps_);

    // Replace the old configuration.
    rs_config_ = new_config;

    // Load and apply JSON settings.
    if (!camera_settings_path_.empty()) {
        loadCameraSettingsFromJson(camera_settings_path_);
    }

    // Start the camera with the new, fully-formed configuration.
    startCamera();
}