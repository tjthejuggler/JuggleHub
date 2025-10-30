
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
cv::Mat renderVisualizationsOnFrame(const cv::Mat& frame, const RecordingFrame& rec_frame,
                                          const CameraIntrinsics& camera_intrinsics,
                                          const juggler::v1::VisualizationStates& viz_states,
                                          bool record_with_yolo_boxes,
                                          IBallTracker* tracker) {
    const auto& viz = viz_states;
    
    // Prepare info panel data
    std::vector<std::string> info_lines;
    std::vector<cv::Scalar> info_colors;
    
    // First, collect all info lines to determine required height
    cv::Mat temp_result = frame.clone();
    
    // Add throw/catch events at the top if they exist
    for (const auto& event : rec_frame.ball_events) {
        std::string hand_side = event.hand_id == 0 ? "LEFT" : "RIGHT";
        std::string event_type = event.type == BallEvent::THROW ? "THROW" : "CATCH";
        
        // Find the ball to get its color name and calculate distance
        std::string ball_color = "UNKNOWN";
        float distance = 0.0f;
        float threshold = 0.0f;
        
        for (const auto& ball : rec_frame.tracked_balls) {
            if (ball.id == event.ball_id) {
                ball_color = ball.color_name;
                
                // Calculate distance between ball and hand
                for (const auto& hand : rec_frame.tracked_hands_simple) {
                    if (hand.id == event.hand_id) {
                        float dx = ball.position.x - hand.wrist_pos_3d.x;
                        float dy = ball.position.y - hand.wrist_pos_3d.y;
                        float dz = ball.position.z - hand.wrist_pos_3d.z;
                        distance = std::sqrt(dx*dx + dy*dy + dz*dz);
                        break;
                    }
                }
                break;
            }
        }
        
        // Get actual threshold values from tracker settings
        if (tracker) {
            const auto& settings = tracker->getTrackingSettings();
            // Use unified hand_distance_threshold (legacy thresholds kept for backward compatibility)
            threshold = settings.hand_distance_threshold;
        } else {
            // Fallback to default values if tracker not available
            // Use default hand_distance_threshold
            threshold = 0.30f;  // default hand_distance_threshold
        }
        
        // Create event text with distance information
        // For THROW: Show that detection was found far from hand (distance > threshold)
        // For CATCH: Show that ball came close to hand (distance < threshold)
        char event_text[256];
        if (event.type == BallEvent::THROW) {
            // THROW: Detection was found at distance > threshold from hand
            snprintf(event_text, sizeof(event_text), "%s %s (%s) | detection %.3fm > %.3fm from hand",
                     event_type.c_str(), hand_side.c_str(), ball_color.c_str(),
                     distance, threshold);
        } else {
            // CATCH: Ball came within threshold distance of hand
            snprintf(event_text, sizeof(event_text), "%s %s (%s) | ball %.3fm < %.3fm to hand",
                     event_type.c_str(), hand_side.c_str(), ball_color.c_str(),
                     distance, threshold);
        }
        
        // Add to the beginning of info lines
        info_lines.insert(info_lines.begin(), std::string(event_text));
        
        // Color: Green for catch, Orange for throw
        cv::Scalar event_color = event.type == BallEvent::CATCH ?
                                 cv::Scalar(0, 255, 0) :      // Green for CATCH
                                 cv::Scalar(0, 165, 255);     // Orange for THROW
        info_colors.insert(info_colors.begin(), event_color);
    }
    
    // Draw raw YOLO detections (before filtering) - darker red, larger boxes
    if (viz.show_raw_detections()) {
        // Load color profiles for distance calculation
        std::vector<ColorProfile> color_profiles;
        try {
            std::ifstream color_file("hub/config/color_profiles.json");
            if (color_file.is_open()) {
                nlohmann::json color_profiles_json;
                color_file >> color_profiles_json;
                
                // Access the "profiles" array from the JSON structure
                if (color_profiles_json.contains("profiles") && color_profiles_json["profiles"].is_array()) {
                    for (const auto& profile : color_profiles_json["profiles"]) {
                    if (profile["enabled"]) {
                        ColorProfile cp;
                        cp.name = profile["name"];
                        cp.enabled = true;
                        cp.avg_hue = profile["avg_hue"];
                        cp.avg_saturation = profile["avg_saturation"];
                        color_profiles.push_back(cp);
                    }
                }
                }
            }
        } catch (...) {
            // If loading fails, continue without color distance info
        }
        
        int det_num = 1;
        for (const auto& det : rec_frame.raw_detections) {
            // Draw darker red box for raw YOLO detection (thicker line)
            cv::Rect enlarged_box = det.box;
            int enlarge = 5;  // Make box slightly larger
            enlarged_box.x -= enlarge;
            enlarged_box.y -= enlarge;
            enlarged_box.width += enlarge * 2;
            enlarged_box.height += enlarge * 2;
            
            cv::rectangle(temp_result, enlarged_box, cv::Scalar(0, 0, 139), 3);  // Dark red, thicker
            
            // Draw detection number on the box
            std::string num_label = "R#" + std::to_string(det_num);  // R for Raw
            cv::putText(temp_result, num_label,
                       cv::Point(det.box.x + 5, det.box.y + 20),
                       cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 0, 0), 3, cv::LINE_AA);
            cv::putText(temp_result, num_label,
                       cv::Point(det.box.x + 5, det.box.y + 20),
                       cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(200, 200, 255), 1, cv::LINE_AA);
            
            // Calculate color distances if we have color profiles
            std::string closest_color = "N/A";
            float min_distance = 999.0f;
            
            if (!color_profiles.empty()) {
                // Sample color at detection center
                int center_x = static_cast<int>(det.box.x + det.box.width / 2);
                int center_y = static_cast<int>(det.box.y + det.box.height / 2);
                
                if (center_x >= 0 && center_x < frame.cols && center_y >= 0 && center_y < frame.rows) {
                    // Convert BGR to HSV for the detection center
                    cv::Mat roi = frame(cv::Rect(center_x, center_y, 1, 1));
                    cv::Mat hsv_roi;
                    cv::cvtColor(roi, hsv_roi, cv::COLOR_BGR2HSV);
                    cv::Vec3b hsv_pixel = hsv_roi.at<cv::Vec3b>(0, 0);
                    float det_hue = hsv_pixel[0];
                    float det_sat = hsv_pixel[1];
                    
                    // Calculate euclidean distance to each color profile
                    for (const auto& profile : color_profiles) {
                        // Handle hue wrap-around (0-180 scale)
                        float hue_diff = std::abs(det_hue - profile.avg_hue);
                        if (hue_diff > 90.0f) {
                            hue_diff = 180.0f - hue_diff;
                        }
                        
                        float sat_diff = det_sat - profile.avg_saturation;
                        float distance = std::sqrt(hue_diff * hue_diff + sat_diff * sat_diff);
                        
                        if (distance < min_distance) {
                            min_distance = distance;
                            closest_color = profile.name;
                        }
                    }
                }
            }
            
            // Add raw YOLO info to panel with color distance
            std::string class_name = (det.class_id == 0) ? "ball" : "ball_held";
            char info_text[256];
            if (min_distance < 999.0f) {
                snprintf(info_text, sizeof(info_text), "R#%d RAW: %s conf=%.2f | closest=%s dist=%.1f",
                         det_num, class_name.c_str(), det.confidence, closest_color.c_str(), min_distance);
            } else {
                snprintf(info_text, sizeof(info_text), "R#%d RAW: %s conf=%.2f",
                         det_num, class_name.c_str(), det.confidence);
            }
            info_lines.push_back(info_text);
            info_colors.push_back(cv::Scalar(200, 200, 255)); // Light red for raw
            
            // Add override evaluation for each ball color
            for (const auto& eval : det.override_evals) {
                char override_text[512];
                snprintf(override_text, sizeof(override_text), "  [%s] %s",
                         eval.ball_color.c_str(), eval.reason.c_str());
                info_lines.push_back(override_text);
                // Color: green if would override, red if not
                cv::Scalar override_color = eval.would_override ?
                                           cv::Scalar(0, 255, 0) :    // Green for override
                                           cv::Scalar(0, 0, 255);     // Red for no override
                info_colors.push_back(override_color);
            }
            
            det_num++;
        }
    }
    
    // Draw YOLO Color Calibration Squares (8x8 colored squares)
    // CRITICAL: Shows the ACTUAL detected color from the pixel, NOT the matched color profile
    // This allows us to see what YOLO actually detected and understand why it may not match
    if (viz.show_yolo_color_calibration()) {
        // Only draw squares if we have detections
        if (!rec_frame.raw_detections.empty()) {
            writeDebugLog("=== RECORDING: YOLO Color Squares ===");
            int det_num = 0;
            for (const auto& det : rec_frame.raw_detections) {
                det_num++;
                // Sample color at detection center
                int center_x = static_cast<int>(det.box.x + det.box.width / 2);
                int center_y = static_cast<int>(det.box.y + det.box.height / 2);
                
                if (center_x >= 0 && center_x < frame.cols && center_y >= 0 && center_y < frame.rows) {
                    // Use the SAMPLED BGR color from detection (median-filtered, saturation-thresholded)
                    // This respects the configured color_sample_radius and min_saturation_threshold settings
                    cv::Vec3b bgr_pixel = det.detected_bgr_color;
                    cv::Scalar actual_color(bgr_pixel[0], bgr_pixel[1], bgr_pixel[2]);
                    
                    // LOG THE EXACT COLOR VALUES FOR RECORDING
                    writeDebugLog("  REC Detection #" + std::to_string(det_num) +
                                 " at (" + std::to_string(center_x) + "," + std::to_string(center_y) +
                                 ") - BGR: (" + std::to_string((int)bgr_pixel[0]) + "," +
                                 std::to_string((int)bgr_pixel[1]) + "," +
                                 std::to_string((int)bgr_pixel[2]) +
                                 ") - Square at (" + std::to_string((int)det.box.x) + "," +
                                 std::to_string((int)det.box.y) + ")");
                    
                    // Draw 8x8 solid square at upper left corner of detection bbox
                    // This shows the ACTUAL detected color, not the matched profile color
                    int square_x = static_cast<int>(det.box.x);
                    int square_y = static_cast<int>(det.box.y);
                    int square_size = 8;
                    
                    // Draw the solid colored square with ACTUAL detected color
                    cv::rectangle(temp_result,
                                cv::Rect(square_x, square_y, square_size, square_size),
                                actual_color, -1);  // -1 for filled rectangle
                    
                    // Draw black border around square for visibility
                    cv::rectangle(temp_result,
                                cv::Rect(square_x, square_y, square_size, square_size),
                                cv::Scalar(0, 0, 0), 1);  // 1px black border
                } else {
                    writeDebugLog("  REC Detection #" + std::to_string(det_num) +
                                 " SKIPPED - center (" + std::to_string(center_x) + "," +
                                 std::to_string(center_y) + ") out of bounds");
                }
            }
        }
    }
    
    // Draw filtered YOLO detections (after confidence filtering) - bright red, normal boxes
    if (record_with_yolo_boxes) {
        // Load color profiles for distance calculation
        std::vector<ColorProfile> color_profiles;
        try {
            std::ifstream color_file("hub/config/color_profiles.json");
            if (color_file.is_open()) {
                nlohmann::json color_profiles_json;
                color_file >> color_profiles_json;
                
                // Access the "profiles" array from the JSON structure
                if (color_profiles_json.contains("profiles") && color_profiles_json["profiles"].is_array()) {
                    for (const auto& profile : color_profiles_json["profiles"]) {
                    if (profile["enabled"]) {
                        ColorProfile cp;
                        cp.name = profile["name"];
                        cp.enabled = true;
                        cp.avg_hue = profile["avg_hue"];
                        cp.avg_saturation = profile["avg_saturation"];
                        color_profiles.push_back(cp);
                    }
                }
                }
            }
        } catch (...) {
            // If loading fails, continue without color distance info
        }
        
        // Note: rec_frame.raw_detections already contains filtered detections after NMS
        // We need to distinguish between truly raw (before threshold) and filtered (after threshold)
        // For now, show the current detections as filtered
        int det_num = 1;
        for (const auto& det : rec_frame.raw_detections) {
            // Draw bright red box for filtered YOLO detection
            cv::rectangle(temp_result, det.box, cv::Scalar(0, 0, 255), 2);
            
            // Draw detection number on the box
            std::string num_label = "#" + std::to_string(det_num);
            cv::putText(temp_result, num_label,
                       cv::Point(det.box.x + 5, det.box.y + 20),
                       cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 0, 0), 3, cv::LINE_AA);
            cv::putText(temp_result, num_label,
                       cv::Point(det.box.x + 5, det.box.y + 20),
                       cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(255, 255, 255), 1, cv::LINE_AA);
            
            // Calculate color distances if we have color profiles
            std::string closest_color = "N/A";
            float min_distance = 999.0f;
            
            if (!color_profiles.empty()) {
                // Sample color at detection center
                int center_x = static_cast<int>(det.box.x + det.box.width / 2);
                int center_y = static_cast<int>(det.box.y + det.box.height / 2);
                
                if (center_x >= 0 && center_x < frame.cols && center_y >= 0 && center_y < frame.rows) {
                    // Convert BGR to HSV for the detection center
                    cv::Mat roi = frame(cv::Rect(center_x, center_y, 1, 1));
                    cv::Mat hsv_roi;
                    cv::cvtColor(roi, hsv_roi, cv::COLOR_BGR2HSV);
                    cv::Vec3b hsv_pixel = hsv_roi.at<cv::Vec3b>(0, 0);
                    float det_hue = hsv_pixel[0];
                    float det_sat = hsv_pixel[1];
                    
                    // Calculate euclidean distance to each color profile
                    for (const auto& profile : color_profiles) {
                        // Handle hue wrap-around (0-180 scale)
                        float hue_diff = std::abs(det_hue - profile.avg_hue);
                        if (hue_diff > 90.0f) {
                            hue_diff = 180.0f - hue_diff;
                        }
                        
                        float sat_diff = det_sat - profile.avg_saturation;
                        float distance = std::sqrt(hue_diff * hue_diff + sat_diff * sat_diff);
                        
                        if (distance < min_distance) {
                            min_distance = distance;
                            closest_color = profile.name;
                        }
                    }
                }
            }
            
            // Add filtered YOLO info to panel with color distance
            std::string class_name = (det.class_id == 0) ? "ball" : "ball_held";
            char info_text[256];
            if (min_distance < 999.0f) {
                snprintf(info_text, sizeof(info_text), "#%d FILTERED: %s conf=%.2f | closest=%s dist=%.1f",
                         det_num, class_name.c_str(), det.confidence, closest_color.c_str(), min_distance);
            } else {
                snprintf(info_text, sizeof(info_text), "#%d FILTERED: %s conf=%.2f",
                         det_num, class_name.c_str(), det.confidence);
            }
            info_lines.push_back(info_text);
            info_colors.push_back(cv::Scalar(255, 255, 255)); // White for filtered
            
            // Add override evaluation for each ball color
            for (const auto& eval : det.override_evals) {
                char override_text[512];
                snprintf(override_text, sizeof(override_text), "  [%s] %s",
                         eval.ball_color.c_str(), eval.reason.c_str());
                info_lines.push_back(override_text);
                // Color: green if would override, red if not
                cv::Scalar override_color = eval.would_override ?
                                           cv::Scalar(0, 255, 0) :    // Green for override
                                           cv::Scalar(0, 0, 255);     // Red for no override
                info_colors.push_back(override_color);
            }
            
            det_num++;
        }
    }
    
    // Draw hand tracking
    if (viz.show_hand_tracking() || viz.show_skeleton()) {
        for (const auto& hand : rec_frame.tracked_hands) {
            // Project 3D wrist position to 2D
            if (hand.wrist_pos_3d.z > 0) {
                int wrist_x = static_cast<int>((hand.wrist_pos_3d.x * camera_intrinsics.fx) / hand.wrist_pos_3d.z + camera_intrinsics.ppx);
                int wrist_y = static_cast<int>((hand.wrist_pos_3d.y * camera_intrinsics.fy) / hand.wrist_pos_3d.z + camera_intrinsics.ppy);
                
                // Draw wrist circle
                cv::circle(temp_result, cv::Point(wrist_x, wrist_y), 20, cv::Scalar(255, 255, 0), 4); // Cyan
                
                // Draw hand label
                std::string label = hand.id == 0 ? "L" : "R";
                cv::putText(temp_result, label, cv::Point(wrist_x - 5, wrist_y + 5),
                           cv::FONT_HERSHEY_SIMPLEX, 1.2, cv::Scalar(255, 255, 255), 2);
                
                // Draw skeleton if enabled
                if (viz.show_skeleton()) {
                    for (const auto& kp : hand.keypoints) {
                        if (kp.z > 0) {
                            int kp_x = static_cast<int>((kp.x * camera_intrinsics.fx) / kp.z + camera_intrinsics.ppx);
                            int kp_y = static_cast<int>((kp.y * camera_intrinsics.fy) / kp.z + camera_intrinsics.ppy);
                            cv::circle(temp_result, cv::Point(kp_x, kp_y), 4, cv::Scalar(255, 255, 0), -1);
                        }
                    }
                }
            }
        }
    }
    
    // Draw color-tracked balls and their evaluation info
    if (viz.show_color_tracker()) {
        // Load color profiles to get RGB colors for each ball
        std::map<std::string, cv::Scalar> color_map;
        try {
            std::ifstream color_file("hub/config/color_profiles.json");
            if (color_file.is_open()) {
                nlohmann::json color_profiles;
                color_file >> color_profiles;
                
                // Access the "profiles" array from the JSON structure
                if (color_profiles.contains("profiles") && color_profiles["profiles"].is_array()) {
                    for (const auto& profile : color_profiles["profiles"]) {
                        std::string name = profile["name"];
                        std::vector<int> rgb = profile["rgb"];
                        // Convert RGB to BGR for OpenCV
                        color_map[name] = cv::Scalar(rgb[2], rgb[1], rgb[0]);
                    }
                }
            }
        } catch (...) {
            // If loading fails, use default colors
        }
        
        for (const auto& ball : rec_frame.tracked_balls) {
            // CRITICAL: Show tracker at ACTUAL color location
            // - If held: show at wrist position (where the ball actually is)
            // - If in flight: show at ball position (YOLO detection or trajectory prediction)
            int center_x, center_y;
            
            if (ball.is_held && ball.held_by_hand_id >= 0) {
                // Ball is held - show tracker at wrist position
                bool found_hand = false;
                for (const auto& hand : rec_frame.tracked_hands_simple) {
                    if (hand.id == ball.held_by_hand_id && hand.is_visible) {
                        // Project wrist 3D position to 2D
                        if (hand.wrist_pos_3d.z > 0) {
                            center_x = static_cast<int>((hand.wrist_pos_3d.x * camera_intrinsics.fx) / hand.wrist_pos_3d.z + camera_intrinsics.ppx);
                            center_y = static_cast<int>((hand.wrist_pos_3d.y * camera_intrinsics.fy) / hand.wrist_pos_3d.z + camera_intrinsics.ppy);
                            found_hand = true;
                            break;
                        }
                    }
                }
                
                // Fallback to ball pixel position if hand not found
                if (!found_hand) {
                    center_x = static_cast<int>(ball.pixel_pos.x);
                    center_y = static_cast<int>(ball.pixel_pos.y);
                }
            } else {
                // Ball is in flight - show at ball position (YOLO or trajectory)
                center_x = static_cast<int>(ball.pixel_pos.x);
                center_y = static_cast<int>(ball.pixel_pos.y);
            }
            
            // Get color for this ball
            cv::Scalar color = cv::Scalar(255, 255, 255);  // Default white
            auto it = color_map.find(ball.color_name);
            if (it != color_map.end()) {
                color = it->second;
            }
            
            // Draw ONLY the color letter with a white circle border - NO filled circle covering the ball
            // This allows us to see the actual ball color underneath
            std::string label = ball.color_name.substr(0, 1);
            
            // Draw white circle border around the letter for visibility
            int label_radius = 15;
            cv::circle(temp_result, cv::Point(center_x, center_y), label_radius, cv::Scalar(255, 255, 255), 2, cv::LINE_AA);
            
            // If held, draw dashed circle (simulate with thicker line)
            if (ball.is_held) {
                cv::circle(temp_result, cv::Point(center_x, center_y), label_radius + 3, color, 2, cv::LINE_AA);
            }
            
            // Draw the color letter with black outline for visibility
            cv::putText(temp_result, label, cv::Point(center_x - 8, center_y + 8),
                       cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0, 0, 0), 4, cv::LINE_AA);
            cv::putText(temp_result, label, cv::Point(center_x - 8, center_y + 8),
                       cv::FONT_HERSHEY_SIMPLEX, 0.8, color, 2, cv::LINE_AA);
            
            // Get color for this ball (for info panel)
            cv::Scalar ball_color = cv::Scalar(255, 255, 255);  // Default white
            auto color_it = color_map.find(ball.color_name);
            if (color_it != color_map.end()) {
                ball_color = color_it->second;
            }
            
            // ALWAYS add color tracker info to panel
            char info_text[512];
            std::string state = ball.is_held ? "HELD" : "FLIGHT";
            std::string hand_info = "";
            if (ball.is_held && ball.held_by_hand_id >= 0) {
                hand_info = " [" + std::string(ball.held_by_hand_id == 0 ? "L" : "R") + "]";
            }
            
            // Get min_frames_before_catch setting
            int min_frames_setting = 3;  // Default value
            if (tracker) {
                min_frames_setting = tracker->getTrackingSettings().min_frames_before_catch;
            }
            
            // Add frames in flight info to the main status line
            char frames_status[64];
            if (!ball.is_held) {
                snprintf(frames_status, sizeof(frames_status), " | Frames: %d/%d",
                         ball.frames_in_flight_since_throw, min_frames_setting);
            } else {
                frames_status[0] = '\0';  // Empty string for held balls
            }
            
            snprintf(info_text, sizeof(info_text), "%s: %s%s z=%.2fm%s | %s",
                     ball.color_name.c_str(), state.c_str(), hand_info.c_str(),
                     ball.position.z, frames_status, ball.tracking_reason.c_str());
            info_lines.push_back(info_text);
            info_colors.push_back(ball_color);
            
            // Add detailed ball position info
            char pos_info[256];
            snprintf(pos_info, sizeof(pos_info), "  Ball pos: (%.2f, %.2f, %.2f)",
                     ball.position.x, ball.position.y, ball.position.z);
            info_lines.push_back(pos_info);
            info_colors.push_back(cv::Scalar(180, 180, 180));
            
            // Add prediction status for HELD balls
            if (ball.is_held) {
                char pred_status[256];
                snprintf(pred_status, sizeof(pred_status),
                         "  Predicted Points: NONE (ball is HELD, no trajectory)");
                info_lines.push_back(pred_status);
                info_colors.push_back(cv::Scalar(100, 100, 100)); // Gray
            }
        }
    }
    
    // Draw "Final Tracker" visualization (white circle with colored letter)
    // This matches the UI's "color_tracked_balls (final)" toggle
    if (viz.show_tracked_boxes()) {
        // Load color profiles to get RGB colors for each ball
        std::map<std::string, cv::Scalar> color_map;
        try {
            std::ifstream color_file("hub/config/color_profiles.json");
            if (color_file.is_open()) {
                nlohmann::json color_profiles;
                color_file >> color_profiles;
                
                // Access the "profiles" array from the JSON structure
                if (color_profiles.contains("profiles") && color_profiles["profiles"].is_array()) {
                    for (const auto& profile : color_profiles["profiles"]) {
                        std::string name = profile["name"];
                        std::vector<int> rgb = profile["rgb"];
                        // Convert RGB to BGR for OpenCV
                        color_map[name] = cv::Scalar(rgb[2], rgb[1], rgb[0]);
                    }
                }
            }
        } catch (...) {
            // If loading fails, use default colors
        }
        
        for (const auto& ball : rec_frame.tracked_balls) {
            // Get color for this ball
            cv::Scalar color = cv::Scalar(255, 255, 255);  // Default white
            auto it = color_map.find(ball.color_name);
            if (it != color_map.end()) {
                color = it->second;
            }
            
            // Use pixel_pos which snaps to wrist when held (same as trajectory system)
            int center_x = static_cast<int>(ball.pixel_pos.x);
            int center_y = static_cast<int>(ball.pixel_pos.y);
            
            // Draw the color letter (first letter of color name)
            std::string label = ball.color_name.empty() ? "?" : ball.color_name.substr(0, 1);
            
            // Draw white circle BORDER only (no fill) - so you can see the ball behind it
            int label_radius = 15;
            cv::circle(temp_result, cv::Point(center_x, center_y), label_radius, cv::Scalar(255, 255, 255), 2, cv::LINE_AA);
            
            // If held, draw dashed circle to indicate held state
            if (ball.is_held) {
                // Simulate dashed circle with multiple arc segments
                for (int angle = 0; angle < 360; angle += 20) {
                    cv::ellipse(temp_result, cv::Point(center_x, center_y),
                               cv::Size(label_radius + 3, label_radius + 3),
                               0, angle, angle + 10, color, 2, cv::LINE_AA);
                }
            }
            
            // Draw the color letter with black outline for visibility
            cv::putText(temp_result, label, cv::Point(center_x - 8, center_y + 8),
                       cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0, 0, 0), 4, cv::LINE_AA);
            cv::putText(temp_result, label, cv::Point(center_x - 8, center_y + 8),
                       cv::FONT_HERSHEY_SIMPLEX, 0.8, color, 2, cv::LINE_AA);
        }
    }
    
    // Draw trajectory visualization for in-flight balls
    // Shows verified tracking points as colored circles
    if (viz.show_trajectory() && tracker) {
        for (const auto& ball : rec_frame.tracked_balls) {
            // Only draw trajectory for in-flight balls
            if (ball.state != IN_FLIGHT) continue;
            
            // Skip if no trajectory points
            if (ball.trajectory.points.empty()) continue;
            
            // Get ball color (convert HSV to BGR)
            cv::Scalar ball_color(0, 255, 0);  // Default green
            for (const auto& profile : tracker->getColorProfiles()) {
                if (profile.name == ball.color_name && profile.avg_hue >= 0) {
                    cv::Mat hsv_color(1, 1, CV_8UC3, cv::Scalar(profile.avg_hue, profile.avg_saturation, 255));
                    cv::Mat bgr_color;
                    cv::cvtColor(hsv_color, bgr_color, cv::COLOR_HSV2BGR);
                    ball_color = cv::Scalar(bgr_color.at<cv::Vec3b>(0, 0)[0],
                                           bgr_color.at<cv::Vec3b>(0, 0)[1],
                                           bgr_color.at<cv::Vec3b>(0, 0)[2]);
                    break;
                }
            }
            
            // Draw all verified trajectory points as colored circles
            for (const auto& traj_point : ball.trajectory.points) {
                if (!traj_point.verified) continue;
                
                // Project 3D point to 2D
                float x_2d = (traj_point.position.x * camera_intrinsics.fx) / traj_point.position.z + camera_intrinsics.ppx;
                float y_2d = (traj_point.position.y * camera_intrinsics.fy) / traj_point.position.z + camera_intrinsics.ppy;
                cv::Point2f point_2d(x_2d, y_2d);
                
                // Check if on-screen
                if (point_2d.x >= 0 && point_2d.x < temp_result.cols &&
                    point_2d.y >= 0 && point_2d.y < temp_result.rows) {
                    // Draw circle with ball's color
                    cv::circle(temp_result, point_2d, 5, ball_color, -1);
                    // Add white border for visibility
                    cv::circle(temp_result, point_2d, 5, cv::Scalar(255, 255, 255), 1);
                }
            }
            
            // Draw predicted future points as darker shaded dots (one per frame)
            // NEW: Show predicted trajectory points in recording visualization
            // Only show predictions when we have more than 3 verified points in history
            if (ball.trajectory.verified_point_count > 3 &&
                !ball.trajectory.predicted_path.empty() &&
                ball.trajectory.prediction_valid) {
                // Create darker version (40% brightness) of ball color for predicted points
                cv::Scalar darker_ball_color(
                    ball_color[0] * 0.4,
                    ball_color[1] * 0.4,
                    ball_color[2] * 0.4
                );
                
                // Draw each predicted point as a darker dot
                for (const auto& point_3d : ball.trajectory.predicted_path) {
                    // Validate depth before projection
                    if (point_3d.z <= 0) continue;
                    
                    // Project 3D point to 2D
                    float x_2d = (point_3d.x * camera_intrinsics.fx) / point_3d.z + camera_intrinsics.ppx;
                    float y_2d = (point_3d.y * camera_intrinsics.fy) / point_3d.z + camera_intrinsics.ppy;
                    
                    // Validate that coordinates are finite and within reasonable bounds
                    if (!std::isfinite(x_2d) || !std::isfinite(y_2d)) continue;
                    if (x_2d < -10000 || x_2d > 10000 || y_2d < -10000 || y_2d > 10000) continue;
                    
                    cv::Point2f point_2d(x_2d, y_2d);
                    
                    // Check if on-screen
                    if (point_2d.x >= 0 && point_2d.x < temp_result.cols &&
                        point_2d.y >= 0 && point_2d.y < temp_result.rows) {
                        // Draw circle with darker ball color (smaller than verified points)
                        cv::circle(temp_result, point_2d, 4, darker_ball_color, -1);
                        // Add subtle border for visibility
                        cv::circle(temp_result, point_2d, 4, cv::Scalar(100, 100, 100), 1);
                    }
                }
            }
            
            // Optionally draw connecting line
            if (ball.trajectory.points.size() > 1) {
                std::vector<cv::Point2f> path_2d;
                for (const auto& traj_point : ball.trajectory.points) {
                    if (!traj_point.verified) continue;
                    
                    // Validate depth before projection
                    if (traj_point.position.z <= 0) continue;
                    
                    float x_2d = (traj_point.position.x * camera_intrinsics.fx) / traj_point.position.z + camera_intrinsics.ppx;
                    float y_2d = (traj_point.position.y * camera_intrinsics.fy) / traj_point.position.z + camera_intrinsics.ppy;
                    
                    // Validate that coordinates are finite and within reasonable bounds
                    if (!std::isfinite(x_2d) || !std::isfinite(y_2d)) continue;
                    if (x_2d < -10000 || x_2d > 10000 || y_2d < -10000 || y_2d > 10000) continue;
                    
                    cv::Point2f point_2d(x_2d, y_2d);
                    
                    if (point_2d.x >= 0 && point_2d.x < temp_result.cols &&
                        point_2d.y >= 0 && point_2d.y < temp_result.rows) {
                        path_2d.push_back(point_2d);
                    }
                }
                
                // Draw polyline connecting the points - with safety check
                if (path_2d.size() > 1) {
                    try {
                        // Convert to vector of vectors as required by cv::polylines
                        std::vector<std::vector<cv::Point2f>> paths = {path_2d};
                        cv::polylines(temp_result, paths, false, ball_color, 2, cv::LINE_AA);
                    } catch (const cv::Exception& e) {
                        writeDebugLog("renderVisualizationsOnFrame() - cv::polylines exception: " + std::string(e.what()));
                    }
                }
            }
            
            // Add trajectory points listing to info panel
            // Count verified points
            int verified_count = 0;
            for (const auto& traj_point : ball.trajectory.points) {
                if (traj_point.verified) verified_count++;
            }
            
            if (verified_count > 0) {
                // Add header line
                char header[256];
                snprintf(header, sizeof(header), "Ball %d (%s) - IN_FLIGHT",
                         ball.id, ball.color_name.c_str());
                info_lines.push_back(header);
                info_colors.push_back(ball_color);
                
                // Add trajectory points count
                char count_line[256];
                snprintf(count_line, sizeof(count_line), "  Trajectory Points: %d", verified_count);
                info_lines.push_back(count_line);
                info_colors.push_back(cv::Scalar(200, 200, 200));
                
                // List each verified trajectory point
                int point_index = 0;
                for (const auto& traj_point : ball.trajectory.points) {
                    if (!traj_point.verified) continue;
                    
                    char point_line[512];
                    snprintf(point_line, sizeof(point_line),
                             "    [%d] (%.3f, %.3f, %.3f) m | conf=%.2f | t=%llu us",
                             point_index,
                             traj_point.position.x,
                             traj_point.position.y,
                             traj_point.position.z,
                             traj_point.confidence,
                             (unsigned long long)traj_point.timestamp);
                    info_lines.push_back(point_line);
                    info_colors.push_back(cv::Scalar(180, 180, 180));
                    point_index++;
                }
                
                // ALWAYS add prediction status information
                char pred_status[512];
                int pred_count = ball.trajectory.predicted_path.size();
                
                if (ball.trajectory.verified_point_count <= 3) {
                    // Not enough points for prediction
                    snprintf(pred_status, sizeof(pred_status),
                             "  Predicted Points: NONE (need >3 verified points, have %d)",
                             ball.trajectory.verified_point_count);
                    info_lines.push_back(pred_status);
                    info_colors.push_back(cv::Scalar(100, 100, 100)); // Gray
                } else if (!ball.trajectory.prediction_valid) {
                    // Prediction is invalid - show detailed reason
                    snprintf(pred_status, sizeof(pred_status),
                             "  Predicted Points: NONE (prediction_valid=false, path_size=%d) | %s",
                             pred_count,
                             ball.trajectory.prediction_failure_reason.c_str());
                    info_lines.push_back(pred_status);
                    info_colors.push_back(cv::Scalar(0, 0, 255)); // Red - this is the problem!
                } else if (ball.trajectory.predicted_path.empty()) {
                    // Prediction path is empty
                    snprintf(pred_status, sizeof(pred_status),
                             "  Predicted Points: NONE (predicted_path is empty)");
                    info_lines.push_back(pred_status);
                    info_colors.push_back(cv::Scalar(0, 0, 255)); // Red
                } else {
                    // We have predictions!
                    snprintf(pred_status, sizeof(pred_status),
                             "  Predicted Points: %d (shown as darker dots)",
                             pred_count);
                    info_lines.push_back(pred_status);
                    info_colors.push_back(cv::Scalar(0, 255, 0)); // Green - success!
                }
            }
        }
    }
    
    // Draw hand threshold circles (throw/catch distance thresholds)
    // Shows yellow circles around hands using held_radius_m and held_circle_offset_cm settings
    // Only draw if hand_threshold visualization is enabled
    if (viz.show_hand_threshold() && tracker) {
        tracker->drawHandThresholds(temp_result, rec_frame.tracked_hands_simple, camera_intrinsics);
    }
    
    // Draw hand velocity zone visualization
    // Shows purple circles around hands when velocity exceeds threshold and hand position history
    if (viz.show_hand_velocity_zone() && tracker) {
        const auto& settings = tracker->getTrackingSettings();
        float hand_velocity_threshold = settings.hand_velocity_threshold;
        float hand_velocity_radius = settings.hand_velocity_detection_radius;
        
        for (const auto& hand : rec_frame.tracked_hands_simple) {
            // Check if hand has valid velocity information
            if (!hand.is_visible || !hand.has_valid_velocity) continue;
            
            // Calculate hand velocity magnitude
            float velocity_magnitude = std::sqrt(
                hand.velocity.x * hand.velocity.x +
                hand.velocity.y * hand.velocity.y +
                hand.velocity.z * hand.velocity.z
            );
            
            // Check if a ball is held by this hand
            bool ball_held = false;
            std::string held_ball_color;
            for (const auto& ball : rec_frame.tracked_balls) {
                if (ball.held_by_hand_id == hand.id) {
                    ball_held = true;
                    held_ball_color = ball.color_name;
                    break;
                }
            }
            
            // Only draw if velocity exceeds threshold OR if a ball is held (to show history)
            bool show_velocity_zone = velocity_magnitude >= hand_velocity_threshold;
            bool show_history = ball_held && !hand.position_history.empty();
            
            if (!show_velocity_zone && !show_history) continue;
            
            // Project hand wrist position to 2D
            if (hand.wrist_pos_3d.z <= 0) continue;
            
            int center_x = static_cast<int>((hand.wrist_pos_3d.x * camera_intrinsics.fx) / hand.wrist_pos_3d.z + camera_intrinsics.ppx);
            int center_y = static_cast<int>((hand.wrist_pos_3d.y * camera_intrinsics.fy) / hand.wrist_pos_3d.z + camera_intrinsics.ppy);
            
            // Check if on-screen
            if (center_x < 0 || center_x >= temp_result.cols || center_y < 0 || center_y >= temp_result.rows) {
                continue;
            }
            
            // Draw velocity zone if hand is moving fast
            if (show_velocity_zone) {
                // Calculate radius in pixels (approximate projection)
                int radius_pixels = static_cast<int>((hand_velocity_radius * camera_intrinsics.fx) / hand.wrist_pos_3d.z);
                
                // Draw purple circle showing detection zone
                cv::circle(temp_result, cv::Point(center_x, center_y), radius_pixels, cv::Scalar(255, 0, 128), 3, cv::LINE_AA);
                
                // Draw velocity magnitude text
                char velocity_text[64];
                snprintf(velocity_text, sizeof(velocity_text), "Hand velocity: %.2f m/s", velocity_magnitude);
                cv::putText(temp_result, velocity_text,
                           cv::Point(center_x + radius_pixels + 10, center_y - 20),
                           cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 255), 1, cv::LINE_AA);
            }
            
            // Draw hand position history if ball is held
            if (show_history) {
                // Draw position history trail
                cv::Point prev_point(-1, -1);
                for (const auto& hist_pos : hand.position_history) {
                    if (hist_pos.z <= 0) continue;
                    
                    // Project history position to 2D
                    int hist_x = static_cast<int>((hist_pos.x * camera_intrinsics.fx) / hist_pos.z + camera_intrinsics.ppx);
                    int hist_y = static_cast<int>((hist_pos.y * camera_intrinsics.fy) / hist_pos.z + camera_intrinsics.ppy);
                    
                    // Draw small circle at history point
                    cv::circle(temp_result, cv::Point(hist_x, hist_y), 3, cv::Scalar(0, 255, 255), -1, cv::LINE_AA);
                    
                    // Draw line connecting to previous point
                    if (prev_point.x >= 0) {
                        cv::line(temp_result, prev_point, cv::Point(hist_x, hist_y), cv::Scalar(0, 255, 255, 150), 2, cv::LINE_AA);
                    }
                    
                    prev_point = cv::Point(hist_x, hist_y);
                }
                
                // Draw text showing history
                std::string history_text = "Hand locations history when " + held_ball_color + " ball is held";
                int text_y = show_velocity_zone ? center_y : center_y - 20;
                cv::putText(temp_result, history_text,
                           cv::Point(center_x + 10, text_y),
                           cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 255), 1, cv::LINE_AA);
            }
        }
    }
    
    // Draw trajectory-based prediction circles
    // Shows predicted search region based on trajectory physics
    if (viz.show_trajectory_predictions()) {
        for (const auto& ball : rec_frame.tracked_balls) {
            // Only draw if we have enough trajectory data
            if (ball.trajectory.verified_point_count < 3) {
                continue;
            }
            
            // Get predicted position from trajectory
            cv::Point3f pred_pos_3d(0, 0, 0);
            if (!ball.trajectory.predicted_path.empty() && ball.trajectory.prediction_valid) {
                pred_pos_3d = ball.trajectory.predicted_path[0];
            }
            
            // Skip if prediction failed
            if (pred_pos_3d.z <= 0) {
                continue;
            }
            
            // Project to 2D
            int pred_x = static_cast<int>((pred_pos_3d.x * camera_intrinsics.fx) / pred_pos_3d.z + camera_intrinsics.ppx);
            int pred_y = static_cast<int>((pred_pos_3d.y * camera_intrinsics.fy) / pred_pos_3d.z + camera_intrinsics.ppy);
            
            // Get prediction radius from trajectory (in meters)
            float uncertainty_meters = ball.trajectory.search_radius_m;
            
            // Project uncertainty to pixel space
            float uncertainty_pixels = (uncertainty_meters * camera_intrinsics.fx) / pred_pos_3d.z;
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
            cv::Mat overlay = temp_result.clone();
            cv::circle(overlay, cv::Point(pred_x, pred_y), radius, circle_color, 2, cv::LINE_AA);
            cv::addWeighted(overlay, 0.5, temp_result, 0.5, 0, temp_result);
            
            // Draw center point
            cv::circle(temp_result, cv::Point(pred_x, pred_y), 4, circle_color, -1, cv::LINE_AA);
            
            // Draw label with detailed prediction info including 3-frame rule tracking
            std::string throwing_hand = (ball.last_throwing_hand_id >= 0) ?
                                       std::to_string(ball.last_throwing_hand_id) : "N";
            std::string label = "P" + std::to_string(ball.id) +
                              "(" + std::string(ball.is_held ? "H" : "F") + ")" +
                              " TH:" + throwing_hand +
                              " FF:" + std::to_string(ball.frames_in_flight_since_throw);
            cv::putText(temp_result, label, cv::Point(pred_x + 10, pred_y - 10),
                       cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0, 0), 3, cv::LINE_AA);
            cv::putText(temp_result, label, cv::Point(pred_x + 10, pred_y - 10),
                       cv::FONT_HERSHEY_SIMPLEX, 0.5, circle_color, 1, cv::LINE_AA);
            
            // Add detailed prediction info to info panel
            char pred_info[512];
            snprintf(pred_info, sizeof(pred_info), "  Pred: pos=(%.2f,%.2f,%.2f) points=%d conf=%.2f grav=%s",
                     pred_pos_3d.x, pred_pos_3d.y, pred_pos_3d.z,
                     ball.trajectory.verified_point_count,
                     ball.trajectory.trajectory_confidence,
                     ball.is_held ? "OFF" : "ON");
            info_lines.push_back(pred_info);
            info_colors.push_back(circle_color);
            
            // Add frames in flight info with 3-frame rule tracking
            int min_frames_setting = 3;  // Default value
            if (tracker) {
                min_frames_setting = tracker->getTrackingSettings().min_frames_before_catch;
            }
            char frames_info[256];
            snprintf(frames_info, sizeof(frames_info), "  Frames in flight: %d / %d (3-frame rule: %s)",
                     ball.frames_in_flight_since_throw,
                     min_frames_setting,
                     ball.frames_in_flight_since_throw >= min_frames_setting ? "READY" : "COOLDOWN");
            info_lines.push_back(frames_info);
            // Color: green if ready for catch, yellow if in cooldown
            cv::Scalar frames_color = ball.frames_in_flight_since_throw >= min_frames_setting ?
                                     cv::Scalar(0, 255, 0) :      // Green - ready
                                     cv::Scalar(0, 255, 255);     // Yellow - cooldown
            info_colors.push_back(frames_color);
            
            // Add velocity info from trajectory
            cv::Point3f velocity = ball.trajectory.initial_velocity;
            char vel_info[256];
            snprintf(vel_info, sizeof(vel_info), "  Velocity: (%.2f, %.2f, %.2f) m/s",
                     velocity.x, velocity.y, velocity.z);
            info_lines.push_back(vel_info);
            info_colors.push_back(cv::Scalar(150, 150, 150));
            
            // Show distance between ball and prediction
            if (ball.has_yolo_detection) {
                float dx = ball.position.x - pred_pos_3d.x;
                float dy = ball.position.y - pred_pos_3d.y;
                float dz = ball.position.z - pred_pos_3d.z;
                float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
                char dist_info[256];
                snprintf(dist_info, sizeof(dist_info), "  Ball-Pred dist: %.3fm (dx=%.2f dy=%.2f dz=%.2f)",
                         dist, dx, dy, dz);
                info_lines.push_back(dist_info);
                info_colors.push_back(cv::Scalar(150, 150, 150));
            }
            
            // Show ball state info
            char state_info[256];
            snprintf(state_info, sizeof(state_info), "  is_held=%s yolo_class=%d held_by_hand=%d",
                     ball.is_held ? "TRUE" : "FALSE", ball.yolo_class_id, ball.held_by_hand_id);
            info_lines.push_back(state_info);
            info_colors.push_back(cv::Scalar(200, 200, 100));
            
            // Show trajectory points for debugging
            const auto& traj_points = ball.trajectory.points;
            if (!traj_points.empty()) {
                char hist_info[512];
                if (traj_points.size() >= 2) {
                    const auto& last = traj_points.back().position;
                    const auto& prev = traj_points[traj_points.size()-2].position;
                    snprintf(hist_info, sizeof(hist_info), "  Trajectory: last=(%.2f,%.2f,%.2f) prev=(%.2f,%.2f,%.2f)",
                             last.x, last.y, last.z, prev.x, prev.y, prev.z);
                } else {
                    const auto& last = traj_points.back().position;
                    snprintf(hist_info, sizeof(hist_info), "  Trajectory: last=(%.2f,%.2f,%.2f) only",
                             last.x, last.y, last.z);
                }
                info_lines.push_back(hist_info);
                info_colors.push_back(cv::Scalar(180, 180, 180));
            }
        }
    }
    
    // Process text wrapping and calculate required height
    int line_height = 22;
    int max_width = temp_result.cols - 20;  // Maximum width (full image width minus margins)
    float font_scale = 0.45f;
    int font_thickness = 1;
    int font_face = cv::FONT_HERSHEY_SIMPLEX;
    
    std::vector<std::string> wrapped_lines;
    std::vector<cv::Scalar> wrapped_colors;
    
    if (!info_lines.empty()) {
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
    }
    
    // Calculate required text panel height
    int text_panel_height = wrapped_lines.empty() ? 0 : (wrapped_lines.size() * line_height + 20);
    
    // Create larger image with space for text below
    int total_height = frame.rows + text_panel_height;
    cv::Mat result(total_height, frame.cols, frame.type(), cv::Scalar(0, 0, 0));
    
    // Copy original frame with visualizations to top portion
    temp_result.copyTo(result(cv::Rect(0, 0, frame.cols, frame.rows)));
    
    // Draw text panel below the image
    if (!wrapped_lines.empty()) {
        int panel_x = 10;
        int panel_y = frame.rows + 10;  // Start below the image
        
        // Draw dark background for text area
        cv::rectangle(result,
                     cv::Point(0, frame.rows),
                     cv::Point(frame.cols, total_height),
                     cv::Scalar(20, 20, 20), -1);
        
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
   
   writeDebugLog("renderVisualizationsOnFrame() - Complete");
   return result;
}
