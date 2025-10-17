#include "../include/New3DTracker.hpp"
#include "../include/SimpleBallTracker.hpp"  // For shared type definitions
#include <fstream>
#include <iostream>
#include <chrono>
#include <cmath>

// ============================================================================
// CONSTRUCTOR
// ============================================================================

New3DTracker::New3DTracker(const std::string& ball_model_path,
                           const std::string& pose_model_path,
                           const std::string& device_name,
                           const std::string& settings_file)
    : settings_file_(settings_file) {
    
    std::cout << "[New3DTracker] Initializing New 3D Tracker..." << std::endl;
    
    // Initialize OpenVINO models
    std::cout << "[New3DTracker] Loading ball detection model: " << ball_model_path << std::endl;
    auto ball_model = core_.read_model(ball_model_path);
    ball_model_ = core_.compile_model(ball_model, device_name);
    ball_infer_ = ball_model_.create_infer_request();
    
    std::cout << "[New3DTracker] Loading pose estimation model: " << pose_model_path << std::endl;
    auto pose_model = core_.read_model(pose_model_path);
    pose_model_ = core_.compile_model(pose_model, device_name);
    pose_infer_ = pose_model_.create_infer_request();
    
    // Load settings from JSON file
    if (!loadSettings()) {
        std::cout << "[New3DTracker] Warning: Could not load settings from " << settings_file 
                  << ", using defaults" << std::endl;
    }
    
    // Initialize GPU HSV converter
    try {
        gpu_hsv_converter_ = std::make_unique<GpuHsvConverter>();
        std::cout << "[New3DTracker] GPU HSV converter initialized" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[New3DTracker] Warning: Could not initialize GPU HSV converter: " 
                  << e.what() << std::endl;
    }
    
    // Initialize timing
    last_update_time_ = std::chrono::steady_clock::now();
    
    std::cout << "[New3DTracker] Initialization complete" << std::endl;
}

// ============================================================================
// KALMAN FILTER SETUP
// ============================================================================

cv::KalmanFilter New3DTracker::createKalmanFilter(const cv::Point3f& initial_pos) {
    // 6-state Kalman filter: [x, y, z, vx, vy, vz]
    // We measure position only (3 measurements)
    // No control input (0)
    cv::KalmanFilter kf(6, 3, 0);
    
    // State transition matrix (constant velocity model)
    // Will be updated with dt each frame
    // x_new = x + vx*dt
    // vx_new = vx (velocity is constant, gravity applied separately)
    kf.transitionMatrix = (cv::Mat_<float>(6, 6) <<
        1, 0, 0, 1, 0, 0,  // x = x + vx*dt (dt will be set per frame)
        0, 1, 0, 0, 1, 0,  // y = y + vy*dt
        0, 0, 1, 0, 0, 1,  // z = z + vz*dt
        0, 0, 0, 1, 0, 0,  // vx = vx
        0, 0, 0, 0, 1, 0,  // vy = vy
        0, 0, 0, 0, 0, 1   // vz = vz
    );
    
    // Measurement matrix (we measure position only)
    kf.measurementMatrix = (cv::Mat_<float>(3, 6) <<
        1, 0, 0, 0, 0, 0,  // Measure x
        0, 1, 0, 0, 0, 0,  // Measure y
        0, 0, 1, 0, 0, 0   // Measure z
    );
    
    // Process noise covariance (uncertainty in the model)
    // Higher values = trust measurements more than predictions
    cv::setIdentity(kf.processNoiseCov, cv::Scalar::all(1e-2));
    
    // Measurement noise covariance (uncertainty in measurements)
    // Higher values = trust predictions more than measurements
    cv::setIdentity(kf.measurementNoiseCov, cv::Scalar::all(1e-1));
    
    // Error covariance (initial uncertainty)
    cv::setIdentity(kf.errorCovPost, cv::Scalar::all(1));
    
    // Initialize state with position and zero velocity
    kf.statePost.at<float>(0) = initial_pos.x;
    kf.statePost.at<float>(1) = initial_pos.y;
    kf.statePost.at<float>(2) = initial_pos.z;
    kf.statePost.at<float>(3) = 0.0f;  // vx
    kf.statePost.at<float>(4) = 0.0f;  // vy
    kf.statePost.at<float>(5) = 0.0f;  // vz
    
    return kf;
}

// ============================================================================
// PREDICTION METHODS
// ============================================================================

void New3DTracker::predictAllBalls(float dt) {
    for (auto& ball : tracked_balls_) {
        if (ball.state == HELD) {
            // Find the hand holding this ball
            const SimpleHand* holding_hand = nullptr;
            for (const auto& hand : hands_) {
                if (hand.id == ball.associated_hand_id) {
                    holding_hand = &hand;
                    break;
                }
            }
            
            if (holding_hand) {
                predictHeldBall(ball, *holding_hand, dt);
            } else {
                // Hand not found, but ball is marked as held
                // Keep last known position
                ball.predicted_position = ball.last_known_position;
            }
        } else {
            // Ball is in flight
            predictInFlightBall(ball, dt);
        }
    }
}

void New3DTracker::predictHeldBall(New3DBall& ball, const SimpleHand& hand, float dt) {
    // For held balls, the Kalman filter tracks the hand's position
    // The predicted position is simply the hand's wrist position
    ball.predicted_position = hand.wrist_pos_3d;
    
    // Update Kalman filter to track hand movement
    // This allows us to estimate velocity when the ball is thrown
    
    // Update transition matrix with dt
    ball.kf.transitionMatrix.at<float>(0, 3) = dt;
    ball.kf.transitionMatrix.at<float>(1, 4) = dt;
    ball.kf.transitionMatrix.at<float>(2, 5) = dt;
    
    // Predict (this updates internal state)
    ball.kf.predict();
    
    // Correct with hand position measurement
    cv::Mat measurement = (cv::Mat_<float>(3, 1) <<
        hand.wrist_pos_3d.x,
        hand.wrist_pos_3d.y,
        hand.wrist_pos_3d.z
    );
    ball.kf.correct(measurement);
    
    // Extract velocity from Kalman state for potential throw detection
    // This velocity will be used when transitioning to IN_FLIGHT state
}

void New3DTracker::predictInFlightBall(New3DBall& ball, float dt) {
    // Update transition matrix with dt
    ball.kf.transitionMatrix.at<float>(0, 3) = dt;
    ball.kf.transitionMatrix.at<float>(1, 4) = dt;
    ball.kf.transitionMatrix.at<float>(2, 5) = dt;
    
    // Apply gravity to velocity states before prediction
    // Gravity affects velocity: v_new = v_old + g*dt
    // We add this as a control input to the state
    ball.kf.statePost.at<float>(3) += settings_.gravity_mps2.x * dt;  // vx
    ball.kf.statePost.at<float>(4) += settings_.gravity_mps2.y * dt;  // vy
    ball.kf.statePost.at<float>(5) += settings_.gravity_mps2.z * dt;  // vz
    
    // Run Kalman prediction
    cv::Mat prediction = ball.kf.predict();
    
    // Store predicted position
    ball.predicted_position = cv::Point3f(
        prediction.at<float>(0),
        prediction.at<float>(1),
        prediction.at<float>(2)
    );
}

// ============================================================================
// HELPER METHODS (Stubs for Phase 2)
// ============================================================================

cv::Point3f New3DTracker::calculateHandVelocity(const SimpleHand& hand, 
                                                const Pose3D& previous_pose, 
                                                float dt) {
    // Determine which hand we're calculating velocity for
    cv::Point3f previous_pos;
    bool has_previous = false;
    
    if (hand.id == 0) {  // Left hand
        if (previous_pose.is_left_wrist_valid) {
            previous_pos = previous_pose.left_wrist_pos;
            has_previous = true;
        }
    } else if (hand.id == 1) {  // Right hand
        if (previous_pose.is_right_wrist_valid) {
            previous_pos = previous_pose.right_wrist_pos;
            has_previous = true;
        }
    }
    
    if (!has_previous || dt <= 0.0f) {
        return cv::Point3f(0, 0, 0);
    }
    
    // Calculate velocity: v = (current_pos - previous_pos) / dt
    cv::Point3f velocity = (hand.wrist_pos_3d - previous_pos) / dt;
    
    return velocity;
}

Pose3D New3DTracker::createPose3D(const std::vector<SimpleHand>& hands) {
    Pose3D pose;
    
    for (const auto& hand : hands) {
        if (hand.id == 0) {  // Left hand
            pose.left_wrist_pos = hand.wrist_pos_3d;
            pose.is_left_wrist_valid = hand.is_visible;
        } else if (hand.id == 1) {  // Right hand
            pose.right_wrist_pos = hand.wrist_pos_3d;
            pose.is_right_wrist_valid = hand.is_visible;
        }
    }
    
    return pose;
}

// ============================================================================
// SETTINGS MANAGEMENT
// ============================================================================

bool New3DTracker::loadSettings() {
    try {
        std::ifstream file(settings_file_);
        if (!file.is_open()) {
            return false;
        }
        
        json j;
        file >> j;
        
        // Load New3DTrackerSettings
        if (j.contains("held_radius_m")) {
            settings_.held_radius_m = j["held_radius_m"];
        }
        if (j.contains("association_max_distance_m")) {
            settings_.association_max_distance_m = j["association_max_distance_m"];
        }
        if (j.contains("throw_velocity_threshold_mps")) {
            settings_.throw_velocity_threshold_mps = j["throw_velocity_threshold_mps"];
        }
        if (j.contains("gravity_mps2")) {
            auto g = j["gravity_mps2"];
            settings_.gravity_mps2.x = g["x"];
            settings_.gravity_mps2.y = g["y"];
            settings_.gravity_mps2.z = g["z"];
        }
        if (j.contains("max_frames_unseen")) {
            settings_.max_frames_unseen = j["max_frames_unseen"];
        }
        if (j.contains("min_frames_for_new_track")) {
            settings_.min_frames_for_new_track = j["min_frames_for_new_track"];
        }
        if (j.contains("min_frames_for_color_lock")) {
            settings_.min_frames_for_color_lock = j["min_frames_for_color_lock"];
        }
        if (j.contains("use_color_tracking")) {
            settings_.use_color_tracking = j["use_color_tracking"];
        }
        if (j.contains("color_match_threshold")) {
            settings_.color_match_threshold = j["color_match_threshold"];
        }
        if (j.contains("color_sample_radius")) {
            settings_.color_sample_radius = j["color_sample_radius"];
        }
        if (j.contains("ball_confidence_threshold")) {
            settings_.ball_confidence_threshold = j["ball_confidence_threshold"];
        }
        if (j.contains("ball_held_confidence_threshold")) {
            settings_.ball_held_confidence_threshold = j["ball_held_confidence_threshold"];
        }
        if (j.contains("ignore_class")) {
            settings_.ignore_class = j["ignore_class"];
        }
        if (j.contains("hand_velocity_enabled")) {
            settings_.hand_velocity_enabled = j["hand_velocity_enabled"];
        }
        if (j.contains("hand_velocity_threshold")) {
            settings_.hand_velocity_threshold = j["hand_velocity_threshold"];
        }
        if (j.contains("show_kalman_prediction")) {
            settings_.show_kalman_prediction = j["show_kalman_prediction"];
        }
        if (j.contains("show_held_radius")) {
            settings_.show_held_radius = j["show_held_radius"];
        }
        if (j.contains("show_association_lines")) {
            settings_.show_association_lines = j["show_association_lines"];
        }
        
        // Load color profiles
        if (j.contains("color_profiles")) {
            color_profiles_.clear();
            for (const auto& profile_json : j["color_profiles"]) {
                ColorProfile profile;
                profile.name = profile_json["name"];
                profile.enabled = profile_json.value("enabled", true);
                profile.avg_hue = profile_json.value("avg_hue", -1.0f);
                profile.avg_saturation = profile_json.value("avg_saturation", -1.0f);
                
                // Load legacy min/max ranges if present
                if (profile_json.contains("min_hsv")) {
                    auto min = profile_json["min_hsv"];
                    profile.min_hsv = cv::Scalar(min[0], min[1], min[2]);
                }
                if (profile_json.contains("max_hsv")) {
                    auto max = profile_json["max_hsv"];
                    profile.max_hsv = cv::Scalar(max[0], max[1], max[2]);
                }
                if (profile_json.contains("min_hsv2")) {
                    auto min2 = profile_json["min_hsv2"];
                    profile.min_hsv2 = cv::Scalar(min2[0], min2[1], min2[2]);
                }
                if (profile_json.contains("max_hsv2")) {
                    auto max2 = profile_json["max_hsv2"];
                    profile.max_hsv2 = cv::Scalar(max2[0], max2[1], max2[2]);
                }
                
                color_profiles_.push_back(profile);
            }
        }
        
        std::cout << "[New3DTracker] Settings loaded from " << settings_file_ << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "[New3DTracker] Error loading settings: " << e.what() << std::endl;
        return false;
    }
}

void New3DTracker::saveSettings() {
    try {
        json j;
        
        // Save New3DTrackerSettings
        j["held_radius_m"] = settings_.held_radius_m;
        j["association_max_distance_m"] = settings_.association_max_distance_m;
        j["throw_velocity_threshold_mps"] = settings_.throw_velocity_threshold_mps;
        j["gravity_mps2"] = {
            {"x", settings_.gravity_mps2.x},
            {"y", settings_.gravity_mps2.y},
            {"z", settings_.gravity_mps2.z}
        };
        j["max_frames_unseen"] = settings_.max_frames_unseen;
        j["min_frames_for_new_track"] = settings_.min_frames_for_new_track;
        j["min_frames_for_color_lock"] = settings_.min_frames_for_color_lock;
        j["use_color_tracking"] = settings_.use_color_tracking;
        j["color_match_threshold"] = settings_.color_match_threshold;
        j["color_sample_radius"] = settings_.color_sample_radius;
        j["ball_confidence_threshold"] = settings_.ball_confidence_threshold;
        j["ball_held_confidence_threshold"] = settings_.ball_held_confidence_threshold;
        j["ignore_class"] = settings_.ignore_class;
        j["hand_velocity_enabled"] = settings_.hand_velocity_enabled;
        j["hand_velocity_threshold"] = settings_.hand_velocity_threshold;
        j["show_kalman_prediction"] = settings_.show_kalman_prediction;
        j["show_held_radius"] = settings_.show_held_radius;
        j["show_association_lines"] = settings_.show_association_lines;
        
        // Save color profiles
        json profiles_json = json::array();
        for (const auto& profile : color_profiles_) {
            json profile_json;
            profile_json["name"] = profile.name;
            profile_json["enabled"] = profile.enabled;
            profile_json["avg_hue"] = profile.avg_hue;
            profile_json["avg_saturation"] = profile.avg_saturation;
            profile_json["min_hsv"] = {profile.min_hsv[0], profile.min_hsv[1], profile.min_hsv[2]};
            profile_json["max_hsv"] = {profile.max_hsv[0], profile.max_hsv[1], profile.max_hsv[2]};
            profile_json["min_hsv2"] = {profile.min_hsv2[0], profile.min_hsv2[1], profile.min_hsv2[2]};
            profile_json["max_hsv2"] = {profile.max_hsv2[0], profile.max_hsv2[1], profile.max_hsv2[2]};
            profiles_json.push_back(profile_json);
        }
        j["color_profiles"] = profiles_json;
        
        std::ofstream file(settings_file_);
        file << j.dump(4);  // Pretty print with 4-space indent
        
        std::cout << "[New3DTracker] Settings saved to " << settings_file_ << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "[New3DTracker] Error saving settings: " << e.what() << std::endl;
    }
}

// ============================================================================
// PHASE 3: DETECTION ASSOCIATION
// ============================================================================

New3DTracker::AssociationResult New3DTracker::associateDetections(
    std::vector<New3DBall>& balls,
    const std::vector<Detection>& detections,
    float max_distance) {
    
    AssociationResult result;
    
    // Handle edge cases
    if (balls.empty() && detections.empty()) {
        return result;  // Nothing to associate
    }
    
    if (balls.empty()) {
        // All detections are unmatched
        for (const auto& det : detections) {
            result.unmatched_detections.push_back(&det);
        }
        return result;
    }
    
    if (detections.empty()) {
        // All balls are unmatched
        for (auto& ball : balls) {
            result.unmatched_balls.push_back(&ball);
        }
        return result;
    }
    
    // Create tracking sets for matched items
    std::vector<bool> ball_matched(balls.size(), false);
    std::vector<bool> detection_matched(detections.size(), false);
    
    // Greedy nearest-neighbor association
    // Repeatedly find the closest ball-detection pair until no valid matches remain
    while (true) {
        float min_distance = max_distance;
        int best_ball_idx = -1;
        int best_detection_idx = -1;
        
        // Find the closest unmatched ball-detection pair
        for (size_t i = 0; i < balls.size(); ++i) {
            if (ball_matched[i]) continue;
            
            for (size_t j = 0; j < detections.size(); ++j) {
                if (detection_matched[j]) continue;
                
                // Calculate 3D Euclidean distance between predicted ball position and detection
                const cv::Point3f& pred_pos = balls[i].predicted_position;
                const cv::Point3f& det_pos = detections[j].world_pos;
                
                float dx = pred_pos.x - det_pos.x;
                float dy = pred_pos.y - det_pos.y;
                float dz = pred_pos.z - det_pos.z;
                float distance = std::sqrt(dx*dx + dy*dy + dz*dz);
                
                // Check if this is the best match so far
                // Note: Color matching as a tie-breaker would require calling matchColor()
                // which is expensive. For now, we use pure distance-based matching.
                // In future phases, we can add color scoring here if needed.
                
                if (distance < min_distance) {
                    min_distance = distance;
                    best_ball_idx = static_cast<int>(i);
                    best_detection_idx = static_cast<int>(j);
                }
            }
        }
        
        // If no valid match found, we're done
        if (best_ball_idx == -1 || best_detection_idx == -1) {
            break;
        }
        
        // Add the match to results
        MatchPair match;
        match.ball = &balls[best_ball_idx];
        match.detection = &detections[best_detection_idx];
        match.distance = min_distance;
        result.matched_pairs.push_back(match);
        
        // Mark as matched
        ball_matched[best_ball_idx] = true;
        detection_matched[best_detection_idx] = true;
    }
    
    // Collect unmatched balls
    for (size_t i = 0; i < balls.size(); ++i) {
        if (!ball_matched[i]) {
            result.unmatched_balls.push_back(&balls[i]);
        }
    }
    
    // Collect unmatched detections
    for (size_t j = 0; j < detections.size(); ++j) {
        if (!detection_matched[j]) {
            result.unmatched_detections.push_back(&detections[j]);
        }
    }
    
    return result;
}

// ============================================================================
// PHASE 4: STATE MACHINE LOGIC
// ============================================================================

void New3DTracker::updateMatchedBalls(
    const std::vector<MatchPair>& matches,
    const std::vector<SimpleHand>& hands,
    const Pose3D& previous_pose,
    float dt,
    std::vector<BallEvent>& events) {
    
    for (const auto& match : matches) {
        New3DBall& ball = *match.ball;
        const Detection& detection = *match.detection;
        
        // Reset frames_since_seen for matched balls
        ball.frames_since_seen = 0;
        ball.consecutive_frames_seen++;
        
        // Update visualization data
        // Calculate center from box
        ball.pixel_pos = cv::Point2f(
            detection.box.x + detection.box.width / 2.0f,
            detection.box.y + detection.box.height / 2.0f
        );
        ball.bbox = detection.box;
        ball.yolo_confidence = detection.confidence;
        
        // Call appropriate state handler based on current ball state
        if (ball.state == HELD) {
            handleHeldStateUpdate(ball, detection, hands, previous_pose, dt, events);
        } else {  // IN_FLIGHT
            handleInFlightStateUpdate(ball, detection, hands, events);
        }
    }
}

void New3DTracker::handleHeldStateUpdate(
    New3DBall& ball,
    const Detection& detection,
    const std::vector<SimpleHand>& current_hands,
    const Pose3D& previous_pose,
    float dt,
    std::vector<BallEvent>& events) {
    
    // Update Kalman filter with detection measurement
    cv::Mat measurement = (cv::Mat_<float>(3, 1) <<
        detection.world_pos.x,
        detection.world_pos.y,
        detection.world_pos.z
    );
    ball.kf.correct(measurement);
    
    // Extract current velocity from Kalman state
    cv::Point3f ball_velocity(
        ball.kf.statePost.at<float>(3),
        ball.kf.statePost.at<float>(4),
        ball.kf.statePost.at<float>(5)
    );
    
    // Find the hand that should be holding this ball
    const SimpleHand* holding_hand = nullptr;
    for (const auto& hand : current_hands) {
        if (hand.id == ball.associated_hand_id) {
            holding_hand = &hand;
            break;
        }
    }
    
    if (!holding_hand) {
        // Hand not found - ball may have been thrown
        // Transition to IN_FLIGHT state
        ball.state = IN_FLIGHT;
        ball.associated_hand_id = -1;
        ball.tracking_reason = "Hand lost";
        
        // Generate THROW event
        BallEvent throw_event;
        throw_event.type = BallEvent::THROW;
        throw_event.ball_id = static_cast<int>(ball.id);
        throw_event.hand_id = ball.associated_hand_id;
        throw_event.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        // Note: BallEvent doesn't have velocity field, velocity is tracked in ball state
        events.push_back(throw_event);
        
        return;
    }
    
    // Calculate distance to associated hand
    cv::Point3f hand_pos = holding_hand->wrist_pos_3d;
    float dx = detection.world_pos.x - hand_pos.x;
    float dy = detection.world_pos.y - hand_pos.y;
    float dz = detection.world_pos.z - hand_pos.z;
    float distance_to_hand = std::sqrt(dx*dx + dy*dy + dz*dz);
    
    // Calculate hand velocity
    cv::Point3f hand_velocity = calculateHandVelocity(*holding_hand, previous_pose, dt);
    
    // Calculate relative velocity between ball and hand
    cv::Point3f relative_velocity = ball_velocity - hand_velocity;
    float relative_speed = std::sqrt(
        relative_velocity.x * relative_velocity.x +
        relative_velocity.y * relative_velocity.y +
        relative_velocity.z * relative_velocity.z
    );
    
    // Check for throw conditions
    bool distance_exceeded = distance_to_hand > settings_.held_radius_m;
    bool velocity_exceeded = relative_speed > settings_.throw_velocity_threshold_mps;
    bool hand_velocity_check = true;
    
    if (settings_.hand_velocity_enabled) {
        float hand_speed = std::sqrt(
            hand_velocity.x * hand_velocity.x +
            hand_velocity.y * hand_velocity.y +
            hand_velocity.z * hand_velocity.z
        );
        hand_velocity_check = hand_speed > settings_.hand_velocity_threshold;
    }
    
    // Detect throw: ball moves beyond held_radius AND has sufficient relative velocity
    if (distance_exceeded && velocity_exceeded && hand_velocity_check) {
        // Transition to IN_FLIGHT state
        ball.state = IN_FLIGHT;
        int previous_hand_id = ball.associated_hand_id;
        ball.associated_hand_id = -1;
        ball.tracking_reason = "Throw detected";
        
        // Generate THROW event
        BallEvent throw_event;
        throw_event.type = BallEvent::THROW;
        throw_event.ball_id = static_cast<int>(ball.id);
        throw_event.hand_id = previous_hand_id;
        throw_event.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        // Note: BallEvent doesn't have velocity field, velocity is tracked in ball state
        events.push_back(throw_event);
        
        return;
    }
    
    // Check for hand-off (ball moves to other hand while staying held)
    if (distance_to_hand > settings_.held_radius_m) {
        // Check if ball is near the other hand
        for (const auto& hand : current_hands) {
            if (hand.id == ball.associated_hand_id) continue;  // Skip current hand
            
            cv::Point3f other_hand_pos = hand.wrist_pos_3d;
            float dx_other = detection.world_pos.x - other_hand_pos.x;
            float dy_other = detection.world_pos.y - other_hand_pos.y;
            float dz_other = detection.world_pos.z - other_hand_pos.z;
            float distance_to_other = std::sqrt(dx_other*dx_other + dy_other*dy_other + dz_other*dz_other);
            
            if (distance_to_other < settings_.held_radius_m && isHandAvailable(hand.id, tracked_balls_)) {
                // Hand-off detected
                ball.associated_hand_id = hand.id;
                ball.tracking_reason = "Hand-off detected";
                break;
            }
        }
    }
    
    // Update last known position
    ball.last_known_position = detection.world_pos;
}

void New3DTracker::handleInFlightStateUpdate(
    New3DBall& ball,
    const Detection& detection,
    const std::vector<SimpleHand>& current_hands,
    std::vector<BallEvent>& events) {
    
    // Update Kalman filter with detection measurement
    cv::Mat measurement = (cv::Mat_<float>(3, 1) <<
        detection.world_pos.x,
        detection.world_pos.y,
        detection.world_pos.z
    );
    ball.kf.correct(measurement);
    
    // Check distance to all available hands for catch detection
    for (const auto& hand : current_hands) {
        cv::Point3f hand_pos = hand.wrist_pos_3d;
        float dx = detection.world_pos.x - hand_pos.x;
        float dy = detection.world_pos.y - hand_pos.y;
        float dz = detection.world_pos.z - hand_pos.z;
        float distance_to_hand = std::sqrt(dx*dx + dy*dy + dz*dz);
        
        // Detect catch: ball comes within held_radius of an available hand
        if (distance_to_hand < settings_.held_radius_m && isHandAvailable(hand.id, tracked_balls_)) {
            // Transition to HELD state
            ball.state = HELD;
            ball.associated_hand_id = hand.id;
            ball.tracking_reason = "Catch detected";
            
            // Generate CATCH event
            BallEvent catch_event;
            catch_event.type = BallEvent::CATCH;
            catch_event.ball_id = static_cast<int>(ball.id);
            catch_event.hand_id = hand.id;
            catch_event.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count();
            
            // Note: BallEvent doesn't have velocity field, velocity is tracked in ball state
            
            events.push_back(catch_event);
            break;  // Only catch with one hand
        }
    }
    
    // Update last known position
    ball.last_known_position = detection.world_pos;
}

bool New3DTracker::isHandAvailable(int hand_id, const std::vector<New3DBall>& balls) {
    // Check if any ball is already held by this hand
    for (const auto& ball : balls) {
        if (ball.state == HELD && ball.associated_hand_id == hand_id) {
            return false;  // Hand is already holding a ball
        }
    }
    return true;  // Hand is available
}

// ============================================================================
// PHASE 5: TRACK MANAGEMENT
// ============================================================================

void New3DTracker::handleUnmatchedBalls(
    const std::vector<New3DBall*>& unmatched_balls) {
    
    // Iterate through unmatched balls and increment their unseen counter
    for (auto* ball : unmatched_balls) {
        ball->frames_since_seen++;
        ball->consecutive_frames_seen = 0;  // Reset consecutive frames counter
        
        // Update tracking reason
        ball->tracking_reason = "Unmatched (" + std::to_string(ball->frames_since_seen) + " frames)";
    }
    
    // Delete tracks that exceed max_frames_unseen threshold
    // Use erase-remove idiom for efficient deletion
    tracked_balls_.erase(
        std::remove_if(tracked_balls_.begin(), tracked_balls_.end(),
            [this](const New3DBall& ball) {
                return ball.frames_since_seen > settings_.max_frames_unseen;
            }),
        tracked_balls_.end()
    );
}

void New3DTracker::createNewTracks(
    const std::vector<const Detection*>& unmatched_detections,
    const cv::Mat& color_frame) {
    
    // For simplicity, use immediate track creation (Option 1)
    // Multi-frame confirmation can be added later if needed
    
    for (const auto* detection : unmatched_detections) {
        // Check if detection meets confidence threshold
        if (detection->confidence < settings_.ball_confidence_threshold) {
            continue;  // Skip low-confidence detections
        }
        
        // Create new ball
        New3DBall new_ball;
        
        // Assign unique ID
        new_ball.id = next_track_id_++;
        
        // Determine color
        new_ball.color_name = determineColor(*detection, color_frame);
        
        // Find color profile
        for (const auto& profile : color_profiles_) {
            if (profile.name == new_ball.color_name) {
                new_ball.color_profile = profile;
                break;
            }
        }
        
        // Initialize Kalman filter with detection position
        new_ball.kf = createKalmanFilter(detection->world_pos);
        
        // Set initial positions
        new_ball.last_known_position = detection->world_pos;
        new_ball.predicted_position = detection->world_pos;
        
        // Determine initial state based on proximity to hands
        bool near_hand = false;
        int closest_hand_id = -1;
        float min_distance = settings_.held_radius_m;
        
        for (const auto& hand : hands_) {
            float dx = detection->world_pos.x - hand.wrist_pos_3d.x;
            float dy = detection->world_pos.y - hand.wrist_pos_3d.y;
            float dz = detection->world_pos.z - hand.wrist_pos_3d.z;
            float distance = std::sqrt(dx*dx + dy*dy + dz*dz);
            
            if (distance < min_distance && isHandAvailable(hand.id, tracked_balls_)) {
                near_hand = true;
                min_distance = distance;
                closest_hand_id = hand.id;
            }
        }
        
        // Set initial state
        if (near_hand) {
            new_ball.state = HELD;
            new_ball.associated_hand_id = closest_hand_id;
            new_ball.tracking_reason = "New track (HELD)";
        } else {
            new_ball.state = IN_FLIGHT;
            new_ball.associated_hand_id = -1;
            new_ball.tracking_reason = "New track (IN_FLIGHT)";
        }
        
        // Initialize tracking quality
        new_ball.frames_since_seen = 0;
        new_ball.consecutive_frames_seen = 1;
        new_ball.color_locked = false;
        
        // Initialize visualization data
        new_ball.pixel_pos = cv::Point2f(
            detection->box.x + detection->box.width / 2.0f,
            detection->box.y + detection->box.height / 2.0f
        );
        new_ball.bbox = detection->box;
        new_ball.yolo_confidence = detection->confidence;
        new_ball.color_match_score = matchColor(*detection, new_ball.color_profile, color_frame);
        
        // Add to tracked balls
        tracked_balls_.push_back(new_ball);
        
        std::cout << "[New3DTracker] Created new track ID=" << new_ball.id
                  << " color=" << new_ball.color_name
                  << " state=" << (new_ball.state == HELD ? "HELD" : "IN_FLIGHT")
                  << std::endl;
    }
}

void New3DTracker::finalizeBallPositions(const std::vector<SimpleHand>& hands) {
    for (auto& ball : tracked_balls_) {
        // Update final position based on state
        if (ball.state == HELD) {
            // For HELD balls: Use hand position as final position
            const SimpleHand* holding_hand = nullptr;
            for (const auto& hand : hands) {
                if (hand.id == ball.associated_hand_id) {
                    holding_hand = &hand;
                    break;
                }
            }
            
            if (holding_hand) {
                ball.last_known_position = holding_hand->wrist_pos_3d;
            } else {
                // Hand not found, keep predicted position
                ball.last_known_position = ball.predicted_position;
            }
        } else {
            // For IN_FLIGHT balls: Use Kalman prediction as final position
            ball.last_known_position = ball.predicted_position;
        }
        
        // Lock color after sufficient frames
        if (!ball.color_locked && ball.consecutive_frames_seen >= settings_.min_frames_for_color_lock) {
            ball.color_locked = true;
        }
        
        // Optional: Update position history for pattern analysis
        // This can be used for future siteswap detection
        ball.position_history.push_back(ball.last_known_position);
        ball.timestamp_history.push_back(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count()
        );
        
        // Keep history limited to last 100 positions
        if (ball.position_history.size() > 100) {
            ball.position_history.erase(ball.position_history.begin());
            ball.timestamp_history.erase(ball.timestamp_history.begin());
        }
    }
}

// ============================================================================
// HELPER METHODS
// ============================================================================

std::string New3DTracker::determineColor(const Detection& det, const cv::Mat& color_frame) {
    // Find the best matching color profile
    std::string best_color = "unknown";
    float best_score = 0.0f;
    
    for (const auto& profile : color_profiles_) {
        if (!profile.enabled) continue;
        
        float score = matchColor(det, profile, color_frame);
        if (score > best_score) {
            best_score = score;
            best_color = profile.name;
        }
    }
    
    return best_color;
}

float New3DTracker::matchColor(const Detection& det, const ColorProfile& profile,
                                const cv::Mat& color_frame) {
    // Get detection center
    cv::Point2f center(
        det.box.x + det.box.width / 2.0f,
        det.box.y + det.box.height / 2.0f
    );
    
    // Check bounds
    if (center.x < 0 || center.x >= color_frame.cols ||
        center.y < 0 || center.y >= color_frame.rows) {
        return 0.0f;
    }
    
    // Convert ROI to HSV using GPU if available
    const int sample_radius = settings_.color_sample_radius;
    int roi_x = std::max(0, static_cast<int>(center.x) - sample_radius);
    int roi_y = std::max(0, static_cast<int>(center.y) - sample_radius);
    int roi_width = std::min(color_frame.cols - roi_x, sample_radius * 2 + 1);
    int roi_height = std::min(color_frame.rows - roi_y, sample_radius * 2 + 1);
    
    cv::Rect roi(roi_x, roi_y, roi_width, roi_height);
    cv::Mat hsv_roi;
    
    if (gpu_hsv_converter_) {
        hsv_roi = gpu_hsv_converter_->convertRoiToHsv(color_frame, roi);
    } else {
        // Fallback to CPU conversion
        cv::Mat color_roi = color_frame(roi);
        cv::cvtColor(color_roi, hsv_roi, cv::COLOR_BGR2HSV);
    }
    
    // Use calibrated avg_hue and avg_saturation if available
    if (profile.avg_hue >= 0.0f && profile.avg_saturation >= 0.0f) {
        // Sample pixels around center
        std::vector<float> hue_samples;
        std::vector<float> sat_samples;
        
        for (int dy = -sample_radius; dy <= sample_radius; dy++) {
            for (int dx = -sample_radius; dx <= sample_radius; dx++) {
                int x = static_cast<int>(center.x) + dx - roi_x;
                int y = static_cast<int>(center.y) + dy - roi_y;
                
                if (x >= 0 && x < hsv_roi.cols && y >= 0 && y < hsv_roi.rows) {
                    cv::Vec3b hsv = hsv_roi.at<cv::Vec3b>(y, x);
                    hue_samples.push_back(static_cast<float>(hsv[0]));
                    sat_samples.push_back(static_cast<float>(hsv[1]));
                }
            }
        }
        
        if (hue_samples.empty()) return 0.0f;
        
        // Calculate average hue and saturation
        float avg_hue = 0.0f;
        float avg_sat = 0.0f;
        for (float h : hue_samples) avg_hue += h;
        for (float s : sat_samples) avg_sat += s;
        avg_hue /= hue_samples.size();
        avg_sat /= sat_samples.size();
        
        // Calculate euclidean distance in hue-saturation space
        float hue_diff = (avg_hue / 180.0f) - (profile.avg_hue / 180.0f);
        float sat_diff = (avg_sat / 255.0f) - (profile.avg_saturation / 255.0f);
        
        // Handle hue wrap-around
        if (hue_diff > 0.5f) hue_diff -= 1.0f;
        if (hue_diff < -0.5f) hue_diff += 1.0f;
        
        float euclidean_dist = std::sqrt(hue_diff * hue_diff + sat_diff * sat_diff);
        
        // Convert distance to similarity score
        return std::exp(-euclidean_dist * 10.0f);
    }
    
    // Legacy: Range-based matching
    int match_count = 0;
    int total_count = 0;
    
    for (int dy = -sample_radius; dy <= sample_radius; dy++) {
        for (int dx = -sample_radius; dx <= sample_radius; dx++) {
            int x = static_cast<int>(center.x) + dx - roi_x;
            int y = static_cast<int>(center.y) + dy - roi_y;
            
            if (x >= 0 && x < hsv_roi.cols && y >= 0 && y < hsv_roi.rows) {
                cv::Vec3b hsv = hsv_roi.at<cv::Vec3b>(y, x);
                
                // Check primary range
                bool matches = (hsv[0] >= profile.min_hsv[0] && hsv[0] <= profile.max_hsv[0] &&
                               hsv[1] >= profile.min_hsv[1] && hsv[1] <= profile.max_hsv[1] &&
                               hsv[2] >= profile.min_hsv[2] && hsv[2] <= profile.max_hsv[2]);
                
                // Check secondary range for wrap-around colors
                if (!matches && profile.min_hsv2[0] >= 0) {
                    matches = (hsv[0] >= profile.min_hsv2[0] && hsv[0] <= profile.max_hsv2[0] &&
                              hsv[1] >= profile.min_hsv2[1] && hsv[1] <= profile.max_hsv2[1] &&
                              hsv[2] >= profile.min_hsv2[2] && hsv[2] <= profile.max_hsv2[2]);
                }
                
                if (matches) match_count++;
                total_count++;
            }
        }
    }
    
    return total_count > 0 ? static_cast<float>(match_count) / total_count : 0.0f;
}

// ============================================================================
// YOLO DETECTION UTILITY METHODS
// ============================================================================

cv::Mat New3DTracker::preprocess(const cv::Mat& frame, float& scale_x, float& scale_y) {
    const int input_width = 640;
    const int input_height = 640;
    
    cv::Mat resized_frame;
    cv::resize(frame, resized_frame, cv::Size(input_width, input_height));
    scale_x = static_cast<float>(frame.cols) / input_width;
    scale_y = static_cast<float>(frame.rows) / input_height;
    
    cv::Mat float_frame;
    resized_frame.convertTo(float_frame, CV_32F, 1.0 / 255.0);
    
    // Manual blob conversion (HWC to CHW format)
    std::vector<cv::Mat> channels(3);
    cv::split(float_frame, channels);
    
    // Create output blob in NCHW format: [1, 3, 640, 640]
    cv::Mat blob(1, 3 * input_height * input_width, CV_32F);
    
    int channel_size = input_height * input_width;
    for (int c = 0; c < 3; c++) {
        std::memcpy(blob.ptr<float>() + c * channel_size,
                   channels[c].ptr<float>(),
                   channel_size * sizeof(float));
    }
    
    return blob.reshape(1, {1, 3, input_height, input_width});
}

float New3DTracker::getDepthAtPoint(const cv::Mat& depth_frame, const cv::Point2f& pixel) {
    if (pixel.x < 0 || pixel.x >= depth_frame.cols ||
        pixel.y < 0 || pixel.y >= depth_frame.rows) {
        return 0.0f;
    }
    
    // Sample 3x3 region and use median
    const int sample_size = 3;
    const int half_size = sample_size / 2;
    std::vector<float> samples;
    samples.reserve(sample_size * sample_size);
    
    int cx = static_cast<int>(pixel.x);
    int cy = static_cast<int>(pixel.y);
    
    for (int dy = -half_size; dy <= half_size; dy++) {
        for (int dx = -half_size; dx <= half_size; dx++) {
            int x = cx + dx;
            int y = cy + dy;
            
            if (x >= 0 && x < depth_frame.cols && y >= 0 && y < depth_frame.rows) {
                uint16_t depth_mm = depth_frame.at<uint16_t>(y, x);
                float depth_m = depth_mm / 1000.0f;
                
                if (depth_m > 0.1f && depth_m < 3.0f) {
                    samples.push_back(depth_m);
                }
            }
        }
    }
    
    if (samples.empty()) return 0.0f;
    
    size_t mid = samples.size() / 2;
    std::nth_element(samples.begin(), samples.begin() + mid, samples.end());
    return samples[mid];
}

cv::Point3f New3DTracker::deprojectToWorld(const cv::Point2f& pixel, float depth,
                                           const CameraIntrinsics& intrinsics) {
    if (depth <= 0) {
        return cv::Point3f(0, 0, 0);
    }
    
    float x = (pixel.x - intrinsics.ppx) * depth / intrinsics.fx;
    float y = (pixel.y - intrinsics.ppy) * depth / intrinsics.fy;
    return cv::Point3f(x, y, depth);
}

cv::Point2f New3DTracker::project3DTo2D(const cv::Point3f& world_pos,
                                        const CameraIntrinsics& intrinsics) {
    if (world_pos.z > 0) {
        float x_2d = (world_pos.x * intrinsics.fx) / world_pos.z + intrinsics.ppx;
        float y_2d = (world_pos.y * intrinsics.fy) / world_pos.z + intrinsics.ppy;
        return cv::Point2f(x_2d, y_2d);
    }
    return cv::Point2f(-1, -1);
}

// ============================================================================
// YOLO DETECTION METHODS
// ============================================================================

std::vector<Detection> New3DTracker::runBallDetection(
    const cv::Mat& preprocessed,
    float scale_x,
    float scale_y,
    const cv::Mat& color_frame,
    const cv::Mat& depth_frame,
    const CameraIntrinsics& intrinsics) {
    
    // Run inference
    ov::Tensor input_tensor(ball_model_.input().get_element_type(),
                           ball_model_.input().get_shape(),
                           preprocessed.data);
    ball_infer_.set_input_tensor(input_tensor);
    ball_infer_.infer();
    const ov::Tensor& output_tensor = ball_infer_.get_output_tensor();
    
    // Parse output
    const float* output_data = output_tensor.data<const float>();
    const int num_classes = 2;  // ball and ball_held
    const int num_channels = 4 + num_classes;
    
    cv::Mat output_buffer(num_channels, output_tensor.get_shape()[2], CV_32F, (void*)output_data);
    cv::transpose(output_buffer, output_buffer);
    
    std::vector<cv::Rect> boxes;
    std::vector<float> confidences;
    std::vector<int> class_ids;
    std::vector<Detection> raw_detections;
    
    for (int i = 0; i < output_buffer.rows; ++i) {
        cv::Mat class_scores = output_buffer.row(i).colRange(4, num_channels);
        cv::Point class_id_point;
        double max_class_score;
        cv::minMaxLoc(class_scores, nullptr, &max_class_score, nullptr, &class_id_point);
        
        float confidence = static_cast<float>(max_class_score);
        int class_id = class_id_point.x;
        
        // Apply class-specific confidence thresholds
        float threshold = (settings_.ignore_class || class_id == 0) ?
            settings_.ball_confidence_threshold : settings_.ball_held_confidence_threshold;
        
        if (confidence > threshold) {
            float cx = output_buffer.at<float>(i, 0);
            float cy = output_buffer.at<float>(i, 1);
            float w = output_buffer.at<float>(i, 2);
            float h = output_buffer.at<float>(i, 3);
            
            int left = static_cast<int>((cx - 0.5 * w) * scale_x);
            int top = static_cast<int>((cy - 0.5 * h) * scale_y);
            int width = static_cast<int>(w * scale_x);
            int height = static_cast<int>(h * scale_y);
            
            boxes.push_back(cv::Rect(left, top, width, height));
            confidences.push_back(confidence);
            class_ids.push_back(class_id);
            
            // Calculate 3D position
            cv::Point2f center_pixel(left + width / 2.0f, top + height / 2.0f);
            cv::Point3f world_pos(0, 0, 0);
            
            if (center_pixel.x >= 0 && center_pixel.x < depth_frame.cols &&
                center_pixel.y >= 0 && center_pixel.y < depth_frame.rows) {
                float depth_value_m = getDepthAtPoint(depth_frame, center_pixel);
                world_pos = deprojectToWorld(center_pixel, depth_value_m, intrinsics);
            }
            
            Detection det;
            det.box = cv::Rect_<float>(left, top, width, height);
            // Note: Detection doesn't have center field, calculate from box when needed
            det.world_pos = world_pos;
            det.confidence = confidence;
            det.class_id = class_id;
            raw_detections.push_back(det);
        }
    }
    
    // Apply NMS
    std::vector<int> nms_indices;
    float min_threshold = std::min(settings_.ball_confidence_threshold,
                                   settings_.ball_held_confidence_threshold);
    cv::dnn::NMSBoxes(boxes, confidences, min_threshold, 0.5f, nms_indices);
    
    // Filter detections by NMS results
    std::vector<Detection> filtered_detections;
    for (int idx : nms_indices) {
        for (auto& det : raw_detections) {
            if (det.box.x == boxes[idx].x && det.box.y == boxes[idx].y) {
                filtered_detections.push_back(det);
                break;
            }
        }
    }
    
    return filtered_detections;
}

std::vector<SimpleHand> New3DTracker::runPoseEstimation(
    const cv::Mat& preprocessed,
    float scale_x,
    float scale_y,
    const cv::Mat& color_frame,
    const cv::Mat& depth_frame,
    const CameraIntrinsics& intrinsics) {
    
    std::vector<SimpleHand> hands;
    
    // Run inference
    ov::Tensor input_tensor(pose_model_.input().get_element_type(),
                           pose_model_.input().get_shape(),
                           preprocessed.data);
    pose_infer_.set_input_tensor(input_tensor);
    pose_infer_.infer();
    const ov::Tensor& output_tensor = pose_infer_.get_output_tensor();
    
    // Parse YOLO-Pose output
    const float* output_data = output_tensor.data<const float>();
    const auto& shape = output_tensor.get_shape();
    
    if (shape.size() < 3) {
        return hands;
    }
    
    const int num_channels = shape[1];
    const int num_detections = shape[2];
    
    cv::Mat output_buffer(num_channels, num_detections, CV_32F, (void*)output_data);
    cv::transpose(output_buffer, output_buffer);
    
    const float pose_confidence_threshold = 0.3f;
    const float keypoint_confidence_threshold = 0.5f;
    
    for (int i = 0; i < output_buffer.rows; ++i) {
        float person_confidence = output_buffer.at<float>(i, 4);
        
        if (person_confidence < pose_confidence_threshold) continue;
        
        // Extract keypoints (17 keypoints in COCO format)
        std::vector<cv::Point3f> keypoints_3d(17, cv::Point3f(0, 0, 0));
        std::vector<float> keypoint_confidences(17, 0.0f);
        
        for (int kp_idx = 0; kp_idx < 17; ++kp_idx) {
            int base_idx = 5 + kp_idx * 3;
            
            float kp_x = output_buffer.at<float>(i, base_idx + 0) * scale_x;
            float kp_y = output_buffer.at<float>(i, base_idx + 1) * scale_y;
            float kp_conf = output_buffer.at<float>(i, base_idx + 2);
            
            keypoint_confidences[kp_idx] = kp_conf;
            
            if (kp_conf > keypoint_confidence_threshold) {
                cv::Point2f pixel(kp_x, kp_y);
                
                if (pixel.x >= 0 && pixel.x < depth_frame.cols &&
                    pixel.y >= 0 && pixel.y < depth_frame.rows) {
                    
                    float depth_value_m = getDepthAtPoint(depth_frame, pixel);
                    
                    if (depth_value_m > 0.1f && depth_value_m < 3.0f) {
                        keypoints_3d[kp_idx] = deprojectToWorld(pixel, depth_value_m, intrinsics);
                    }
                }
            }
        }
        
        // Create hands from wrist keypoints (9=left wrist, 10=right wrist)
        if (keypoint_confidences[9] > keypoint_confidence_threshold &&
            keypoints_3d[9].z > 0.1f && keypoints_3d[9].z < 3.0f) {
            
            SimpleHand left_hand;
            left_hand.wrist_pos_3d = keypoints_3d[9];
            left_hand.confidence = keypoint_confidences[9];
            left_hand.id = 0;
            left_hand.is_visible = true;
            hands.push_back(left_hand);
        }
        
        if (keypoint_confidences[10] > keypoint_confidence_threshold &&
            keypoints_3d[10].z > 0.1f && keypoints_3d[10].z < 3.0f) {
            
            SimpleHand right_hand;
            right_hand.wrist_pos_3d = keypoints_3d[10];
            right_hand.confidence = keypoint_confidences[10];
            right_hand.id = 1;
            right_hand.is_visible = true;
            hands.push_back(right_hand);
        }
        
        // Only process first person
        break;
    }
    
    return hands;
}

// ============================================================================
// MAIN UPDATE LOOP
// ============================================================================

std::pair<std::vector<New3DBall>, std::vector<BallEvent>> New3DTracker::updateNew3D(
    const cv::Mat& color_frame,
    const cv::Mat& depth_frame,
    const CameraIntrinsics& intrinsics) {
    
    std::vector<BallEvent> events;
    
    // Calculate time delta
    auto current_time = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(current_time - last_update_time_).count();
    last_update_time_ = current_time;
    
    // Clamp dt to reasonable values (10ms to 1s)
    dt = std::max(0.01f, std::min(dt, 1.0f));
    
    // Preprocess frame once for both models
    float scale_x, scale_y;
    cv::Mat preprocessed = preprocess(color_frame, scale_x, scale_y);
    
    // Run YOLO ball detection
    std::vector<Detection> detections = runBallDetection(
        preprocessed, scale_x, scale_y, color_frame, depth_frame, intrinsics);
    
    // Run YOLO pose estimation
    std::vector<SimpleHand> current_hands = runPoseEstimation(
        preprocessed, scale_x, scale_y, color_frame, depth_frame, intrinsics);
    
    // Store for visualization/debugging
    hands_ = current_hands;
    last_raw_detections_ = detections;
    
    // STEP 1: PREDICTION
    predictAllBalls(dt);
    
    // STEP 2: ASSOCIATION
    auto association = associateDetections(tracked_balls_, detections,
                                          settings_.association_max_distance_m);
    
    // STEP 3: UPDATE MATCHED
    updateMatchedBalls(association.matched_pairs, current_hands,
                      previous_frame_pose_, dt, events);
    
    // STEP 4: HANDLE UNMATCHED BALLS
    handleUnmatchedBalls(association.unmatched_balls);
    
    // STEP 5: HANDLE UNMATCHED DETECTIONS
    createNewTracks(association.unmatched_detections, color_frame);
    
    // STEP 6: FINALIZE
    finalizeBallPositions(current_hands);
    
    // Store current pose for next frame
    previous_frame_pose_ = createPose3D(current_hands);
    
    return {tracked_balls_, events};
}

std::pair<std::vector<SimpleBall>, std::vector<BallEvent>> New3DTracker::update(
    const cv::Mat& color_image,
    const cv::Mat& depth_image,
    const CameraIntrinsics& intrinsics) {
    
    // Call the main update function
    auto [new_balls, events] = updateNew3D(color_image, depth_image, intrinsics);
    
    // Convert New3DBall to SimpleBall for interface compatibility
    std::vector<SimpleBall> simple_balls;
    for (const auto& new_ball : new_balls) {
        simple_balls.push_back(convertToSimpleBall(new_ball));
    }
    
    return {simple_balls, events};
}

SimpleBall New3DTracker::convertToSimpleBall(const New3DBall& new_ball) {
    SimpleBall simple;
    simple.id = static_cast<int>(new_ball.id);
    simple.color_name = new_ball.color_name;
    simple.position = new_ball.last_known_position;
    simple.pixel_pos = new_ball.pixel_pos;
    simple.bbox = new_ball.bbox;
    simple.state = (new_ball.state == BallState::HELD) ? BallState::HELD : BallState::IN_FLIGHT;
    simple.is_held = (new_ball.state == BallState::HELD);
    simple.held_by_hand_id = new_ball.associated_hand_id;
    simple.yolo_confidence = new_ball.yolo_confidence;
    simple.color_match_score = new_ball.color_match_score;
    simple.tracking_reason = new_ball.tracking_reason;
    return simple;
}

TrackingSettings& New3DTracker::getTrackingSettings() {
    // Return a dummy TrackingSettings for now
    // This will be properly implemented when we integrate with the UI
    static TrackingSettings dummy;
    return dummy;
}

bool New3DTracker::calibrateColor(const std::string& color_name,
                                  cv::Point click_point,
                                  std::string& error_message) {
    // Stub - will be implemented in later phases
    error_message = "Color calibration not yet implemented for New3DTracker";
    return false;
}

// ============================================================================
// VISUALIZATION METHODS
// ============================================================================

void New3DTracker::drawBall(cv::Mat& frame, const New3DBall& ball,
                            const CameraIntrinsics& intrinsics) {
    // Determine bounding box color based on state
    cv::Scalar bbox_color = (ball.state == BallState::HELD) ?
        cv::Scalar(0, 255, 0) :   // Green for HELD
        cv::Scalar(255, 255, 0);  // Cyan for IN_FLIGHT
    
    // Draw bounding box
    cv::rectangle(frame, ball.bbox, bbox_color, 2);
    
    // Draw Kalman prediction if enabled
    if (settings_.show_kalman_prediction && ball.predicted_position.z > 0) {
        cv::Point2f pred_2d = project3DTo2D(ball.predicted_position, intrinsics);
        
        // Check if projection is valid and within frame bounds
        if (pred_2d.x >= 0 && pred_2d.x < frame.cols &&
            pred_2d.y >= 0 && pred_2d.y < frame.rows) {
            
            // Draw magenta circle at predicted position
            cv::circle(frame, pred_2d, 5, cv::Scalar(255, 0, 255), -1);
            
            // Draw line from current position to predicted position
            if (ball.pixel_pos.x >= 0 && ball.pixel_pos.x < frame.cols &&
                ball.pixel_pos.y >= 0 && ball.pixel_pos.y < frame.rows) {
                cv::line(frame, ball.pixel_pos, pred_2d, cv::Scalar(255, 0, 255), 1);
            }
        }
    }
    
    // Draw ball ID and color name
    std::string label = "ID:" + std::to_string(ball.id) + " " + ball.color_name;
    cv::Point label_pos(static_cast<int>(ball.bbox.x),
                       static_cast<int>(ball.bbox.y) - 5);
    
    // Ensure label position is within frame
    if (label_pos.y < 0) label_pos.y = 10;
    
    cv::putText(frame, label, label_pos, cv::FONT_HERSHEY_SIMPLEX, 0.5,
               bbox_color, 1);
    
    // Draw confidence scores below the label
    std::string conf_label = cv::format("YOLO:%.2f Color:%.2f",
                                       ball.yolo_confidence,
                                       ball.color_match_score);
    cv::Point conf_pos(static_cast<int>(ball.bbox.x),
                      static_cast<int>(ball.bbox.y) + static_cast<int>(ball.bbox.height) + 15);
    
    // Ensure confidence label is within frame
    if (conf_pos.y >= frame.rows) {
        conf_pos.y = static_cast<int>(ball.bbox.y) - 20;
    }
    
    cv::putText(frame, conf_label, conf_pos, cv::FONT_HERSHEY_SIMPLEX, 0.4,
               bbox_color, 1);
    
    // Draw tracking reason for debugging (below confidence)
    if (!ball.tracking_reason.empty()) {
        cv::Point reason_pos(static_cast<int>(ball.bbox.x),
                           conf_pos.y + 12);
        
        if (reason_pos.y < frame.rows) {
            cv::putText(frame, ball.tracking_reason, reason_pos,
                       cv::FONT_HERSHEY_SIMPLEX, 0.35, bbox_color, 1);
        }
    }
}

void New3DTracker::drawAssociations(cv::Mat& frame,
                                    const std::vector<MatchPair>& matches,
                                    const CameraIntrinsics& intrinsics) {
    if (!settings_.show_association_lines) {
        return;
    }
    
    for (const auto& match : matches) {
        cv::Point2f ball_pos = match.ball->pixel_pos;
        cv::Point2f det_pos = cv::Point2f(
            match.detection->box.x + match.detection->box.width / 2.0f,
            match.detection->box.y + match.detection->box.height / 2.0f
        );
        
        // Determine line color based on distance quality
        // Green = good match (< 0.2m)
        // Yellow = ok match (0.2m - 0.4m)
        // Red = poor match (> 0.4m)
        cv::Scalar line_color;
        if (match.distance < 0.2f) {
            line_color = cv::Scalar(0, 255, 0);  // Green
        } else if (match.distance < 0.4f) {
            line_color = cv::Scalar(0, 255, 255);  // Yellow
        } else {
            line_color = cv::Scalar(0, 0, 255);  // Red
        }
        
        // Draw line from ball to detection
        cv::line(frame, ball_pos, det_pos, line_color, 1);
        
        // Draw distance label at midpoint
        cv::Point2f mid = (ball_pos + det_pos) * 0.5f;
        std::string dist_label = cv::format("%.2fm", match.distance);
        
        // Ensure midpoint is within frame
        if (mid.x >= 0 && mid.x < frame.cols && mid.y >= 0 && mid.y < frame.rows) {
            cv::putText(frame, dist_label, mid, cv::FONT_HERSHEY_SIMPLEX, 0.4,
                       line_color, 1);
        }
    }
}

void New3DTracker::drawHandThresholds(cv::Mat& frame,
                                     const std::vector<SimpleHand>& hands,
                                     const CameraIntrinsics& intrinsics) {
    if (!settings_.show_held_radius) {
        return;
    }
    
    for (const auto& hand : hands) {
        // Project wrist position to 2D
        cv::Point2f wrist_2d = project3DTo2D(hand.wrist_pos_3d, intrinsics);
        
        // Check if projection is valid
        if (wrist_2d.x < 0 || wrist_2d.y < 0) {
            continue;
        }
        
        // Calculate radius in pixels (scale with depth)
        float depth = hand.wrist_pos_3d.z;
        if (depth <= 0) {
            continue;
        }
        
        float radius_pixels = (settings_.held_radius_m / depth) * intrinsics.fx;
        
        // Draw yellow circle around wrist
        cv::circle(frame, wrist_2d, static_cast<int>(radius_pixels),
                  cv::Scalar(0, 255, 255), 2);
        
        // Label hand (L/R)
        std::string label = (hand.id == 0) ? "L" : "R";
        cv::putText(frame, label, wrist_2d, cv::FONT_HERSHEY_SIMPLEX, 0.7,
                   cv::Scalar(0, 255, 255), 2);
    }
}

void New3DTracker::evaluateOverrideCriteria(std::vector<Detection>& detections,
                                           const cv::Mat& color_image) {
    // Stub - will be implemented in later phases
}

bool New3DTracker::updateSetting(const std::string& key, const std::string& value) {
    // Stub - will be implemented when UI integration is added
    return false;
}