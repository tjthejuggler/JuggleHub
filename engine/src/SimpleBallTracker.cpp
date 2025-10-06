#include "SimpleBallTracker.hpp"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <set>

// Helper function for depth filtering
static float get_filtered_depth(const cv::Mat& depth_frame, const cv::Point2f& pixel) {
    const int SAMPLE_SIZE = 5;
    const int half_size = SAMPLE_SIZE / 2;
    
    std::vector<float> depth_samples;
    depth_samples.reserve(SAMPLE_SIZE * SAMPLE_SIZE);
    
    int center_x = static_cast<int>(pixel.x);
    int center_y = static_cast<int>(pixel.y);
    
    for (int dy = -half_size; dy <= half_size; ++dy) {
        for (int dx = -half_size; dx <= half_size; ++dx) {
            int x = center_x + dx;
            int y = center_y + dy;
            
            if (x >= 0 && x < depth_frame.cols && y >= 0 && y < depth_frame.rows) {
                uint16_t depth_mm = depth_frame.at<uint16_t>(y, x);
                float depth_m = depth_mm / 1000.0f;
                
                if (depth_m > 0.1f && depth_m < 3.0f) {
                    depth_samples.push_back(depth_m);
                }
            }
        }
    }
    
    if (depth_samples.empty()) return 0.0f;
    
    std::sort(depth_samples.begin(), depth_samples.end());
    return depth_samples[depth_samples.size() / 2];
}

SimpleBallTracker::SimpleBallTracker(const std::string& ball_model_path,
                                    const std::string& pose_model_path,
                                    const std::string& device_name,
                                    const std::string& settings_file)
    : settings_file_(settings_file) {
    
    std::cout << "[SimpleBallTracker] Initializing..." << std::endl;
    
    // Load OpenVINO models
    std::cout << "[SimpleBallTracker] Loading ball model: " << ball_model_path << std::endl;
    ball_model_ = core_.compile_model(ball_model_path, device_name);
    ball_infer_ = ball_model_.create_infer_request();
    
    std::cout << "[SimpleBallTracker] Loading pose model: " << pose_model_path << std::endl;
    pose_model_ = core_.compile_model(pose_model_path, device_name);
    pose_infer_ = pose_model_.create_infer_request();
    
    last_update_time_ = std::chrono::steady_clock::now();
    
    // Load color profiles from settings file
    if (!loadSettings()) {
        std::cerr << "[SimpleBallTracker] Failed to load settings, using defaults" << std::endl;
        // Set up default color profiles
        color_profiles_.push_back(ColorProfile("green", cv::Scalar(45, 100, 100), cv::Scalar(75, 255, 255)));
        color_profiles_.push_back(ColorProfile("pink", cv::Scalar(140, 100, 100), cv::Scalar(175, 255, 255)));
        color_profiles_.push_back(ColorProfile("orange", cv::Scalar(5, 100, 100), cv::Scalar(20, 255, 255)));
    }
    
    // Initialize balls based on enabled color profiles
    int ball_id = 0;
    for (const auto& profile : color_profiles_) {
        if (profile.enabled && ball_id < 3) {  // Max 3 balls
            SimpleBall ball;
            ball.id = ball_id++;
            ball.color_name = profile.name;
            
            // Initialize color predictor with tracking settings
            ColorBasedPredictor::PredictionSettings pred_settings;
            pred_settings.history_frames = tracking_settings_.prediction_history_frames;
            pred_settings.prediction_radius_m = tracking_settings_.prediction_radius_m;
            ball.color_predictor.setSettings(pred_settings);
            
            balls_.push_back(ball);
        }
    }
    
    std::cout << "[SimpleBallTracker] Initialized with " << balls_.size() << " balls" << std::endl;
}

bool SimpleBallTracker::loadSettings() {
    try {
        std::ifstream file(settings_file_);
        if (!file.is_open()) {
            return false;
        }
        
        json j;
        file >> j;
        
        color_profiles_.clear();
        
        // ball_settings.json format: top-level keys are color names
        for (auto& [color_name, color_data] : j.items()) {
            bool enabled = color_data.value("enabled", true);
            
            std::cout << "[SimpleBallTracker] Loading color profile: " << color_name << std::endl;
            
            cv::Scalar min_hsv(
                color_data["min_hsv"][0],
                color_data["min_hsv"][1],
                color_data["min_hsv"][2]
            );
            
            cv::Scalar max_hsv(
                color_data["max_hsv"][0],
                color_data["max_hsv"][1],
                color_data["max_hsv"][2]
            );
            
            cv::Scalar min_hsv2(-1, -1, -1);
            cv::Scalar max_hsv2(-1, -1, -1);
            
            if (color_data.contains("min_hsv2")) {
                min_hsv2 = cv::Scalar(
                    color_data["min_hsv2"][0],
                    color_data["min_hsv2"][1],
                    color_data["min_hsv2"][2]
                );
            }
            
            if (color_data.contains("max_hsv2")) {
                max_hsv2 = cv::Scalar(
                    color_data["max_hsv2"][0],
                    color_data["max_hsv2"][1],
                    color_data["max_hsv2"][2]
                );
            }
            
            color_profiles_.push_back(ColorProfile(color_name, min_hsv, max_hsv, min_hsv2, max_hsv2, enabled));
            
            std::cout << "[SimpleBallTracker] Loaded color '" << color_name << "' enabled=" << enabled << std::endl;
        }
        
        std::cout << "[SimpleBallTracker] Loaded " << color_profiles_.size() << " color profiles" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "[SimpleBallTracker] Error loading settings: " << e.what() << std::endl;
        return false;
    }
}

void SimpleBallTracker::saveSettings() {
    try {
        json j;
        
        // Save in ball_settings.json format: top-level keys are color names
        for (const auto& profile : color_profiles_) {
            json color_data;
            color_data["enabled"] = profile.enabled;
            color_data["min_hsv"] = {profile.min_hsv[0], profile.min_hsv[1], profile.min_hsv[2]};
            color_data["max_hsv"] = {profile.max_hsv[0], profile.max_hsv[1], profile.max_hsv[2]};
            
            if (profile.min_hsv2[0] >= 0) {
                color_data["min_hsv2"] = {profile.min_hsv2[0], profile.min_hsv2[1], profile.min_hsv2[2]};
                color_data["max_hsv2"] = {profile.max_hsv2[0], profile.max_hsv2[1], profile.max_hsv2[2]};
            }
            
            j[profile.name] = color_data;
        }
        
        std::ofstream file(settings_file_);
        file << j.dump(4);  // Pretty print with 4-space indent
        
        std::cout << "[SimpleBallTracker] Settings saved to " << settings_file_ << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "[SimpleBallTracker] Error saving settings: " << e.what() << std::endl;
    }
}

bool SimpleBallTracker::updateSetting(const std::string& key, const std::string& value) {
    // Handle color profile enable/disable
    if (key.find("track_") == 0) {
        std::string color_name = key.substr(6);  // Remove "track_" prefix
        bool enable = (value == "true" || value == "1");
        
        for (auto& profile : color_profiles_) {
            if (profile.name == color_name) {
                profile.enabled = enable;
                saveSettings();
                return true;
            }
        }
    }
    
    // Handle tracking settings
    try {
        if (key == "ml_ball_weight") {
            tracking_settings_.ml_ball_weight = std::stof(value);
            std::cout << "[SimpleBallTracker] Updated ml_ball_weight to " << value << std::endl;
            return true;
        }
        else if (key == "ml_ball_held_weight") {
            tracking_settings_.ml_ball_held_weight = std::stof(value);
            std::cout << "[SimpleBallTracker] Updated ml_ball_held_weight to " << value << std::endl;
            return true;
        }
        else if (key == "wrist_proximity_weight") {
            tracking_settings_.wrist_proximity_weight = std::stof(value);
            std::cout << "[SimpleBallTracker] Updated wrist_proximity_weight to " << value << std::endl;
            return true;
        }
        else if (key == "wrist_proximity_threshold") {
            tracking_settings_.wrist_proximity_threshold = std::stof(value);
            std::cout << "[SimpleBallTracker] Updated wrist_proximity_threshold to " << value << "m" << std::endl;
            return true;
        }
        else if (key == "undetected_near_hand_threshold") {
            tracking_settings_.undetected_near_hand_threshold = std::stof(value);
            std::cout << "[SimpleBallTracker] Updated undetected_near_hand_threshold to " << value << "m" << std::endl;
            return true;
        }
        else if (key == "min_frames_for_state_change") {
            tracking_settings_.min_frames_for_state_change = std::stoi(value);
            std::cout << "[SimpleBallTracker] Updated min_frames_for_state_change to " << value << std::endl;
            return true;
        }
        else if (key == "prediction_history_frames") {
            tracking_settings_.prediction_history_frames = std::stoi(value);
            // Update all ball predictors
            for (auto& ball : balls_) {
                auto settings = ball.color_predictor.getSettings();
                settings.history_frames = tracking_settings_.prediction_history_frames;
                ball.color_predictor.setSettings(settings);
            }
            std::cout << "[SimpleBallTracker] Updated prediction_history_frames to " << value << std::endl;
            return true;
        }
        else if (key == "prediction_radius_m") {
            tracking_settings_.prediction_radius_m = std::stof(value);
            // Update all ball predictors
            for (auto& ball : balls_) {
                auto settings = ball.color_predictor.getSettings();
                settings.prediction_radius_m = tracking_settings_.prediction_radius_m;
                ball.color_predictor.setSettings(settings);
            }
            std::cout << "[SimpleBallTracker] Updated prediction_radius_m to " << value << "m" << std::endl;
            return true;
        }
        // Color tracker matching weights
        else if (key == "yolo_confidence_weight") {
            tracking_settings_.yolo_confidence_weight = std::stof(value);
            std::cout << "[SimpleBallTracker] Updated yolo_confidence_weight to " << value << std::endl;
            return true;
        }
        else if (key == "yolo_class_weight") {
            tracking_settings_.yolo_class_weight = std::stof(value);
            std::cout << "[SimpleBallTracker] Updated yolo_class_weight to " << value << std::endl;
            return true;
        }
        else if (key == "color_match_weight") {
            tracking_settings_.color_match_weight = std::stof(value);
            std::cout << "[SimpleBallTracker] Updated color_match_weight to " << value << std::endl;
            return true;
        }
        else if (key == "kalman_proximity_weight") {
            tracking_settings_.kalman_proximity_weight = std::stof(value);
            std::cout << "[SimpleBallTracker] Updated kalman_proximity_weight to " << value << std::endl;
            return true;
        }
        else if (key == "min_yolo_score_threshold") {
            tracking_settings_.min_yolo_score_threshold = std::stof(value);
            std::cout << "[SimpleBallTracker] Updated min_yolo_score_threshold to " << value << std::endl;
            return true;
        }
        else if (key == "override_confidence_threshold") {
            tracking_settings_.override_confidence_threshold = std::stof(value);
            std::cout << "[SimpleBallTracker] Updated override_confidence_threshold to " << value << std::endl;
            return true;
        }
        else if (key == "override_color_threshold") {
            tracking_settings_.override_color_threshold = std::stof(value);
            std::cout << "[SimpleBallTracker] Updated override_color_threshold to " << value << std::endl;
            return true;
        }
        else if (key == "override_require_ball_class") {
            tracking_settings_.override_require_ball_class = (value == "true" || value == "1");
            std::cout << "[SimpleBallTracker] Updated override_require_ball_class to " << value << std::endl;
            return true;
        }
    } catch (const std::exception& e) {
        std::cerr << "[SimpleBallTracker] Error parsing setting " << key << "=" << value << ": " << e.what() << std::endl;
        return false;
    }
    
    return false;
}

float SimpleBallTracker::matchColor(const Detection& det, const ColorProfile& profile, 
                                   const cv::Mat& hsv_frame) {
    // Get detection center
    cv::Point2f center(det.box.x + det.box.width / 2.0f,
                      det.box.y + det.box.height / 2.0f);
    
    // Check bounds
    if (center.x < 0 || center.x >= hsv_frame.cols ||
        center.y < 0 || center.y >= hsv_frame.rows) {
        return 0.0f;
    }
    
    // Sample 7x7 region around center
    const int sample_radius = 7;
    int match_count = 0;
    int total_count = 0;
    
    for (int dy = -sample_radius; dy <= sample_radius; dy++) {
        for (int dx = -sample_radius; dx <= sample_radius; dx++) {
            int x = static_cast<int>(center.x) + dx;
            int y = static_cast<int>(center.y) + dy;
            
            if (x >= 0 && x < hsv_frame.cols && y >= 0 && y < hsv_frame.rows) {
                cv::Vec3b hsv = hsv_frame.at<cv::Vec3b>(y, x);
                
                // Check primary range
                bool matches = (hsv[0] >= profile.min_hsv[0] && hsv[0] <= profile.max_hsv[0] &&
                               hsv[1] >= profile.min_hsv[1] && hsv[1] <= profile.max_hsv[1] &&
                               hsv[2] >= profile.min_hsv[2] && hsv[2] <= profile.max_hsv[2]);
                
                // Check secondary range for wrap-around colors (like red)
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

const Detection* SimpleBallTracker::findBestColorMatch(
    const std::vector<Detection>& detections,
    const ColorProfile& profile,
    const cv::Mat& hsv_frame,
    const std::set<int>& used_indices,
    const cv::Point3f& kalman_prediction) {
    
    const Detection* best_det = nullptr;
    float best_combined_score = 0.0f;
    
    // Store best scoring components for visualization
    float best_class_score = 0.0f;
    float best_confidence_score = 0.0f;
    float best_color_score = 0.0f;
    float best_kalman_score = 0.0f;
    
    // Store rejection reasons for debug output
    std::string rejection_reason = "";
    
    // Store detailed evaluation for ALL detections (for visualization)
    std::vector<DetectionEvaluation> evaluations;
    
    // Check if Kalman prediction is valid (non-zero)
    bool has_kalman_prediction = (kalman_prediction.z > 0.01f);
    
    // Open debug log
    std::ofstream debug_log("engine_debug.log", std::ios::app);
    debug_log << "\n  >> findBestColorMatch() for " << profile.name << " <<" << std::endl;
    debug_log << "  Has Kalman prediction: " << (has_kalman_prediction ? "YES" : "NO") << std::endl;
    if (has_kalman_prediction) {
        debug_log << "  Kalman pred pos: (" << kalman_prediction.x << ", " << kalman_prediction.y << ", " << kalman_prediction.z << ")" << std::endl;
        debug_log << "  Prediction radius: " << tracking_settings_.prediction_radius_m << "m" << std::endl;
    }
    debug_log << "  Evaluating " << detections.size() << " detections:" << std::endl;
    
    for (const auto& det : detections) {
        debug_log << "\n  Detection #" << det.index << ":" << std::endl;
        debug_log << "    Position: (" << det.world_pos.x << ", " << det.world_pos.y << ", " << det.world_pos.z << ")" << std::endl;
        debug_log << "    Confidence: " << det.confidence << std::endl;
        debug_log << "    Class ID: " << det.class_id << " (" << (det.class_id == 0 ? "ball" : "ball_held") << ")" << std::endl;
        DetectionEvaluation eval;
        eval.detection_index = det.index;
        eval.passed_filters = false;
        eval.total_score = 0.0f;
        eval.class_score = 0.0f;
        eval.confidence_score = 0.0f;
        eval.color_score = 0.0f;
        eval.kalman_score = 0.0f;
        eval.distance_to_prediction = has_kalman_prediction ? cv::norm(det.world_pos - kalman_prediction) : -1.0f;
        
        // Skip if already used
        if (used_indices.find(det.index) != used_indices.end()) {
            rejection_reason = "Already used";
            eval.result = "REJECTED: Used";
            evaluations.push_back(eval);
            debug_log << "    REJECTED: Already used by another ball" << std::endl;
            continue;
        }
        
        // Skip if invalid depth
        if (det.world_pos.z < MIN_DEPTH || det.world_pos.z > MAX_DEPTH) {
            rejection_reason = "Invalid depth";
            eval.result = "REJECTED: Depth";
            evaluations.push_back(eval);
            debug_log << "    REJECTED: Invalid depth (" << det.world_pos.z << "m not in range " << MIN_DEPTH << "-" << MAX_DEPTH << ")" << std::endl;
            continue;
        }
        
        // CRITICAL: If we have a valid Kalman prediction, reject detections outside the prediction radius
        // This ensures we only use YOLO detections that are within the expected search area
        if (has_kalman_prediction) {
            float dist_3d = cv::norm(det.world_pos - kalman_prediction);
            float radius = tracking_settings_.prediction_radius_m;
            debug_log << "    Distance to Kalman pred: " << dist_3d << "m (radius: " << radius << "m)" << std::endl;
            
            // Reject detection if it's outside the prediction circumference
            if (dist_3d > radius) {
                char reason[128];
                snprintf(reason, sizeof(reason), "REJECTED: Dist %.2fm>%.2fm", dist_3d, radius);
                rejection_reason = reason;
                eval.result = reason;
                evaluations.push_back(eval);
                debug_log << "    REJECTED: Outside Kalman radius" << std::endl;
                continue;  // Skip this detection - it's too far from where we expect the ball to be
            }
        }
        
        float color_score = matchColor(det, profile, hsv_frame);
        debug_log << "    Color match score: " << color_score << " (threshold: " << MIN_COLOR_MATCH_SCORE << ")" << std::endl;
        
        // Only consider detections with reasonable color match
        if (color_score < MIN_COLOR_MATCH_SCORE) {
            char reason[128];
            snprintf(reason, sizeof(reason), "REJECTED: Color %.2f<%.2f", color_score, MIN_COLOR_MATCH_SCORE);
            rejection_reason = reason;
            eval.result = reason;
            evaluations.push_back(eval);
            debug_log << "    REJECTED: Low color match" << std::endl;
            continue;
        }
        
        eval.passed_filters = true;
        
        // CONFIGURABLE PRIORITY SCORING:
        // Uses weights from tracking_settings_ to balance different factors
        
        // 1. YOLO class score (ball vs ball_held)
        float class_score = (det.class_id == 0) ? tracking_settings_.yolo_class_weight : 1.0f;
        
        // 2. YOLO confidence score
        float confidence_score = det.confidence * tracking_settings_.yolo_confidence_weight;
        
        // 3. Color match score
        float weighted_color_score = color_score * tracking_settings_.color_match_weight;
        
        // 4. Kalman prediction proximity score (if enabled and available)
        float kalman_score = 0.0f;
        if (has_kalman_prediction && tracking_settings_.kalman_proximity_weight > 0.0f) {
            // Calculate 3D distance from detection to Kalman prediction
            float dist_3d = cv::norm(det.world_pos - kalman_prediction);
            
            // Convert distance to score: closer = higher score
            // Use exponential decay: score = weight * exp(-dist / radius)
            // This gives full weight at 0 distance, half weight at ~0.7*radius
            float radius = tracking_settings_.prediction_radius_m;
            kalman_score = tracking_settings_.kalman_proximity_weight * std::exp(-dist_3d / radius);
        }
        
        // Combine all scores
        float combined_score = class_score + confidence_score + weighted_color_score + kalman_score;
        
        // Store in evaluation
        eval.class_score = class_score;
        eval.confidence_score = confidence_score;
        eval.color_score = weighted_color_score;
        eval.kalman_score = kalman_score;
        eval.total_score = combined_score;
        
        debug_log << "    SCORING:" << std::endl;
        debug_log << "      Class score: " << class_score << std::endl;
        debug_log << "      Confidence score: " << confidence_score << std::endl;
        debug_log << "      Color score: " << weighted_color_score << std::endl;
        debug_log << "      Kalman score: " << kalman_score << std::endl;
        debug_log << "      TOTAL: " << combined_score << std::endl;
        
        if (combined_score > best_combined_score) {
            debug_log << "    >>> NEW BEST DETECTION <<<" << std::endl;
            best_combined_score = combined_score;
            best_det = &det;
            
            // Store scoring components for this detection (will be saved to ball if this is the best match)
            best_class_score = class_score;
            best_confidence_score = confidence_score;
            best_color_score = weighted_color_score;
            best_kalman_score = kalman_score;
            
            eval.result = "SELECTED";
        } else {
            debug_log << "    Not better than current best (" << best_combined_score << ")" << std::endl;
            char result[64];
            snprintf(result, sizeof(result), "Score %.2f < %.2f", combined_score, best_combined_score);
            eval.result = result;
        }
        
        evaluations.push_back(eval);
    }
    
    debug_log << "\n  FINAL RESULT:" << std::endl;
    if (best_det) {
        debug_log << "    Selected detection #" << best_det->index << " with score " << best_combined_score << std::endl;
    } else {
        debug_log << "    NO DETECTION SELECTED" << std::endl;
        if (!rejection_reason.empty()) {
            debug_log << "    Last rejection reason: " << rejection_reason << std::endl;
        }
    }
    debug_log.close();
    
    // Store evaluations for visualization
    last_detection_evaluations_ = evaluations;
    
    // Store the scoring components in a temporary location (will be retrieved by caller)
    last_match_class_score_ = best_class_score;
    last_match_confidence_score_ = best_confidence_score;
    last_match_color_score_ = best_color_score;
    last_match_kalman_score_ = best_kalman_score;
    last_match_total_score_ = best_combined_score;
    
    // If no detection was found, store the rejection reason
    if (!best_det && !rejection_reason.empty()) {
        // Store in a member variable so caller can access it
        last_rejection_reason_ = rejection_reason;
    } else {
        last_rejection_reason_ = "";
    }
    
    return best_det;
}

cv::Point2f SimpleBallTracker::searchForColorBlob(const cv::Mat& hsv_frame,
                                                  const ColorProfile& profile,
                                                  const cv::Point2f& search_center,
                                                  int radius) {
    // Create mask for color
    cv::Mat mask1, mask2, mask;
    cv::inRange(hsv_frame, profile.min_hsv, profile.max_hsv, mask1);
    
    if (profile.min_hsv2[0] >= 0) {
        cv::inRange(hsv_frame, profile.min_hsv2, profile.max_hsv2, mask2);
        cv::bitwise_or(mask1, mask2, mask);
    } else {
        mask = mask1;
    }
    
    // Find contours
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    
    // Find largest contour (prioritize size over proximity when near hands)
    double max_area = 0;
    cv::Point2f best_center(-1, -1);
    
    for (const auto& contour : contours) {
        double area = cv::contourArea(contour);
        if (area < 50.0) continue;  // Minimum blob size
        
        cv::Moments m = cv::moments(contour);
        if (m.m00 == 0) continue;
        
        cv::Point2f center(m.m10 / m.m00, m.m01 / m.m00);
        
        // Check if within search radius
        float dist = cv::norm(center - search_center);
        if (dist > radius) continue;
        
        // Prefer larger blobs - this helps when ball is occluded by hand
        // The largest blob is most likely the actual ball
        if (area > max_area) {
            max_area = area;
            best_center = center;
        }
    }
    
    return best_center;
}

float SimpleBallTracker::getDepthAtPoint(const cv::Mat& depth_frame, const cv::Point2f& point) {
    if (point.x < 0 || point.x >= depth_frame.cols ||
        point.y < 0 || point.y >= depth_frame.rows) {
        return 0.0f;
    }
    
    // Sample 5x5 region and take median
    const int sample_size = 5;
    const int half_size = sample_size / 2;
    std::vector<float> samples;
    
    int cx = static_cast<int>(point.x);
    int cy = static_cast<int>(point.y);
    
    for (int dy = -half_size; dy <= half_size; dy++) {
        for (int dx = -half_size; dx <= half_size; dx++) {
            int x = cx + dx;
            int y = cy + dy;
            
            if (x >= 0 && x < depth_frame.cols && y >= 0 && y < depth_frame.rows) {
                uint16_t depth_mm = depth_frame.at<uint16_t>(y, x);
                float depth_m = depth_mm / 1000.0f;
                
                if (depth_m > MIN_DEPTH && depth_m < MAX_DEPTH) {
                    samples.push_back(depth_m);
                }
            }
        }
    }
    
    if (samples.empty()) return 0.0f;
    
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

cv::Point3f SimpleBallTracker::deprojectToWorld(const cv::Point2f& pixel, float depth,
                                               const CameraIntrinsics& intrinsics) {
    if (depth <= 0) {
        return cv::Point3f(0, 0, 0);
    }
    
    float x = (pixel.x - intrinsics.ppx) * depth / intrinsics.fx;
    float y = (pixel.y - intrinsics.ppy) * depth / intrinsics.fy;
    return cv::Point3f(x, y, depth);
}

uint64_t SimpleBallTracker::getCurrentTimestamp() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

bool SimpleBallTracker::isBallHeld(SimpleBall& ball, const std::vector<SimpleHand>& hands) {
    // Open debug log file in append mode
    std::ofstream debug_log("engine_debug.log", std::ios::app);
    debug_log << "\n  >> isBallHeld() for Ball " << ball.id << " <<" << std::endl;
    
    // Use weighted scoring system based on tracking settings
    float held_score = 0.0f;
    float in_air_score = 0.0f;
    
    debug_log << "  Tracking settings:" << std::endl;
    debug_log << "    ml_ball_weight: " << tracking_settings_.ml_ball_weight << std::endl;
    debug_log << "    ml_ball_held_weight: " << tracking_settings_.ml_ball_held_weight << std::endl;
    debug_log << "    wrist_proximity_weight: " << tracking_settings_.wrist_proximity_weight << std::endl;
    debug_log << "    wrist_proximity_threshold: " << tracking_settings_.wrist_proximity_threshold << "m" << std::endl;
    
    // 1. ML Classification Evidence
    debug_log << "  ML Classification:" << std::endl;
    debug_log << "    YOLO class_id: " << ball.yolo_class_id << std::endl;
    if (ball.yolo_class_id == 1) {  // ball_held class
        held_score += tracking_settings_.ml_ball_held_weight;
        debug_log << "    Class is 'ball_held' -> adding " << tracking_settings_.ml_ball_held_weight
                 << " to held_score" << std::endl;
    } else if (ball.yolo_class_id == 0) {  // ball (in-air) class
        in_air_score += tracking_settings_.ml_ball_weight;
        debug_log << "    Class is 'ball' (in-air) -> adding " << tracking_settings_.ml_ball_weight
                 << " to in_air_score" << std::endl;
    } else {
        debug_log << "    Unknown class_id!" << std::endl;
    }
    
    // 2. Wrist Proximity Evidence - also store distance for UI display
    float min_dist = std::numeric_limits<float>::max();
    int closest_hand = -1;
    
    debug_log << "  Wrist Proximity Check:" << std::endl;
    debug_log << "    Number of hands: " << hands.size() << std::endl;
    
    for (const auto& hand : hands) {
        debug_log << "    Hand " << hand.id << " (visible=" << hand.is_visible << ")" << std::endl;
        if (!hand.is_visible) continue;
        
        float dist = cv::norm(ball.position - hand.wrist_pos_3d);
        debug_log << "      Distance to ball: " << dist << "m" << std::endl;
        
        if (dist < min_dist) {
            min_dist = dist;
            closest_hand = hand.id;
            debug_log << "      This is the closest hand so far" << std::endl;
        }
    }
    
    // Store distance for UI display
    ball.distance_to_nearest_wrist = (min_dist < std::numeric_limits<float>::max()) ? min_dist : -1.0f;
    
    debug_log << "    Minimum distance to any wrist: " << min_dist << "m" << std::endl;
    debug_log << "    Threshold: " << tracking_settings_.wrist_proximity_threshold << "m" << std::endl;
    
    if (min_dist < tracking_settings_.wrist_proximity_threshold) {
        held_score += tracking_settings_.wrist_proximity_weight;
        ball.held_by_hand_id = closest_hand;
        debug_log << "    Ball is NEAR hand " << closest_hand << " -> adding "
                 << tracking_settings_.wrist_proximity_weight << " to held_score" << std::endl;
    } else {
        ball.held_by_hand_id = -1;
        debug_log << "    Ball is NOT near any hand" << std::endl;
    }
    
    debug_log << "  FINAL SCORES:" << std::endl;
    debug_log << "    held_score: " << held_score << std::endl;
    debug_log << "    in_air_score: " << in_air_score << std::endl;
    
    // Decision: held if held_score is higher
    bool result = held_score > in_air_score;
    debug_log << "  DECISION: " << (result ? "HELD" : "IN_AIR")
             << " (held_score " << (result ? ">" : "<=") << " in_air_score)" << std::endl;
    
    debug_log.close();
    return result;
}

std::vector<BallEvent> SimpleBallTracker::detectStatesAndEvents(
    std::vector<SimpleBall>& balls,
    const std::vector<SimpleHand>& hands) {
    
    std::vector<BallEvent> events;
    
    // Open debug log file in append mode
    std::ofstream debug_log("engine_debug.log", std::ios::app);
    debug_log << "\n=== detectStatesAndEvents() ===" << std::endl;
    debug_log << "Number of balls: " << balls.size() << std::endl;
    debug_log << "Number of hands: " << hands.size() << std::endl;
    
    for (auto& ball : balls) {
        debug_log << "\n--- Ball " << ball.id << " (" << ball.color_name << ") ---" << std::endl;
        debug_log << "  Current is_held state: " << (ball.is_held ? "HELD" : "IN_AIR") << std::endl;
        debug_log << "  YOLO class_id: " << ball.yolo_class_id << " (0=ball, 1=ball_held)" << std::endl;
        debug_log << "  Has YOLO detection: " << (ball.has_yolo_detection ? "YES" : "NO") << std::endl;
        debug_log << "  Frames without YOLO: " << ball.frames_without_yolo << std::endl;
        debug_log << "  Distance to nearest wrist: " << ball.distance_to_nearest_wrist << "m" << std::endl;
        debug_log << "  Held by hand ID: " << ball.held_by_hand_id << std::endl;
        debug_log << "  State change counter: " << ball.state_change_counter << std::endl;
        
        // Get current held state based on detection
        bool now_held = isBallHeld(ball, hands);
        debug_log << "  isBallHeld() returned: " << (now_held ? "HELD" : "IN_AIR") << std::endl;
        
        // Debounce state changes (require min_frames_for_state_change consecutive frames)
        if (now_held != ball.is_held) {
            ball.state_change_counter++;
            debug_log << "  STATE MISMATCH! now_held=" << (now_held ? "HELD" : "IN_AIR")
                     << " vs ball.is_held=" << (ball.is_held ? "HELD" : "IN_AIR") << std::endl;
            debug_log << "  Incrementing state_change_counter to: " << ball.state_change_counter << std::endl;
            debug_log << "  Need " << tracking_settings_.min_frames_for_state_change
                     << " frames to confirm state change" << std::endl;
            
            if (ball.state_change_counter >= tracking_settings_.min_frames_for_state_change) {
                // State change confirmed - generate event based on OLD state → NEW state
                bool old_state_was_held = ball.is_held;  // Current state before change
                
                debug_log << "  *** STATE CHANGE CONFIRMED ***" << std::endl;
                debug_log << "  OLD state (ball.is_held): " << (old_state_was_held ? "HELD" : "IN_AIR") << std::endl;
                debug_log << "  NEW state (now_held): " << (now_held ? "HELD" : "IN_AIR") << std::endl;
                debug_log << "  held_by_hand_id: " << ball.held_by_hand_id << std::endl;
                
                ball.is_held = now_held;  // Update to new state
                ball.state_change_counter = 0;
                
                debug_log << "  Transition: " << (old_state_was_held ? "HELD" : "IN_AIR")
                         << " -> " << (now_held ? "HELD" : "IN_AIR") << std::endl;
                
                // Generate event based on transition
                if (old_state_was_held && !now_held) {
                    // Was held, now in air = THROW
                    debug_log << "  Condition check: old_state_was_held=" << old_state_was_held
                             << " && !now_held=" << !now_held << std::endl;
                    debug_log << "  >>> GENERATING THROW EVENT <<<" << std::endl;
                    
                    events.push_back({
                        BallEvent::THROW,
                        ball.id,
                        ball.held_by_hand_id,
                        getCurrentTimestamp()
                    });
                    debug_log << "  >>> THROW EVENT GENERATED <<<" << std::endl;
                    std::cout << "[SimpleBallTracker] THROW detected: Ball " << ball.id
                             << " from hand " << ball.held_by_hand_id << std::endl;
                }
                else if (!old_state_was_held && now_held) {
                    // Was in air, now held = CATCH
                    debug_log << "  Condition check: !old_state_was_held=" << !old_state_was_held
                             << " && now_held=" << now_held << std::endl;
                    debug_log << "  >>> GENERATING CATCH EVENT <<<" << std::endl;
                    
                    events.push_back({
                        BallEvent::CATCH,
                        ball.id,
                        ball.held_by_hand_id,
                        getCurrentTimestamp()
                    });
                    debug_log << "  >>> CATCH EVENT GENERATED <<<" << std::endl;
                    std::cout << "[SimpleBallTracker] CATCH detected: Ball " << ball.id
                             << " by hand " << ball.held_by_hand_id << std::endl;
                }
                else {
                    debug_log << "  WARNING: State change confirmed but no event generated!" << std::endl;
                    debug_log << "  This means both old and new states are the same, which shouldn't happen!" << std::endl;
                }
            }
        }
        else {
            // State is stable, reset counter
            debug_log << "  State is STABLE (now_held == ball.is_held)" << std::endl;
            if (ball.state_change_counter > 0) {
                debug_log << "  Resetting state_change_counter from " << ball.state_change_counter << " to 0" << std::endl;
            }
            ball.state_change_counter = 0;
        }
    }
    
    debug_log << "\nTotal events generated: " << events.size() << std::endl;
    debug_log.close();
    
    return events;
}

std::pair<std::vector<SimpleBall>, std::vector<BallEvent>> SimpleBallTracker::update(
    const cv::Mat& color_frame,
    const cv::Mat& depth_frame,
    const CameraIntrinsics& intrinsics) {
    
    // Open debug log file
    std::ofstream debug_log("engine_debug.log", std::ios::app);
    debug_log << "\n\n========================================" << std::endl;
    debug_log << "=== SimpleBallTracker::update() ===" << std::endl;
    debug_log << "========================================" << std::endl;
    
    // Calculate dt
    auto current_time = std::chrono::steady_clock::now();
    float dt = std::chrono::duration_cast<std::chrono::duration<float>>(
        current_time - last_update_time_).count();
    last_update_time_ = current_time;
    
    debug_log << "Delta time: " << dt << "s" << std::endl;
    
    // Run YOLO detection
    std::vector<Detection> yolo_detections = runBallDetection(color_frame, depth_frame, intrinsics);
    debug_log << "YOLO detections: " << yolo_detections.size() << std::endl;
    for (size_t i = 0; i < yolo_detections.size(); ++i) {
        const auto& det = yolo_detections[i];
        debug_log << "  Detection " << i << ": class_id=" << det.class_id
                 << " (0=ball, 1=ball_held), conf=" << det.confidence
                 << ", pos=(" << det.world_pos.x << ", " << det.world_pos.y << ", " << det.world_pos.z << ")" << std::endl;
    }
    
    // Run pose estimation
    std::vector<SimpleHand> hands = runPoseEstimation(color_frame, depth_frame, intrinsics);
    hands_ = hands;  // Store for getters
    
    debug_log << "Hands detected: " << hands.size() << std::endl;
    for (const auto& hand : hands) {
        debug_log << "  Hand " << hand.id << " (" << (hand.id == 0 ? "LEFT" : "RIGHT")
                 << "): pos=(" << hand.wrist_pos_3d.x << ", " << hand.wrist_pos_3d.y
                 << ", " << hand.wrist_pos_3d.z << "), visible=" << hand.is_visible << std::endl;
    }
    debug_log.close();
    
    // Convert to HSV once
    cv::Mat hsv_frame;
    cv::cvtColor(color_frame, hsv_frame, cv::COLOR_BGR2HSV);
    
    // Track which detections are used
    std::set<int> used_detections;
    
    // For each ball, try to match with YOLO detection
    for (auto& ball : balls_) {
        // Find matching color profile
        const ColorProfile* profile = nullptr;
        for (const auto& p : color_profiles_) {
            if (p.name == ball.color_name && p.enabled) {
                profile = &p;
                break;
            }
        }
        
        if (!profile) continue;
        
        // Get Kalman prediction for this ball (for boundary checking)
        // We calculate this to enforce the prediction boundary, but we DON'T advance the filter state yet
        cv::Point3f kalman_pred(0, 0, 0);
        bool has_prediction = false;
        
        // Calculate prediction if we have recent tracking history (within last 5 frames)
        if (ball.frames_without_yolo < 5) {
            // Get predicted state WITHOUT advancing the filter
            // We'll only advance it if we actually use the prediction
            ball.kalman.predict(dt);
            auto state = ball.kalman.get_state();
            kalman_pred = cv::Point3f(state(0), state(1), state(2));
            has_prediction = (kalman_pred.z > 0.01f);
        }
        
        // Find best matching detection (now enforces Kalman prediction boundary)
        const Detection* best_det = findBestColorMatch(yolo_detections, *profile,
                                                       hsv_frame, used_detections, kalman_pred);
        
        // Check if the best detection meets the minimum score threshold
        bool score_meets_threshold = (best_det && last_match_total_score_ >= tracking_settings_.min_yolo_score_threshold);
        
        // Check if detection qualifies for override (high confidence, good color, correct class)
        bool qualifies_for_override = false;
        if (best_det && !score_meets_threshold) {
            float color_match = matchColor(*best_det, *profile, hsv_frame);
            bool is_ball_class = (best_det->class_id == 0);  // 0 = ball (in-air)
            
            qualifies_for_override =
                (best_det->confidence >= tracking_settings_.override_confidence_threshold) &&
                (color_match >= tracking_settings_.override_color_threshold) &&
                (!tracking_settings_.override_require_ball_class || is_ball_class);
        }
        
        if (best_det && (score_meets_threshold || qualifies_for_override)) {
            // Update from YOLO detection
            ball.position = best_det->world_pos;
            ball.pixel_pos = cv::Point2f(best_det->box.x + best_det->box.width / 2.0f,
                                         best_det->box.y + best_det->box.height / 2.0f);
            ball.bbox = best_det->box;
            ball.has_yolo_detection = true;
            ball.frames_without_yolo = 0;
            ball.yolo_confidence = best_det->confidence;
            ball.yolo_class_id = best_det->class_id;
            ball.color_match_score = matchColor(*best_det, *profile, hsv_frame);
            
            // Store scoring components for visualization
            ball.score_class = last_match_class_score_;
            ball.score_confidence = last_match_confidence_score_;
            ball.score_color = last_match_color_score_;
            ball.score_kalman = last_match_kalman_score_;
            ball.score_total = last_match_total_score_;
            
            // Store detection evaluations for visualization
            ball.detection_evaluations = last_detection_evaluations_;
            
            // Set tracking reason for debug visualization with full equation
            char reason[256];
            if (qualifies_for_override && !score_meets_threshold) {
                snprintf(reason, sizeof(reason), "OVERRIDE Score=%.2f (cls:%.2f + conf:%.2f + col:%.2f + kal:%.2f)",
                         ball.score_total, ball.score_class, ball.score_confidence,
                         ball.score_color, ball.score_kalman);
            } else {
                snprintf(reason, sizeof(reason), "Score=%.2f (cls:%.2f + conf:%.2f + col:%.2f + kal:%.2f)",
                         ball.score_total, ball.score_class, ball.score_confidence,
                         ball.score_color, ball.score_kalman);
            }
            ball.tracking_reason = reason;
            
            // Update Kalman filter (legacy)
            ball.kalman.update(KalmanFilter3D::MeasurementVector(
                ball.position.x, ball.position.y, ball.position.z));
            
            // NEW: Update color-based predictor with this detection
            ball.color_predictor.addDetection(ball.position);
            
            used_detections.insert(best_det->index);
        }
        else {
            // No YOLO detection within prediction radius, no detection at all, or score below threshold
            ball.has_yolo_detection = false;
            ball.frames_without_yolo++;
            
            // Store detection evaluations even when no match found
            ball.detection_evaluations = last_detection_evaluations_;
            
            // Show rejection reason in debug output
            if (best_det && !score_meets_threshold) {
                // Detection exists but score is below threshold
                char reason[256];
                snprintf(reason, sizeof(reason), "Score %.2f < threshold %.2f - using Kalman",
                         last_match_total_score_, tracking_settings_.min_yolo_score_threshold);
                ball.tracking_reason = reason;
            } else if (!last_rejection_reason_.empty()) {
                ball.tracking_reason = "YOLO rejected: " + last_rejection_reason_;
            }
            
            // If we have a valid Kalman prediction, try fallback strategies
            if (has_prediction && ball.frames_without_yolo < 5) {
                // Check if Kalman prediction is near any hand
                float min_hand_dist = std::numeric_limits<float>::max();
                int closest_hand_id = -1;
                cv::Point3f closest_hand_pos(0, 0, 0);
                
                for (const auto& hand : hands) {
                    if (!hand.is_visible) continue;
                    float dist = cv::norm(kalman_pred - hand.wrist_pos_3d);
                    if (dist < min_hand_dist) {
                        min_hand_dist = dist;
                        closest_hand_id = hand.id;
                        closest_hand_pos = hand.wrist_pos_3d;
                    }
                }
                
                // If prediction is very close to a hand (within wrist proximity threshold),
                // attach tracker to hand and search for color blob near hand
                if (min_hand_dist < tracking_settings_.wrist_proximity_threshold) {
                    // Project hand position to 2D
                    cv::Point2f hand_2d = project_3d_to_2d(closest_hand_pos, intrinsics);
                    
                    // Search for color blob near the hand (smaller radius for hand-attached search)
                    cv::Point2f color_blob = searchForColorBlob(hsv_frame, *profile, hand_2d, 80);
                    
                    if (color_blob.x > 0 && color_blob.y > 0) {
                        // Found color blob near hand - use it!
                        float depth = getDepthAtPoint(depth_frame, color_blob);
                        if (depth > MIN_DEPTH && depth < MAX_DEPTH) {
                            cv::Point3f color_pos = deprojectToWorld(color_blob, depth, intrinsics);
                            ball.position = color_pos;
                            ball.pixel_pos = color_blob;
                            ball.held_by_hand_id = closest_hand_id;
                            ball.yolo_class_id = 1;  // Mark as held
                            char reason[128];
                            snprintf(reason, sizeof(reason), "Color@Hand[%c] d=%.2fm",
                                     closest_hand_id == 0 ? 'L' : 'R', min_hand_dist);
                            ball.tracking_reason = reason;
                            
                            // Update Kalman with color detection
                            ball.kalman.update(KalmanFilter3D::MeasurementVector(
                                color_pos.x, color_pos.y, color_pos.z));
                            ball.color_predictor.addDetection(color_pos);
                            ball.frames_without_yolo = 0;
                            continue;
                        }
                    }
                    
                    // No color blob found - snap directly to hand position
                    ball.position = closest_hand_pos;
                    ball.pixel_pos = hand_2d;
                    ball.held_by_hand_id = closest_hand_id;
                    ball.yolo_class_id = 1;  // Mark as held
                    char reason[128];
                    snprintf(reason, sizeof(reason), "Snap→Hand[%c] d=%.2fm",
                             closest_hand_id == 0 ? 'L' : 'R', min_hand_dist);
                    ball.tracking_reason = reason;
                    
                    // Update Kalman with hand position
                    ball.kalman.update(KalmanFilter3D::MeasurementVector(
                        closest_hand_pos.x, closest_hand_pos.y, closest_hand_pos.z));
                    ball.color_predictor.addDetection(closest_hand_pos);
                    ball.frames_without_yolo = 0;
                    continue;
                }
                
                // Prediction is NOT near a hand - try color tracking at prediction point
                cv::Point2f pred_2d = project_3d_to_2d(kalman_pred, intrinsics);
                cv::Point2f color_blob = searchForColorBlob(hsv_frame, *profile, pred_2d, COLOR_SEARCH_RADIUS);
                
                if (color_blob.x > 0 && color_blob.y > 0) {
                    // Found color blob at predicted location - use it!
                    float depth = getDepthAtPoint(depth_frame, color_blob);
                    if (depth > MIN_DEPTH && depth < MAX_DEPTH) {
                        cv::Point3f color_pos = deprojectToWorld(color_blob, depth, intrinsics);
                        
                        ball.position = color_pos;
                        ball.pixel_pos = color_blob;
                        ball.tracking_reason = "Color@Kalman";
                        
                        // Update Kalman with color detection
                        ball.kalman.update(KalmanFilter3D::MeasurementVector(
                            color_pos.x, color_pos.y, color_pos.z));
                        ball.color_predictor.addDetection(color_pos);
                        ball.frames_without_yolo = 0;
                        continue;
                    }
                }
                
                // If color tracking failed, fall through to Kalman-only prediction below
            }
            
            if (ball.frames_without_yolo < 5) {
                // Use Kalman prediction as the color tracker position
                // NOTE: Kalman was already predicted at line 884, just use the state
                auto state = ball.kalman.get_state();
                ball.position = cv::Point3f(state(0), state(1), state(2));
                
                // CRITICAL: Update Kalman filter with this position
                // The Kalman filter learns from ALL color tracker positions, regardless of source
                // This maintains trajectory continuity and allows prediction to continue moving
                ball.kalman.update(KalmanFilter3D::MeasurementVector(
                    ball.position.x, ball.position.y, ball.position.z));
                ball.color_predictor.addDetection(ball.position);
                
                // CRITICAL: Update yolo_class_id based on proximity to hands
                // Don't let old class_id persist and cause wrong state detection
                bool near_any_hand = false;
                int nearest_hand_id = -1;
                float nearest_dist = 999.0f;
                for (const auto& hand : hands) {
                    if (!hand.is_visible) continue;
                    float dist = cv::norm(ball.position - hand.wrist_pos_3d);
                    if (dist < tracking_settings_.wrist_proximity_threshold && dist < nearest_dist) {
                        near_any_hand = true;
                        nearest_hand_id = hand.id;
                        nearest_dist = dist;
                    }
                }
                
                if (near_any_hand) {
                    ball.held_by_hand_id = nearest_hand_id;
                    ball.yolo_class_id = 1;
                    char reason[128];
                    snprintf(reason, sizeof(reason), "Kalman+Near[%c] d=%.2fm",
                             nearest_hand_id == 0 ? 'L' : 'R', nearest_dist);
                    ball.tracking_reason = reason;
                } else {
                    ball.yolo_class_id = 0;
                    ball.tracking_reason = "Kalman pred";
                }
            }
            else if (ball.frames_without_yolo < MAX_FRAMES_WITHOUT_YOLO) {
                // TRAJECTORY-BASED VALIDATION:
                // Only allow hand association if ball's trajectory could reasonably reach that hand
                
                // Get Kalman predicted position (already computed above in line 682)
                auto predicted_state = ball.kalman.get_state();
                cv::Point3f predicted_pos(predicted_state(0), predicted_state(1), predicted_state(2));
                
                // Check if predicted position is near any hand
                bool trajectory_near_hand = false;
                int closest_hand_id = -1;
                float closest_predicted_dist = std::numeric_limits<float>::max();
                
                for (const auto& hand : hands) {
                    if (!hand.is_visible) continue;
                    
                    // Check distance from PREDICTED position to hand (not current position)
                    float dist_to_predicted = cv::norm(predicted_pos - hand.wrist_pos_3d);
                    
                    // Only consider this hand if trajectory is heading toward it
                    if (dist_to_predicted < tracking_settings_.undetected_near_hand_threshold) {
                        if (dist_to_predicted < closest_predicted_dist) {
                            closest_predicted_dist = dist_to_predicted;
                            closest_hand_id = hand.id;
                            trajectory_near_hand = true;
                        }
                    }
                }
                
                if (trajectory_near_hand) {
                    // Trajectory indicates ball is approaching/at this hand
                    // Snap to hand position
                    for (const auto& hand : hands) {
                        if (hand.id == closest_hand_id) {
                            ball.position = hand.wrist_pos_3d;
                            ball.held_by_hand_id = hand.id;
                            ball.yolo_class_id = 1;  // Mark as held
                            char reason[128];
                            snprintf(reason, sizeof(reason), "Traj→[%c] d=%.2fm",
                                     hand.id == 0 ? 'L' : 'R', closest_predicted_dist);
                            ball.tracking_reason = reason;
                            break;
                        }
                    }
                } else {
                    // Trajectory does NOT lead to any hand - ball is in free flight
                    // Use Kalman prediction, do NOT snap to hands
                    ball.position = predicted_pos;
                    ball.yolo_class_id = 0;  // Mark as in-air
                    ball.held_by_hand_id = -1;
                    ball.tracking_reason = "Traj→Flight";
                }
            }
        }
    }
    
    // Detect ball states and events
    std::vector<BallEvent> events = detectStatesAndEvents(balls_, hands);
    
    std::ofstream debug_log_end("engine_debug.log", std::ios::app);
    debug_log_end << "\n=== Update Complete ===" << std::endl;
    debug_log_end << "Events generated: " << events.size() << std::endl;
    for (const auto& event : events) {
        debug_log_end << "  " << (event.type == BallEvent::THROW ? "THROW" : "CATCH")
                  << " - Ball " << event.ball_id << ", Hand " << event.hand_id << std::endl;
    }
    debug_log_end << "========================================\n" << std::endl;
    debug_log_end.close();
    
    return {balls_, events};
}

bool SimpleBallTracker::calibrateColor(const std::string& color_name,
                                      const cv::Point& click_point,
                                      std::string& error_message) {
    // Use last raw detections and last color frame
    if (last_color_frame_.empty()) {
        error_message = "No color frame available for calibration";
        return false;
    }
    
    // Find detection containing click point
    const Detection* clicked_det = nullptr;
    for (const auto& det : last_raw_detections_) {
        if (click_point.x >= det.box.x && 
            click_point.x <= (det.box.x + det.box.width) &&
            click_point.y >= det.box.y && 
            click_point.y <= (det.box.y + det.box.height)) {
            clicked_det = &det;
            break;
        }
    }
    
    if (!clicked_det) {
        error_message = "No detection found at click location (" +
                       std::to_string(click_point.x) + "," + std::to_string(click_point.y) + ")";
        return false;
    }
    
    // Convert to HSV and sample detection box
    cv::Mat hsv_frame;
    cv::cvtColor(last_color_frame_, hsv_frame, cv::COLOR_BGR2HSV);
    
    cv::Rect roi(
        std::max(0, static_cast<int>(clicked_det->box.x)),
        std::max(0, static_cast<int>(clicked_det->box.y)),
        std::min(static_cast<int>(clicked_det->box.width),
                 hsv_frame.cols - static_cast<int>(clicked_det->box.x)),
        std::min(static_cast<int>(clicked_det->box.height),
                 hsv_frame.rows - static_cast<int>(clicked_det->box.y))
    );
    
    if (roi.width <= 0 || roi.height <= 0) {
        error_message = "Invalid detection box dimensions";
        return false;
    }
    
    cv::Mat roi_hsv = hsv_frame(roi);
    
    // Calculate mean and stddev
    cv::Scalar mean, stddev;
    cv::meanStdDev(roi_hsv, mean, stddev);
    
    // Set range as mean ± 2*stddev (with minimum tolerances)
    float hue_tol = std::max(15.0f, static_cast<float>(stddev[0]) * 2.0f);
    float sat_tol = std::max(50.0f, static_cast<float>(stddev[1]) * 2.0f);
    float val_tol = std::max(50.0f, static_cast<float>(stddev[2]) * 2.0f);
    
    cv::Scalar min_hsv(
        std::max(0.0, mean[0] - hue_tol),
        std::max(0.0, mean[1] - sat_tol),
        std::max(0.0, mean[2] - val_tol)
    );
    
    cv::Scalar max_hsv(
        std::min(180.0, mean[0] + hue_tol),
        std::min(255.0, mean[1] + sat_tol),
        std::min(255.0, mean[2] + val_tol)
    );
    
    // Find and update color profile
    for (auto& profile : color_profiles_) {
        if (profile.name == color_name) {
            profile.min_hsv = min_hsv;
            profile.max_hsv = max_hsv;
            
            saveSettings();
            
            std::cout << "[SimpleBallTracker] Calibrated color '" << color_name << "'" << std::endl;
            std::cout << "  H: [" << min_hsv[0] << ", " << max_hsv[0] << "]" << std::endl;
            std::cout << "  S: [" << min_hsv[1] << ", " << max_hsv[1] << "]" << std::endl;
            std::cout << "  V: [" << min_hsv[2] << ", " << max_hsv[2] << "]" << std::endl;
            
            return true;
        }
    }
    
    error_message = "Color profile not found: " + color_name;
    return false;
}

// Add YOLO detection and pose estimation methods

cv::Mat SimpleBallTracker::preprocess(const cv::Mat& frame, float& scale_x, float& scale_y) {
    cv::Mat resized_frame;
    cv::resize(frame, resized_frame, cv::Size(input_width_, input_height_));
    scale_x = (float)frame.cols / input_width_;
    scale_y = (float)frame.rows / input_height_;
    cv::Mat float_frame;
    resized_frame.convertTo(float_frame, CV_32F, 1.0 / 255.0);
    return cv::dnn::blobFromImage(float_frame);
}

std::vector<Detection> SimpleBallTracker::runBallDetection(
    const cv::Mat& color_frame,
    const cv::Mat& depth_frame,
    const CameraIntrinsics& intrinsics) {
    
    // Store color frame for calibration
    last_color_frame_ = color_frame.clone();
    
    // Preprocess
    float scale_x, scale_y;
    cv::Mat preprocessed = preprocess(color_frame, scale_x, scale_y);
    
    // Run inference
    ov::Tensor input_tensor(ball_model_.input().get_element_type(),
                           ball_model_.input().get_shape(),
                           preprocessed.data);
    ball_infer_.set_input_tensor(input_tensor);
    ball_infer_.infer();
    const ov::Tensor& output_tensor = ball_infer_.get_output_tensor();
    
    // Postprocess
    const float* output_data = output_tensor.data<const float>();
    const int num_channels = 4 + num_classes_;
    
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
        
        if (confidence > confidence_threshold_) {
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
            class_ids.push_back(class_id_point.x);
            
            // Calculate 3D position
            cv::Point2f center_pixel(left + width / 2.0f, top + height / 2.0f);
            cv::Point3f world_pos(0, 0, 0);
            
            if (center_pixel.x >= 0 && center_pixel.x < depth_frame.cols &&
                center_pixel.y >= 0 && center_pixel.y < depth_frame.rows) {
                float depth_value_m = get_filtered_depth(depth_frame, center_pixel);
                world_pos = deprojectToWorld(center_pixel, depth_value_m, intrinsics);
            }
            
            Detection det;
            det.box = cv::Rect_<float>(left, top, width, height);
            det.world_pos = world_pos;
            det.confidence = confidence;
            det.class_id = class_id_point.x;
            det.index = raw_detections.size();
            raw_detections.push_back(det);
        }
    }
    
    // Apply NMS
    std::vector<int> nms_indices;
    cv::dnn::NMSBoxes(boxes, confidences, confidence_threshold_, nms_threshold_, nms_indices);
    
    // Filter detections by NMS results
    std::vector<Detection> filtered_detections;
    for (int idx : nms_indices) {
        for (auto& det : raw_detections) {
            if (det.box.x == boxes[idx].x && det.box.y == boxes[idx].y) {
                det.index = filtered_detections.size();
                filtered_detections.push_back(det);
                break;
            }
        }
    }
    
    last_raw_detections_ = filtered_detections;
    return filtered_detections;
}

std::vector<SimpleHand> SimpleBallTracker::runPoseEstimation(
    const cv::Mat& color_frame,
    const cv::Mat& depth_frame,
    const CameraIntrinsics& intrinsics) {
    
    std::vector<SimpleHand> hands;
    
    // Preprocess
    float scale_x, scale_y;
    cv::Mat preprocessed = preprocess(color_frame, scale_x, scale_y);
    
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
        float cx = output_buffer.at<float>(i, 0);
        float cy = output_buffer.at<float>(i, 1);
        float w = output_buffer.at<float>(i, 2);
        float h = output_buffer.at<float>(i, 3);
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
                    
                    float depth_value_m = get_filtered_depth(depth_frame, pixel);
                    
                    if (depth_value_m > MIN_DEPTH && depth_value_m < MAX_DEPTH) {
                        keypoints_3d[kp_idx] = deprojectToWorld(pixel, depth_value_m, intrinsics);
                    }
                }
            }
        }
        
        // Create hands from wrist keypoints (9=left wrist, 10=right wrist)
        if (keypoint_confidences[9] > keypoint_confidence_threshold &&
            keypoints_3d[9].z > MIN_DEPTH && keypoints_3d[9].z < MAX_DEPTH) {
            
            SimpleHand left_hand;
            left_hand.wrist_pos_3d = keypoints_3d[9];
            left_hand.confidence = keypoint_confidences[9];
            left_hand.id = 0;
            left_hand.is_visible = true;
            left_hand.keypoints = keypoints_3d;
            hands.push_back(left_hand);
        }
        
        if (keypoint_confidences[10] > keypoint_confidence_threshold &&
            keypoints_3d[10].z > MIN_DEPTH && keypoints_3d[10].z < MAX_DEPTH) {
            
            SimpleHand right_hand;
            right_hand.wrist_pos_3d = keypoints_3d[10];
            right_hand.confidence = keypoint_confidences[10];
            right_hand.id = 1;
            right_hand.is_visible = true;
            right_hand.keypoints = keypoints_3d;
            hands.push_back(right_hand);
        }
        
        // Only process first person
        break;
    }
    
    return hands;
}

cv::Point2f SimpleBallTracker::project_3d_to_2d(const cv::Point3f& world_pos,
                                               const CameraIntrinsics& intrinsics) {
    if (world_pos.z > 0) {
        float x_2d = (world_pos.x * intrinsics.fx) / world_pos.z + intrinsics.ppx;
        float y_2d = (world_pos.y * intrinsics.fy) / world_pos.z + intrinsics.ppy;
        return cv::Point2f(x_2d, y_2d);
    }
    return cv::Point2f(-1, -1);
}
