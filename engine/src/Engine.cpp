#include "Engine.hpp"
#include "DebugLog.hpp"
#include "BallTracker.hpp"
#include "modules/UdpBallColorModule.hpp"
#include "modules/PositionToRgbModule.hpp"
#include "SimpleBallTracker.hpp" // Include the SimpleBallTracker header
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

    // Initialize SimpleBallTracker with model paths
    try {
        const std::string ball_model_path = "engine/models/" + model_name + ".xml";
        const std::string pose_model_path = "engine/models/" + pose_model_name + ".xml";
        simple_tracker_ = std::make_shared<SimpleBallTracker>(
            ball_model_path, pose_model_path, device_name, "ball_settings.json");
        DEBUG_LOG("[LOG] SimpleBallTracker initialized successfully");
    } catch (const std::exception& e) {
        ERROR_LOG("FATAL ERROR: Failed to initialize SimpleBallTracker: ", e.what());
        return;
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

    // Initialize settings module with SimpleBallTracker
    if (use_dnn_tracker_) {
        settings_module_ = std::make_unique<juggler::modules::UdpBallSettingsModule>(simple_tracker_);
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
        std::vector<SimpleBall> tracked_balls;
        std::vector<BallEvent> ball_events;
        std::vector<SimpleHand> tracked_hands;
        
        if (use_dnn_tracker_) {
            if (!simple_tracker_) return; // Safety check
            
            // Update SimpleBallTracker (includes YOLO detection and pose estimation internally)
            auto [balls, events] = simple_tracker_->update(color_image, depth_image, camera_intrinsics_);
            tracked_balls = balls;
            ball_events = events;
            tracked_hands = simple_tracker_->getHands();
            
            // Get the raw detections for recording/visualization
            last_raw_detections_ = simple_tracker_->getLastRawDetections();
            
            DEBUG_LOG("[LOG] Frame ", frame_data.frame_number(), ": SimpleBallTracker returned ",
                      tracked_balls.size(), " balls, ",
                      ball_events.size(), " events, and ",
                      tracked_hands.size(), " hands.");

            // Populate hands
            for (const auto& hand_obj : tracked_hands) {
                auto* hand = frame_data.add_hands();
                hand->set_id(hand_obj.id);
                hand->set_side(hand_obj.id == 0 ? "left" : "right");
                
                auto* pos = hand->mutable_wrist_pos_3d();
                pos->set_x(hand_obj.wrist_pos_3d.x);
                pos->set_y(hand_obj.wrist_pos_3d.y);
                pos->set_z(hand_obj.wrist_pos_3d.z);
                
                auto* pos_3d = hand->mutable_position_3d();
                pos_3d->set_x(hand_obj.wrist_pos_3d.x);
                pos_3d->set_y(hand_obj.wrist_pos_3d.y);
                pos_3d->set_z(hand_obj.wrist_pos_3d.z);
                
                hand->set_confidence(hand_obj.confidence);
                hand->set_is_visible(hand_obj.is_visible);

                cv::Point2f wrist_2d = SimpleBallTracker::project_3d_to_2d(hand_obj.wrist_pos_3d, camera_intrinsics_);
                auto* hand_pos_2d = hand->mutable_position_2d();
                hand_pos_2d->set_x(wrist_2d.x);
                hand_pos_2d->set_y(wrist_2d.y);

                for (const auto& kp : hand_obj.keypoints) {
                    auto* keypoint = hand->add_keypoints();
                    
                    cv::Point2f kp_2d = SimpleBallTracker::project_3d_to_2d(kp, camera_intrinsics_);
                    auto* pos_2d = keypoint->mutable_pos_2d();
                    pos_2d->set_x(kp_2d.x);
                    pos_2d->set_y(kp_2d.y);
                    
                    auto* kp_pos_3d = keypoint->mutable_pos_3d();
                    kp_pos_3d->set_x(kp.x);
                    kp_pos_3d->set_y(kp.y);
                    kp_pos_3d->set_z(kp.z);
                    
                    keypoint->set_confidence(hand_obj.confidence);
                }
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

            // Note: We no longer have "unmatched detections" in the simplified system
            // All detections are either matched to a ball or ignored

            // Convert SimpleBall to TrackedObject for recording (temporary compatibility)
            std::vector<TrackedObject> tracked_objects_compat;
            for (const auto& ball : tracked_balls) {
                TrackedObject obj;
                obj.box = ball.bbox;
                obj.world_pos = ball.position;
                obj.id = ball.id;
                obj.class_id = ball.yolo_class_id;
                obj.class_name = ball.is_held ? "ball_held" : "ball";
                obj.status = TrackerStatus::TRACKED;  // Simplified - always tracked if in list
                obj.logical_id = ball.id;
                obj.is_left = false;
                tracked_objects_compat.push_back(obj);
            }
            
            // Convert SimpleHand to TrackedHand for recording (temporary compatibility)
            std::vector<TrackedHand> tracked_hands_compat;
            for (const auto& hand : tracked_hands) {
                TrackedHand th;
                th.wrist_pos_3d = hand.wrist_pos_3d;
                th.confidence = hand.confidence;
                th.id = hand.id;
                th.keypoints = hand.keypoints;
                tracked_hands_compat.push_back(th);
            }
            
            last_tracked_objects_ = tracked_objects_compat;

            // Add to frame buffers
            RecordingFrame rec_frame = {color_image.clone(), last_raw_detections_,
                                       tracked_objects_compat, tracked_hands_compat, tracked_balls, visualization_states_};
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
 
        } else {
            // Old BallTracker path (not used when use_dnn_tracker_ is true)
            auto rs_intrinsics = depth_frame.get_profile().as<rs2::video_stream_profile>().get_intrinsics();
            auto detections = ball_tracker_->detectBalls(color_image, depth_frame, rs_intrinsics);
        }

        // Populate color-based predictions for visualization
        // NEW: Uses color detection history instead of Kalman filter
        if (use_dnn_tracker_ && simple_tracker_) {
            for (const auto& ball : tracked_balls) {
                // Only create prediction if we have enough color detection history
                if (!ball.color_predictor.hasEnoughData()) {
                    continue;  // Skip balls without sufficient history
                }
                
                auto* kalman_pred = frame_data.add_kalman_predictions();
                kalman_pred->set_logical_id(ball.id);
                
                // Get predicted position from color-based predictor
                // This uses recent color detections and applies gravity for in-air balls
                cv::Point3f pred_pos_3d = ball.color_predictor.getPredictedPosition(!ball.is_held);
                
                // Skip if prediction failed (returns 0,0,0)
                if (pred_pos_3d.z <= 0) {
                    continue;
                }
                
                auto* pred_pos = kalman_pred->mutable_predicted_pos();
                pred_pos->set_x(pred_pos_3d.x);
                pred_pos->set_y(pred_pos_3d.y);
                pred_pos->set_z(pred_pos_3d.z);
                
                // Project to 2D
                cv::Point2f pred_pos_2d = SimpleBallTracker::project_3d_to_2d(pred_pos_3d, camera_intrinsics_);
                auto* pred_2d = kalman_pred->mutable_predicted_pos_2d();
                pred_2d->set_x(pred_pos_2d.x);
                pred_2d->set_y(pred_pos_2d.y);
                
                // Determine if in freefall (not held)
                kalman_pred->set_is_in_freefall(!ball.is_held);
            }
        }
        
        // Populate balls from SimpleBall
        for (const auto& ball : tracked_balls) {
            if (ball.position.z <= 0) continue;  // Skip invalid depth

            auto* ball_pb = frame_data.add_balls();
            ball_pb->set_id(ball.id);
            ball_pb->set_logical_id(ball.id);
            ball_pb->set_status(juggler::v1::Ball::Status::Ball_Status_TRACKED);

            auto* pos = ball_pb->mutable_position();
            pos->set_x(ball.position.x);
            pos->set_y(ball.position.y);
            pos->set_z(ball.position.z);

            auto* bbox = ball_pb->mutable_bounding_box_2d();
            bbox->set_x(ball.bbox.x);
            bbox->set_y(ball.bbox.y);
            bbox->set_width(ball.bbox.width);
            bbox->set_height(ball.bbox.height);

            ball_pb->set_class_name(ball.is_held ? "ball_held" : "ball");
            ball_pb->set_distance_to_nearest_wrist(ball.distance_to_nearest_wrist);
            
            cv::Point2f projected_pos = SimpleBallTracker::project_3d_to_2d(ball.position, camera_intrinsics_);
            auto* proj_pos_2d = ball_pb->mutable_projected_pos_2d();
            proj_pos_2d->set_x(projected_pos.x);
            proj_pos_2d->set_y(projected_pos.y);
            
            // Add color tracked ball for UI visualization
            auto* color_ball = frame_data.add_color_tracked_balls();
            color_ball->set_logical_id(ball.id);
            color_ball->set_color_name(ball.color_name);
            auto* pixel_pos = color_ball->mutable_pixel_pos();
            pixel_pos->set_x(ball.pixel_pos.x);
            pixel_pos->set_y(ball.pixel_pos.y);
            auto* world_pos = color_ball->mutable_world_pos();
            world_pos->set_x(ball.position.x);
            world_pos->set_y(ball.position.y);
            world_pos->set_z(ball.position.z);
            color_ball->set_is_active(ball.has_yolo_detection);
            color_ball->set_associated_wrist_id(ball.held_by_hand_id);
            color_ball->set_frames_since_seen(ball.frames_without_yolo);
            
            // Add ball state
            auto* ball_state = frame_data.add_ball_states();
            ball_state->set_logical_id(ball.id);
            // Map is_held to ball state enum
            if (ball.is_held) {
                ball_state->set_state(juggler::v1::BallState::HELD);
                ball_state->set_associated_hand_id(ball.held_by_hand_id);
            } else {
                ball_state->set_state(juggler::v1::BallState::IN_FLIGHT);
                ball_state->set_associated_hand_id(-1);
            }
            ball_state->set_frames_in_state(ball.state_change_counter);
        }
        
        // Populate throw/catch events in protobuf
        for (const auto& event : ball_events) {
            auto* event_pb = frame_data.add_throw_catch_events();
            event_pb->set_type(event.type == BallEvent::THROW ?
                              juggler::v1::ThrowCatchEvent::THROW :
                              juggler::v1::ThrowCatchEvent::CATCH);
            event_pb->set_ball_id(event.ball_id);
            event_pb->set_hand_id(event.hand_id);
            event_pb->set_timestamp_us(event.timestamp);
            
            // Find the ball to get its position
            cv::Point3f ball_position(0, 0, 0);
            float confidence = 0.8f;  // Default confidence for events
            for (const auto& ball : tracked_balls) {
                if (ball.id == event.ball_id) {
                    ball_position = ball.position;
                    confidence = ball.yolo_confidence;
                    break;
                }
            }
            
            auto* pos = event_pb->mutable_position();
            pos->set_x(ball_position.x);
            pos->set_y(ball_position.y);
            pos->set_z(ball_position.z);
            
            event_pb->set_confidence(confidence);
            
            DEBUG_LOG("[EVENT] ", (event.type == BallEvent::THROW ? "THROW" : "CATCH"),
                     " - Ball ", event.ball_id, " Hand ", event.hand_id,
                     " at (", ball_position.x, ", ", ball_position.y, ", ", ball_position.z, ")");
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
                    if (command.has_visualization_states()) {
                        visualization_states_ = command.visualization_states();
                    }
                    saveRecording();
                    response.set_message("Recording saved");
                    break;
                case juggler::v1::CommandRequest::RECORD_CONTINUOUS_START:
                    record_with_yolo_boxes_ = command.record_with_yolo_boxes();
                    record_with_bytetrack_boxes_ = command.record_with_bytetrack_boxes();
                    if (command.has_visualization_states()) {
                        visualization_states_ = command.visualization_states();
                    }
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
                    // Note: SimpleBallTracker doesn't support manual object calibration
                    // Balls are automatically identified by color
                    response.set_success(false);
                    response.set_message("Manual object calibration not supported in SimpleBallTracker. Use color calibration instead.");
                    break;
                case juggler::v1::CommandRequest::SET_POSE_MODEL_ENABLED:
                    // Note: Pose model is always enabled in SimpleBallTracker
                    response.set_message("Pose model is always enabled in SimpleBallTracker");
                    break;
                case juggler::v1::CommandRequest::CALIBRATE_COLOR:
                    if (simple_tracker_) {
                        cv::Point click_point(command.click_x(), command.click_y());
                        std::string error_message;
                        bool success = simple_tracker_->calibrateColor(command.color_name(), click_point, error_message);
                        
                        if (success) {
                            response.set_message("Color profile '" + command.color_name() + "' calibrated successfully");
                        } else {
                            response.set_success(false);
                            response.set_message(error_message);
                        }
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

        // Check if any visualizations are enabled
        bool has_visualizations = record_with_yolo_boxes_ || record_with_bytetrack_boxes_ ||
                                 visualization_states_.show_kalman_predictions() ||
                                 visualization_states_.show_raw_detections() ||
                                 visualization_states_.show_filtered_detections() ||
                                 visualization_states_.show_associations() ||
                                 visualization_states_.show_new_trackers() ||
                                 visualization_states_.show_hand_tracking() ||
                                 visualization_states_.show_ball_states() ||
                                 visualization_states_.show_occlusion() ||
                                 visualization_states_.show_skeleton() ||
                                 visualization_states_.show_color_search() ||
                                 visualization_states_.show_color_tracker() ||
                                 visualization_states_.show_tracked_boxes() ||
                                 visualization_states_.show_unmatched_detections() ||
                                 visualization_states_.show_tails();

        if (has_visualizations) {
            fs::path recording_dir_with_viz = recording_dir / "with_visualizations";
            fs::create_directories(recording_dir_with_viz);

            int frame_num_viz = 0;
            for (const auto& rec_frame : frame_buffer_) {
                cv::Mat frame_with_viz = renderVisualizationsOnFrame(rec_frame.frame, rec_frame);
                std::string filename = ss.str() + "_frame_" + std::to_string(frame_num_viz++) + "_viz.jpg";
                fs::path filepath = recording_dir_with_viz / filename;
                cv::imwrite(filepath.string(), frame_with_viz);
            }
            INFO_LOG("Saved ", frame_buffer_.size(), " frames with visualizations to ", recording_dir_with_viz.string());
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

        // Check if any visualizations are enabled
        bool has_visualizations = record_with_yolo_boxes_ || record_with_bytetrack_boxes_ ||
                                 visualization_states_.show_kalman_predictions() ||
                                 visualization_states_.show_raw_detections() ||
                                 visualization_states_.show_filtered_detections() ||
                                 visualization_states_.show_associations() ||
                                 visualization_states_.show_new_trackers() ||
                                 visualization_states_.show_hand_tracking() ||
                                 visualization_states_.show_ball_states() ||
                                 visualization_states_.show_occlusion() ||
                                 visualization_states_.show_skeleton() ||
                                 visualization_states_.show_color_search() ||
                                 visualization_states_.show_color_tracker() ||
                                 visualization_states_.show_tracked_boxes() ||
                                 visualization_states_.show_unmatched_detections() ||
                                 visualization_states_.show_tails();

        if (has_visualizations) {
            fs::path recording_dir_with_viz = recording_dir / "with_visualizations";
            fs::create_directories(recording_dir_with_viz);

            int frame_num_viz = 0;
            for (const auto& rec_frame : continuous_frame_buffer_) {
                cv::Mat frame_with_viz = renderVisualizationsOnFrame(rec_frame.frame, rec_frame);
                std::string filename = continuous_recording_session_ + "_frame_" + std::to_string(frame_num_viz++) + "_viz.jpg";
                fs::path filepath = recording_dir_with_viz / filename;
                cv::imwrite(filepath.string(), frame_with_viz);
            }
            INFO_LOG("Saved ", continuous_frame_buffer_.size(), " frames with visualizations to ", recording_dir_with_viz.string());
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
cv::Mat Engine::renderVisualizationsOnFrame(const cv::Mat& frame, const RecordingFrame& rec_frame) {
    cv::Mat result = frame.clone();
    const auto& viz = rec_frame.viz_states;
    
    // Prepare info panel data
    std::vector<std::string> info_lines;
    std::vector<cv::Scalar> info_colors;
    
    // Draw YOLO detections with numbering
    if (record_with_yolo_boxes_ || viz.show_raw_detections()) {
        int det_num = 1;
        for (const auto& det : rec_frame.raw_detections) {
            // Draw red box for YOLO detection
            cv::rectangle(result, det.box, cv::Scalar(0, 0, 255), 2);
            
            // Draw detection number on the box
            std::string num_label = "#" + std::to_string(det_num);
            cv::putText(result, num_label, 
                       cv::Point(det.box.x + 5, det.box.y + 20),
                       cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 0, 0), 3, cv::LINE_AA);
            cv::putText(result, num_label, 
                       cv::Point(det.box.x + 5, det.box.y + 20),
                       cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(255, 255, 255), 1, cv::LINE_AA);
            
            // Add YOLO info to panel
            std::string class_name = (det.class_id == 0) ? "ball" : "ball_held";
            char info_text[128];
            snprintf(info_text, sizeof(info_text), "#%d YOLO: %s conf=%.2f", 
                     det_num, class_name.c_str(), det.confidence);
            info_lines.push_back(info_text);
            info_colors.push_back(cv::Scalar(255, 255, 255)); // White for YOLO
            
            det_num++;
        }
    }
    
    // Draw ByteTrack boxes (legacy support)
    if (record_with_bytetrack_boxes_ || viz.show_tracked_boxes()) {
        for (const auto& obj : rec_frame.tracked_objects) {
            cv::rectangle(result, obj.box, cv::Scalar(0, 165, 255), 3); // Orange for ByteTrack, thicker
        }
    }
    
    // Draw hand tracking
    if (viz.show_hand_tracking() || viz.show_skeleton()) {
        for (const auto& hand : rec_frame.tracked_hands) {
            // Project 3D wrist position to 2D
            if (hand.wrist_pos_3d.z > 0) {
                int wrist_x = static_cast<int>((hand.wrist_pos_3d.x * camera_intrinsics_.fx) / hand.wrist_pos_3d.z + camera_intrinsics_.ppx);
                int wrist_y = static_cast<int>((hand.wrist_pos_3d.y * camera_intrinsics_.fy) / hand.wrist_pos_3d.z + camera_intrinsics_.ppy);
                
                // Draw wrist circle
                cv::circle(result, cv::Point(wrist_x, wrist_y), 20, cv::Scalar(255, 255, 0), 4); // Cyan
                
                // Draw hand label
                std::string label = hand.id == 0 ? "L" : "R";
                cv::putText(result, label, cv::Point(wrist_x - 5, wrist_y + 5),
                           cv::FONT_HERSHEY_SIMPLEX, 1.2, cv::Scalar(255, 255, 255), 2);
                
                // Draw skeleton if enabled
                if (viz.show_skeleton()) {
                    for (const auto& kp : hand.keypoints) {
                        if (kp.z > 0) {
                            int kp_x = static_cast<int>((kp.x * camera_intrinsics_.fx) / kp.z + camera_intrinsics_.ppx);
                            int kp_y = static_cast<int>((kp.y * camera_intrinsics_.fy) / kp.z + camera_intrinsics_.ppy);
                            cv::circle(result, cv::Point(kp_x, kp_y), 4, cv::Scalar(255, 255, 0), -1);
                        }
                    }
                }
            }
        }
    }
    
    // Draw color-tracked balls
    if (viz.show_color_tracker()) {
        // Load color profiles to get RGB colors for each ball
        std::map<std::string, cv::Scalar> color_map;
        try {
            std::ifstream color_file("hub/color_profiles.json");
            if (color_file.is_open()) {
                nlohmann::json color_profiles;
                color_file >> color_profiles;
                
                for (const auto& profile : color_profiles) {
                    std::string name = profile["name"];
                    std::vector<int> rgb = profile["rgb"];
                    // Convert RGB to BGR for OpenCV
                    color_map[name] = cv::Scalar(rgb[2], rgb[1], rgb[0]);
                }
            }
        } catch (...) {
            // If loading fails, use default colors
        }
        
        for (const auto& ball : rec_frame.tracked_balls) {
            if (!ball.has_yolo_detection) continue;  // Only draw active balls
            
            int center_x = static_cast<int>(ball.pixel_pos.x);
            int center_y = static_cast<int>(ball.pixel_pos.y);
            int radius = 12;
            
            // Get color for this ball
            cv::Scalar color = cv::Scalar(255, 255, 255);  // Default white
            auto it = color_map.find(ball.color_name);
            if (it != color_map.end()) {
                color = it->second;
            }
            
            // Draw based on whether ball is held
            if (ball.is_held) {
                // Dashed circle for held balls (draw as regular circle with thicker line)
                cv::circle(result, cv::Point(center_x, center_y), radius, color, 3, cv::LINE_AA);
            } else {
                // Filled circle for in-air balls
                cv::circle(result, cv::Point(center_x, center_y), radius, color, -1, cv::LINE_AA);
                // Black border for visibility
                if (ball.color_name == "white") {
                    cv::circle(result, cv::Point(center_x, center_y), radius, cv::Scalar(0, 0, 0), 3, cv::LINE_AA);
                } else {
                    cv::circle(result, cv::Point(center_x, center_y), radius, cv::Scalar(0, 0, 0), 1, cv::LINE_AA);
                }
            }
            
            // Draw simple label on ball (just first letter)
            std::string label = ball.color_name.substr(0, 1);
            cv::putText(result, label, cv::Point(center_x - 5, center_y + 5),
                       cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 0, 0), 3, cv::LINE_AA);
            cv::putText(result, label, cv::Point(center_x - 5, center_y + 5),
                       cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(255, 255, 255), 1, cv::LINE_AA);
            
            // Add color tracker info to panel
            char info_text[256];
            std::string state = ball.is_held ? "HELD" : "FLIGHT";
            std::string hand_info = "";
            if (ball.is_held && ball.held_by_hand_id >= 0) {
                hand_info = " [" + std::string(ball.held_by_hand_id == 0 ? "L" : "R") + "]";
            }
            
            snprintf(info_text, sizeof(info_text), "%s: %s%s z=%.2fm | %s", 
                     ball.color_name.c_str(), state.c_str(), hand_info.c_str(),
                     ball.position.z, ball.tracking_reason.c_str());
            info_lines.push_back(info_text);
            info_colors.push_back(color);
        }
    
    // Draw color-based prediction circles
    // NEW: Shows predicted search region based on color detection history
    if (viz.show_kalman_predictions()) {
        for (const auto& ball : rec_frame.tracked_balls) {
            // Only draw if we have enough history
            if (!ball.color_predictor.hasEnoughData()) {
                continue;
            }
            
            // Get predicted position from color-based predictor
            cv::Point3f pred_pos_3d = ball.color_predictor.getPredictedPosition(!ball.is_held);
            
            // Skip if prediction failed
            if (pred_pos_3d.z <= 0) {
                continue;
            }
            
            // Project to 2D
            int pred_x = static_cast<int>((pred_pos_3d.x * camera_intrinsics_.fx) / pred_pos_3d.z + camera_intrinsics_.ppx);
            int pred_y = static_cast<int>((pred_pos_3d.y * camera_intrinsics_.fy) / pred_pos_3d.z + camera_intrinsics_.ppy);
            
            // Get prediction radius from settings (in meters)
            float uncertainty_meters = ball.color_predictor.getPredictionRadius();
            
            // Project uncertainty to pixel space
            float uncertainty_pixels = (uncertainty_meters * camera_intrinsics_.fx) / pred_pos_3d.z;
            int radius = static_cast<int>(uncertainty_pixels);
            
            // Clamp radius to reasonable bounds
            radius = std::max(20, std::min(radius, 150));
            
            // Choose color based on ball state
            cv::Scalar circle_color;
            if (ball.is_held) {
                circle_color = cv::Scalar(100, 100, 255);  // Red-ish for held balls (no gravity)
            } else {
                circle_color = cv::Scalar(255, 255, 100);  // Cyan-ish for in-flight balls (with gravity)
            }
            
            // Draw semi-transparent circle showing prediction search region
            cv::Mat overlay = result.clone();
            cv::circle(overlay, cv::Point(pred_x, pred_y), radius, circle_color, 2, cv::LINE_AA);
            cv::addWeighted(overlay, 0.5, result, 0.5, 0, result);
            
            // Draw center point
            cv::circle(result, cv::Point(pred_x, pred_y), 4, circle_color, -1, cv::LINE_AA);
            
            // Draw label with history size
            std::string label = "P" + std::to_string(ball.id) + "(" + std::to_string(ball.color_predictor.getHistorySize()) + ")";
            cv::putText(result, label, cv::Point(pred_x + 10, pred_y - 10),
                       cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0, 0), 3, cv::LINE_AA);
            cv::putText(result, label, cv::Point(pred_x + 10, pred_y - 10),
                       cv::FONT_HERSHEY_SIMPLEX, 0.5, circle_color, 1, cv::LINE_AA);
        }
    }
    
    // Draw info panel in upper left corner with text wrapping
    if (!info_lines.empty()) {
        int panel_x = 10;  // Left side with small margin
        int panel_y = 10;  // Top margin
        int line_height = 22;
        int max_width = result.cols - 20;  // Maximum width (full image width minus margins)
        float font_scale = 0.45f;
        int font_thickness = 1;
        int font_face = cv::FONT_HERSHEY_SIMPLEX;
        
        // Process each info line and wrap if needed
        std::vector<std::string> wrapped_lines;
        std::vector<cv::Scalar> wrapped_colors;
        
        for (size_t i = 0; i < info_lines.size(); i++) {
            std::string line = info_lines[i];
            cv::Scalar color = info_colors[i];
            
            // Measure text width
            int baseline = 0;
            cv::Size text_size = cv::getTextSize(line, font_face, font_scale, font_thickness, &baseline);
            
            if (text_size.width <= max_width) {
                // Line fits, add as-is
                wrapped_lines.push_back(line);
                wrapped_colors.push_back(color);
            } else {
                // Line is too long, need to wrap
                std::string current_line;
                std::istringstream words(line);
                std::string word;
                
                while (words >> word) {
                    std::string test_line = current_line.empty() ? word : current_line + " " + word;
                    cv::Size test_size = cv::getTextSize(test_line, font_face, font_scale, font_thickness, &baseline);
                    
                    if (test_size.width <= max_width) {
                        current_line = test_line;
                    } else {
                        // Current line is full, save it and start new line
                        if (!current_line.empty()) {
                            wrapped_lines.push_back(current_line);
                            wrapped_colors.push_back(color);
                        }
                        current_line = word;
                    }
                }
                
                // Add remaining text
                if (!current_line.empty()) {
                    wrapped_lines.push_back(current_line);
                    wrapped_colors.push_back(color);
                }
            }
        }
        
        // Calculate panel dimensions based on wrapped lines
        int panel_height = wrapped_lines.size() * line_height + 10;
        
        // Draw semi-transparent background
        cv::Mat overlay = result.clone();
        cv::rectangle(overlay,
                     cv::Point(panel_x - 5, panel_y - 5),
                     cv::Point(max_width + 10, panel_y + panel_height),
                     cv::Scalar(0, 0, 0), -1);
        cv::addWeighted(overlay, 0.7, result, 0.3, 0, result);
        
        // Draw each wrapped line
        for (size_t i = 0; i < wrapped_lines.size(); i++) {
            int y = panel_y + (i + 1) * line_height - 5;
            
            // Draw black outline for readability
            cv::putText(result, wrapped_lines[i],
                       cv::Point(panel_x, y),
                       font_face, font_scale, cv::Scalar(0, 0, 0), 3, cv::LINE_AA);
            
            // Draw colored text
            cv::putText(result, wrapped_lines[i],
                       cv::Point(panel_x, y),
                       font_face, font_scale, wrapped_colors[i], font_thickness, cv::LINE_AA);
        }
    }
    }
    
    return result;
}
