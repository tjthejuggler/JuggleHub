
#include "Engine.hpp"
#include "DebugLog.hpp"
#include "modules/UdpBallColorModule.hpp"
#include "modules/PositionToRgbModule.hpp"
#include "SimpleBallTracker.hpp"
#include "Simple2DBallTracker.hpp"
#include "New3DTracker.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <iomanip>
#include <filesystem>
#include <opencv2/imgcodecs.hpp>

namespace fs = std::filesystem;

// External debug log function from main.cpp
extern void writeDebugLog(const std::string& message);

Engine::Engine(const std::string& camera_settings_path, const std::string& device_name, const std::string& model_name, const std::string& pose_model_name, OutputFormat format, bool use_dnn_tracker, bool verbose)
    : output_format_(format),
      running_(false),
      color_module_(std::make_unique<UdpBallColorModule>()),
      current_tracker_type_("depth_based"),  // Default to depth-based tracking
      use_dnn_tracker_(use_dnn_tracker),
      verbose_(verbose),
      zmq_context_(1),
      zmq_publisher_(zmq_context_, ZMQ_PUB),
      frame_counter_(0),
      record_with_yolo_boxes_(false),
      video_feed_enabled_(true) {  // Start with video feed enabled by default
   writeDebugLog("Engine constructor: Initializing...");
   
   // Initialize managers
   writeDebugLog("Engine constructor: Initializing CameraManager...");
   camera_manager_ = std::make_unique<CameraManager>();
   
   writeDebugLog("Engine constructor: Initializing RecordingManager...");
   recording_manager_ = std::make_unique<RecordingManager>();
   
   writeDebugLog("Engine constructor: Initializing PlaybackController...");
   playback_controller_ = std::make_unique<PlaybackController>();
   
   writeDebugLog("Engine constructor: Initializing VisualizationRenderer...");
   visualization_renderer_ = std::make_unique<VisualizationRenderer>();
   
   // Bind ZMQ publisher socket
   writeDebugLog("Engine constructor: Binding ZMQ publisher socket...");
   zmq_publisher_.bind("tcp://127.0.0.1:5555");
   writeDebugLog("Engine constructor: ZMQ publisher socket bound successfully");

    // Initialize default tracker (depth-based SimpleBallTracker)
    try {
        writeDebugLog("Engine constructor: Initializing default tracker (depth_based)...");
        const std::string ball_model_path = "engine/models/" + model_name + ".xml";
        const std::string pose_model_path = "engine/models/" + pose_model_name + ".xml";
        writeDebugLog("Engine constructor: Ball model path: " + ball_model_path);
        writeDebugLog("Engine constructor: Pose model path: " + pose_model_path);
        
        simple_tracker_ = std::make_shared<SimpleBallTracker>(
            ball_model_path, pose_model_path, device_name, "hub/ball_settings.json");
        tracker_ = simple_tracker_;  // Set polymorphic pointer to default tracker
        writeDebugLog("Engine constructor: Default tracker initialized successfully");
        
        // Initialize 2D-only tracker
        simple_2d_tracker_ = std::make_shared<Simple2DBallTracker>(
            ball_model_path, pose_model_path, device_name);
        writeDebugLog("Engine constructor: 2D tracker initialized successfully");
        
        // Initialize New3D tracker
        new_3d_tracker_ = std::make_shared<New3DTracker>(
            ball_model_path, pose_model_path, device_name, "hub/calibration_settings_new3d.json");
        writeDebugLog("Engine constructor: New3D tracker initialized successfully");
    } catch (const std::exception& e) {
        writeDebugLog("Engine constructor: EXCEPTION in tracker init: " + std::string(e.what()));
        return;
    }

    // Setup the default color module
    writeDebugLog("Engine constructor: Setting up color module...");
    color_module_->setup();
    
    // Initialize CommandProcessor with all dependencies
    writeDebugLog("Engine constructor: Initializing CommandProcessor...");
    command_processor_ = std::make_unique<CommandProcessor>(zmq_context_);
    command_processor_->setDependencies(
        camera_manager_.get(),
        recording_manager_.get(),
        playback_controller_.get(),
        color_module_.get(),
        &visualization_states_,
        &record_with_yolo_boxes_,
        &video_feed_enabled_,
        &current_tracker_type_
    );
    command_processor_->setTrackerReferences(tracker_, simple_tracker_, simple_2d_tracker_, new_3d_tracker_);
    command_processor_->setTrackerSwitchCallback([this](const std::string& type) {
        this->setTrackerType(type);
    });
    
    // Set tracker reference for RecordingManager (will be updated when tracker switches)
    recording_manager_->setTracker(tracker_.get());
    
    writeDebugLog("Engine constructor: Initialization complete");
}

Engine::~Engine() {
    stop();
}

void Engine::run() {
    writeDebugLog("Engine::run() - Starting...");
    running_ = true;
    
    // Start command processing thread
    writeDebugLog("Engine::run() - Starting command processing thread...");
    command_processor_->start();
    std::thread command_thread([this]() {
        command_processor_->processCommands();
    });
    writeDebugLog("Engine::run() - Command thread started");
    
    // Initialize and start the camera with default settings using CameraManager
    writeDebugLog("Engine::run() - Initializing camera...");
    camera_manager_->initialize("", 640, 480, 60);  // Default settings
    writeDebugLog("Engine::run() - Camera initialized");
    
    writeDebugLog("Engine::run() - Starting camera...");
    camera_manager_->start();
    writeDebugLog("Engine::run() - Camera started");

    // Initialize settings module with current tracker and use_dnn_tracker flag
    writeDebugLog("Engine::run() - Initializing settings module...");
    settings_module_ = std::make_unique<juggler::modules::UdpBallSettingsModule>(tracker_, &use_dnn_tracker_);
    settings_module_->setup();
    writeDebugLog("Engine::run() - Settings module initialized");

    writeDebugLog("Engine::run() - Entering main loop...");
    int loop_iteration = 0;
    while (running_) {
        loop_iteration++;
        if (loop_iteration % 30 == 0) {  // Log every 30 frames to avoid spam
            writeDebugLog("Engine::run() - Main loop iteration: " + std::to_string(loop_iteration));
        }
        
        cv::Mat color_image, depth_image;
        bool frame_acquired = false;
        
        // ========== PLAYBACK MODE OR LIVE CAMERA MODE ==========
        if (playback_controller_->isActive() && playback_controller_->isLoaded()) {
            // PLAYBACK MODE
            if (!playback_controller_->isPaused()) {
                // Get next frame with timing
                if (playback_controller_->getNextFrame(color_image, depth_image, camera_manager_->getFPS())) {
                    frame_acquired = true;
                } else {
                    // Waiting for next frame or end of playback
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }
            } else {
                // Paused - get current frame
                if (playback_controller_->getCurrentFrame(color_image, depth_image)) {
                    frame_acquired = true;
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }
            }
        } else if (camera_manager_->isRunning()) {
            // LIVE CAMERA MODE - use CameraManager
            if (camera_manager_->getFrames(color_image, depth_image)) {
                frame_acquired = true;
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
        } else {
            // Neither playback nor camera running
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        
        // ========== PROCESS FRAME (same for both modes) ==========
        if (!frame_acquired || color_image.empty()) {
            continue;
        }
        
        // Frames are already cached in CameraManager
        
        juggler::v1::FrameData frame_data;
        frame_data.set_timestamp_us(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
        frame_data.set_frame_number(frame_counter_++);

        // Populate SystemStatus with mode and playback information
        auto* status = frame_data.mutable_status();
        status->set_camera_connected(camera_manager_->isRunning());
        status->set_engine_running(running_);
        status->set_frame_count(frame_counter_);
        status->set_timestamp_us(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
        
        // Set mode based on current state
        if (playback_controller_->isActive() && playback_controller_->isLoaded()) {
            status->set_mode("playback");
            status->set_playback_mode(true);
            status->set_playback_directory(playback_controller_->getRecordingDirectory());
            status->set_playback_current_frame(playback_controller_->getCurrentFrameNumber());
            status->set_playback_total_frames(playback_controller_->getTotalFrames());
            status->set_playback_paused(playback_controller_->isPaused());
            status->set_playback_speed(playback_controller_->getSpeed());
        } else {
            status->set_mode("live");
            status->set_playback_mode(false);
        }

        // Only encode JPG if video feed is enabled (FPS optimization)
        if (video_feed_enabled_) {
            // Clone the color image so we can draw visualizations on it without affecting the original
            cv::Mat display_image = color_image.clone();
            
            // ALWAYS log YOLO detected colors to engine_debug.log (regardless of visualization toggle)
            if (!last_raw_detections_.empty()) {
                INFO_LOG("=== YOLO DETECTED COLORS - Frame ", frame_counter_, " ===");
                int det_num = 0;
                for (const auto& det : last_raw_detections_) {
                    det_num++;
                    int center_x = static_cast<int>(det.box.x + det.box.width / 2);
                    int center_y = static_cast<int>(det.box.y + det.box.height / 2);
                    
                    if (center_x >= 0 && center_x < color_image.cols && center_y >= 0 && center_y < color_image.rows) {
                        cv::Vec3b bgr_pixel = color_image.at<cv::Vec3b>(center_y, center_x);
                        INFO_LOG("  Detection #", det_num, " at (", center_x, ",", center_y,
                                ") - Class: ", (det.class_id == 0 ? "ball" : "ball_held"),
                                " - BGR: (", (int)bgr_pixel[0], ",", (int)bgr_pixel[1], ",", (int)bgr_pixel[2], ")");
                    }
                }
            }
            
            // Draw YOLO Color Calibration Squares on real-time feed if enabled
            if (visualization_states_.show_yolo_color_calibration() && !last_raw_detections_.empty()) {
                INFO_LOG("=== YOLO Color Squares - Frame ", frame_counter_, " ===");
                int det_num = 0;
                for (const auto& det : last_raw_detections_) {
                    det_num++;
                    cv::Vec3b bgr_pixel = det.detected_bgr_color;
                    cv::Scalar actual_color(bgr_pixel[0], bgr_pixel[1], bgr_pixel[2]);
                    
                    int center_x = static_cast<int>(det.box.x + det.box.width / 2);
                    int center_y = static_cast<int>(det.box.y + det.box.height / 2);
                    
                    INFO_LOG("  Detection #", det_num, " at (", center_x, ",", center_y,
                            ") - Sampled BGR: (", (int)bgr_pixel[0], ",", (int)bgr_pixel[1], ",", (int)bgr_pixel[2],
                            ") - Square drawn at (", (int)det.box.x, ",", (int)det.box.y, ")");
                    
                    int square_x = static_cast<int>(det.box.x);
                    int square_y = static_cast<int>(det.box.y);
                    int square_size = 8;
                    
                    cv::rectangle(display_image,
                                cv::Rect(square_x, square_y, square_size, square_size),
                                actual_color, -1);
                    
                    cv::rectangle(display_image,
                                cv::Rect(square_x, square_y, square_size, square_size),
                                cv::Scalar(0, 0, 0), 1);
                }
            }
            
            std::vector<uchar> buf;
            std::vector<int> compression_params;
            compression_params.push_back(cv::IMWRITE_JPEG_QUALITY);
            compression_params.push_back(70);  // 70% quality for faster encoding
            cv::imencode(".jpg", display_image, buf, compression_params);
            frame_data.set_color_image_b64(buf.data(), buf.size());
        }
        frame_data.set_ir_projector_active(camera_manager_->isIRProjectorActive());

        // --- BALL TRACKING CODE ---
        std::vector<SimpleBall> tracked_balls;
        std::vector<BallEvent> ball_events;
        std::vector<SimpleHand> tracked_hands;
        
        // ALWAYS run tracker for pose detection
        if (tracker_) {
            // Set recording frame number based on whether we're actively recording
            if (recording_manager_->isContinuousRecording()) {
                tracker_->setRecordingFrameNumber(recording_manager_->getContinuousBufferSize());
            } else {
                tracker_->setRecordingFrameNumber(-1);
            }
            
            // Update current tracker
            auto [balls, events] = tracker_->update(color_image, depth_image, camera_manager_->getIntrinsics());
            tracked_balls = balls;
            ball_events = events;
            tracked_hands = tracker_->getHands();
            
            // Get the raw detections for recording/visualization
            last_raw_detections_ = tracker_->getLastRawDetections();
            
            // PERFORMANCE FIX: Only evaluate override criteria when recording
            if (recording_manager_->isContinuousRecording() || recording_manager_->getBufferSize() > 0) {
                tracker_->evaluateOverrideCriteria(last_raw_detections_, color_image);
            }

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
                
                auto* velocity = hand->mutable_velocity_3d();
                velocity->set_x(hand_obj.velocity.x);
                velocity->set_y(hand_obj.velocity.y);
                velocity->set_z(hand_obj.velocity.z);
                hand->set_has_valid_velocity(hand_obj.has_valid_velocity);
                
                for (const auto& hist_pos : hand_obj.position_history) {
                    auto* hist = hand->add_position_history();
                    hist->set_x(hist_pos.x);
                    hist->set_y(hist_pos.y);
                    hist->set_z(hist_pos.z);
                }

                cv::Point2f wrist_2d = SimpleBallTracker::project_3d_to_2d(hand_obj.wrist_pos_3d, camera_manager_->getIntrinsics());
                auto* hand_pos_2d = hand->mutable_position_2d();
                hand_pos_2d->set_x(wrist_2d.x);
                hand_pos_2d->set_y(wrist_2d.y);

                for (const auto& kp : hand_obj.keypoints) {
                    auto* keypoint = hand->add_keypoints();
                    
                    cv::Point2f kp_2d = SimpleBallTracker::project_3d_to_2d(kp, camera_manager_->getIntrinsics());
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

            // Convert SimpleBall to TrackedObject for recording (temporary compatibility)
            std::vector<TrackedObject> tracked_objects_compat;
            for (const auto& ball : tracked_balls) {
                TrackedObject obj;
                obj.box = ball.bbox;
                obj.world_pos = ball.position;
                obj.id = ball.id;
                obj.class_id = ball.yolo_class_id;
                obj.class_name = ball.is_held ? "ball_held" : "ball";
                obj.status = TrackerStatus::TRACKED;
                obj.logical_id = ball.id;
                obj.is_left = false;
                tracked_objects_compat.push_back(obj);
            }
            
            // Convert SimpleHand to TrackedHand for recording
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

            // Add to frame buffers using RecordingManager
            RecordingFrame rec_frame = {color_image.clone(), depth_image.clone(), last_raw_detections_,
                                       tracked_objects_compat, tracked_hands_compat, tracked_balls,
                                       tracked_hands, ball_events, visualization_states_};
            recording_manager_->addFrame(rec_frame);
            if (recording_manager_->isContinuousRecording()) {
                recording_manager_->addContinuousFrame(rec_frame);
            }
        }
        
        // Draw hand threshold circles on real-time feed if enabled
        if (video_feed_enabled_ && visualization_states_.show_hand_threshold() && tracker_ && !tracked_hands.empty()) {
            cv::Mat display_with_viz = color_image.clone();
            tracker_->drawHandThresholds(display_with_viz, tracked_hands, camera_manager_->getIntrinsics());
            
            std::vector<uchar> buf;
            std::vector<int> compression_params;
            compression_params.push_back(cv::IMWRITE_JPEG_QUALITY);
            compression_params.push_back(70);
            cv::imencode(".jpg", display_with_viz, buf, compression_params);
            frame_data.set_color_image_b64(buf.data(), buf.size());
        }
        
        // Populate trajectory-based predictions for visualization
        if (tracker_) {
            for (const auto& ball : tracked_balls) {
                if (ball.trajectory.verified_point_count < 3) {
                    continue;
                }
                
                auto* traj_pred = frame_data.add_trajectory_predictions();
                traj_pred->set_logical_id(ball.id);
                
                cv::Point3f pred_pos_3d(0, 0, 0);
                if (!ball.trajectory.predicted_path.empty() && ball.trajectory.prediction_valid) {
                    pred_pos_3d = ball.trajectory.predicted_path[0];
                }
                
                if (pred_pos_3d.z <= 0) {
                    continue;
                }
                
                auto* pred_pos = traj_pred->mutable_predicted_pos();
                pred_pos->set_x(pred_pos_3d.x);
                pred_pos->set_y(pred_pos_3d.y);
                pred_pos->set_z(pred_pos_3d.z);
                
                cv::Point2f pred_pos_2d = SimpleBallTracker::project_3d_to_2d(pred_pos_3d, camera_manager_->getIntrinsics());
                auto* pred_2d = traj_pred->mutable_predicted_pos_2d();
                pred_2d->set_x(pred_pos_2d.x);
                pred_2d->set_y(pred_pos_2d.y);
                
                traj_pred->set_is_in_freefall(!ball.is_held);
            }
        }
        
        // Populate balls from SimpleBall
        for (auto& ball : tracked_balls) {
            if (ball.position.z <= 0) continue;
            
            // Sample the detected BGR color if ball has a YOLO detection
            if (ball.has_yolo_detection && !color_image.empty()) {
                for (const auto& det : last_raw_detections_) {
                    float det_center_x = det.box.x + det.box.width / 2;
                    float det_center_y = det.box.y + det.box.height / 2;
                    
                    float dx = ball.pixel_pos.x - det_center_x;
                    float dy = ball.pixel_pos.y - det_center_y;
                    float dist = std::sqrt(dx*dx + dy*dy);
                    
                    if (dist < 50.0f) {
                        int center_x = static_cast<int>(det_center_x);
                        int center_y = static_cast<int>(det_center_y);
                        
                        if (center_x >= 0 && center_x < color_image.cols &&
                            center_y >= 0 && center_y < color_image.rows) {
                            ball.detected_bgr_color = color_image.at<cv::Vec3b>(center_y, center_x);
                        }
                        break;
                    }
                }
            }

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
            
            cv::Point2f projected_pos = SimpleBallTracker::project_3d_to_2d(ball.position, camera_manager_->getIntrinsics());
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
            color_ball->set_frames_since_seen(0);
            
            // Add ball state
            auto* ball_state = frame_data.add_ball_states();
            ball_state->set_logical_id(ball.id);
            if (ball.is_held) {
                ball_state->set_state(juggler::v1::BallState::HELD);
                ball_state->set_associated_hand_id(ball.held_by_hand_id);
            } else {
                ball_state->set_state(juggler::v1::BallState::IN_FLIGHT);
                ball_state->set_associated_hand_id(-1);
                
                ball_state->set_verified_point_count(ball.trajectory.verified_point_count);
                for (const auto& traj_point : ball.trajectory.points) {
                    if (traj_point.verified) {
                        auto* point_pb = ball_state->add_trajectory_points();
                        auto* pos = point_pb->mutable_position();
                        pos->set_x(traj_point.position.x);
                        pos->set_y(traj_point.position.y);
                        pos->set_z(traj_point.position.z);
                        point_pb->set_timestamp_us(traj_point.timestamp);
                        point_pb->set_verified(traj_point.verified);
                        point_pb->set_confidence(traj_point.confidence);
                    }
                }
            }
            ball_state->set_frames_in_state(0);
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
            
            cv::Point3f ball_position(0, 0, 0);
            float confidence = 0.8f;
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
        }

        if (loop_iteration % 30 == 0) {
            writeDebugLog("Engine::run() - Updating active module...");
        }
        if (command_processor_->hasActiveModule()) {
            command_processor_->updateActiveModule(frame_data, [this](const juggler::v1::CommandRequest& command) {
                command_processor_->sendCommand(command);
            });
        }

        if (loop_iteration % 30 == 0) {
            writeDebugLog("Engine::run() - Serializing and publishing frame data...");
        }
        // Publish FrameData
        std::string serialized_data;
        frame_data.SerializeToString(&serialized_data);

        zmq::message_t message(serialized_data.size());
        memcpy(message.data(), serialized_data.c_str(), serialized_data.size());
        zmq_publisher_.send(message, zmq::send_flags::dontwait);
        
        if (loop_iteration % 30 == 0) {
            writeDebugLog("Engine::run() - Frame published successfully");
        }
    }

    writeDebugLog("Engine::run() - Exited main loop, joining command thread...");
    command_processor_->stop();
    command_thread.join();
    writeDebugLog("Engine::run() - Command thread joined, exiting run()");
}

void Engine::stop() {
    running_ = false;
    if (settings_module_) {
        settings_module_->cleanup();
    }
}

void Engine::setTrackerType(const std::string& tracker_type) {
    writeDebugLog("setTrackerType() - Switching to: " + tracker_type);
    
    if (tracker_type == current_tracker_type_) {
        writeDebugLog("setTrackerType() - Already using " + tracker_type + ", no change needed");
        return;
    }
    
    if (tracker_type == "depth_based") {
        if (!simple_tracker_) {
            throw std::runtime_error("SimpleBallTracker not initialized");
        }
        tracker_ = simple_tracker_;
        current_tracker_type_ = "depth_based";
        writeDebugLog("setTrackerType() - Switched to depth_based tracker");
        
    } else if (tracker_type == "simple_2d") {
        if (!simple_2d_tracker_) {
            throw std::runtime_error("Simple2DBallTracker not initialized");
        }
        tracker_ = simple_2d_tracker_;
        current_tracker_type_ = "simple_2d";
        writeDebugLog("setTrackerType() - Switched to simple_2d tracker");
        
    } else if (tracker_type == "new_3d") {
        if (!new_3d_tracker_) {
            throw std::runtime_error("New3DTracker not initialized");
        }
        tracker_ = new_3d_tracker_;
        current_tracker_type_ = "new_3d";
        writeDebugLog("setTrackerType() - Switched to new_3d tracker");
        
    } else {
        throw std::runtime_error("Unknown tracker type: " + tracker_type);
    }
    
    // Update the settings module to use the new tracker
    if (settings_module_) {
        settings_module_->setTracker(tracker_);
        writeDebugLog("setTrackerType() - Updated settings module tracker pointer");
    }
    
    // Update the recording manager to use the new tracker
    if (recording_manager_) {
        recording_manager_->setTracker(tracker_.get());
        writeDebugLog("setTrackerType() - Updated recording manager tracker pointer");
    }
    
    INFO_LOG("Tracker switched to: ", tracker_type);
}
