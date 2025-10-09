#include "SimpleBallTracker.hpp"
#include "DebugLogControl.hpp"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <set>

// Helper macro for conditional debug logging
// Only creates stream object when logging is enabled
#define OPEN_DEBUG_LOG(var_name) \
    std::ofstream var_name; \
    if (g_enable_debug_log) { var_name.open("engine_debug.log", std::ios::app); }

// Helper macro to wrap debug log writes
#define DEBUG_LOG(stream, code) \
    if (g_enable_debug_log) { code }

// Helper function for depth filtering using quick-select for median
// OPTIMIZED: Reduced from 5x5 to 3x3 sampling and using nth_element instead of full sort
static float get_filtered_depth(const cv::Mat& depth_frame, const cv::Point2f& pixel) {
    const int SAMPLE_SIZE = 3;  // Reduced from 5 to 3 for ~3-5% FPS improvement
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
    
    // Use nth_element for O(n) median finding instead of O(n log n) sort
    size_t mid = depth_samples.size() / 2;
    std::nth_element(depth_samples.begin(), depth_samples.begin() + mid, depth_samples.end());
    return depth_samples[mid];
}

SimpleBallTracker::SimpleBallTracker(const std::string& ball_model_path,
                                    const std::string& pose_model_path,
                                    const std::string& device_name,
                                    const std::string& settings_file)
    : settings_file_(settings_file) {
    
    // Load OpenVINO models
    ball_model_ = core_.compile_model(ball_model_path, device_name);
    ball_infer_ = ball_model_.create_infer_request();
    
    pose_model_ = core_.compile_model(pose_model_path, device_name);
    pose_infer_ = pose_model_.create_infer_request();
    
    last_update_time_ = std::chrono::steady_clock::now();
    
    // Load color profiles from settings file
    if (!loadSettings()) {
        // Set up default color profiles
        color_profiles_.push_back(ColorProfile("green", -1.0f, -1.0f, cv::Scalar(45, 100, 100), cv::Scalar(75, 255, 255)));
        color_profiles_.push_back(ColorProfile("pink", -1.0f, -1.0f, cv::Scalar(140, 100, 100), cv::Scalar(175, 255, 255)));
        color_profiles_.push_back(ColorProfile("orange", -1.0f, -1.0f, cv::Scalar(5, 100, 100), cv::Scalar(20, 255, 255)));
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
            
            // Load avg_hue and avg_saturation if available
            float avg_hue = color_data.value("avg_hue", -1.0f);
            float avg_sat = color_data.value("avg_saturation", -1.0f);
            
            color_profiles_.push_back(ColorProfile(color_name, avg_hue, avg_sat, min_hsv, max_hsv, min_hsv2, max_hsv2, enabled));
            
        }
        
        return true;
        
    } catch (const std::exception& e) {
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
            
            // NEW: Save average hue and saturation if calibrated
            if (profile.avg_hue >= 0.0f) {
                color_data["avg_hue"] = profile.avg_hue;
            }
            if (profile.avg_saturation >= 0.0f) {
                color_data["avg_saturation"] = profile.avg_saturation;
            }
            
            // LEGACY: Keep min/max ranges for backward compatibility
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
        
    } catch (const std::exception& e) {
    }
}

bool SimpleBallTracker::updateSetting(const std::string& key, const std::string& value) {
    // Handle color profile enable/disable
    if (key.find("track_") == 0) {
        std::string color_name = key.substr(6);  // Remove "track_" prefix
        bool enable = (value == "true" || value == "1");
        
        bool found = false;
        for (auto& profile : color_profiles_) {
            if (profile.name == color_name) {
                profile.enabled = enable;
                found = true;
                break;
            }
        }
        
        if (!found) {
            return false;
        }
        
        saveSettings();
        
        // CRITICAL: Reinitialize balls list based on new enabled profiles
        balls_.clear();
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
        return true;
    }
    
    // Handle tracking settings
    try {
        if (key == "ml_ball_weight") {
            tracking_settings_.ml_ball_weight = std::stof(value);
            return true;
        }
        else if (key == "ml_ball_held_weight") {
            tracking_settings_.ml_ball_held_weight = std::stof(value);
            return true;
        }
        else if (key == "wrist_proximity_weight") {
            tracking_settings_.wrist_proximity_weight = std::stof(value);
            return true;
        }
        else if (key == "wrist_proximity_threshold") {
            tracking_settings_.wrist_proximity_threshold = std::stof(value);
            return true;
        }
        else if (key == "undetected_near_hand_threshold") {
            tracking_settings_.undetected_near_hand_threshold = std::stof(value);
            return true;
        }
        else if (key == "min_frames_for_state_change") {
            tracking_settings_.min_frames_for_state_change = std::stoi(value);
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
            return true;
        }
        // Color tracker matching weights
        else if (key == "yolo_confidence_weight") {
            tracking_settings_.yolo_confidence_weight = std::stof(value);
            return true;
        }
        else if (key == "yolo_class_weight") {
            tracking_settings_.yolo_class_weight = std::stof(value);
            return true;
        }
        else if (key == "color_match_weight") {
            tracking_settings_.color_match_weight = std::stof(value);
            return true;
        }
        else if (key == "kalman_proximity_weight") {
            tracking_settings_.kalman_proximity_weight = std::stof(value);
            return true;
        }
        else if (key == "color_sample_radius") {
            tracking_settings_.color_sample_radius = std::stoi(value);
            return true;
        }
        else if (key == "min_yolo_score_threshold") {
            tracking_settings_.min_yolo_score_threshold = std::stof(value);
            return true;
        }
        else if (key == "override_confidence_threshold") {
            tracking_settings_.override_confidence_threshold = std::stof(value);
            return true;
        }
        else if (key == "override_color_threshold") {
            tracking_settings_.override_color_threshold = std::stof(value);
            return true;
        }
        else if (key == "override_require_ball_class") {
            tracking_settings_.override_require_ball_class = (value == "true" || value == "1");
            return true;
        }
        else if (key == "max_tracker_distance_per_frame") {
            tracking_settings_.max_tracker_distance_per_frame = std::stof(value);
            return true;
        }
        else if (key == "temporal_consistency_bonus") {
            tracking_settings_.temporal_consistency_bonus = std::stof(value);
            return true;
        }
        else if (key == "spatial_threshold") {
            tracking_settings_.spatial_threshold = std::stof(value);
            return true;
        }
    } catch (const std::exception& e) {
        return false;
    }
    
    return false;
}

float SimpleBallTracker::matchColor(const Detection& det, const ColorProfile& profile,
                                   const cv::Mat& color_frame) {
    // Get detection center
    cv::Point2f center(det.box.x + det.box.width / 2.0f,
                      det.box.y + det.box.height / 2.0f);
    
    // Check bounds
    if (center.x < 0 || center.x >= color_frame.cols ||
        center.y < 0 || center.y >= color_frame.rows) {
        return 0.0f;
    }
    
    // OPTIMIZATION: Convert only the ROI around detection center to HSV
    // Sample region: 5x5 for calibrated, 15x15 for legacy (radius of 2 vs 7)
    const int max_sample_radius = 7;  // Legacy mode uses larger radius
    int roi_x = std::max(0, static_cast<int>(center.x) - max_sample_radius);
    int roi_y = std::max(0, static_cast<int>(center.y) - max_sample_radius);
    int roi_width = std::min(color_frame.cols - roi_x, max_sample_radius * 2 + 1);
    int roi_height = std::min(color_frame.rows - roi_y, max_sample_radius * 2 + 1);
    
    cv::Rect roi(roi_x, roi_y, roi_width, roi_height);
    cv::Mat color_roi = color_frame(roi);
    cv::Mat hsv_roi;
    cv::cvtColor(color_roi, hsv_roi, cv::COLOR_BGR2HSV);
    
    // NEW: If profile has calibrated avg_hue and avg_saturation, use euclidean distance
    if (profile.avg_hue >= 0.0f && profile.avg_saturation >= 0.0f) {
        // Sample 5x5 region around center to get average hue/saturation
        const int sample_radius = 2;  // 5x5 = radius of 2
        std::vector<float> hue_samples;
        std::vector<float> sat_samples;
        
        for (int dy = -sample_radius; dy <= sample_radius; dy++) {
            for (int dx = -sample_radius; dx <= sample_radius; dx++) {
                // Convert to ROI coordinates
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
        // Normalize hue to 0-1 range (divide by 180) and saturation to 0-1 range (divide by 255)
        float hue_diff = (avg_hue / 180.0f) - (profile.avg_hue / 180.0f);
        float sat_diff = (avg_sat / 255.0f) - (profile.avg_saturation / 255.0f);
        
        // Handle hue wrap-around (hue is circular)
        if (hue_diff > 0.5f) hue_diff -= 1.0f;
        if (hue_diff < -0.5f) hue_diff += 1.0f;
        
        float euclidean_dist = std::sqrt(hue_diff * hue_diff + sat_diff * sat_diff);
        
        // Convert distance to similarity score (0 = perfect match, higher = worse match)
        // Use exponential decay: score = exp(-distance * scale_factor)
        // Scale factor of 10 means distance of 0.1 gives score of ~0.37
        float similarity = std::exp(-euclidean_dist * 10.0f);
        
        return similarity;
    }
    
    // LEGACY: Fall back to range-based matching if not calibrated
    const int sample_radius = 7;
    int match_count = 0;
    int total_count = 0;
    
    for (int dy = -sample_radius; dy <= sample_radius; dy++) {
        for (int dx = -sample_radius; dx <= sample_radius; dx++) {
            // Convert to ROI coordinates
            int x = static_cast<int>(center.x) + dx - roi_x;
            int y = static_cast<int>(center.y) + dy - roi_y;
            
            if (x >= 0 && x < hsv_roi.cols && y >= 0 && y < hsv_roi.rows) {
                cv::Vec3b hsv = hsv_roi.at<cv::Vec3b>(y, x);
                
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


cv::Point2f SimpleBallTracker::searchForColorBlob(const cv::Mat& color_frame,
                                                  const ColorProfile& profile,
                                                  const cv::Point2f& search_center,
                                                  int radius) {
    // OPTIMIZATION: Convert only the ROI around search center to HSV
    int roi_x = std::max(0, static_cast<int>(search_center.x) - radius);
    int roi_y = std::max(0, static_cast<int>(search_center.y) - radius);
    int roi_width = std::min(color_frame.cols - roi_x, radius * 2);
    int roi_height = std::min(color_frame.rows - roi_y, radius * 2);
    
    cv::Rect roi(roi_x, roi_y, roi_width, roi_height);
    cv::Mat color_roi = color_frame(roi);
    cv::Mat hsv_roi;
    cv::cvtColor(color_roi, hsv_roi, cv::COLOR_BGR2HSV);
    
    // Create mask for color
    cv::Mat mask1, mask2, mask;
    cv::inRange(hsv_roi, profile.min_hsv, profile.max_hsv, mask1);
    
    if (profile.min_hsv2[0] >= 0) {
        cv::inRange(hsv_roi, profile.min_hsv2, profile.max_hsv2, mask2);
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
        
        // Convert center back to original frame coordinates
        cv::Point2f center(m.m10 / m.m00 + roi_x, m.m01 / m.m00 + roi_y);
        
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
    
    // OPTIMIZED: Sample 3x3 region and use nth_element for median (reduced from 5x5 + sort)
    const int sample_size = 3;  // Reduced from 5 to 3
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
    
    // Use nth_element for O(n) median finding instead of O(n log n) sort
    size_t mid = samples.size() / 2;
    std::nth_element(samples.begin(), samples.begin() + mid, samples.end());
    return samples[mid];
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
    // Use weighted scoring system based on tracking settings
    float held_score = 0.0f;
    float in_air_score = 0.0f;
    
    DEBUG_LOG(debug_log, {
        OPEN_DEBUG_LOG(debug_log);
        debug_log << "\n  >> FRAME " << frame_counter_ << " - isBallHeld() for Ball " << ball.id << " <<" << std::endl;
        debug_log << "  Tracking settings:" << std::endl;
        debug_log << "    ml_ball_weight: " << tracking_settings_.ml_ball_weight << std::endl;
        debug_log << "    ml_ball_held_weight: " << tracking_settings_.ml_ball_held_weight << std::endl;
        debug_log << "    wrist_proximity_weight: " << tracking_settings_.wrist_proximity_weight << std::endl;
        debug_log << "    wrist_proximity_threshold: " << tracking_settings_.wrist_proximity_threshold << "m" << std::endl;
        debug_log << "  ML Classification:" << std::endl;
        debug_log << "    YOLO class_id: " << ball.yolo_class_id << std::endl;
    });
    
    // 1. ML Classification Evidence - TRUST YOLO FIRST
    if (ball.yolo_class_id == 1) {  // ball_held class
        held_score += tracking_settings_.ml_ball_held_weight;
        DEBUG_LOG(debug_log, {
            OPEN_DEBUG_LOG(debug_log);
            debug_log << "    Class is 'ball_held' -> adding " << tracking_settings_.ml_ball_held_weight
                     << " to held_score" << std::endl;
        });
    } else if (ball.yolo_class_id == 0) {  // ball (in-air) class
        in_air_score += tracking_settings_.ml_ball_weight;
        DEBUG_LOG(debug_log, {
            OPEN_DEBUG_LOG(debug_log);
            debug_log << "    Class is 'ball' (in-air) -> adding " << tracking_settings_.ml_ball_weight
                     << " to in_air_score" << std::endl;
        });
    } else {
        DEBUG_LOG(debug_log, {
            OPEN_DEBUG_LOG(debug_log);
            debug_log << "    Unknown class_id!" << std::endl;
        });
    }
    
    // 2. Wrist Proximity Evidence - also store distance for UI display
    float min_dist = std::numeric_limits<float>::max();
    int closest_hand = -1;
    
    DEBUG_LOG(debug_log, {
        OPEN_DEBUG_LOG(debug_log);
        debug_log << "  Wrist Proximity Check:" << std::endl;
        debug_log << "    Number of hands: " << hands.size() << std::endl;
    });
    
    for (const auto& hand : hands) {
        DEBUG_LOG(debug_log, {
            OPEN_DEBUG_LOG(debug_log);
            debug_log << "    Hand " << hand.id << " (" << (hand.id == 0 ? "LEFT" : "RIGHT")
                      << ", visible=" << hand.is_visible << ")" << std::endl;
            debug_log << "      Hand position: (" << hand.wrist_pos_3d.x << ", "
                      << hand.wrist_pos_3d.y << ", " << hand.wrist_pos_3d.z << ")" << std::endl;
        });
        if (!hand.is_visible) continue;
        
        float dist = cv::norm(ball.position - hand.wrist_pos_3d);
        DEBUG_LOG(debug_log, {
            OPEN_DEBUG_LOG(debug_log);
            debug_log << "      Distance to ball: " << dist << "m" << std::endl;
        });
        
        if (dist < min_dist) {
            min_dist = dist;
            closest_hand = hand.id;
            DEBUG_LOG(debug_log, {
                OPEN_DEBUG_LOG(debug_log);
                debug_log << "      >>> This is the closest hand so far (hand.id=" << hand.id
                          << " = " << (hand.id == 0 ? "LEFT" : "RIGHT") << ") <<<" << std::endl;
            });
        }
    }
    
    // Store distance for UI display
    ball.distance_to_nearest_wrist = (min_dist < std::numeric_limits<float>::max()) ? min_dist : -1.0f;
    
    DEBUG_LOG(debug_log, {
        OPEN_DEBUG_LOG(debug_log);
        debug_log << "    Minimum distance to any wrist: " << min_dist << "m" << std::endl;
        debug_log << "    Threshold: " << tracking_settings_.wrist_proximity_threshold << "m" << std::endl;
    });
    
    // CRITICAL FIX: Only use proximity as evidence if YOLO hasn't given us a strong signal
    // If YOLO clearly says "ball" (in-air), don't let proximity override it
    // Only add proximity weight if we don't have a YOLO detection OR if YOLO says ball_held
    int old_held_by_hand_id = ball.held_by_hand_id;
    
    if (min_dist < tracking_settings_.wrist_proximity_threshold) {
        // Only add proximity weight if YOLO agrees (ball_held) or if we have no YOLO detection
        if (ball.yolo_class_id == 1 || !ball.has_yolo_detection) {
            held_score += tracking_settings_.wrist_proximity_weight;
            ball.held_by_hand_id = closest_hand;
            DEBUG_LOG(debug_log, {
                OPEN_DEBUG_LOG(debug_log);
                debug_log << "    Ball is NEAR hand " << closest_hand << " (" << (closest_hand == 0 ? "LEFT" : "RIGHT")
                          << ") AND (YOLO agrees OR no YOLO) -> adding "
                         << tracking_settings_.wrist_proximity_weight << " to held_score" << std::endl;
                if (old_held_by_hand_id != ball.held_by_hand_id) {
                    debug_log << "    *** held_by_hand_id CHANGED: " << old_held_by_hand_id << " -> " << ball.held_by_hand_id << " ***" << std::endl;
                }
            });
        } else {
            DEBUG_LOG(debug_log, {
                OPEN_DEBUG_LOG(debug_log);
                debug_log << "    Ball is NEAR hand " << closest_hand << " (" << (closest_hand == 0 ? "LEFT" : "RIGHT")
                          << ") BUT YOLO says in-air (class=0) -> NOT adding proximity weight" << std::endl;
                // CRITICAL FIX: Don't clear held_by_hand_id - preserve it for throw detection
                // ball.held_by_hand_id = -1;  // OLD: This was clearing the hand ID too early
                debug_log << "    PRESERVING held_by_hand_id=" << ball.held_by_hand_id << " (ball is in-air but we remember which hand threw it)" << std::endl;
            });
        }
    } else {
        // CRITICAL FIX: Don't clear held_by_hand_id when ball moves away - preserve it for throw detection
        // ball.held_by_hand_id = -1;  // OLD: This was clearing the hand ID too early
        DEBUG_LOG(debug_log, {
            OPEN_DEBUG_LOG(debug_log);
            debug_log << "    Ball is NOT near any hand" << std::endl;
            debug_log << "    PRESERVING held_by_hand_id=" << ball.held_by_hand_id << " (ball moved away but we remember which hand it came from)" << std::endl;
        });
    }
    
    DEBUG_LOG(debug_log, {
        OPEN_DEBUG_LOG(debug_log);
        debug_log << "    FINAL held_by_hand_id: " << ball.held_by_hand_id
                  << (ball.held_by_hand_id >= 0 ? (ball.held_by_hand_id == 0 ? " (LEFT)" : " (RIGHT)") : " (NONE)")
                  << std::endl;
        debug_log << "  FINAL SCORES:" << std::endl;
        debug_log << "    held_score: " << held_score << std::endl;
        debug_log << "    in_air_score: " << in_air_score << std::endl;
    });
    
    // Decision: held if held_score is higher
    bool result = held_score > in_air_score;
    
    DEBUG_LOG(debug_log, {
        OPEN_DEBUG_LOG(debug_log);
        debug_log << "  DECISION: " << (result ? "HELD" : "IN_AIR")
                 << " (held_score " << (result ? ">" : "<=") << " in_air_score)" << std::endl;
        debug_log.close();
    });
    
    return result;
}

std::vector<BallEvent> SimpleBallTracker::detectStatesAndEvents(
    std::vector<SimpleBall>& balls,
    const std::vector<SimpleHand>& hands) {
    
    std::vector<BallEvent> events;
    
    DEBUG_LOG(debug_log, {
        OPEN_DEBUG_LOG(debug_log);
        debug_log << "\n=== FRAME " << frame_counter_ << " - detectStatesAndEvents() ===" << std::endl;
        debug_log << "Number of balls: " << balls.size() << std::endl;
        debug_log << "Number of hands: " << hands.size() << std::endl;
    });
    
    for (auto& ball : balls) {
        DEBUG_LOG(debug_log, {
            OPEN_DEBUG_LOG(debug_log);
            debug_log << "\n--- Ball " << ball.id << " (" << ball.color_name << ") ---" << std::endl;
            debug_log << "  Current is_held state: " << (ball.is_held ? "HELD" : "IN_AIR") << std::endl;
            debug_log << "  YOLO class_id: " << ball.yolo_class_id << " (0=ball, 1=ball_held)" << std::endl;
            debug_log << "  Has YOLO detection: " << (ball.has_yolo_detection ? "YES" : "NO") << std::endl;
            debug_log << "  Frames without YOLO: " << ball.frames_without_yolo << std::endl;
            debug_log << "  Distance to nearest wrist: " << ball.distance_to_nearest_wrist << "m" << std::endl;
            debug_log << "  Held by hand ID: " << ball.held_by_hand_id << std::endl;
            debug_log << "  Previous held by hand ID: " << ball.previous_held_by_hand_id << std::endl;
            debug_log << "  State change counter: " << ball.state_change_counter << std::endl;
        });
        
        // Get current held state based on detection
        bool now_held = isBallHeld(ball, hands);
        DEBUG_LOG(debug_log, {
            OPEN_DEBUG_LOG(debug_log);
            debug_log << "  isBallHeld() returned: " << (now_held ? "HELD" : "IN_AIR") << std::endl;
        });
        
        // Debounce state changes (require min_frames_for_state_change consecutive frames)
        if (now_held != ball.is_held) {
            ball.state_change_counter++;
            DEBUG_LOG(debug_log, {
                OPEN_DEBUG_LOG(debug_log);
                debug_log << "  STATE MISMATCH! now_held=" << (now_held ? "HELD" : "IN_AIR")
                         << " vs ball.is_held=" << (ball.is_held ? "HELD" : "IN_AIR") << std::endl;
                debug_log << "  Incrementing state_change_counter to: " << ball.state_change_counter << std::endl;
                debug_log << "  Need " << tracking_settings_.min_frames_for_state_change
                         << " frames to confirm state change" << std::endl;
            });
            
            if (ball.state_change_counter >= tracking_settings_.min_frames_for_state_change) {
                // State change confirmed - generate event based on OLD state → NEW state
                bool old_state_was_held = ball.is_held;  // Current state before change
                
                DEBUG_LOG(debug_log, {
                    OPEN_DEBUG_LOG(debug_log);
                    debug_log << "  *** STATE CHANGE CONFIRMED ***" << std::endl;
                    debug_log << "  OLD state (ball.is_held): " << (old_state_was_held ? "HELD" : "IN_AIR") << std::endl;
                    debug_log << "  NEW state (now_held): " << (now_held ? "HELD" : "IN_AIR") << std::endl;
                    debug_log << "  held_by_hand_id: " << ball.held_by_hand_id << std::endl;
                    debug_log << "  previous_held_by_hand_id: " << ball.previous_held_by_hand_id << std::endl;
                });
                
                // CRITICAL: Check for rapid hand-to-hand transfer
                // If ball was held and is still held, but the hand changed, this is a fast throw+catch
                bool is_hand_switch = old_state_was_held && now_held &&
                                     ball.previous_held_by_hand_id >= 0 &&
                                     ball.held_by_hand_id >= 0 &&
                                     ball.previous_held_by_hand_id != ball.held_by_hand_id;
                
                if (is_hand_switch) {
                    DEBUG_LOG(debug_log, {
                        OPEN_DEBUG_LOG(debug_log);
                        debug_log << "  *** RAPID HAND SWITCH DETECTED ***" << std::endl;
                        debug_log << "  Ball switched from hand " << ball.previous_held_by_hand_id
                                 << " (" << (ball.previous_held_by_hand_id == 0 ? "LEFT" : "RIGHT") << ")"
                                 << " to hand " << ball.held_by_hand_id
                                 << " (" << (ball.held_by_hand_id == 0 ? "LEFT" : "RIGHT") << ")" << std::endl;
                    });
                }
                
                ball.is_held = now_held;  // Update to new state
                ball.state_change_counter = 0;
                
                DEBUG_LOG(debug_log, {
                    OPEN_DEBUG_LOG(debug_log);
                    debug_log << "  Transition: " << (old_state_was_held ? "HELD" : "IN_AIR")
                             << " -> " << (now_held ? "HELD" : "IN_AIR") << std::endl;
                });
                
                // Generate event based on transition
                if (is_hand_switch) {
                    // RAPID HAND-TO-HAND TRANSFER: Generate both THROW and CATCH events
                    DEBUG_LOG(debug_log, {
                        OPEN_DEBUG_LOG(debug_log);
                        debug_log << "  >>> GENERATING THROW EVENT (from hand " << ball.previous_held_by_hand_id << ") <<<" << std::endl;
                    });
                    
                    // Estimate throw velocity from color predictor history
                    auto estimated_velocity = ball.color_predictor.getVelocity();
                    if (estimated_velocity.z != 0.0f) {
                        auto& state = ball.kalman.get_state();
                        state(3) = estimated_velocity.x;
                        state(4) = estimated_velocity.y;
                        state(5) = estimated_velocity.z;
                        
                        DEBUG_LOG(debug_log, {
                            OPEN_DEBUG_LOG(debug_log);
                            debug_log << "  THROW VELOCITY INITIALIZED: ("
                                     << estimated_velocity.x << ", "
                                     << estimated_velocity.y << ", "
                                     << estimated_velocity.z << ") m/s" << std::endl;
                        });
                    }
                    
                    events.push_back({
                        BallEvent::THROW,
                        ball.id,
                        ball.previous_held_by_hand_id,
                        getCurrentTimestamp()
                    });
                    
                    DEBUG_LOG(debug_log, {
                        OPEN_DEBUG_LOG(debug_log);
                        debug_log << "  >>> THROW EVENT GENERATED <<<" << std::endl;
                        debug_log << "  >>> GENERATING CATCH EVENT (by hand " << ball.held_by_hand_id << ") <<<" << std::endl;
                    });
                    
                    events.push_back({
                        BallEvent::CATCH,
                        ball.id,
                        ball.held_by_hand_id,
                        getCurrentTimestamp()
                    });
                    
                    DEBUG_LOG(debug_log, {
                        OPEN_DEBUG_LOG(debug_log);
                        debug_log << "  >>> CATCH EVENT GENERATED <<<" << std::endl;
                    });
                    
                    // Log to console and debug file
                    DEBUG_LOG_WRITE({
                        OPEN_DEBUG_LOG(hand_switch_log);
                        hand_switch_log << "\n[HAND_SWITCH] Ball " << ball.id
                                       << " | THROW from hand " << ball.previous_held_by_hand_id;
                        if (ball.previous_held_by_hand_id == 0) {
                            hand_switch_log << " (LEFT)";
                        } else if (ball.previous_held_by_hand_id == 1) {
                            hand_switch_log << " (RIGHT)";
                        }
                        hand_switch_log << " | CATCH by hand " << ball.held_by_hand_id;
                        if (ball.held_by_hand_id == 0) {
                            hand_switch_log << " (LEFT)";
                        } else if (ball.held_by_hand_id == 1) {
                            hand_switch_log << " (RIGHT)";
                        }
                        hand_switch_log << " | velocity=(" << estimated_velocity.x << ", "
                                       << estimated_velocity.y << ", " << estimated_velocity.z << ") m/s"
                                       << std::endl;
                    });
                }
                else if (old_state_was_held && !now_held) {
                    // Was held, now in air = THROW
                    DEBUG_LOG(debug_log, {
                        OPEN_DEBUG_LOG(debug_log);
                        debug_log << "  Condition check: old_state_was_held=" << old_state_was_held
                                 << " && !now_held=" << !now_held << std::endl;
                        debug_log << "  >>> GENERATING THROW EVENT <<<" << std::endl;
                    });
                    
                    // CRITICAL FIX: Estimate throw velocity from color predictor history
                    // This prevents Kalman from immediately predicting downward motion
                    auto estimated_velocity = ball.color_predictor.getVelocity();
                    if (estimated_velocity.z != 0.0f) {  // Valid velocity estimate
                        // Update Kalman velocity with throw velocity
                        auto& state = ball.kalman.get_state();
                        state(3) = estimated_velocity.x;  // vx
                        state(4) = estimated_velocity.y;  // vy
                        state(5) = estimated_velocity.z;  // vz
                        
                        DEBUG_LOG(debug_log, {
                            OPEN_DEBUG_LOG(debug_log);
                            debug_log << "  THROW VELOCITY INITIALIZED: ("
                                     << estimated_velocity.x << ", "
                                     << estimated_velocity.y << ", "
                                     << estimated_velocity.z << ") m/s" << std::endl;
                        });
                    } else {
                        DEBUG_LOG(debug_log, {
                            OPEN_DEBUG_LOG(debug_log);
                            debug_log << "  WARNING: No valid velocity estimate for throw!" << std::endl;
                        });
                    }
                    
                    events.push_back({
                        BallEvent::THROW,
                        ball.id,
                        ball.held_by_hand_id,
                        getCurrentTimestamp()
                    });
                    
                    DEBUG_LOG(debug_log, {
                        OPEN_DEBUG_LOG(debug_log);
                        debug_log << "  >>> THROW EVENT GENERATED <<<" << std::endl;
                    });
                    
                    // Log to both console and debug log file
                    DEBUG_LOG_WRITE({
                        OPEN_DEBUG_LOG(throw_log);
                        throw_log << "\n[THROW] Ball " << ball.id
                                  << " thrown from hand " << ball.held_by_hand_id;
                        if (ball.held_by_hand_id == 0) {
                            throw_log << " (LEFT)";
                        } else if (ball.held_by_hand_id == 1) {
                            throw_log << " (RIGHT)";
                        } else {
                            throw_log << " (UNKNOWN/NOT_SET)";
                        }
                        throw_log << " | velocity=(" << estimated_velocity.x << ", "
                                  << estimated_velocity.y << ", " << estimated_velocity.z << ") m/s"
                                  << std::endl;
                    });
                }
                else if (!old_state_was_held && now_held) {
                    // Was in air, now held = CATCH
                    DEBUG_LOG(debug_log, {
                        OPEN_DEBUG_LOG(debug_log);
                        debug_log << "  Condition check: !old_state_was_held=" << !old_state_was_held
                                 << " && now_held=" << now_held << std::endl;
                        debug_log << "  >>> GENERATING CATCH EVENT <<<" << std::endl;
                    });
                    
                    events.push_back({
                        BallEvent::CATCH,
                        ball.id,
                        ball.held_by_hand_id,
                        getCurrentTimestamp()
                    });
                    
                    DEBUG_LOG(debug_log, {
                        OPEN_DEBUG_LOG(debug_log);
                        debug_log << "  >>> CATCH EVENT GENERATED <<<" << std::endl;
                    });
                    
                    // Log to both console and debug log file
                    DEBUG_LOG_WRITE({
                        OPEN_DEBUG_LOG(catch_log);
                        catch_log << "\n[CATCH] Ball " << ball.id
                                  << " caught by hand " << ball.held_by_hand_id;
                        if (ball.held_by_hand_id == 0) {
                            catch_log << " (LEFT)";
                        } else if (ball.held_by_hand_id == 1) {
                            catch_log << " (RIGHT)";
                        } else {
                            catch_log << " (UNKNOWN/NOT_SET)";
                        }
                        catch_log << " | ball_color=" << ball.color_name
                                  << std::endl;
                    });
                }
                else {
                    DEBUG_LOG(debug_log, {
                        OPEN_DEBUG_LOG(debug_log);
                        debug_log << "  WARNING: State change confirmed but no event generated!" << std::endl;
                        debug_log << "  This means both old and new states are the same, which shouldn't happen!" << std::endl;
                    });
                }
            }
        }
        else {
            // State is stable, reset counter
            DEBUG_LOG(debug_log, {
                OPEN_DEBUG_LOG(debug_log);
                debug_log << "  State is STABLE (now_held == ball.is_held)" << std::endl;
                if (ball.state_change_counter > 0) {
                    debug_log << "  Resetting state_change_counter from " << ball.state_change_counter << " to 0" << std::endl;
                }
            });
            ball.state_change_counter = 0;
            
            // CRITICAL: Check for hand switch even when state is stable (HELD → HELD with different hand)
            // This catches fast hand-to-hand transfers that happen so quickly the ball never appears "in-air"
            if (ball.is_held && now_held &&
                ball.previous_held_by_hand_id >= 0 &&
                ball.held_by_hand_id >= 0 &&
                ball.previous_held_by_hand_id != ball.held_by_hand_id) {
                
                DEBUG_LOG(debug_log, {
                    OPEN_DEBUG_LOG(debug_log);
                    debug_log << "  *** STABLE STATE HAND SWITCH DETECTED ***" << std::endl;
                    debug_log << "  Ball switched from hand " << ball.previous_held_by_hand_id
                             << " (" << (ball.previous_held_by_hand_id == 0 ? "LEFT" : "RIGHT") << ")"
                             << " to hand " << ball.held_by_hand_id
                             << " (" << (ball.held_by_hand_id == 0 ? "LEFT" : "RIGHT") << ")" << std::endl;
                });
                
                // Generate THROW from previous hand
                auto estimated_velocity = ball.color_predictor.getVelocity();
                if (estimated_velocity.z != 0.0f) {
                    auto& state = ball.kalman.get_state();
                    state(3) = estimated_velocity.x;
                    state(4) = estimated_velocity.y;
                    state(5) = estimated_velocity.z;
                    
                    DEBUG_LOG(debug_log, {
                        OPEN_DEBUG_LOG(debug_log);
                        debug_log << "  THROW VELOCITY INITIALIZED: ("
                                 << estimated_velocity.x << ", "
                                 << estimated_velocity.y << ", "
                                 << estimated_velocity.z << ") m/s" << std::endl;
                    });
                }
                
                events.push_back({
                    BallEvent::THROW,
                    ball.id,
                    ball.previous_held_by_hand_id,
                    getCurrentTimestamp()
                });
                
                DEBUG_LOG(debug_log, {
                    OPEN_DEBUG_LOG(debug_log);
                    debug_log << "  >>> THROW EVENT GENERATED (stable state) <<<" << std::endl;
                });
                
                // Generate CATCH by current hand
                events.push_back({
                    BallEvent::CATCH,
                    ball.id,
                    ball.held_by_hand_id,
                    getCurrentTimestamp()
                });
                
                DEBUG_LOG(debug_log, {
                    OPEN_DEBUG_LOG(debug_log);
                    debug_log << "  >>> CATCH EVENT GENERATED (stable state) <<<" << std::endl;
                });
                
                // Log to console and debug file
                DEBUG_LOG_WRITE({
                    OPEN_DEBUG_LOG(stable_hand_switch_log);
                    stable_hand_switch_log << "\n[STABLE_HAND_SWITCH] Ball " << ball.id
                                          << " | THROW from hand " << ball.previous_held_by_hand_id;
                    if (ball.previous_held_by_hand_id == 0) {
                        stable_hand_switch_log << " (LEFT)";
                    } else if (ball.previous_held_by_hand_id == 1) {
                        stable_hand_switch_log << " (RIGHT)";
                    }
                    stable_hand_switch_log << " | CATCH by hand " << ball.held_by_hand_id;
                    if (ball.held_by_hand_id == 0) {
                        stable_hand_switch_log << " (LEFT)";
                    } else if (ball.held_by_hand_id == 1) {
                        stable_hand_switch_log << " (RIGHT)";
                    }
                    stable_hand_switch_log << " | velocity=(" << estimated_velocity.x << ", "
                                          << estimated_velocity.y << ", " << estimated_velocity.z << ") m/s"
                                          << std::endl;
                });
            }
        }
        
        // CRITICAL: Update previous_held_by_hand_id for next frame's hand switch detection
        // This must be done AFTER all state changes are processed
        ball.previous_held_by_hand_id = ball.held_by_hand_id;
    }
    
    DEBUG_LOG(debug_log, {
        OPEN_DEBUG_LOG(debug_log);
        debug_log << "\nTotal events generated: " << events.size() << std::endl;
        debug_log.close();
    });
    
    return events;
}

std::pair<std::vector<SimpleBall>, std::vector<BallEvent>> SimpleBallTracker::update(
    const cv::Mat& color_frame,
    const cv::Mat& depth_frame,
    const CameraIntrinsics& intrinsics) {
    
    // Increment frame counter
    frame_counter_++;
    
    DEBUG_LOG(debug_log, {
        OPEN_DEBUG_LOG(debug_log);
        debug_log << "\n\n========================================" << std::endl;
        debug_log << "=== FRAME " << frame_counter_ << " ===" << std::endl;
        debug_log << "========================================" << std::endl;
    });

    // Calculate dt
    auto current_time = std::chrono::steady_clock::now();
    float dt = std::chrono::duration_cast<std::chrono::duration<float>>(
        current_time - last_update_time_).count();
    last_update_time_ = current_time;

    DEBUG_LOG(debug_log, {
        OPEN_DEBUG_LOG(debug_log);
        debug_log << "Delta time: " << dt << "s" << std::endl;
    });

    // Run YOLO detection
    std::vector<Detection> yolo_detections = runBallDetection(color_frame, depth_frame, intrinsics);
    
    DEBUG_LOG(debug_log, {
        OPEN_DEBUG_LOG(debug_log);
        debug_log << "YOLO detections: " << yolo_detections.size() << std::endl;
        for (size_t i = 0; i < yolo_detections.size(); ++i) {
            const auto& det = yolo_detections[i];
            debug_log << "  Detection " << i << ": class_id=" << det.class_id
                     << " (0=ball, 1=ball_held), conf=" << det.confidence
                     << ", pos=(" << det.world_pos.x << ", " << det.world_pos.y << ", " << det.world_pos.z << ")" << std::endl;
        }
    });

    // Run pose estimation
    std::vector<SimpleHand> hands = runPoseEstimation(color_frame, depth_frame, intrinsics);
    hands_ = hands;  // Store for getters

    DEBUG_LOG(debug_log, {
        OPEN_DEBUG_LOG(debug_log);
        debug_log << "Hands detected: " << hands.size() << std::endl;
        for (const auto& hand : hands) {
            debug_log << "  Hand " << hand.id << " (" << (hand.id == 0 ? "LEFT" : "RIGHT")
                     << "): pos=(" << hand.wrist_pos_3d.x << ", " << hand.wrist_pos_3d.y
                     << ", " << hand.wrist_pos_3d.z << "), visible=" << hand.is_visible << std::endl;
        }
        debug_log.close();
    });
    
    // OPTIMIZATION: Don't convert entire frame to HSV - only convert ROIs as needed
    // This saves ~5-8% FPS by avoiding redundant pixel conversions
    // hsv_frame is now passed as color_frame, and matchColor/searchForColorBlob
    // will convert only the regions they need
    
    // Track which detections are used
    std::set<int> used_detections;
    
    // NEW: Check if ALL enabled profiles are calibrated for euclidean matching
    bool all_calibrated = true;
    for (const auto& profile : color_profiles_) {
        if (profile.enabled && (profile.avg_hue < 0.0f || profile.avg_saturation < 0.0f)) {
            all_calibrated = false;
            break;
        }
    }
    
    // Use euclidean distance matching for all calibrated balls
    if (balls_.size() > 0 && yolo_detections.size() > 0) {
        DEBUG_LOG(euclidean_log, {
            OPEN_DEBUG_LOG(euclidean_log);
            euclidean_log << "\n=== FRAME " << frame_counter_ << " - EUCLIDEAN COLOR MATCHING ===" << std::endl;
            euclidean_log << "Using PURE euclidean distance matching for all balls" << std::endl;
        });
        
        // Calculate euclidean distances for all ball-detection pairs
        struct BallDetectionMatch {
            int ball_idx;
            int det_idx;
            float euclidean_distance;
            float measured_hue;
            float measured_sat;
            std::string ball_color;
        };
        std::vector<BallDetectionMatch> matches;
        
        for (size_t ball_idx = 0; ball_idx < balls_.size(); ++ball_idx) {
            auto& ball = balls_[ball_idx];
            
            // Find color profile
            const ColorProfile* profile = nullptr;
            for (const auto& p : color_profiles_) {
                if (p.name == ball.color_name && p.enabled) {
                    profile = &p;
                    break;
                }
            }
            
            if (!profile) continue;
            
            DEBUG_LOG(euclidean_log, {
                OPEN_DEBUG_LOG(euclidean_log);
                euclidean_log << "\nBall[" << ball_idx << "] '" << ball.color_name << "' calibrated: H="
                             << profile->avg_hue << ", S=" << profile->avg_saturation << std::endl;
            });
            
            // Calculate distance to each detection
            for (size_t det_idx = 0; det_idx < yolo_detections.size(); ++det_idx) {
                const auto& det = yolo_detections[det_idx];
                
                // Skip if already used
                if (used_detections.find(det.index) != used_detections.end()) continue;
                
                // Skip if invalid depth
                if (det.world_pos.z < MIN_DEPTH || det.world_pos.z > MAX_DEPTH) {
                    DEBUG_LOG(euclidean_log, {
                        OPEN_DEBUG_LOG(euclidean_log);
                        euclidean_log << "  Det#" << det.index << " REJECTED: Depth " << det.world_pos.z
                                     << "m is " << (det.world_pos.z < MIN_DEPTH ? "< MIN_DEPTH=" : "> MAX_DEPTH=")
                                     << (det.world_pos.z < MIN_DEPTH ? MIN_DEPTH : MAX_DEPTH) << "m" << std::endl;
                    });
                    continue;
                }
                
                // CRITICAL: Check if detection is within max distance from ball's previous position
                // This prevents trackers from flickering to far away balls
                // Apply this constraint if the ball had ANY position in the previous frame
                // (whether from YOLO, Kalman, color tracking, or hand snapping)
                if (ball.position.z > 0.01f) {  // Ball has a valid previous position
                    float dx = det.world_pos.x - ball.position.x;
                    float dy = det.world_pos.y - ball.position.y;
                    float dz = det.world_pos.z - ball.position.z;
                    float distance_from_previous = std::sqrt(dx*dx + dy*dy + dz*dz);
                    
                    if (distance_from_previous > tracking_settings_.max_tracker_distance_per_frame) {
                        DEBUG_LOG(euclidean_log, {
                            OPEN_DEBUG_LOG(euclidean_log);
                            euclidean_log << "  Det#" << det.index << " REJECTED: Distance " << distance_from_previous
                                         << "m from previous tracker position exceeds max_tracker_distance_per_frame="
                                         << tracking_settings_.max_tracker_distance_per_frame << "m" << std::endl;
                        });
                        continue;
                    }
                }
                
                // Sample 3x3 pixels from detection center
                cv::Point2f center(det.box.x + det.box.width / 2.0f,
                                  det.box.y + det.box.height / 2.0f);
                
                if (center.x < 0 || center.x >= color_frame.cols ||
                    center.y < 0 || center.y >= color_frame.rows) continue;
                
                // OPTIMIZATION: Convert only small ROI for sampling
                const int sample_radius = tracking_settings_.color_sample_radius;
                int roi_x = std::max(0, static_cast<int>(center.x) - sample_radius);
                int roi_y = std::max(0, static_cast<int>(center.y) - sample_radius);
                int roi_width = std::min(color_frame.cols - roi_x, sample_radius * 2 + 1);
                int roi_height = std::min(color_frame.rows - roi_y, sample_radius * 2 + 1);
                
                cv::Rect roi(roi_x, roi_y, roi_width, roi_height);
                cv::Mat color_roi = color_frame(roi);
                cv::Mat hsv_roi;
                cv::cvtColor(color_roi, hsv_roi, cv::COLOR_BGR2HSV);
                
                std::vector<float> hue_samples, sat_samples;
                
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
                
                if (hue_samples.empty()) continue;
                
                // Calculate average
                float avg_hue = 0.0f, avg_sat = 0.0f;
                for (float h : hue_samples) avg_hue += h;
                for (float s : sat_samples) avg_sat += s;
                avg_hue /= hue_samples.size();
                avg_sat /= sat_samples.size();
                
                // Calculate euclidean distance (normalized)
                float hue_diff = (avg_hue / 180.0f) - (profile->avg_hue / 180.0f);
                float sat_diff = (avg_sat / 255.0f) - (profile->avg_saturation / 255.0f);
                
                // Handle hue wrap-around
                if (hue_diff > 0.5f) hue_diff -= 1.0f;
                if (hue_diff < -0.5f) hue_diff += 1.0f;
                
                float distance = std::sqrt(hue_diff * hue_diff + sat_diff * sat_diff);
                
                BallDetectionMatch match;
                match.ball_idx = static_cast<int>(ball_idx);
                match.det_idx = static_cast<int>(det_idx);
                match.euclidean_distance = distance;
                match.measured_hue = avg_hue;
                match.measured_sat = avg_sat;
                match.ball_color = ball.color_name;
                matches.push_back(match);
                
                DEBUG_LOG(euclidean_log, {
                    OPEN_DEBUG_LOG(euclidean_log);
                    euclidean_log << "  Det#" << det.index << " -> " << ball.color_name
                                 << ": measured H=" << avg_hue << ", S=" << avg_sat
                                 << " | target H=" << profile->avg_hue << ", S=" << profile->avg_saturation
                                 << " | dist=" << distance << std::endl;
                });
            }
        }
        
        // CRITICAL FIX: Add temporal consistency bonus to prevent flip-flopping
        // If a ball was matched to a detection in the previous frame, give it a bonus
        // This creates "stickiness" that prevents rapid reassignment when distances are similar
        const float TEMPORAL_CONSISTENCY_BONUS = tracking_settings_.temporal_consistency_bonus;
        const float SPATIAL_THRESHOLD = tracking_settings_.spatial_threshold;
        
        DEBUG_LOG(euclidean_log, {
            OPEN_DEBUG_LOG(euclidean_log);
            euclidean_log << "\nApplying temporal consistency bonus (bonus=" << TEMPORAL_CONSISTENCY_BONUS
                         << ", spatial_threshold=" << SPATIAL_THRESHOLD << "m):" << std::endl;
        });
        for (auto& match : matches) {
            auto& ball = balls_[match.ball_idx];
            
            // Check if this ball had a YOLO detection in the previous frame
            // and if the detection index is still valid
            if (ball.has_yolo_detection && ball.frames_without_yolo == 0) {
                // Ball had a detection last frame - check if this detection is close to the previous position
                const auto& det = yolo_detections[match.det_idx];
                
                // Calculate 3D distance from current detection to ball's previous position
                float dx = det.world_pos.x - ball.position.x;
                float dy = det.world_pos.y - ball.position.y;
                float dz = det.world_pos.z - ball.position.z;
                float spatial_dist = std::sqrt(dx*dx + dy*dy + dz*dz);
                
                // If detection is close to where the ball was, apply bonus
                // Use larger threshold to handle fast-moving balls during juggling
                if (spatial_dist < SPATIAL_THRESHOLD) {
                    float original_dist = match.euclidean_distance;
                    // Scale bonus based on proximity: closer = stronger bonus
                    float proximity_factor = 1.0f - (spatial_dist / SPATIAL_THRESHOLD);
                    float scaled_bonus = TEMPORAL_CONSISTENCY_BONUS * proximity_factor;
                    match.euclidean_distance -= scaled_bonus;
                    DEBUG_LOG(euclidean_log, {
                        OPEN_DEBUG_LOG(euclidean_log);
                        euclidean_log << "  Ball[" << match.ball_idx << "](" << match.ball_color
                                     << ") <-> Det[" << match.det_idx << "]: spatial_dist=" << spatial_dist
                                     << "m, proximity_factor=" << proximity_factor
                                     << ", bonus=" << scaled_bonus
                                     << " | " << original_dist << " -> " << match.euclidean_distance << std::endl;
                    });
                }
            }
        }
        
        // Sort by distance (closest first) - now includes temporal consistency bonus
        std::sort(matches.begin(), matches.end(),
                 [](const BallDetectionMatch& a, const BallDetectionMatch& b) {
                     return a.euclidean_distance < b.euclidean_distance;
                 });
        
        // CRITICAL: Greedy assignment with proper one-to-one matching
        // Each ball gets exactly one detection, each detection assigned to exactly one ball
        std::set<int> assigned_balls, assigned_dets;
        
        DEBUG_LOG(euclidean_log, {
            OPEN_DEBUG_LOG(euclidean_log);
            euclidean_log << "\nEuclidean matching - sorted pairs (closest first):" << std::endl;
            for (const auto& match : matches) {
                euclidean_log << "  Ball[" << match.ball_idx << "](" << match.ball_color << ") <-> Det[" << match.det_idx
                             << "] dist=" << match.euclidean_distance
                             << " (H=" << match.measured_hue << ", S=" << match.measured_sat << ")" << std::endl;
            }
            
            euclidean_log << "\nAssignment process:" << std::endl;
        });
        for (const auto& match : matches) {
            // Skip if this ball already has a detection
            if (assigned_balls.find(match.ball_idx) != assigned_balls.end()) {
                DEBUG_LOG(euclidean_log, {
                    OPEN_DEBUG_LOG(euclidean_log);
                    euclidean_log << "  SKIP: Ball[" << match.ball_idx << "](" << match.ball_color
                                 << ") already assigned" << std::endl;
                });
                continue;
            }
            
            // Skip if this detection already assigned to another ball
            if (assigned_dets.find(match.det_idx) != assigned_dets.end()) {
                DEBUG_LOG(euclidean_log, {
                    OPEN_DEBUG_LOG(euclidean_log);
                    euclidean_log << "  SKIP: Det[" << match.det_idx << "] already assigned" << std::endl;
                });
                continue;
            }
            
            auto& ball = balls_[match.ball_idx];
            const auto& det = yolo_detections[match.det_idx];
            
            // Mark as assigned
            assigned_balls.insert(match.ball_idx);
            assigned_dets.insert(match.det_idx);
            used_detections.insert(det.index);
            
            // Assign detection to ball
            ball.position = det.world_pos;
            ball.pixel_pos = cv::Point2f(det.box.x + det.box.width / 2.0f,
                                         det.box.y + det.box.height / 2.0f);
            ball.bbox = det.box;
            ball.has_yolo_detection = true;
            ball.frames_without_yolo = 0;
            ball.yolo_confidence = det.confidence;
            ball.yolo_class_id = det.class_id;
            ball.color_match_score = std::exp(-match.euclidean_distance * 10.0f);  // For display
            
            char reason[128];
            snprintf(reason, sizeof(reason), "Euclidean dist=%.3f", match.euclidean_distance);
            ball.tracking_reason = reason;
            
            // Update Kalman
            ball.kalman.update(KalmanFilter3D::MeasurementVector(
                ball.position.x, ball.position.y, ball.position.z));
            ball.color_predictor.addDetection(ball.position);
            
            DEBUG_LOG(euclidean_log, {
                OPEN_DEBUG_LOG(euclidean_log);
                euclidean_log << "  *** MATCHED: Ball[" << match.ball_idx << "](" << ball.color_name
                             << ") <- Det#" << det.index << " (dist=" << match.euclidean_distance << ") ***" << std::endl;
            });
        }
        
        // Mark unmatched balls
        DEBUG_LOG(euclidean_log, {
            OPEN_DEBUG_LOG(euclidean_log);
            euclidean_log << "\nUnmatched balls:" << std::endl;
            for (size_t ball_idx = 0; ball_idx < balls_.size(); ++ball_idx) {
                if (assigned_balls.find(ball_idx) == assigned_balls.end()) {
                    balls_[ball_idx].has_yolo_detection = false;
                    balls_[ball_idx].frames_without_yolo++;
                    euclidean_log << "  Ball[" << ball_idx << "](" << balls_[ball_idx].color_name
                                 << ") - NO MATCH" << std::endl;
                }
            }
            
            euclidean_log << "=== END EUCLIDEAN MATCHING ===" << std::endl;
            euclidean_log.close();
        });
    }
    
    // Handle unmatched balls with fallback strategies (Kalman prediction, color tracking, hand snapping)
    for (auto& ball : balls_) {
        // Skip if already matched by euclidean system
        if (ball.has_yolo_detection && ball.frames_without_yolo == 0) {
            continue;
        }
        
        // Ball was not matched - mark as undetected and increment counter
        ball.has_yolo_detection = false;
        ball.frames_without_yolo++;
        
        // Get Kalman prediction for fallback tracking
        cv::Point3f kalman_pred(0, 0, 0);
        bool has_prediction = false;
        
        if (ball.kalman.get_state()(2) > 0.01f) {  // Only if Kalman has been initialized (z > 0)
            // SAVE the current Kalman state before prediction
            auto saved_state = ball.kalman.get_state();
            
            // Temporarily predict to see where Kalman thinks the ball should be
            ball.kalman.predict(dt);
            auto predicted_state = ball.kalman.get_state();
            kalman_pred = cv::Point3f(predicted_state(0), predicted_state(1), predicted_state(2));
            has_prediction = (kalman_pred.z > 0.01f);
            
            // RESTORE the original state - we haven't actually measured anything yet!
            ball.kalman.get_state() = saved_state;
        }
        
        // Find matching color profile for fallback color tracking
        const ColorProfile* profile = nullptr;
        for (const auto& p : color_profiles_) {
            if (p.name == ball.color_name && p.enabled) {
                profile = &p;
                break;
            }
        }
        
        if (profile) {
            
            // CATCH DETECTION: If ball just vanished from YOLO (frames_without_yolo == 1)
            // and a hand is nearby, infer that the ball was caught
            if (ball.frames_without_yolo == 1 && !ball.is_held) {
                // Check if any hand is near the ball's last known position
                float min_hand_dist = std::numeric_limits<float>::max();
                int closest_hand_id = -1;
                cv::Point3f closest_hand_pos(0, 0, 0);
                
                for (const auto& hand : hands) {
                    if (!hand.is_visible) continue;
                    float dist = cv::norm(ball.position - hand.wrist_pos_3d);
                    if (dist < min_hand_dist) {
                        min_hand_dist = dist;
                        closest_hand_id = hand.id;
                        closest_hand_pos = hand.wrist_pos_3d;
                    }
                }
                
                // If a hand is within catch distance when ball vanishes, infer catch
                const float CATCH_INFERENCE_DISTANCE = 0.25f;  // 25cm - generous for catch detection
                if (min_hand_dist < CATCH_INFERENCE_DISTANCE) {
                    DEBUG_LOG(catch_inference_log, {
                        OPEN_DEBUG_LOG(catch_inference_log);
                        catch_inference_log << "\n[CATCH_INFERENCE] Ball " << ball.id
                                           << " vanished from YOLO with hand " << closest_hand_id
                                           << " (" << (closest_hand_id == 0 ? "LEFT" : "RIGHT") << ")"
                                           << " at distance " << min_hand_dist << "m"
                                           << " - INFERRING CATCH" << std::endl;
                    });
                    
                    // Mark ball as held by this hand
                    ball.held_by_hand_id = closest_hand_id;
                    ball.yolo_class_id = 1;  // Mark as held
                    ball.position = closest_hand_pos;
                    ball.pixel_pos = project_3d_to_2d(closest_hand_pos, intrinsics);
                    
                    char reason[128];
                    snprintf(reason, sizeof(reason), "CATCH_INFERRED@Hand[%c] d=%.2fm",
                             closest_hand_id == 0 ? 'L' : 'R', min_hand_dist);
                    ball.tracking_reason = reason;
                    
                    // Update color predictor with hand position
                    ball.color_predictor.addDetection(ball.position);
                    
                    // Continue to next ball - skip normal fallback logic
                    continue;
                }
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
                    cv::Point2f color_blob = searchForColorBlob(color_frame, *profile, hand_2d, 80);
                    
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
                    
                    // CRITICAL FIX: DO NOT update Kalman with hand snap positions!
                    // Hand positions are not accurate ball positions and corrupt the Kalman state
                    // This causes bad predictions when the ball is thrown
                    // Only update Kalman with real YOLO or color detections
                    
                    // ALWAYS add position to color predictor - we consider the ball to be here
                    ball.color_predictor.addDetection(ball.position);
                    ball.frames_without_yolo = 0;
                    continue;
                }
                
                // Prediction is NOT near a hand - try color tracking at prediction point
                cv::Point2f pred_2d = project_3d_to_2d(kalman_pred, intrinsics);
                cv::Point2f color_blob = searchForColorBlob(color_frame, *profile, pred_2d, COLOR_SEARCH_RADIUS);
                
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
            
            // CRITICAL FIX: Use Kalman prediction but DON'T update with it
            // Updating Kalman with its own predictions causes drift to compound
            if (has_prediction && ball.frames_without_yolo < MAX_FRAMES_WITHOUT_YOLO) {
                // Use Kalman prediction as the ball position for display
                auto state = ball.kalman.get_state();
                ball.position = cv::Point3f(state(0), state(1), state(2));
                
                // DO NOT update Kalman with its own prediction - this causes drift!
                // The Kalman filter should only be updated with real measurements
                // The prediction uncertainty (P matrix) naturally grows over time
                
                // CRITICAL FIX: DO add to color predictor to maintain history continuity
                // The color predictor needs continuous position updates to maintain velocity estimates
                // This allows the history to continue even when YOLO detections are missing
                ball.color_predictor.addDetection(ball.position);
                
                // Update yolo_class_id based on proximity to hands
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
            else if (!has_prediction && ball.frames_without_yolo < MAX_FRAMES_WITHOUT_YOLO) {
                // LEGACY FALLBACK: Only used when Kalman is not initialized
                // This should rarely happen in normal operation
                
                // Use current Kalman state as fallback
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
                            
                            // CRITICAL FIX: DO NOT update Kalman when snapping to hands
                            // Hand positions corrupt the Kalman state and cause bad predictions
                            
                            // ALWAYS add position to color predictor - we consider the ball to be here
                            ball.color_predictor.addDetection(ball.position);
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
                    
                    // CRITICAL: Update color predictor with flight prediction
                    // This keeps the history active and velocity calculation accurate
                    ball.color_predictor.addDetection(ball.position);
                }
            }
        }
    }
    
    // CRITICAL: Update held ball positions to follow hands
    // If a ball is marked as held, it must move with the hand
    for (auto& ball : balls_) {
        if (ball.is_held && ball.held_by_hand_id >= 0) {
            // Find the hand that's holding this ball
            for (const auto& hand : hands) {
                if (hand.id == ball.held_by_hand_id && hand.is_visible) {
                    // Check if ball is still close enough to be held
                    float dist = cv::norm(ball.position - hand.wrist_pos_3d);
                    
                    // If ball has drifted too far from hand, it's not actually held
                    if (dist > tracking_settings_.wrist_proximity_threshold * 2.0f) {
                        // Ball is too far - mark as not held
                        ball.is_held = false;
                        ball.held_by_hand_id = -1;
                        ball.yolo_class_id = 0;  // Mark as in-air
                        
                        DEBUG_LOG(drift_log, {
                            OPEN_DEBUG_LOG(drift_log);
                            drift_log << "\n[DRIFT] Ball " << ball.id << " drifted " << dist
                                     << "m from hand " << hand.id << " - marking as NOT HELD" << std::endl;
                        });
                    } else {
                        // Ball is close enough - update position to follow hand
                        ball.position = hand.wrist_pos_3d;
                        ball.pixel_pos = project_3d_to_2d(hand.wrist_pos_3d, intrinsics);
                        
                        // Update color predictor with hand position
                        ball.color_predictor.addDetection(ball.position);
                        
                        DEBUG_LOG(follow_log, {
                            OPEN_DEBUG_LOG(follow_log);
                            follow_log << "\n[FOLLOW] Ball " << ball.id << " following hand " << hand.id
                                      << " at (" << ball.position.x << ", " << ball.position.y << ", "
                                      << ball.position.z << ") dist=" << dist << "m" << std::endl;
                        });
                    }
                    break;
                }
            }
        }
    }
    
    // Detect ball states and events
    std::vector<BallEvent> events = detectStatesAndEvents(balls_, hands);
    
    DEBUG_LOG(debug_log_end, {
        OPEN_DEBUG_LOG(debug_log_end);
        debug_log_end << "\n=== Update Complete ===" << std::endl;
        debug_log_end << "Events generated: " << events.size() << std::endl;
        for (const auto& event : events) {
            debug_log_end << "  " << (event.type == BallEvent::THROW ? "THROW" : "CATCH")
                      << " - Ball " << event.ball_id << ", Hand " << event.hand_id << std::endl;
        }
        debug_log_end << "========================================\n" << std::endl;
        debug_log_end.close();
    });
    
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
    
    // OPTIMIZATION: Convert only the ROI around detection center to HSV
    // (calibrateColor only needs a small 5x5 sample)
    
    // Calculate center of bounding box
    int center_x = static_cast<int>(clicked_det->box.x + clicked_det->box.width / 2.0f);
    int center_y = static_cast<int>(clicked_det->box.y + clicked_det->box.height / 2.0f);
    
    // Sample 5x5 pixel square from the center
    const int sample_size = 5;
    const int half_size = sample_size / 2;
    
    std::vector<float> hue_samples;
    std::vector<float> sat_samples;
    
    for (int dy = -half_size; dy <= half_size; ++dy) {
        for (int dx = -half_size; dx <= half_size; ++dx) {
            int x = center_x + dx;
            int y = center_y + dy;
            
            // Check bounds
            if (x >= 0 && x < last_color_frame_.cols && y >= 0 && y < last_color_frame_.rows) {
                // Convert single pixel to HSV on-demand
                cv::Mat pixel_bgr = last_color_frame_(cv::Rect(x, y, 1, 1));
                cv::Mat pixel_hsv;
                cv::cvtColor(pixel_bgr, pixel_hsv, cv::COLOR_BGR2HSV);
                cv::Vec3b hsv = pixel_hsv.at<cv::Vec3b>(0, 0);
                hue_samples.push_back(static_cast<float>(hsv[0]));
                sat_samples.push_back(static_cast<float>(hsv[1]));
            }
        }
    }
    
    if (hue_samples.empty()) {
        error_message = "Could not sample pixels from bounding box center";
        return false;
    }
    
    // Calculate average hue and saturation
    float avg_hue = 0.0f;
    float avg_sat = 0.0f;
    
    for (float h : hue_samples) avg_hue += h;
    for (float s : sat_samples) avg_sat += s;
    
    avg_hue /= hue_samples.size();
    avg_sat /= sat_samples.size();
    
    // Find and update color profile
    for (auto& profile : color_profiles_) {
        if (profile.name == color_name) {
            profile.avg_hue = avg_hue;
            profile.avg_saturation = avg_sat;
            
            // LEGACY: Also update min/max ranges for backward compatibility
            const float hue_tolerance = 20.0f;
            const float sat_min = 50.0f;
            const float val_min = 50.0f;
            
            profile.min_hsv = cv::Scalar(
                std::max(0.0f, avg_hue - hue_tolerance),
                sat_min,
                val_min
            );
            
            profile.max_hsv = cv::Scalar(
                std::min(180.0f, avg_hue + hue_tolerance),
                255.0f,
                255.0f
            );
            
            saveSettings();
            
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


