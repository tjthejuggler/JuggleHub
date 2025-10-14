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
    
    // Initialize GPU-accelerated HSV converter
    gpu_hsv_converter_ = std::make_unique<GpuHsvConverter>();
    std::cout << "[SimpleBallTracker] " << gpu_hsv_converter_->getGpuInfo() << std::endl;
    
    // Initialize GPU-accelerated trajectory predictor
    gpu_trajectory_predictor_ = std::make_unique<GpuTrajectoryPredictor>();
    std::cout << "[SimpleBallTracker] " << gpu_trajectory_predictor_->getGpuInfo() << std::endl;
    
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
            ball.state = HELD;  // CRITICAL: Initialize state
            ball.previous_state = HELD;  // Initialize previous state
            ball.held_by_hand_id = -1;  // No hand assigned yet
            ball.position = cv::Point3f(0, 0, 0);  // Will be set in first update
            
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
                
                balls_.push_back(ball);
            }
        }
        return true;
    }
    
    // Handle YOLO detection settings
    try {
        if (key == "ball_confidence_threshold") {
            ball_confidence_threshold_ = std::stof(value);
            return true;
        }
        else if (key == "ball_held_confidence_threshold") {
            ball_held_confidence_threshold_ = std::stof(value);
            return true;
        }
        else if (key == "nms_threshold") {
            nms_threshold_ = std::stof(value);
            return true;
        }
        else if (key == "show_raw_yolo_detections") {
            show_raw_yolo_detections_ = (value == "true" || value == "1");
            return true;
        }
        // Handle tracking settings
        else if (key == "undetected_near_hand_threshold") {
            tracking_settings_.undetected_near_hand_threshold = std::stof(value);
            return true;
        }
        else if (key == "min_frames_for_state_change") {
            tracking_settings_.min_frames_for_state_change = std::stoi(value);
            return true;
        }
        else if (key == "min_throw_distance") {
            tracking_settings_.min_throw_distance = std::stof(value);
            return true;
        }
        else if (key == "color_sample_radius") {
            tracking_settings_.color_sample_radius = std::stoi(value);
            return true;
        }
        // NEW: Separate override thresholds for ball and ball_held
        else if (key == "override_ball_confidence_threshold") {
            tracking_settings_.override_ball_confidence_threshold = std::stof(value);
            return true;
        }
        else if (key == "override_ball_color_threshold") {
            tracking_settings_.override_ball_color_threshold = std::stof(value);
            return true;
        }
        else if (key == "override_ball_held_confidence_threshold") {
            tracking_settings_.override_ball_held_confidence_threshold = std::stof(value);
            return true;
        }
        else if (key == "override_ball_held_color_threshold") {
            tracking_settings_.override_ball_held_color_threshold = std::stof(value);
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
        // Override detection thresholds (DEPRECATED - kept for backward compatibility)
        else if (key == "override_min_confidence_tracked") {
            tracking_settings_.override_min_confidence_tracked = std::stof(value);
            return true;
        }
        else if (key == "override_min_color_score_tracked") {
            tracking_settings_.override_min_color_score_tracked = std::stof(value);
            return true;
        }
        else if (key == "override_min_confidence_missing") {
            tracking_settings_.override_min_confidence_missing = std::stof(value);
            return true;
        }
        else if (key == "override_min_color_score_missing") {
            tracking_settings_.override_min_color_score_missing = std::stof(value);
            return true;
        }
        // Held ball color blob detection settings
        else if (key == "held_color_search_radius") {
            tracking_settings_.held_color_search_radius = std::stoi(value);
            return true;
        }
        else if (key == "held_color_min_score") {
            tracking_settings_.held_color_min_score = std::stof(value);
            return true;
        }
        else if (key == "held_color_max_distance") {
            tracking_settings_.held_color_max_distance = std::stof(value);
            return true;
        }
        // Color blob search settings
        else if (key == "color_blob_search_enabled") {
            tracking_settings_.color_blob_search_enabled = (value == "true" || value == "1");
            return true;
        }
        else if (key == "color_blob_search_radius") {
            tracking_settings_.color_blob_search_radius = std::stoi(value);
            return true;
        }
        else if (key == "color_blob_min_color_score") {
            tracking_settings_.color_blob_min_color_score = std::stof(value);
            return true;
        }
        else if (key == "color_blob_max_depth_diff") {
            tracking_settings_.color_blob_max_depth_diff = std::stof(value);
            return true;
        }
        // Identity swap prevention settings
        else if (key == "max_euclidean_distance") {
            tracking_settings_.max_euclidean_distance = std::stof(value);
            return true;
        }
        else if (key == "min_euclidean_color_score") {
            tracking_settings_.min_euclidean_color_score = std::stof(value);
            return true;
        }
        else if (key == "max_depth_jump_strict") {
            tracking_settings_.max_depth_jump_strict = std::stof(value);
            return true;
        }
        // Trajectory visualization settings
        else if (key == "show_trajectory") {
            viz_settings_.show_trajectory = (value == "true" || value == "1");
            return true;
        }
        else if (key == "show_verified_points") {
            viz_settings_.show_verified_points = (value == "true" || value == "1");
            return true;
        }
        else if (key == "show_predicted_path") {
            viz_settings_.show_predicted_path = (value == "true" || value == "1");
            return true;
        }
        else if (key == "show_search_radius") {
            viz_settings_.show_search_radius = (value == "true" || value == "1");
            return true;
        }
        else if (key == "show_confidence") {
            viz_settings_.show_confidence = (value == "true" || value == "1");
            return true;
        }
        else if (key == "show_max_distance") {
            viz_settings_.show_max_distance = (value == "true" || value == "1");
            return true;
        }
        else if (key == "show_hand_distance_threshold") {
            viz_settings_.show_hand_distance_threshold = (value == "true" || value == "1");
            return true;
        }
        else if (key == "show_hand_velocity_zone") {
            viz_settings_.show_hand_velocity_zone = (value == "true" || value == "1");
            return true;
        }
        // BACKWARD COMPATIBILITY: Accept old visualization toggle names
        else if (key == "show_throw_distance_threshold") {
            viz_settings_.show_hand_distance_threshold = (value == "true" || value == "1");
            return true;
        }
        else if (key == "show_catch_distance_threshold") {
            viz_settings_.show_hand_distance_threshold = (value == "true" || value == "1");
            return true;
        }
        // Trajectory-based tracking settings
        // NEW: Unified hand_distance_threshold
        else if (key == "hand_distance_threshold") {
            tracking_settings_.hand_distance_threshold = std::stof(value);
            // Also update legacy thresholds for backward compatibility
            tracking_settings_.catch_distance_threshold = std::stof(value);
            tracking_settings_.throw_distance_threshold = std::stof(value);
            return true;
        }
        // BACKWARD COMPATIBILITY: Accept old threshold names and map to unified threshold
        else if (key == "catch_distance_threshold") {
            tracking_settings_.hand_distance_threshold = std::stof(value);
            tracking_settings_.catch_distance_threshold = std::stof(value);
            return true;
        }
        else if (key == "throw_distance_threshold") {
            tracking_settings_.hand_distance_threshold = std::stof(value);
            tracking_settings_.throw_distance_threshold = std::stof(value);
            return true;
        }
        // Trajectory physics settings
        else if (key == "traj_gravity") {
            tracking_settings_.traj_gravity = std::stof(value);
            return true;
        }
        else if (key == "traj_time_step") {
            tracking_settings_.traj_time_step = std::stof(value);
            return true;
        }
        else if (key == "traj_max_time") {
            tracking_settings_.traj_max_time = std::stof(value);
            return true;
        }
        else if (key == "traj_search_radius") {
            tracking_settings_.traj_search_radius = std::stof(value);
            return true;
        }
        else if (key == "traj_min_points_for_prediction") {
            tracking_settings_.traj_min_points_for_prediction = std::stoi(value);
            return true;
        }
        else if (key == "traj_color_match_threshold") {
            tracking_settings_.traj_color_match_threshold = std::stof(value);
            return true;
        }
        else if (key == "traj_velocity_estimation_time") {
            tracking_settings_.traj_velocity_estimation_time = std::stof(value);
            return true;
        }
        else if (key == "traj_max_search_distance") {
            tracking_settings_.traj_max_search_distance = std::stof(value);
            return true;
        }
        // Hand velocity tracking settings
        else if (key == "hand_velocity_enabled") {
            tracking_settings_.hand_velocity_enabled = (value == "true" || value == "1");
            return true;
        }
        else if (key == "hand_velocity_threshold") {
            tracking_settings_.hand_velocity_threshold = std::stof(value);
            return true;
        }
        else if (key == "hand_velocity_confidence_reduction") {
            tracking_settings_.hand_velocity_confidence_reduction = std::stof(value);
            return true;
        }
        else if (key == "hand_velocity_ignore_class") {
            tracking_settings_.hand_velocity_ignore_class = (value == "true" || value == "1");
            return true;
        }
        else if (key == "hand_velocity_detection_radius") {
            tracking_settings_.hand_velocity_detection_radius = std::stof(value);
            return true;
        }
        else if (key == "hand_velocity_distance_reduction") {
            tracking_settings_.hand_velocity_distance_reduction = std::stof(value);
            return true;
        }
        else if (key == "min_frames_before_catch") {
            int val = std::stoi(value);
            // Clamp value to 0-10 range
            tracking_settings_.min_frames_before_catch = std::max(0, std::min(10, val));
            return true;
        }
        else if (key == "enable_ball_detection") {
            enable_ball_detection_ = (value == "true" || value == "1");
            return true;
        }
        else if (key == "enable_pose_detection") {
            enable_pose_detection_ = (value == "true" || value == "1");
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
    
    // PERFORMANCE FIX: Use cached HSV frame instead of GPU conversion
    // The entire frame was converted to HSV once at the start of update()
    // This avoids redundant GPU conversions (was ~15-20 per frame, now just 1)
    cv::Mat hsv_roi;
    const int max_sample_radius = 7;  // Legacy mode uses larger radius
    int roi_x = std::max(0, static_cast<int>(center.x) - max_sample_radius);
    int roi_y = std::max(0, static_cast<int>(center.y) - max_sample_radius);
    int roi_width = std::min(color_frame.cols - roi_x, max_sample_radius * 2 + 1);
    int roi_height = std::min(color_frame.rows - roi_y, max_sample_radius * 2 + 1);
    
    // Extract ROI from cached HSV frame
    if (!cached_hsv_frame_.empty() && cached_color_frame_.data == color_frame.data) {
        cv::Rect roi(roi_x, roi_y, roi_width, roi_height);
        hsv_roi = cached_hsv_frame_(roi);
    } else {
        // Fallback: cache not available, use GPU conversion
        cv::Rect roi(roi_x, roi_y, roi_width, roi_height);
        hsv_roi = gpu_hsv_converter_->convertRoiToHsv(color_frame, roi);
    }
    
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

void SimpleBallTracker::evaluateOverrideCriteria(std::vector<Detection>& detections, const cv::Mat& color_frame) {
    // PERFORMANCE NOTE: This method is expensive (detections × colors color matching operations)
    // Only call this when needed for recording/debugging visualization
    // For normal tracking, use lazy per-ball evaluation instead
    
    // Evaluate each detection against all enabled color profiles
    for (auto& det : detections) {
        det.override_evals.clear();
        
        for (const auto& profile : color_profiles_) {
            if (!profile.enabled) continue;
            
            Detection::OverrideEval eval;
            eval.ball_color = profile.name;
            
            // Calculate color score
            eval.color_score = matchColor(det, profile, color_frame);
            
            // NEW: Use class-specific thresholds based on detection class_id
            // class_id=0 is 'ball', class_id=1 is 'ball_held'
            float confidence_threshold = (det.class_id == 0) ?
                tracking_settings_.override_ball_confidence_threshold :
                tracking_settings_.override_ball_held_confidence_threshold;
            
            float color_threshold = (det.class_id == 0) ?
                tracking_settings_.override_ball_color_threshold :
                tracking_settings_.override_ball_held_color_threshold;
            
            // Check override criteria using class-specific thresholds
            eval.meets_confidence_threshold = (det.confidence >= confidence_threshold);
            eval.meets_color_threshold = (eval.color_score >= color_threshold);
            eval.meets_class_requirement = !tracking_settings_.override_require_ball_class || (det.class_id == 0);
            
            // Determine if this would override
            eval.would_override = eval.meets_confidence_threshold &&
                                 eval.meets_color_threshold &&
                                 eval.meets_class_requirement;
            
            // Build reason string with class-specific thresholds
            std::string class_name = (det.class_id == 0) ? "ball" : "ball_held";
            
            if (eval.would_override) {
                eval.reason = "OVERRIDE: conf=" + std::to_string(det.confidence) +
                             " >= " + std::to_string(confidence_threshold) +
                             ", color=" + std::to_string(eval.color_score) +
                             " >= " + std::to_string(color_threshold) +
                             ", class=" + class_name;
                if (tracking_settings_.override_require_ball_class) {
                    eval.reason += " (requires ball)";
                }
            } else {
                eval.reason = "NO_OVERRIDE:";
                if (!eval.meets_confidence_threshold) {
                    eval.reason += " conf=" + std::to_string(det.confidence) +
                                  " < " + std::to_string(confidence_threshold);
                }
                if (!eval.meets_color_threshold) {
                    eval.reason += " color=" + std::to_string(eval.color_score) +
                                  " < " + std::to_string(color_threshold);
                }
                if (!eval.meets_class_requirement) {
                    eval.reason += " class=" + class_name + " (requires ball)";
                }
            }
            
            det.override_evals.push_back(eval);
        }
    }
}


cv::Point2f SimpleBallTracker::searchForColorBlob(const cv::Mat& color_frame,
                                                  const ColorProfile& profile,
                                                  const cv::Point2f& search_center,
                                                  int radius) {
    // GPU-ACCELERATED: Perform entire color blob search on GPU
    // This includes HSV conversion, inRange masking, and contour detection
    int roi_x = std::max(0, static_cast<int>(search_center.x) - radius);
    int roi_y = std::max(0, static_cast<int>(search_center.y) - radius);
    int roi_width = std::min(color_frame.cols - roi_x, radius * 2);
    int roi_height = std::min(color_frame.rows - roi_y, radius * 2);
    
    cv::Rect roi(roi_x, roi_y, roi_width, roi_height);
    
    // Use GPU-accelerated blob search (HSV conversion + inRange on GPU)
    cv::Point2f blob_center = gpu_hsv_converter_->findColorBlob(
        color_frame, roi,
        profile.min_hsv, profile.max_hsv,
        profile.min_hsv2, profile.max_hsv2,
        roi_x, roi_y
    );
    
    // Check if blob is within search radius
    if (blob_center.x > 0 && blob_center.y > 0) {
        float dist = cv::norm(blob_center - search_center);
        if (dist > radius) {
            return cv::Point2f(-1, -1);
        }
    }
    
    return blob_center;
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

void SimpleBallTracker::updateHandVelocity(SimpleHand& hand, uint64_t current_timestamp) {
    // CRITICAL FIX: Only add position if it's different from the last one
    // This prevents duplicate positions from being added when hand is stationary
    bool should_add = true;
    if (!hand.position_history.empty()) {
        cv::Point3f last_pos = hand.position_history.back();
        float dist = cv::norm(hand.wrist_pos_3d - last_pos);
        // Only add if hand moved at least 1cm
        if (dist < 0.01f) {
            should_add = false;
        }
    }
    
    if (should_add) {
        hand.position_history.push_back(hand.wrist_pos_3d);
        
        // Keep only last 3 positions
        if (hand.position_history.size() > 3) {
            hand.position_history.erase(hand.position_history.begin());
        }
    }
    
    // Need at least 3 positions to calculate velocity reliably
    if (hand.position_history.size() < 3) {
        hand.has_valid_velocity = false;
        hand.velocity = cv::Point3f(0, 0, 0);
        hand.last_update_timestamp = current_timestamp;
        return;
    }
    
    // Calculate time span from first to last position in history
    // This gives us a more stable velocity estimate over ~3 frames
    float dt = 0.1f;  // Default to 100ms (3 frames at 30 FPS)
    if (hand.last_update_timestamp > 0) {
        dt = (current_timestamp - hand.last_update_timestamp) / 1000000.0f;  // Convert microseconds to seconds
        // Clamp dt to reasonable range (10ms to 1s)
        dt = std::max(0.01f, std::min(dt, 1.0f));
    }
    
    // Calculate velocity from first to last position in history (more stable than frame-to-frame)
    cv::Point3f pos_diff = hand.position_history[2] - hand.position_history[0];
    float distance = cv::norm(pos_diff);
    
    // Only update velocity if hand actually moved
    if (distance > 0.001f) {  // Moved at least 1mm
        // Divide by 2*dt because we're spanning ~2 frame intervals (3 positions = 2 intervals)
        hand.velocity = pos_diff / (dt * 2.0f);
        hand.has_valid_velocity = true;
    } else {
        // Hand is stationary
        hand.velocity = cv::Point3f(0, 0, 0);
        hand.has_valid_velocity = true;  // Still valid, just zero velocity
    }
    
    hand.last_update_timestamp = current_timestamp;
    
    DEBUG_LOG(debug_log, {
        OPEN_DEBUG_LOG(debug_log);
        debug_log << "[HAND_VELOCITY] Hand " << hand.id << ": velocity=("
                  << hand.velocity.x << ", " << hand.velocity.y << ", " << hand.velocity.z
                  << ") m/s, speed=" << cv::norm(hand.velocity) << " m/s, history_size="
                  << hand.position_history.size() << std::endl;
        debug_log.close();
    });
}

std::vector<BallEvent> SimpleBallTracker::detectStatesAndEvents(
    std::vector<SimpleBall>& balls,
    const std::vector<SimpleHand>& hands) {
    
    std::vector<BallEvent> events;
    
    DEBUG_LOG(debug_log, {
        OPEN_DEBUG_LOG(debug_log);
        if (recording_frame_number_ >= 0) {
            debug_log << "\n=== FRAME " << frame_counter_ << " (Recording Frame " << recording_frame_number_ << ") - detectStatesAndEvents() ===" << std::endl;
        } else {
            debug_log << "\n=== FRAME " << frame_counter_ << " - detectStatesAndEvents() ===" << std::endl;
        }
        debug_log << "Number of balls: " << balls.size() << std::endl;
        debug_log << "Number of hands: " << hands.size() << std::endl;
        debug_log << "NOTE: State transitions are now handled in updateHeldBall() and updateInFlightBall()" << std::endl;
        debug_log << "This method only updates previous_held_by_hand_id for tracking" << std::endl;
    });
    
    // NOTE: State transitions and event generation are now handled in:
    // - updateHeldBall() for HELD state
    // - updateInFlightBall() for IN_FLIGHT state
    // This method is kept for compatibility but should not generate spurious events
    
    // Only track previous state for detecting actual transitions
    for (auto& ball : balls_) {
        DEBUG_LOG(debug_log, {
            OPEN_DEBUG_LOG(debug_log);
            debug_log << "  Ball " << ball.id << " (" << ball.color_name << "): "
                      << "state=" << (ball.state == HELD ? "HELD" : "IN_FLIGHT")
                      << ", held_by_hand=" << ball.held_by_hand_id << std::endl;
        });
        
        // Update previous state and hand ID for next frame
        ball.previous_state = ball.state;
        ball.previous_held_by_hand_id = ball.held_by_hand_id;
    }
    
    DEBUG_LOG(debug_log, {
        OPEN_DEBUG_LOG(debug_log);
        debug_log << "No events generated (events now generated in state update methods)" << std::endl;
        debug_log.close();
    });
    
    return events;  // Events are generated in initiateThrow() and initiateCatch()
}

std::pair<std::vector<SimpleBall>, std::vector<BallEvent>> SimpleBallTracker::update(
    const cv::Mat& color_frame,
    const cv::Mat& depth_frame,
    const CameraIntrinsics& intrinsics) {
    
    // Increment frame counter
    frame_counter_++;
    
    // LOCKUP DEBUG: Always log frame start to console
    std::cout << "[FRAME " << frame_counter_ << "] Update started" << std::endl;
    
    DEBUG_LOG(debug_log, {
        OPEN_DEBUG_LOG(debug_log);
        debug_log << "\n\n========================================" << std::endl;
        if (recording_frame_number_ >= 0) {
            debug_log << "=== FRAME " << frame_counter_ << " (Recording Frame " << recording_frame_number_ << ") ===" << std::endl;
        } else {
            debug_log << "=== FRAME " << frame_counter_ << " ===" << std::endl;
        }
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

    // Initialize events vector for this frame
    std::vector<BallEvent> events;
    
    // PERFORMANCE FIX: Convert entire frame to HSV once at start
    // This avoids redundant GPU conversions in matchColor() calls
    // Cache it for the duration of this frame
    cached_hsv_frame_ = cv::Mat();
    cv::cvtColor(color_frame, cached_hsv_frame_, cv::COLOR_BGR2HSV);
    cached_color_frame_ = color_frame;  // Store reference for cache validation

    // Run YOLO detection
    std::cout << "[FRAME " << frame_counter_ << "] Running YOLO detection..." << std::endl;
    std::vector<Detection> yolo_detections = runBallDetection(color_frame, depth_frame, intrinsics);
    std::cout << "[FRAME " << frame_counter_ << "] YOLO detection complete: " << yolo_detections.size() << " detections" << std::endl;
    
    // PERFORMANCE FIX: Only evaluate override criteria for recording/debugging
    // During normal operation, we evaluate lazily per-ball (much faster)
    // This reduces color matching from (detections × colors) to just what's needed
    
    // Store detections for recording (will be used by Engine.cpp)
    last_raw_detections_ = yolo_detections;
    
    // OVERRIDE LOGIC: Check if any detection has a successful override for any ball
    // If so, immediately assign that detection to the ball, regardless of current state
    // Track which balls have been overridden so we skip normal update logic for them
    std::set<int> overridden_balls;
    
    for (auto& ball : balls_) {
        // Find the color profile for this ball
        const ColorProfile* profile = nullptr;
        for (const auto& p : color_profiles_) {
            if (p.name == ball.color_name && p.enabled) {
                profile = &p;
                break;
            }
        }
        
        if (!profile) continue;
        
        // Check all detections for override match
        // PERFORMANCE FIX: Evaluate override criteria ONLY for this specific ball color
        for (auto& det : yolo_detections) {
            // Calculate color score for THIS ball's color only
            float color_score = matchColor(det, *profile, color_frame);
            
            // Check override criteria using class-specific thresholds
            float confidence_threshold = (det.class_id == 0) ?
                tracking_settings_.override_ball_confidence_threshold :
                tracking_settings_.override_ball_held_confidence_threshold;
            
            float color_threshold = (det.class_id == 0) ?
                tracking_settings_.override_ball_color_threshold :
                tracking_settings_.override_ball_held_color_threshold;
            
            bool meets_confidence = (det.confidence >= confidence_threshold);
            bool meets_color = (color_score >= color_threshold);
            bool meets_class = !tracking_settings_.override_require_ball_class || (det.class_id == 0);
            
            bool would_override = meets_confidence && meets_color && meets_class;
            
            if (would_override) {
                // OVERRIDE DETECTED - force ball to this detection
                ball.position = det.world_pos;
                ball.pixel_pos = cv::Point2f(det.box.x + det.box.width / 2.0f,
                                             det.box.y + det.box.height / 2.0f);
                ball.bbox = det.box;
                ball.has_yolo_detection = true;
                ball.yolo_confidence = det.confidence;
                ball.yolo_class_id = det.class_id;
                ball.color_match_score = color_score;
                ball.tracking_reason = "OVERRIDE_forced";
                
                // Mark this ball as overridden so we skip normal update logic
                overridden_balls.insert(ball.id);
                
                DEBUG_LOG(debug_log, {
                    OPEN_DEBUG_LOG(debug_log);
                    debug_log << "[OVERRIDE] Ball " << ball.id << " (" << ball.color_name
                              << ") forced to detection: conf=" << det.confidence
                              << ", color=" << color_score << std::endl;
                    debug_log.close();
                });
                
                break;  // Found override for this ball, stop checking detections
            }
        }
    }

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
    std::cout << "[FRAME " << frame_counter_ << "] Running pose estimation..." << std::endl;
    std::vector<SimpleHand> hands = runPoseEstimation(color_frame, depth_frame, intrinsics);
    std::cout << "[FRAME " << frame_counter_ << "] Pose estimation complete: " << hands.size() << " hands" << std::endl;
    
    // Update hand velocity for all detected hands
    // CRITICAL FIX: Transfer position_history from last frame's hands to preserve velocity tracking
    uint64_t current_timestamp = getCurrentTimestamp();
    for (auto& hand : hands) {
        if (hand.is_visible && tracking_settings_.hand_velocity_enabled) {
            // Find matching hand from last frame to transfer position history
            for (const auto& last_hand : last_known_hands_) {
                if (last_hand.id == hand.id) {
                    // Transfer position history and timestamp from last frame
                    hand.position_history = last_hand.position_history;
                    hand.last_update_timestamp = last_hand.last_update_timestamp;
                    hand.velocity = last_hand.velocity;
                    hand.has_valid_velocity = last_hand.has_valid_velocity;
                    break;
                }
            }
            updateHandVelocity(hand, current_timestamp);
        }
    }
    
    // HAND PERSISTENCE: Fill in missing hands with last known positions
    // This prevents tracking issues when a hand temporarily disappears from pose detection
    std::set<int> detected_hand_ids;
    for (const auto& hand : hands) {
        detected_hand_ids.insert(hand.id);
    }
    
    // Check if we have last known hands and if any are missing
    if (!last_known_hands_.empty()) {
        for (const auto& last_hand : last_known_hands_) {
            // If this hand wasn't detected this frame, add it with last known position
            if (detected_hand_ids.find(last_hand.id) == detected_hand_ids.end()) {
                SimpleHand persisted_hand = last_hand;
                persisted_hand.is_visible = false;  // Mark as not freshly detected
                hands.push_back(persisted_hand);
                
                DEBUG_LOG(hand_persist_log, {
                    OPEN_DEBUG_LOG(hand_persist_log);
                    hand_persist_log << "\n[HAND_PERSIST] Frame " << frame_counter_
                                    << " | Hand " << last_hand.id << " (" << (last_hand.id == 0 ? "LEFT" : "RIGHT") << ")"
                                    << " not detected - using last known position: ("
                                    << last_hand.wrist_pos_3d.x << ", "
                                    << last_hand.wrist_pos_3d.y << ", "
                                    << last_hand.wrist_pos_3d.z << ") m" << std::endl;
                });
            }
        }
    }
    
    // Update last known hands with current detections (only freshly detected ones)
    last_known_hands_.clear();
    for (const auto& hand : hands) {
        if (hand.is_visible) {  // Only store freshly detected hands
            last_known_hands_.push_back(hand);
        }
    }
    
    hands_ = hands;  // Store for getters

    DEBUG_LOG(debug_log, {
        OPEN_DEBUG_LOG(debug_log);
        debug_log << "Hands detected: " << hands.size() << std::endl;
        for (const auto& hand : hands) {
            debug_log << "  Hand " << hand.id << " (" << (hand.id == 0 ? "LEFT" : "RIGHT")
                     << "): pos=(" << hand.wrist_pos_3d.x << ", " << hand.wrist_pos_3d.y
                     << ", " << hand.wrist_pos_3d.z << "), visible=" << hand.is_visible
                     << (hand.is_visible ? " (FRESH)" : " (PERSISTED)") << std::endl;
        }
        debug_log.close();
    });
    
    // OPTIMIZATION: Don't convert entire frame to HSV - only convert ROIs as needed
    // This saves ~5-8% FPS by avoiding redundant pixel conversions
    // hsv_frame is now passed as color_frame, and matchColor/searchForColorBlob
    // will convert only the regions they need
    
    // Track which detections are used
    std::set<int> used_detections;
    
    // CRITICAL: Reset detection flags at start of frame
    // This prevents balls from keeping stale "has_yolo_detection" flags
    // NOTE: was_just_thrown_by_hand_id is NOT reset here - it's reset when:
    //   1. A catch is prevented (so it can be caught next frame)
    //   2. A catch happens to a different hand
    //   3. Ball stays in flight for multiple frames
    for (auto& ball : balls_) {
        ball.has_yolo_detection = false;
    }
    
    // DEBUG: Log ball initialization status
    DEBUG_LOG(debug_log, {
        OPEN_DEBUG_LOG(debug_log);
        debug_log << "\n[DEBUG] Balls after initialization: " << balls_.size() << std::endl;
        for (const auto& ball : balls_) {
            debug_log << "  Ball " << ball.id << " (" << ball.color_name << "): "
                      << "state=" << (ball.state == HELD ? "HELD" : "IN_FLIGHT")
                      << ", held_by_hand=" << ball.held_by_hand_id << std::endl;
        }
        debug_log.close();
    });
    
    // CRITICAL: Increment flight frame counter for ALL in-flight balls BEFORE any processing
    // This ensures the cooldown counter works for all code paths (override, normal, etc.)
    for (auto& ball : balls_) {
        if (ball.state == IN_FLIGHT && ball.last_throwing_hand_id >= 0) {
            ball.frames_in_flight_since_throw++;
            
            DEBUG_LOG(debug_log, {
                OPEN_DEBUG_LOG(debug_log);
                debug_log << "[COOLDOWN] Ball " << ball.id << " in flight: frames_since_throw="
                          << ball.frames_in_flight_since_throw << ", last_throwing_hand="
                          << ball.last_throwing_hand_id << std::endl;
                debug_log.close();
            });
        }
    }
    
    // CRITICAL: Process each ball based on its state
    // BUT skip balls that have been overridden - they're already positioned correctly
    for (auto& ball : balls_) {
        // Skip if this ball was overridden
        if (overridden_balls.find(ball.id) != overridden_balls.end()) {
            DEBUG_LOG(debug_log, {
                OPEN_DEBUG_LOG(debug_log);
                debug_log << "  Skipping update for ball " << ball.id
                          << " - already positioned by override" << std::endl;
                debug_log.close();
            });
            
            // CRITICAL FIX: When ball is overridden, we must still update its state
            // based on DISTANCE to hands, NOT YOLO class_id
            // This prevents incorrect state assignments from YOLO misclassifications
            
            // Calculate distance from override detection position to each hand
            float min_dist = std::numeric_limits<float>::max();
            int closest_hand_id = -1;
            
            for (const auto& h : hands_) {
                if (!h.is_visible) continue;
                float dist = cv::norm(ball.position - h.wrist_pos_3d);
                if (dist < min_dist) {
                    min_dist = dist;
                    closest_hand_id = h.id;
                }
            }
            
            // Use distance-based state determination (ignore YOLO class_id)
            // If distance < hand_distance_threshold: HELD state, assign to that hand
            // If distance >= hand_distance_threshold: IN_FLIGHT state
            if (closest_hand_id >= 0 && min_dist < tracking_settings_.hand_distance_threshold) {
                // Ball is near a hand - check if catch is allowed
                
                // CRITICAL: Check if this is the hand that threw the ball
                // Apply the SAME cooldown logic as normal catch detection
                bool was_in_flight = (ball.previous_state == IN_FLIGHT);
                
                if (was_in_flight) {
                    // Ball was in flight - check cooldown before allowing catch
                    
                    // CRITICAL: GLOBAL min_frames_before_catch RULE
                    // A hand that just threw the ball CANNOT catch it again until min_frames_before_catch frames have passed
                    if (ball.last_throwing_hand_id >= 0 &&
                        closest_hand_id == ball.last_throwing_hand_id) {
                        bool has_enough_flight_frames = (ball.frames_in_flight_since_throw >= tracking_settings_.min_frames_before_catch);
                        
                        DEBUG_LOG(debug_log, {
                            OPEN_DEBUG_LOG(debug_log);
                            debug_log << "  [OVERRIDE CATCH CHECK] Ball was thrown by hand " << ball.last_throwing_hand_id << std::endl;
                            debug_log << "    Current hand: " << closest_hand_id << std::endl;
                            debug_log << "    Frames in flight since throw: " << ball.frames_in_flight_since_throw << std::endl;
                            debug_log << "    Min frames required: " << tracking_settings_.min_frames_before_catch << std::endl;
                            debug_log << "    Has enough flight frames: " << (has_enough_flight_frames ? "YES" : "NO") << std::endl;
                            debug_log.close();
                        });
                        
                        if (!has_enough_flight_frames) {
                            // CATCH BLOCKED: Same hand that threw, not enough frames have passed
                            ball.state = IN_FLIGHT;
                            ball.is_held = false;
                            
                            DEBUG_LOG(debug_log, {
                                OPEN_DEBUG_LOG(debug_log);
                                debug_log << "  ✗ OVERRIDE CATCH BLOCKED: Same hand that threw, only "
                                          << ball.frames_in_flight_since_throw << " frames in flight (need "
                                          << tracking_settings_.min_frames_before_catch << "+)" << std::endl;
                                debug_log.close();
                            });
                            
                            continue;  // Skip to next ball, don't allow catch
                        }
                        
                        DEBUG_LOG(debug_log, {
                            OPEN_DEBUG_LOG(debug_log);
                            debug_log << "  ✓ OVERRIDE CATCH ALLOWED: " << ball.frames_in_flight_since_throw << " frames passed" << std::endl;
                            debug_log.close();
                        });
                    }
                    
                    // Catch is allowed - find the catching hand and initiate catch
                    // CRITICAL: Check against last_throwing_hand_id (GLOBAL RULE), not legacy was_just_thrown_by_hand_id
                    if (closest_hand_id != ball.last_throwing_hand_id) {
                        // Clear the legacy throw tracking since we're catching with a different hand
                        ball.was_just_thrown_by_hand_id = -1;
                        // Find the catching hand
                        const SimpleHand* catching_hand = nullptr;
                        for (const auto& h : hands_) {
                            if (h.id == closest_hand_id) {
                                catching_hand = &h;
                                break;
                            }
                        }
                        
                        if (catching_hand) {
                            // Generate catch event using initiateCatch
                            DEBUG_LOG(debug_log, {
                                OPEN_DEBUG_LOG(debug_log);
                                debug_log << "  ★★★ [CATCH LOCATION 1: OVERRIDE PATH] ★★★" << std::endl;
                                debug_log << "  Override: Ball transitioning IN_FLIGHT→HELD via initiateCatch() (distance-based: hand "
                                          << closest_hand_id << ", dist=" << min_dist << "m < threshold="
                                          << tracking_settings_.hand_distance_threshold << "m)" << std::endl;
                                debug_log << "  Ball " << ball.id << " caught by hand " << catching_hand->id << std::endl;
                                debug_log << "  last_throwing_hand_id: " << ball.last_throwing_hand_id << std::endl;
                                debug_log << "  frames_in_flight_since_throw: " << ball.frames_in_flight_since_throw << std::endl;
                                debug_log << "  min_frames_before_catch: " << tracking_settings_.min_frames_before_catch << std::endl;
                                debug_log.close();
                            });
                            initiateCatch(ball, *catching_hand, events);
                        } else {
                            // Fallback: just set state without event (shouldn't happen)
                            ball.state = HELD;
                            ball.is_held = true;
                            ball.held_by_hand_id = closest_hand_id;
                            
                            DEBUG_LOG(debug_log, {
                                OPEN_DEBUG_LOG(debug_log);
                                debug_log << "  Override: Ball set to HELD without event (hand not found)" << std::endl;
                                debug_log.close();
                            });
                        }
                    } else {
                        // Legacy check for was_just_thrown_by_hand_id (should not happen with new cooldown logic)
                        DEBUG_LOG(debug_log, {
                            OPEN_DEBUG_LOG(debug_log);
                            debug_log << "  Override: Legacy throw tracking prevented catch (was_just_thrown_by="
                                      << ball.was_just_thrown_by_hand_id << ")" << std::endl;
                            debug_log.close();
                        });
                        ball.state = IN_FLIGHT;
                        ball.is_held = false;
                        ball.was_just_thrown_by_hand_id = -1;
                    }
                } else {
                    // Ball was not in flight (was HELD) - just update state
                    ball.state = HELD;
                    ball.is_held = true;
                    ball.held_by_hand_id = closest_hand_id;
                    
                    DEBUG_LOG(debug_log, {
                        OPEN_DEBUG_LOG(debug_log);
                        debug_log << "  Override: Ball set to HELD (distance-based: hand "
                                  << closest_hand_id << ", dist=" << min_dist << "m < threshold="
                                  << tracking_settings_.hand_distance_threshold << "m)" << std::endl;
                        debug_log.close();
                    });
                }
            } else {
                // Ball is far from hands - transition to IN_FLIGHT
                bool was_held = (ball.state == HELD);
                
                ball.state = IN_FLIGHT;
                ball.is_held = false;
                
                DEBUG_LOG(debug_log, {
                    OPEN_DEBUG_LOG(debug_log);
                    debug_log << "  Override: Ball set to IN_FLIGHT (distance-based: ";
                    if (closest_hand_id >= 0) {
                        debug_log << "dist=" << min_dist << "m >= threshold="
                                  << tracking_settings_.hand_distance_threshold << "m)";
                    } else {
                        debug_log << "no visible hands)";
                    }
                    debug_log << std::endl;
                    debug_log << "  Was previously HELD: " << (was_held ? "YES" : "NO") << std::endl;
                    debug_log << "  Ball position: (" << ball.position.x << ", " << ball.position.y << ", " << ball.position.z << ")" << std::endl;
                    debug_log.close();
                });
                
                // If transitioning from HELD to IN_FLIGHT, generate throw event
                if (was_held) {
                    // Find the hand that was holding the ball
                    const SimpleHand* throwing_hand = nullptr;
                    for (const auto& h : hands_) {
                        if (h.id == ball.held_by_hand_id) {
                            throwing_hand = &h;
                            break;
                        }
                    }
                    
                    // Create a Detection struct from the override detection for initiateThrow
                    Detection throw_detection;
                    throw_detection.world_pos = ball.position;
                    throw_detection.box = ball.bbox;
                    throw_detection.confidence = ball.yolo_confidence;
                    throw_detection.class_id = ball.yolo_class_id;
                    
                    // Generate throw event using initiateThrow
                    initiateThrow(ball, throw_detection, throwing_hand, events);
                    
                    DEBUG_LOG(debug_log, {
                        OPEN_DEBUG_LOG(debug_log);
                        debug_log << "  THROW EVENT GENERATED via override logic (HELD→IN_FLIGHT transition)" << std::endl;
                        debug_log << "  Hand ID: " << ball.held_by_hand_id << std::endl;
                        debug_log.close();
                    });
                }
            }
            
            // Add trajectory point for overridden in-flight balls (only if actually IN_FLIGHT)
            if (ball.state == IN_FLIGHT && ball.position.z > 0) {
                uint64_t current_timestamp = getCurrentTimestamp();
                addVerifiedPoint(ball, ball.position, current_timestamp);
                
                // CRITICAL: Recalculate prediction immediately after adding point
                // This ensures prediction is valid for rendering in recordings
                // Need >= 3 points (not > 3) to start prediction
                if (ball.trajectory.verified_point_count >= tracking_settings_.traj_min_points_for_prediction) {
                    predictFullTrajectory(ball);
                }
                
                DEBUG_LOG(debug_log, {
                    OPEN_DEBUG_LOG(debug_log);
                    debug_log << "  Added trajectory point for overridden ball: #"
                              << ball.trajectory.verified_point_count << std::endl;
                    if (ball.trajectory.verified_point_count >= tracking_settings_.traj_min_points_for_prediction) {
                        debug_log << "  Prediction recalculated: valid=" << ball.trajectory.prediction_valid
                                  << ", path_size=" << ball.trajectory.predicted_path.size() << std::endl;
                    }
                    debug_log.close();
                });
            }
            
            continue;  // Skip normal update logic
        }
        
        std::cout << "[FRAME " << frame_counter_ << "] Updating ball " << ball.id
                  << " (" << ball.color_name << ") state="
                  << (ball.state == HELD ? "HELD" : "IN_FLIGHT") << std::endl;
        
        if (ball.state == HELD) {
            updateHeldBall(ball, hands, yolo_detections, color_frame, depth_frame, intrinsics, events);
        } else {  // IN_FLIGHT
            updateInFlightBall(ball, yolo_detections, color_frame, depth_frame, intrinsics, events);
        }
        
        std::cout << "[FRAME " << frame_counter_ << "] Ball " << ball.id << " update complete" << std::endl;
    }
    
    // DEBUG: Log ball positions after update
    DEBUG_LOG(debug_log, {
        OPEN_DEBUG_LOG(debug_log);
        debug_log << "\n[DEBUG] Balls after update: " << balls_.size() << std::endl;
        for (const auto& ball : balls_) {
            debug_log << "  Ball " << ball.id << " (" << ball.color_name << "): "
                      << "state=" << (ball.state == HELD ? "HELD" : "IN_FLIGHT")
                      << ", pos=(" << ball.position.x << "," << ball.position.y << "," << ball.position.z << ")"
                      << ", pixel_pos=(" << ball.pixel_pos.x << "," << ball.pixel_pos.y << ")"
                      << ", reason=" << ball.tracking_reason << std::endl;
        }
        debug_log.close();
    });
    
    // NOTE: Call drawTrajectory() externally for each ball to visualize trajectories
    
    // Detect ball states and events (additional events from state detection)
    std::vector<BallEvent> additional_events = detectStatesAndEvents(balls_, hands);
    events.insert(events.end(), additional_events.begin(), additional_events.end());
    
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
    
    std::cout << "[FRAME " << frame_counter_ << "] Update complete, returning results" << std::endl;
    
    return {balls_, events};
}

bool SimpleBallTracker::calibrateColor(const std::string& color_name,
                                      cv::Point click_point,
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
    
    // GPU-ACCELERATED: Convert small ROI around detection center to HSV using GPU
    // (calibrateColor needs a 5x5 sample for averaging)
    
    // Calculate center of bounding box
    int center_x = static_cast<int>(clicked_det->box.x + clicked_det->box.width / 2.0f);
    int center_y = static_cast<int>(clicked_det->box.y + clicked_det->box.height / 2.0f);
    
    // Sample 5x5 pixel square from the center
    const int sample_size = 5;
    const int half_size = sample_size / 2;
    
    // Create ROI for the 5x5 sample area
    int roi_x = std::max(0, center_x - half_size);
    int roi_y = std::max(0, center_y - half_size);
    int roi_width = std::min(last_color_frame_.cols - roi_x, sample_size);
    int roi_height = std::min(last_color_frame_.rows - roi_y, sample_size);
    
    cv::Rect sample_roi(roi_x, roi_y, roi_width, roi_height);
    cv::Mat hsv_roi = gpu_hsv_converter_->convertRoiToHsv(last_color_frame_, sample_roi);
    
    std::vector<float> hue_samples;
    std::vector<float> sat_samples;
    
    // Sample from the converted HSV ROI
    for (int dy = 0; dy < hsv_roi.rows; ++dy) {
        for (int dx = 0; dx < hsv_roi.cols; ++dx) {
            cv::Vec3b hsv = hsv_roi.at<cv::Vec3b>(dy, dx);
            hue_samples.push_back(static_cast<float>(hsv[0]));
            sat_samples.push_back(static_cast<float>(hsv[1]));
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
    
    // PERFORMANCE FIX: Manual blob conversion instead of cv::dnn::blobFromImage
    // cv::dnn::blobFromImage is extremely slow (~30ms) because it does unnecessary operations
    // Manual conversion matches the Python test script and is much faster (~2ms)
    // Convert HWC (Height, Width, Channels) to CHW (Channels, Height, Width) format
    std::vector<cv::Mat> channels(3);
    cv::split(float_frame, channels);
    
    // Create output blob in NCHW format: [1, 3, 640, 640]
    cv::Mat blob(1, 3 * input_height_ * input_width_, CV_32F);
    
    // Copy each channel sequentially: B, G, R -> becomes C dimension
    int channel_size = input_height_ * input_width_;
    for (int c = 0; c < 3; c++) {
        std::memcpy(blob.ptr<float>() + c * channel_size,
                   channels[c].ptr<float>(),
                   channel_size * sizeof(float));
    }
    
    // Reshape to [1, 3, 640, 640]
    return blob.reshape(1, {1, 3, input_height_, input_width_});
}

std::vector<Detection> SimpleBallTracker::runBallDetection(
    const cv::Mat& color_frame,
    const cv::Mat& depth_frame,
    const CameraIntrinsics& intrinsics) {
    
    // Store color frame for calibration
    last_color_frame_ = color_frame.clone();
    
    // If ball detection is disabled, return empty vector
    if (!enable_ball_detection_) {
        return std::vector<Detection>();
    }
    
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
        int class_id = class_id_point.x;
        
        // Apply class-specific confidence thresholds
        float threshold = (class_id == 0) ? ball_confidence_threshold_ : ball_held_confidence_threshold_;
        
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
                float depth_value_m = get_filtered_depth(depth_frame, center_pixel);
                world_pos = deprojectToWorld(center_pixel, depth_value_m, intrinsics);
            }
            
            Detection det;
            det.box = cv::Rect_<float>(left, top, width, height);
            det.world_pos = world_pos;
            det.confidence = confidence;
            det.class_id = class_id;
            det.index = raw_detections.size();
            raw_detections.push_back(det);
        }
    }
    
    // Apply NMS - use minimum of both thresholds for NMS filtering
    std::vector<int> nms_indices;
    float min_threshold = std::min(ball_confidence_threshold_, ball_held_confidence_threshold_);
    cv::dnn::NMSBoxes(boxes, confidences, min_threshold, nms_threshold_, nms_indices);
    
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
    
    // NOTE: last_raw_detections_ is now set in update() AFTER evaluateOverrideCriteria()
    // so that override_evals are included in the stored detections
    
    return filtered_detections;
}

std::vector<SimpleHand> SimpleBallTracker::runPoseEstimation(
    const cv::Mat& color_frame,
    const cv::Mat& depth_frame,
    const CameraIntrinsics& intrinsics) {
    
    // If pose detection is disabled, return empty vector
    if (!enable_pose_detection_) {
        return std::vector<SimpleHand>();
    }
    
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
        // Extract bounding box (not used for hand tracking, only keypoints)
        // float cx = output_buffer.at<float>(i, 0);
        // float cy = output_buffer.at<float>(i, 1);
        // float w = output_buffer.at<float>(i, 2);
        // float h = output_buffer.at<float>(i, 3);
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

// ============================================================================
// TRAJECTORY-BASED TRACKING METHODS (Phase 3)
// ============================================================================

void SimpleBallTracker::addVerifiedPoint(SimpleBall& ball, const cv::Point3f& position, uint64_t timestamp) {
    TrajectoryPoint point;
    point.position = position;
    point.timestamp = timestamp;
    point.verified = true;
    point.confidence = 1.0f;
    
    ball.trajectory.points.push_back(point);
    ball.trajectory.verified_point_count++;
    
    // Update confidence and search radius
    gpu_trajectory_predictor_->updateConfidence(
        ball.trajectory,
        tracking_settings_.points_for_full_confidence,
        tracking_settings_.min_search_radius,
        tracking_settings_.initial_search_radius
    );
}

void SimpleBallTracker::updateInFlightBall(
    SimpleBall& ball,
    const std::vector<Detection>& detections,
    const cv::Mat& color_frame,
    const cv::Mat& depth_frame,
    const CameraIntrinsics& intrinsics,
    std::vector<BallEvent>& events) {
    
    // NOTE: Flight frame counter is now incremented globally in update() before ball processing
    // This ensures it works for all code paths (override, normal, etc.)
    DEBUG_LOG(debug_log, {
        OPEN_DEBUG_LOG(debug_log);
        debug_log << "\n[updateInFlightBall] Ball " << ball.id << " (" << ball.color_name << ")" << std::endl;
        if (ball.last_throwing_hand_id >= 0) {
            debug_log << "  GLOBAL COOLDOWN RULE ACTIVE:" << std::endl;
            debug_log << "    last_throwing_hand_id: " << ball.last_throwing_hand_id << std::endl;
            debug_log << "    frames_in_flight_since_throw: " << ball.frames_in_flight_since_throw << std::endl;
            debug_log << "    min_frames_before_catch: " << tracking_settings_.min_frames_before_catch << std::endl;
            debug_log << "    (Hand " << ball.last_throwing_hand_id << " cannot catch until "
                      << tracking_settings_.min_frames_before_catch << "+ frames)" << std::endl;
        }
        debug_log.close();
    });
    
    DEBUG_LOG(debug_log, {
        OPEN_DEBUG_LOG(debug_log);
        debug_log << "  verified_point_count: " << ball.trajectory.verified_point_count << std::endl;
        debug_log << "  frames_without_verified_detection: " << ball.frames_without_verified_detection << std::endl;
        debug_log << "  unverified_trajectory_points: " << ball.unverified_trajectory_points << std::endl;
        debug_log.close();
    });
    
    // LOCKUP PREVENTION: Check if ball has been lost for too long
    // If we've gone 90 frames (~3 seconds at 30fps) without a verified detection, force a catch
    const int MAX_FRAMES_WITHOUT_DETECTION = 90;
    const int MAX_UNVERIFIED_POINTS = 30;  // ~1 second at 30fps
    
    if (ball.frames_without_verified_detection > MAX_FRAMES_WITHOUT_DETECTION) {
        DEBUG_LOG(debug_log, {
            OPEN_DEBUG_LOG(debug_log);
            debug_log << "  LOCKUP PREVENTION: Ball lost for " << ball.frames_without_verified_detection
                      << " frames (>" << MAX_FRAMES_WITHOUT_DETECTION << ") - forcing catch to nearest hand" << std::endl;
            debug_log.close();
        });
        
        // Find nearest hand and force catch
        if (!hands_.empty()) {
            float min_dist = std::numeric_limits<float>::max();
            const SimpleHand* nearest_hand = nullptr;
            
            for (const auto& hand : hands_) {
                if (!hand.is_visible) continue;
                float dist = cv::norm(ball.position - hand.wrist_pos_3d);
                if (dist < min_dist) {
                    min_dist = dist;
                    nearest_hand = &hand;
                }
            }
            
            if (nearest_hand) {
                DEBUG_LOG(debug_log, {
                    OPEN_DEBUG_LOG(debug_log);
                    debug_log << "  ★★★ [CATCH LOCATION 5: LOCKUP PREVENTION - MAX FRAMES] ★★★" << std::endl;
                    debug_log << "  FORCED CATCH to hand " << nearest_hand->id << " (dist=" << min_dist << "m)" << std::endl;
                    debug_log << "  Ball " << ball.id << " caught by hand " << nearest_hand->id << std::endl;
                    debug_log << "  last_throwing_hand_id: " << ball.last_throwing_hand_id << std::endl;
                    debug_log << "  frames_in_flight_since_throw: " << ball.frames_in_flight_since_throw << std::endl;
                    debug_log << "  min_frames_before_catch: " << tracking_settings_.min_frames_before_catch << std::endl;
                    debug_log.close();
                });
                initiateCatch(ball, *nearest_hand, events);
                return;
            }
        }
        
        // If no hands available, reset trajectory and keep at last position
        ball.trajectory.points.clear();
        ball.trajectory.verified_point_count = 0;
        ball.frames_without_verified_detection = 0;
        ball.unverified_trajectory_points = 0;
        ball.tracking_reason = "IN_FLIGHT_timeout_reset";
        
        DEBUG_LOG(debug_log, {
            OPEN_DEBUG_LOG(debug_log);
            debug_log << "  No hands available - trajectory reset, keeping last position" << std::endl;
            debug_log.close();
        });
        return;
    }
    
    // LOCKUP PREVENTION: Check if we've accumulated too many unverified points
    if (ball.unverified_trajectory_points > MAX_UNVERIFIED_POINTS) {
        DEBUG_LOG(debug_log, {
            OPEN_DEBUG_LOG(debug_log);
            debug_log << "  LOCKUP PREVENTION: Too many unverified points (" << ball.unverified_trajectory_points
                      << " > " << MAX_UNVERIFIED_POINTS << ") - forcing catch" << std::endl;
            debug_log.close();
        });
        
        // Find nearest hand within reasonable distance
        if (!hands_.empty()) {
            float min_dist = std::numeric_limits<float>::max();
            const SimpleHand* nearest_hand = nullptr;
            
            for (const auto& hand : hands_) {
                if (!hand.is_visible) continue;
                float dist = cv::norm(ball.position - hand.wrist_pos_3d);
                if (dist < min_dist) {
                    min_dist = dist;
                    nearest_hand = &hand;
                }
            }
            
            // Force catch if hand is within 3x max_tracker_distance
            if (nearest_hand && min_dist < tracking_settings_.max_tracker_distance_per_frame * 3.0f) {
                DEBUG_LOG(debug_log, {
                    OPEN_DEBUG_LOG(debug_log);
                    debug_log << "  ★★★ [CATCH LOCATION 6: LOCKUP PREVENTION - MAX UNVERIFIED] ★★★" << std::endl;
                    debug_log << "  FORCED CATCH to hand " << nearest_hand->id << " (dist=" << min_dist << "m)" << std::endl;
                    debug_log << "  Ball " << ball.id << " caught by hand " << nearest_hand->id << std::endl;
                    debug_log << "  last_throwing_hand_id: " << ball.last_throwing_hand_id << std::endl;
                    debug_log << "  frames_in_flight_since_throw: " << ball.frames_in_flight_since_throw << std::endl;
                    debug_log << "  min_frames_before_catch: " << tracking_settings_.min_frames_before_catch << std::endl;
                    debug_log.close();
                });
                initiateCatch(ball, *nearest_hand, events);
                return;
            }
        }
        
        // If no suitable hand, reset trajectory
        ball.trajectory.points.clear();
        ball.trajectory.verified_point_count = 0;
        ball.frames_without_verified_detection = 0;
        ball.unverified_trajectory_points = 0;
        ball.tracking_reason = "IN_FLIGHT_unverified_limit_reset";
        
        DEBUG_LOG(debug_log, {
            OPEN_DEBUG_LOG(debug_log);
            debug_log << "  No suitable hand - trajectory reset" << std::endl;
            debug_log.close();
        });
        return;
    }
    
    // Step 1: Determine prediction strategy based on point count
    int point_count = ball.trajectory.verified_point_count;
    cv::Point3f predicted_next;
    bool use_prediction = false;
    
    if (point_count == 0) {
        // CRITICAL FALLBACK: No trajectory points - force catch to nearest hand
        // This ensures we ALWAYS have a tracker position
        DEBUG_LOG(debug_log, {
            OPEN_DEBUG_LOG(debug_log);
            debug_log << "  CRITICAL: No trajectory points - forcing catch to nearest hand" << std::endl;
            debug_log.close();
        });
        
        // Find nearest hand and force catch
        if (!hands_.empty()) {
            float min_dist = std::numeric_limits<float>::max();
            const SimpleHand* nearest_hand = nullptr;
            
            for (const auto& hand : hands_) {
                if (!hand.is_visible) continue;
                float dist = cv::norm(ball.position - hand.wrist_pos_3d);
                if (dist < min_dist) {
                    min_dist = dist;
                    nearest_hand = &hand;
                }
            }
            
            if (nearest_hand) {
                DEBUG_LOG(debug_log, {
                    OPEN_DEBUG_LOG(debug_log);
                    debug_log << "  ★★★ [CATCH LOCATION 7: CRITICAL FALLBACK - NO POINTS] ★★★" << std::endl;
                    debug_log << "  FORCED CATCH to hand " << nearest_hand->id << " (dist=" << min_dist << "m)" << std::endl;
                    debug_log << "  Ball " << ball.id << " caught by hand " << nearest_hand->id << std::endl;
                    debug_log << "  last_throwing_hand_id: " << ball.last_throwing_hand_id << std::endl;
                    debug_log << "  frames_in_flight_since_throw: " << ball.frames_in_flight_since_throw << std::endl;
                    debug_log << "  min_frames_before_catch: " << tracking_settings_.min_frames_before_catch << std::endl;
                    debug_log.close();
                });
                initiateCatch(ball, *nearest_hand, events);
                return;
            }
        }
        
        // Absolute last resort: keep ball at last known position
        ball.tracking_reason = "IN_FLIGHT_no_points_no_hands";
        return;
    }
    else if (point_count == 1) {
        // CRITICAL: With only 1 point, we can't predict - just search near last position
        predicted_next = ball.trajectory.points[0].position;
        use_prediction = false;
        
        DEBUG_LOG(debug_log, {
            OPEN_DEBUG_LOG(debug_log);
            debug_log << "  Only 1 point - no prediction yet, searching near last position" << std::endl;
            debug_log.close();
        });
    }
    else if (point_count == 2) {
        // Use two points for linear prediction
        predicted_next = predictWithTwoPoints(ball);
        use_prediction = true;
        
        DEBUG_LOG(debug_log, {
            OPEN_DEBUG_LOG(debug_log);
            debug_log << "  Using TWO POINT prediction" << std::endl;
            debug_log.close();
        });
    }
    else {  // point_count >= 3
        // Use full physics-based trajectory
        std::vector<cv::Point3f> predicted_path = predictFullTrajectory(ball);
        
        if (!predicted_path.empty()) {
            predicted_next = predicted_path[0];  // Next frame prediction
            use_prediction = true;
        } else {
            predicted_next = ball.position;  // Fallback to current position
            use_prediction = false;
        }
        
        DEBUG_LOG(debug_log, {
            OPEN_DEBUG_LOG(debug_log);
            debug_log << "  Using FULL PHYSICS prediction (" << predicted_path.size() << " points)" << std::endl;
            debug_log.close();
        });
    }
    
    // Step 2: Search for detection along prediction line (or near last position if no prediction)
    float search_radius = use_prediction ? ball.trajectory.search_radius_m : 0.30f;  // Wider search if no prediction
    const Detection* detection = searchAlongPredictionLine(
        predicted_next,
        search_radius,
        detections,
        color_frame,
        ball.color_name
    );
    
    // Step 3: Handle detection result
    bool verified = false;
    uint64_t current_timestamp = getCurrentTimestamp();
    
    if (detection) {
        // YOLO detection found and verified
        ball.position = detection->world_pos;
        ball.pixel_pos = cv::Point2f(detection->box.x + detection->box.width / 2.0f,
                                     detection->box.y + detection->box.height / 2.0f);
        ball.bbox = detection->box;
        ball.has_yolo_detection = true;
        ball.yolo_confidence = detection->confidence;
        ball.yolo_class_id = detection->class_id;
        
        // Get color profile for color score
        const ColorProfile* profile = nullptr;
        for (const auto& p : color_profiles_) {
            if (p.name == ball.color_name && p.enabled) {
                profile = &p;
                break;
            }
        }
        if (profile) {
            ball.color_match_score = matchColor(*detection, *profile, color_frame);
        }
        
        ball.tracking_reason = "IN_FLIGHT_yolo_verified";
        verified = true;
        
        DEBUG_LOG(debug_log, {
            OPEN_DEBUG_LOG(debug_log);
            debug_log << "  YOLO detection verified!" << std::endl;
            debug_log.close();
        });
    }
    else {
        // No YOLO detection - use color blob fallback
        cv::Point2f predicted_2d = project_3d_to_2d(predicted_next, intrinsics);
        
        // Find color profile
        const ColorProfile* profile = nullptr;
        for (const auto& p : color_profiles_) {
            if (p.name == ball.color_name && p.enabled) {
                profile = &p;
                break;
            }
        }
        
        if (profile) {
            // Calculate search radius in pixels
            int search_radius_px = static_cast<int>(
                ball.trajectory.search_radius_m * intrinsics.fx / std::max(0.1f, predicted_next.z)
            );
            search_radius_px = std::min(search_radius_px, 200);
            
            cv::Point2f blob = searchForColorBlob(color_frame, *profile, predicted_2d, search_radius_px);
            
            if (blob.x > 0 && blob.y > 0) {
                float depth = getDepthAtPoint(depth_frame, blob);
                
                if (depth > MIN_DEPTH && depth < MAX_DEPTH) {
                    cv::Point3f blob_3d = deprojectToWorld(blob, depth, intrinsics);
                    
                    // Verify blob is within search distance
                    float dist = cv::norm(blob_3d - predicted_next);
                    if (dist < tracking_settings_.traj_max_search_distance) {
                        // TRAJECTORY CONSISTENCY CHECK: Verify the blob follows expected motion
                        bool trajectory_consistent = true;
                        
                        if (ball.trajectory.verified_point_count >= 2) {
                            // Get last known position and velocity
                            cv::Point3f last_pos = ball.trajectory.points.back().position;
                            cv::Point3f motion_vector = blob_3d - last_pos;
                            
                            // Calculate expected motion direction from velocity
                            cv::Point3f expected_direction = ball.trajectory.initial_velocity;
                            float expected_speed = cv::norm(expected_direction);
                            
                            if (expected_speed > 0.1f) {  // Only check if we have significant velocity
                                expected_direction = expected_direction / expected_speed;  // Normalize
                                
                                // Normalize actual motion
                                float actual_distance = cv::norm(motion_vector);
                                if (actual_distance > 0.01f) {
                                    cv::Point3f actual_direction = motion_vector / actual_distance;
                                    
                                    // Check if motion is in roughly the same direction (dot product)
                                    float direction_similarity = expected_direction.dot(actual_direction);
                                    
                                    // Reject if moving in opposite direction or perpendicular
                                    // (dot product < 0.5 means angle > 60 degrees)
                                    if (direction_similarity < 0.5f) {
                                        trajectory_consistent = false;
                                        
                                        DEBUG_LOG(debug_log, {
                                            OPEN_DEBUG_LOG(debug_log);
                                            debug_log << "  Color blob REJECTED: inconsistent trajectory direction" << std::endl;
                                            debug_log << "    Direction similarity: " << direction_similarity << " (need >= 0.5)" << std::endl;
                                            debug_log << "    Expected direction: (" << expected_direction.x << ", "
                                                      << expected_direction.y << ", " << expected_direction.z << ")" << std::endl;
                                            debug_log << "    Actual direction: (" << actual_direction.x << ", "
                                                      << actual_direction.y << ", " << actual_direction.z << ")" << std::endl;
                                            debug_log.close();
                                        });
                                    }
                                    
                                    // Also check if speed change is reasonable (not more than 3x or less than 0.3x)
                                    float time_delta = 0.033f;  // Assume 30 FPS
                                    float expected_distance = expected_speed * time_delta;
                                    float speed_ratio = actual_distance / expected_distance;
                                    
                                    if (speed_ratio > 3.0f || speed_ratio < 0.3f) {
                                        trajectory_consistent = false;
                                        
                                        DEBUG_LOG(debug_log, {
                                            OPEN_DEBUG_LOG(debug_log);
                                            debug_log << "  Color blob REJECTED: unreasonable speed change" << std::endl;
                                            debug_log << "    Speed ratio: " << speed_ratio << " (need 0.3-3.0)" << std::endl;
                                            debug_log << "    Expected distance: " << expected_distance << "m" << std::endl;
                                            debug_log << "    Actual distance: " << actual_distance << "m" << std::endl;
                                            debug_log.close();
                                        });
                                    }
                                }
                            }
                        }
                        
                        if (trajectory_consistent) {
                            ball.position = blob_3d;
                            ball.pixel_pos = blob;
                            
                            // CRITICAL: Set has_yolo_detection to TRUE for visualization
                            ball.has_yolo_detection = true;
                            ball.yolo_confidence = 0.6f;  // Good confidence for color blob
                            ball.yolo_class_id = 0;  // ball (in-flight) class
                            ball.tracking_reason = "IN_FLIGHT_color_blob";
                            
                            float bbox_size = 30.0f;
                            ball.bbox = cv::Rect_<float>(
                                blob.x - bbox_size/2, blob.y - bbox_size/2,
                                bbox_size, bbox_size
                            );
                            
                            verified = true;
                            
                            DEBUG_LOG(debug_log, {
                                OPEN_DEBUG_LOG(debug_log);
                                debug_log << "  Color blob found and trajectory consistent!" << std::endl;
                                debug_log.close();
                            });
                        }
                    }
                }
            }
        }
        
        // If still not verified, use predicted position
        if (!verified) {
            ball.position = predicted_next;
            ball.pixel_pos = project_3d_to_2d(predicted_next, intrinsics);
            
            // CRITICAL: Set has_yolo_detection to TRUE for visualization
            // Even though we're using prediction, we want the tracker to stay visible
            ball.has_yolo_detection = true;
            ball.yolo_confidence = 0.4f;  // Moderate-low confidence for prediction
            ball.yolo_class_id = 0;  // ball (in-flight) class
            ball.tracking_reason = "IN_FLIGHT_predicted";
            
            float bbox_size = 30.0f;
            ball.bbox = cv::Rect_<float>(
                ball.pixel_pos.x - bbox_size/2, ball.pixel_pos.y - bbox_size/2,
                bbox_size, bbox_size
            );
            
            DEBUG_LOG(debug_log, {
                OPEN_DEBUG_LOG(debug_log);
                debug_log << "  Using predicted position (no detection found)" << std::endl;
                debug_log.close();
            });
        }
    }
    
    // Step 4: Add verified point to trajectory ONLY if we have a real detection
    // When using predicted position, DON'T add it as a verified point
    if (verified && ball.position.z > 0) {
        addVerifiedPoint(ball, ball.position, current_timestamp);
        
        // CRITICAL: Recalculate prediction immediately after adding point
        // This ensures prediction is valid for rendering in recordings
        // Need >= 3 points (not > 3) to start prediction
        if (ball.trajectory.verified_point_count >= tracking_settings_.traj_min_points_for_prediction) {
            predictFullTrajectory(ball);
        }
        
        DEBUG_LOG(debug_log, {
            OPEN_DEBUG_LOG(debug_log);
            debug_log << "  Added verified point #" << ball.trajectory.verified_point_count << std::endl;
            if (ball.trajectory.verified_point_count >= tracking_settings_.traj_min_points_for_prediction) {
                debug_log << "  Prediction recalculated: valid=" << ball.trajectory.prediction_valid
                          << ", path_size=" << ball.trajectory.predicted_path.size() << std::endl;
            }
            debug_log.close();
        });
    } else if (!verified && ball.position.z > 0) {
        // CRITICAL: When we don't have a detection, we're using predicted position
        // Don't add this as a verified point, but DO update the ball position for rendering
        // The trajectory prediction will continue from the last verified point
        
        // CRITICAL FIX: Add the predicted position as an UNVERIFIED point
        // This allows the trajectory to advance even when we lose tracking
        // The prediction will use these unverified points to continue forward
        TrajectoryPoint predicted_point;
        predicted_point.position = predicted_next;
        predicted_point.timestamp = current_timestamp;
        predicted_point.verified = false;  // Mark as unverified
        predicted_point.confidence = 0.3f;  // Low confidence
        ball.trajectory.points.push_back(predicted_point);
        // Don't increment verified_point_count since this is unverified
        
        DEBUG_LOG(debug_log, {
            OPEN_DEBUG_LOG(debug_log);
            debug_log << "  Added UNVERIFIED trajectory point to prevent stuck prediction" << std::endl;
            debug_log << "  Total points: " << ball.trajectory.points.size()
                      << ", verified: " << ball.trajectory.verified_point_count << std::endl;
            debug_log << "  Frames without detection: " << ball.frames_without_verified_detection << std::endl;
            debug_log << "  Unverified points: " << ball.unverified_trajectory_points << std::endl;
            debug_log.close();
        });
        
        // NEW FIX: Check if ball is very close to any hand - if so, initiate catch immediately
        // This prevents tracker from getting stuck in air when ball returns to hand
        // and prevents spurious catch/throw events from the ball being snapped to hand while IN_FLIGHT
        for (const auto& hand : hands_) {
            if (!hand.is_visible) continue;
            
            float dist = cv::norm(predicted_next - hand.wrist_pos_3d);
            if (dist < 0.15f) {  // Very close to hand (15cm)
                // CRITICAL: Check catch cooldown before allowing catch
                if (ball.last_throwing_hand_id >= 0 &&
                    hand.id == ball.last_throwing_hand_id &&
                    ball.frames_in_flight_since_throw < tracking_settings_.min_frames_before_catch) {
                    
                    DEBUG_LOG(debug_log, {
                        OPEN_DEBUG_LOG(debug_log);
                        debug_log << "  ✗ NEAR-HAND CATCH BLOCKED: Same hand that threw, only "
                                  << ball.frames_in_flight_since_throw << " frames in flight (need "
                                  << tracking_settings_.min_frames_before_catch << "+)" << std::endl;
                        debug_log.close();
                    });
                    continue;  // Skip this hand, cooldown not satisfied
                }
                
                DEBUG_LOG(debug_log, {
                    OPEN_DEBUG_LOG(debug_log);
                    debug_log << "  ★★★ [CATCH LOCATION 2: NEAR-HAND FALLBACK] ★★★" << std::endl;
                    debug_log << "  NEAR-HAND FALLBACK: Ball within 0.15m of hand " << hand.id
                              << " (dist=" << dist << "m) - initiating catch" << std::endl;
                    debug_log << "  Ball " << ball.id << " caught by hand " << hand.id << std::endl;
                    debug_log << "  last_throwing_hand_id: " << ball.last_throwing_hand_id << std::endl;
                    debug_log << "  frames_in_flight_since_throw: " << ball.frames_in_flight_since_throw << std::endl;
                    debug_log << "  min_frames_before_catch: " << tracking_settings_.min_frames_before_catch << std::endl;
                    debug_log.close();
                });
                
                // Immediately transition to HELD state and generate catch event
                initiateCatch(ball, hand, events);
                return;  // Exit early, ball is now caught
            }
        }
    }
    
    // CRITICAL GUARANTEE: If ball position is still invalid (z <= 0), force catch to nearest hand
    // BUT ONLY if within max_tracker_distance_per_frame threshold
    if (ball.position.z <= 0 && !hands_.empty()) {
        float min_dist = std::numeric_limits<float>::max();
        const SimpleHand* nearest_hand = nullptr;
        
        // Use last verified position for distance check, or predicted_next if no verified points
        cv::Point3f reference_pos = predicted_next;
        if (!ball.trajectory.points.empty()) {
            for (int i = ball.trajectory.points.size() - 1; i >= 0; i--) {
                if (ball.trajectory.points[i].verified) {
                    reference_pos = ball.trajectory.points[i].position;
                    break;
                }
            }
        }
        
        for (const auto& hand : hands_) {
            if (!hand.is_visible) continue;
            float dist = cv::norm(reference_pos - hand.wrist_pos_3d);
            if (dist < min_dist) {
                min_dist = dist;
                nearest_hand = &hand;
            }
        }
        
        // CRITICAL FIX: Only force catch if within max_tracker_distance threshold
        if (nearest_hand && min_dist < tracking_settings_.max_tracker_distance_per_frame) {
            // CRITICAL: Check catch cooldown before allowing catch
            if (ball.last_throwing_hand_id >= 0 &&
                nearest_hand->id == ball.last_throwing_hand_id &&
                ball.frames_in_flight_since_throw < tracking_settings_.min_frames_before_catch) {
                
                DEBUG_LOG(debug_log, {
                    OPEN_DEBUG_LOG(debug_log);
                    debug_log << "  ✗ CRITICAL FALLBACK CATCH BLOCKED: Same hand that threw, only "
                              << ball.frames_in_flight_since_throw << " frames in flight (need "
                              << tracking_settings_.min_frames_before_catch << "+)" << std::endl;
                    debug_log.close();
                });
                
                // Don't force catch - keep ball at last known position instead
                ball.position = reference_pos;
                ball.pixel_pos = project_3d_to_2d(reference_pos, intrinsics);
                ball.tracking_reason = "IN_FLIGHT_fallback_blocked_cooldown";
                return;
            }
            
            DEBUG_LOG(debug_log, {
                OPEN_DEBUG_LOG(debug_log);
                debug_log << "  ★★★ [CATCH LOCATION 8: CRITICAL FALLBACK - INVALID POSITION] ★★★" << std::endl;
                debug_log << "  CRITICAL FALLBACK: Invalid position (z=" << ball.position.z
                          << ") - forcing catch to nearest hand " << nearest_hand->id
                          << " (dist=" << min_dist << "m < max="
                          << tracking_settings_.max_tracker_distance_per_frame << "m)" << std::endl;
                debug_log << "  Ball " << ball.id << " caught by hand " << nearest_hand->id << std::endl;
                debug_log << "  last_throwing_hand_id: " << ball.last_throwing_hand_id << std::endl;
                debug_log << "  frames_in_flight_since_throw: " << ball.frames_in_flight_since_throw << std::endl;
                debug_log << "  min_frames_before_catch: " << tracking_settings_.min_frames_before_catch << std::endl;
                debug_log.close();
            });
            initiateCatch(ball, *nearest_hand, events);
            return;
        } else if (nearest_hand) {
            DEBUG_LOG(debug_log, {
                OPEN_DEBUG_LOG(debug_log);
                debug_log << "  CRITICAL FALLBACK REJECTED: Nearest hand " << nearest_hand->id
                          << " is too far (dist=" << min_dist << "m >= max="
                          << tracking_settings_.max_tracker_distance_per_frame << "m)" << std::endl;
                debug_log << "  Keeping ball at last known position instead" << std::endl;
                debug_log.close();
            });
            // Keep ball at last known position - don't force invalid catch
            ball.position = reference_pos;
            ball.pixel_pos = project_3d_to_2d(reference_pos, intrinsics);
            ball.tracking_reason = "IN_FLIGHT_fallback_rejected_too_far";
            return;
        }
    }
    
    // Step 5: Check for catch - ball must be IN_FLIGHT and close to hand
    // CRITICAL: Only balls that are IN_FLIGHT can be caught!
    // NEW FIX: Only register catch when ball was IN_FLIGHT in the previous frame
    // This prevents multiple catches when ball is just passing through catch zone
    if (ball.state == IN_FLIGHT && ball.previous_state == IN_FLIGHT) {
        // Ball was truly in flight last frame, check for catch
        
        // Use current ball position for catch detection
        cv::Point3f position_for_catch = ball.position;
        
        // CRITICAL FIX: Require minimum number of verified trajectory points before allowing catch
        // This prevents immediate catch right after throw when ball hasn't traveled far enough
        const int MIN_POINTS_BEFORE_CATCH = 3;  // Need at least 3 verified points (~2-3 frames)
        
        if (ball.trajectory.verified_point_count < MIN_POINTS_BEFORE_CATCH) {
            DEBUG_LOG(debug_log, {
                OPEN_DEBUG_LOG(debug_log);
                debug_log << "  Skipping catch detection - only " << ball.trajectory.verified_point_count
                          << " verified points (need >= " << MIN_POINTS_BEFORE_CATCH << ")" << std::endl;
                debug_log.close();
            });
            return;
        }
        
        // ADDITIONAL CHECK: Ball must have moved significantly away from throw position
        // This provides a secondary safety check
        bool has_moved_away = false;
        if (!ball.trajectory.points.empty()) {
            // Get the first trajectory point (throw position)
            cv::Point3f throw_position = ball.trajectory.points[0].position;
            float distance_from_throw = cv::norm(position_for_catch - throw_position);
            
            // Ball must have moved at least the throw threshold distance away
            // This ensures the ball has actually left the hand before we can catch it
            has_moved_away = (distance_from_throw >= tracking_settings_.hand_distance_threshold);
            
            DEBUG_LOG(debug_log, {
                OPEN_DEBUG_LOG(debug_log);
                debug_log << "  Distance from throw position: " << distance_from_throw
                          << "m, threshold: " << tracking_settings_.hand_distance_threshold
                          << "m, has_moved_away: " << (has_moved_away ? "YES" : "NO") << std::endl;
                debug_log.close();
            });
        } else {
            // No trajectory points - assume ball has moved away
            has_moved_away = true;
        }
        
        if (!has_moved_away) {
            DEBUG_LOG(debug_log, {
                OPEN_DEBUG_LOG(debug_log);
                debug_log << "  Skipping catch detection - ball hasn't moved far enough from throw position" << std::endl;
                debug_log.close();
            });
            return;
        }
        
        // Find the CLOSEST hand within threshold
        // CRITICAL: Also check that ball is far enough from the THROWING hand
        // This prevents immediate re-catch by the same hand that just threw
        float min_dist = tracking_settings_.hand_distance_threshold;
        const SimpleHand* closest_hand = nullptr;
        
        for (const auto& hand : hands_) {
            if (!hand.is_visible) continue;
            
            float dist_to_hand = cv::norm(position_for_catch - hand.wrist_pos_3d);
            
            // CRITICAL FIX: If this is the hand that threw the ball, it must be far enough away
            // to prevent immediate re-catch during the throw motion
            // BUT: Only apply this check if we don't have many verified points yet
            // After the ball has been in flight for a while (many points), allow catch by any hand
            if (hand.id == ball.held_by_hand_id && ball.trajectory.verified_point_count < 10) {
                // This is the throwing hand and ball hasn't been in flight long - require distance
                if (dist_to_hand < tracking_settings_.hand_distance_threshold) {
                    DEBUG_LOG(debug_log, {
                        OPEN_DEBUG_LOG(debug_log);
                        debug_log << "  Skipping catch for throwing hand " << hand.id
                                  << ": dist=" << dist_to_hand
                                  << "m < hand_threshold=" << tracking_settings_.hand_distance_threshold
                                  << "m (verified_points=" << ball.trajectory.verified_point_count << " < 10)" << std::endl;
                        debug_log.close();
                    });
                    continue;  // Skip this hand - ball too close to throwing hand
                }
            }
            
            DEBUG_LOG(debug_log, {
                OPEN_DEBUG_LOG(debug_log);
                debug_log << "  Checking catch for hand " << hand.id << ": dist=" << dist_to_hand
                          << "m, hand_threshold=" << tracking_settings_.hand_distance_threshold << "m" << std::endl;
                debug_log.close();
            });
            
            if (dist_to_hand < min_dist) {
                // This hand is closer - update closest hand
                min_dist = dist_to_hand;
                closest_hand = &hand;
            }
        }
        
        // If we found a hand within threshold, catch to it
        // CRITICAL: Check if this is the hand that just threw the ball
        if (closest_hand != nullptr) {
            DEBUG_LOG(debug_log, {
                OPEN_DEBUG_LOG(debug_log);
                debug_log << "  [CATCH CHECK] Closest hand found: " << closest_hand->id << std::endl;
                debug_log << "    ball.last_throwing_hand_id: " << ball.last_throwing_hand_id << std::endl;
                debug_log << "    ball.frames_in_flight_since_throw: " << ball.frames_in_flight_since_throw << std::endl;
                debug_log << "    ball.held_by_hand_id: " << ball.held_by_hand_id << std::endl;
                debug_log << "    closest_hand->id: " << closest_hand->id << std::endl;
                debug_log.close();
            });
            
            // CRITICAL: GLOBAL 3-FRAME RULE
            // A hand that just threw the ball CANNOT catch it again until min_frames_before_catch frames have passed
            if (ball.last_throwing_hand_id >= 0 &&
                closest_hand->id == ball.last_throwing_hand_id) {
                bool has_enough_flight_frames = (ball.frames_in_flight_since_throw >= tracking_settings_.min_frames_before_catch);
                
                DEBUG_LOG(debug_log, {
                    OPEN_DEBUG_LOG(debug_log);
                    debug_log << "  [GLOBAL 3-FRAME RULE CHECK]" << std::endl;
                    debug_log << "    Ball was thrown by hand " << ball.last_throwing_hand_id << std::endl;
                    debug_log << "    Current hand: " << closest_hand->id << std::endl;
                    debug_log << "    Frames in flight since throw: " << ball.frames_in_flight_since_throw << std::endl;
                    debug_log << "    Has enough flight frames (>=3): " << (has_enough_flight_frames ? "YES" : "NO") << std::endl;
                    debug_log.close();
                });
                
                if (!has_enough_flight_frames) {
                    DEBUG_LOG(debug_log, {
                        OPEN_DEBUG_LOG(debug_log);
                        debug_log << "  ✗ CATCH BLOCKED: Same hand that threw, only "
                                  << ball.frames_in_flight_since_throw << " frames in flight (need 3+)" << std::endl;
                        debug_log.close();
                    });
                    return;  // Block the catch
                }
                
                DEBUG_LOG(debug_log, {
                    OPEN_DEBUG_LOG(debug_log);
                    debug_log << "  ✓ CATCH ALLOWED: " << ball.frames_in_flight_since_throw << " frames passed" << std::endl;
                    debug_log.close();
                });
            }
            // Prevent immediate re-catch by throwing hand (legacy check)
            else if (closest_hand->id == ball.was_just_thrown_by_hand_id) {
                DEBUG_LOG(debug_log, {
                    OPEN_DEBUG_LOG(debug_log);
                    debug_log << "  CATCH PREVENTED: Hand " << closest_hand->id
                              << " just threw this ball (was_just_thrown_by="
                              << ball.was_just_thrown_by_hand_id << ")" << std::endl;
                    debug_log.close();
                });
                // Clear the throw tracking after preventing the catch
                // This allows the ball to be caught next frame if still near the hand
                ball.was_just_thrown_by_hand_id = -1;
                // Don't catch - let ball stay in flight
            } else {
                DEBUG_LOG(debug_log, {
                    OPEN_DEBUG_LOG(debug_log);
                    debug_log << "  ★★★ [CATCH LOCATION 3: MAIN CATCH DETECTION] ★★★" << std::endl;
                    debug_log << "  CATCH DETECTED to CLOSEST hand " << closest_hand->id
                              << ": dist=" << min_dist
                              << "m, threshold=" << tracking_settings_.hand_distance_threshold << "m" << std::endl;
                    debug_log << "  Ball " << ball.id << " caught by hand " << closest_hand->id << std::endl;
                    debug_log << "  last_throwing_hand_id: " << ball.last_throwing_hand_id << std::endl;
                    debug_log << "  frames_in_flight_since_throw: " << ball.frames_in_flight_since_throw << std::endl;
                    debug_log << "  min_frames_before_catch: " << tracking_settings_.min_frames_before_catch << std::endl;
                    debug_log.close();
                });
                
                // Clear the throw tracking since we're catching with a different hand
                ball.was_just_thrown_by_hand_id = -1;
                
                // 3-frame rule will be cleared in initiateCatch()
                
                initiateCatch(ball, *closest_hand, events);
                return;
            }
        }
    } else {
        DEBUG_LOG(debug_log, {
            OPEN_DEBUG_LOG(debug_log);
            debug_log << "  Skipping catch detection - ball is not IN_FLIGHT (state="
                      << (ball.state == HELD ? "HELD" : "UNKNOWN") << ")" << std::endl;
            debug_log.close();
        });
    }
    
    // CRITICAL SAFETY CHECK: If ball is still IN_FLIGHT but within catch distance of ANY hand,
    // force a catch immediately. This prevents the ball from being stuck in IN_FLIGHT state
    // when it's clearly at a hand position.
    if (ball.state == IN_FLIGHT) {
        for (const auto& hand : hands_) {
            if (!hand.is_visible) continue;
            
            float dist_to_hand = cv::norm(ball.position - hand.wrist_pos_3d);
            
            if (dist_to_hand < tracking_settings_.hand_distance_threshold) {
                // CRITICAL: Check catch cooldown before allowing catch
                if (ball.last_throwing_hand_id >= 0 &&
                    hand.id == ball.last_throwing_hand_id &&
                    ball.frames_in_flight_since_throw < tracking_settings_.min_frames_before_catch) {
                    
                    DEBUG_LOG(debug_log, {
                        OPEN_DEBUG_LOG(debug_log);
                        debug_log << "  ✗ SAFETY CATCH BLOCKED: Same hand that threw, only "
                                  << ball.frames_in_flight_since_throw << " frames in flight (need "
                                  << tracking_settings_.min_frames_before_catch << "+)" << std::endl;
                        debug_log.close();
                    });
                    continue;  // Skip this hand, cooldown not satisfied
                }
                
                DEBUG_LOG(debug_log, {
                    OPEN_DEBUG_LOG(debug_log);
                    debug_log << "  ★★★ [CATCH LOCATION 4: SAFETY CATCH] ★★★" << std::endl;
                    debug_log << "  SAFETY CATCH: Ball within " << dist_to_hand
                              << "m of hand " << hand.id << " (threshold: "
                              << tracking_settings_.hand_distance_threshold << "m) - forcing catch" << std::endl;
                    debug_log << "  Ball " << ball.id << " caught by hand " << hand.id << std::endl;
                    debug_log << "  last_throwing_hand_id: " << ball.last_throwing_hand_id << std::endl;
                    debug_log << "  frames_in_flight_since_throw: " << ball.frames_in_flight_since_throw << std::endl;
                    debug_log << "  min_frames_before_catch: " << tracking_settings_.min_frames_before_catch << std::endl;
                    debug_log.close();
                });
                
                initiateCatch(ball, hand, events);
                return;
            }
        }
    }
}


// ============================================================================
// STATE TRANSITION METHODS (Event Generation)
// ============================================================================

void SimpleBallTracker::initiateThrow(
    SimpleBall& ball,
    const Detection& first_detection,
    const SimpleHand* hand,
    std::vector<BallEvent>& events) {
    
    // 1. Store last held position (for reference, but not used for velocity)
    if (hand != nullptr) {
        ball.last_held_position = hand->wrist_pos_3d;
        // CRITICAL: Mark which hand threw the ball (prevents immediate re-catch)
        ball.was_just_thrown_by_hand_id = hand->id;
    } else {
        ball.last_held_position = ball.position;
        ball.was_just_thrown_by_hand_id = ball.held_by_hand_id;  // Use last known hand
    }
    
    // 2. Clear trajectory list completely
    ball.trajectory.points.clear();
    ball.trajectory.predicted_path.clear();
    ball.trajectory.verified_point_count = 0;
    ball.trajectory.trajectory_confidence = 0.0f;
    ball.trajectory.prediction_valid = false;
    ball.trajectory.prediction_failure_reason = "TRAJECTORY_CLEARED: initiateThrow() - starting new trajectory";
    
    // 3. Transition to IN_FLIGHT state
    ball.state = IN_FLIGHT;
    ball.is_held = false;
    
    // 4. Initialize trajectory parameters
    ball.trajectory.throw_timestamp = getCurrentTimestamp();
    ball.trajectory.initial_position = first_detection.world_pos;  // Use first detection as initial position
    ball.trajectory.gravity = tracking_settings_.traj_gravity;
    ball.trajectory.search_radius_m = tracking_settings_.traj_search_radius;
    
    // 5. Add first flight position as verified point
    TrajectoryPoint first_point;
    first_point.position = first_detection.world_pos;
    first_point.timestamp = ball.trajectory.throw_timestamp;
    first_point.verified = true;
    first_point.confidence = 1.0f;
    ball.trajectory.points.push_back(first_point);
    ball.trajectory.verified_point_count = 1;
    
    // 6. Initial velocity will be estimated once we have 2+ points
    ball.trajectory.initial_velocity = cv::Point3f(0, 0, 0);
    
    // 7. Generate THROW event
    events.push_back({
        BallEvent::THROW,
        ball.id,
        ball.held_by_hand_id,
        ball.trajectory.throw_timestamp
    });
    
    std::cout << "[THROW] Ball " << ball.id << " (" << ball.color_name
              << ") thrown from hand " << ball.held_by_hand_id << std::endl;
    
    DEBUG_LOG(debug_log, {
        OPEN_DEBUG_LOG(debug_log);
        debug_log << "[THROW] Ball " << ball.id << " (" << ball.color_name
                  << ") thrown from hand " << ball.held_by_hand_id
                  << " - waiting for 2nd point to estimate velocity" << std::endl;
        debug_log.close();
    });
}

void SimpleBallTracker::initiateCatch(
    SimpleBall& ball,
    const SimpleHand& hand,
    std::vector<BallEvent>& events) {
    
    // 1. Clear entire trajectory list
    ball.trajectory.points.clear();
    ball.trajectory.predicted_path.clear();
    ball.trajectory.verified_point_count = 0;
    ball.trajectory.trajectory_confidence = 0.0f;
    ball.trajectory.prediction_valid = false;
    ball.trajectory.prediction_failure_reason = "TRAJECTORY_CLEARED: initiateCatch() - ball caught";
    
    // LOCKUP PREVENTION: Reset counters when ball is caught
    ball.frames_without_verified_detection = 0;
    ball.unverified_trajectory_points = 0;
    
    // CRITICAL: Clear throw tracking when ball is caught
    ball.was_just_thrown_by_hand_id = -1;
    ball.last_throwing_hand_id = -1;  // Clear throwing hand ID (GLOBAL 3-FRAME RULE)
    ball.frames_in_flight_since_throw = 0;  // Reset flight frame counter
    
    // 2. Reset physics parameters
    ball.trajectory.initial_velocity = cv::Point3f(0, 0, 0);
    ball.trajectory.initial_position = cv::Point3f(0, 0, 0);
    
    // 3. Transition to HELD state
    ball.state = HELD;
    ball.is_held = true;
    ball.held_by_hand_id = hand.id;
    
    // 4. Update position to wrist
    ball.position = hand.wrist_pos_3d;
    
    // 5. Generate CATCH event
    uint64_t timestamp = getCurrentTimestamp();
    events.push_back({
        BallEvent::CATCH,
        ball.id,
        hand.id,
        timestamp
    });
    
    std::cout << "[CATCH] Ball " << ball.id << " (" << ball.color_name
              << ") caught by hand " << hand.id << std::endl;
    
    DEBUG_LOG(debug_log, {
        OPEN_DEBUG_LOG(debug_log);
        debug_log << "[CATCH] Ball " << ball.id << " (" << ball.color_name
                  << ") caught by hand " << hand.id << std::endl;
        debug_log.close();
    });
}

// ============================================================================
// TRAJECTORY PREDICTION HELPER METHODS
// ============================================================================

cv::Point3f SimpleBallTracker::predictWithTwoPoints(SimpleBall& ball) {
    // Use last two verified points for prediction
    if (ball.trajectory.points.size() < 2) {
        // Not enough points - return last known position
        if (!ball.trajectory.points.empty()) {
            return ball.trajectory.points.back().position;
        }
        return ball.position;
    }
    
    // Use general parabolic fit even for 2 points
    // This gives us position, velocity, and acceleration
    GpuTrajectoryPredictor::ParabolicFitResult state =
        gpu_trajectory_predictor_->estimateCurrentStateCpu(ball.trajectory.points);
    
    // Update stored velocity
    ball.trajectory.initial_velocity = state.velocity;
    
    // Predict next position (one frame ahead) using full parabolic motion
    float predict_dt = tracking_settings_.traj_time_step;
    cv::Point3f predicted;
    predicted.x = state.position.x + state.velocity.x * predict_dt + 0.5f * state.acceleration.x * predict_dt * predict_dt;
    predicted.y = state.position.y + state.velocity.y * predict_dt + 0.5f * state.acceleration.y * predict_dt * predict_dt;
    predicted.z = state.position.z + state.velocity.z * predict_dt + 0.5f * state.acceleration.z * predict_dt * predict_dt;
    
    return predicted;
}

std::vector<cv::Point3f> SimpleBallTracker::predictFullTrajectory(SimpleBall& ball) {
    // PERFORMANCE FIX: Use CPU-only trajectory prediction (GPU overhead was 48% of CPU time!)
    // Requirement: need at least traj_min_points_for_prediction points (default 3)
    if (ball.trajectory.points.size() < static_cast<size_t>(tracking_settings_.traj_min_points_for_prediction)) {
        // Not enough points yet, return empty
        ball.trajectory.prediction_valid = false;
        ball.trajectory.prediction_failure_reason = "INSUFFICIENT_POINTS: have " +
            std::to_string(ball.trajectory.points.size()) + " points, need >=" +
            std::to_string(tracking_settings_.traj_min_points_for_prediction);
        return std::vector<cv::Point3f>();
    }
    
    // CPU-ONLY: Use general parabolic fit to estimate position, velocity, AND acceleration
    // This is MUCH faster than GPU for small trajectory calculations
    GpuTrajectoryPredictor::ParabolicFitResult state =
        gpu_trajectory_predictor_->estimateCurrentStateCpu(ball.trajectory.points);
    
    // Update trajectory parameters with current state
    ball.trajectory.initial_velocity = state.velocity;
    // Note: state.position and state.acceleration are used directly in the prediction loop below
    
    // CPU-ONLY: Predict full trajectory path using simple ballistic equations
    // This replaces the GPU predictor which was causing 48% CPU overhead
    std::vector<cv::Point3f> predicted_path;
    
    float time_step = tracking_settings_.traj_time_step;
    float max_time = tracking_settings_.traj_max_time;
    int num_steps = static_cast<int>(max_time / time_step);
    
    for (int i = 0; i < num_steps; ++i) {
        float t = i * time_step;
        
        // Ballistic motion: p(t) = p0 + v0*t + 0.5*a*t^2
        cv::Point3f predicted;
        predicted.x = state.position.x + state.velocity.x * t + 0.5f * state.acceleration.x * t * t;
        predicted.y = state.position.y + state.velocity.y * t + 0.5f * state.acceleration.y * t * t;
        predicted.z = state.position.z + state.velocity.z * t + 0.5f * state.acceleration.z * t * t;
        
        // Stop if ball goes below ground (z < 0)
        if (predicted.z < 0) break;
        
        predicted_path.push_back(predicted);
    }
    
    // Cache prediction
    ball.trajectory.predicted_path = predicted_path;
    ball.trajectory.prediction_timestamp = getCurrentTimestamp();
    
    if (predicted_path.empty()) {
        ball.trajectory.prediction_valid = false;
        ball.trajectory.prediction_failure_reason = "GPU_PREDICTOR_RETURNED_EMPTY";
    } else {
        ball.trajectory.prediction_valid = true;
        ball.trajectory.prediction_failure_reason = "";
    }
    
    std::cout << "[PREDICT] predictFullTrajectory complete" << std::endl;
    
    return predicted_path;
}

const Detection* SimpleBallTracker::searchAlongPredictionLine(
    const cv::Point3f& predicted_pos,
    float search_radius,
    const std::vector<Detection>& yolo_detections,
    const cv::Mat& color_frame,
    const std::string& ball_color) {
    
    // Find the color profile for this ball
    const ColorProfile* profile = nullptr;
    for (const auto& p : color_profiles_) {
        if (p.name == ball_color && p.enabled) {
            profile = &p;
            break;
        }
    }
    
    if (!profile) {
        return nullptr;
    }
    
    const Detection* best_detection = nullptr;
    float best_combined_score = 0.0f;
    
    // Search through all YOLO detections
    for (const auto& det : yolo_detections) {
        // Calculate 3D distance to predicted position
        float distance = cv::norm(det.world_pos - predicted_pos);
        
        if (distance < search_radius) {
            // Within search radius - check color match
            float color_score = matchColor(det, *profile, color_frame);
            
            if (color_score >= tracking_settings_.traj_color_match_threshold) {
                // Prefer closer detections with better color match
                float combined_score = color_score - (distance / search_radius) * 0.3f;
                
                if (combined_score > best_combined_score) {
                    best_detection = &det;
                    best_combined_score = combined_score;
                }
            }
        }
    }
    
    return best_detection;
}

// ============================================================================
// TRAJECTORY VISUALIZATION (Phase 4)
// ============================================================================

void SimpleBallTracker::drawTrajectory(
    cv::Mat& frame,
    const SimpleBall& ball,
    const CameraIntrinsics& intrinsics
) const {
    if (ball.state != IN_FLIGHT || !viz_settings_.show_trajectory) return;
    
    // Get ball's color from color profile for both verified and predicted points
    cv::Scalar ball_color = viz_settings_.verified_point_color;  // Default green
    cv::Scalar darker_ball_color = cv::Scalar(50, 50, 50);  // Default dark gray
    
    // Try to get the actual ball color
    for (const auto& profile : color_profiles_) {
        if (profile.name == ball.color_name) {
            // Convert HSV to BGR for visualization
            // Use the average hue and saturation from the profile
            if (profile.avg_hue >= 0) {
                cv::Mat hsv_color(1, 1, CV_8UC3, cv::Scalar(profile.avg_hue, profile.avg_saturation, 255));
                cv::Mat bgr_color;
                cv::cvtColor(hsv_color, bgr_color, cv::COLOR_HSV2BGR);
                ball_color = cv::Scalar(bgr_color.at<cv::Vec3b>(0, 0)[0],
                                       bgr_color.at<cv::Vec3b>(0, 0)[1],
                                       bgr_color.at<cv::Vec3b>(0, 0)[2]);
                
                // Create darker version (40% brightness) for predicted points
                darker_ball_color = cv::Scalar(
                    ball_color[0] * 0.4,
                    ball_color[1] * 0.4,
                    ball_color[2] * 0.4
                );
            }
            break;
        }
    }
    
    // 1. Draw verified points with ball's full color
    if (viz_settings_.show_verified_points) {
        for (const auto& traj_point : ball.trajectory.points) {
            if (!traj_point.verified) continue;
            
            cv::Point2f point_2d = project_3d_to_2d(traj_point.position, intrinsics);
            
            // Check if on-screen
            if (point_2d.x >= 0 && point_2d.x < frame.cols &&
                point_2d.y >= 0 && point_2d.y < frame.rows) {
                // Draw circle with ball's full color
                cv::circle(frame, point_2d, viz_settings_.point_radius, ball_color, -1);
                // Add white border for visibility
                cv::circle(frame, point_2d, viz_settings_.point_radius, cv::Scalar(255, 255, 255), 1);
            }
        }
    }
    
    // 2. Draw predicted future points as darker shaded dots (one per frame)
    if (viz_settings_.show_predicted_path && !ball.trajectory.predicted_path.empty()) {
        // Draw each predicted point as a darker dot
        for (const auto& point_3d : ball.trajectory.predicted_path) {
            cv::Point2f point_2d = project_3d_to_2d(point_3d, intrinsics);
            
            // Check if on-screen
            if (point_2d.x >= 0 && point_2d.x < frame.cols &&
                point_2d.y >= 0 && point_2d.y < frame.rows) {
                // Draw circle with darker ball color
                cv::circle(frame, point_2d, viz_settings_.point_radius - 1, darker_ball_color, -1);
                // Add subtle border for visibility
                cv::circle(frame, point_2d, viz_settings_.point_radius - 1, cv::Scalar(100, 100, 100), 1);
            }
        }
    }
    
    // 3. Draw current search radius
    if (viz_settings_.show_search_radius && ball.pixel_pos.x >= 0) {
        // Approximate pixel radius from meters
        float radius_pixels = ball.trajectory.search_radius_m *
                             intrinsics.fx / ball.position.z;
        
        cv::circle(frame, ball.pixel_pos, static_cast<int>(radius_pixels),
                  viz_settings_.search_radius_color, 2);
    }
    
    // 3.5. Draw max tracker distance circle (NEW)
    if (viz_settings_.show_max_distance && ball.pixel_pos.x >= 0) {
        // Get reference position (last verified or current)
        cv::Point3f reference_pos = ball.position;
        if (ball.state == IN_FLIGHT && !ball.trajectory.points.empty()) {
            // Use last verified point for IN_FLIGHT balls
            for (int i = ball.trajectory.points.size() - 1; i >= 0; i--) {
                if (ball.trajectory.points[i].verified) {
                    reference_pos = ball.trajectory.points[i].position;
                    break;
                }
            }
        }
        
        // Project reference position to 2D
        cv::Point2f reference_2d = project_3d_to_2d(reference_pos, intrinsics);
        
        // Calculate pixel radius for max_tracker_distance_per_frame
        float max_dist_pixels = tracking_settings_.max_tracker_distance_per_frame *
                               intrinsics.fx / reference_pos.z;
        
        // Draw red circle showing max allowed distance
        if (reference_2d.x >= 0 && reference_2d.x < frame.cols &&
            reference_2d.y >= 0 && reference_2d.y < frame.rows) {
            cv::circle(frame, reference_2d, static_cast<int>(max_dist_pixels),
                      viz_settings_.max_distance_color, 2);
            
            // Add label
            std::string label = cv::format("Max: %.0fcm",
                                          tracking_settings_.max_tracker_distance_per_frame * 100);
            cv::Point text_pos(reference_2d.x + 10, reference_2d.y + 20);
            cv::putText(frame, label, text_pos,
                       cv::FONT_HERSHEY_SIMPLEX, 0.4,
                       viz_settings_.max_distance_color, 1);
        }
    }
    
    // NEW: Draw hand velocity debug info at bottom of frame
    if (viz_settings_.show_hand_velocity_zone && tracking_settings_.hand_velocity_enabled) {
        int text_y = frame.rows - 10;  // Start from bottom
        int line_height = 20;
        
        for (const auto& hand : hands_) {
            // Calculate hand speed
            float hand_speed = cv::norm(hand.velocity);
            bool has_velocity = hand.has_valid_velocity;
            bool exceeds_threshold = has_velocity && (hand_speed >= tracking_settings_.hand_velocity_threshold);
            
            // Format debug text
            std::string hand_label = (hand.id == 0) ? "LEFT" : "RIGHT";
            std::string debug_text = cv::format("%s Hand: speed=%.2f m/s, threshold=%.2f m/s, valid=%s, active=%s",
                                                hand_label.c_str(),
                                                hand_speed,
                                                tracking_settings_.hand_velocity_threshold,
                                                has_velocity ? "YES" : "NO",
                                                exceeds_threshold ? "YES" : "NO");
            
            // Choose color based on whether velocity zone is active
            cv::Scalar text_color = exceeds_threshold ? cv::Scalar(255, 0, 255) : cv::Scalar(150, 150, 150);
            
            // Draw text with black background for readability
            int baseline = 0;
            cv::Size text_size = cv::getTextSize(debug_text, cv::FONT_HERSHEY_SIMPLEX, 0.5, 1, &baseline);
            cv::rectangle(frame,
                         cv::Point(5, text_y - text_size.height - 5),
                         cv::Point(5 + text_size.width + 10, text_y + 5),
                         cv::Scalar(0, 0, 0), -1);
            
            cv::putText(frame, debug_text, cv::Point(10, text_y),
                       cv::FONT_HERSHEY_SIMPLEX, 0.5, text_color, 1);
            
            text_y -= line_height;
        }
    }
    
    // 4. Draw confidence indicator
    if (viz_settings_.show_confidence && ball.pixel_pos.x >= 0) {
        std::string conf_text = cv::format("Conf: %.2f",
                                           ball.trajectory.trajectory_confidence);
        cv::Point text_pos(ball.pixel_pos.x + 10, ball.pixel_pos.y - 10);
        
        cv::putText(frame, conf_text, text_pos,
                    cv::FONT_HERSHEY_SIMPLEX, 0.5,
                    viz_settings_.trajectory_color, 1);
    }
    
    // 5. Draw hand velocity detection zones (NEW)
    if (viz_settings_.show_hand_velocity_zone) {
        for (const auto& tracked_ball : balls_) {
            if (tracked_ball.hand_velocity_active && tracked_ball.hand_velocity_radius > 0) {
                // Project 3D center to 2D
                cv::Point2f center_2d = project_3d_to_2d(tracked_ball.hand_velocity_center, intrinsics);
                
                // Check if on-screen
                if (center_2d.x >= 0 && center_2d.x < frame.cols &&
                    center_2d.y >= 0 && center_2d.y < frame.rows) {
                    
                    // Calculate pixel radius from meters
                    float radius_pixels = tracked_ball.hand_velocity_radius * intrinsics.fx / tracked_ball.hand_velocity_center.z;
                    
                    // Draw purple circle showing detection zone
                    cv::circle(frame, center_2d, static_cast<int>(radius_pixels),
                              viz_settings_.hand_velocity_zone_color, 2);
                    
                    // Draw direction arrow from hand to zone center
                    cv::Point3f arrow_start = tracked_ball.hand_velocity_center - 
                                             tracked_ball.hand_velocity_direction * tracked_ball.hand_velocity_radius;
                    cv::Point2f hand_2d = project_3d_to_2d(arrow_start, intrinsics);
                    
                    if (hand_2d.x >= 0 && hand_2d.x < frame.cols &&
                        hand_2d.y >= 0 && hand_2d.y < frame.rows) {
                        cv::arrowedLine(frame, hand_2d, center_2d,
                                       viz_settings_.hand_velocity_zone_color, 2);
                    }
                    
                    // Add label with hand speed
                    float hand_speed = cv::norm(tracked_ball.hand_velocity_direction);
                    std::string label = cv::format("Hand: %.2fm/s", hand_speed);
                    cv::Point text_pos(center_2d.x + 10, center_2d.y - static_cast<int>(radius_pixels) - 5);
                    cv::putText(frame, label, text_pos,
                               cv::FONT_HERSHEY_SIMPLEX, 0.4,
                               viz_settings_.hand_velocity_zone_color, 1);
                }
            }
        }
    }
}

void SimpleBallTracker::drawHandThresholds(
    cv::Mat& frame,
    const std::vector<SimpleHand>& hands,
    const CameraIntrinsics& intrinsics
) {
    // Draw threshold circles around each hand's wrist
    for (const auto& hand : hands) {
        if (!hand.is_visible) continue;
        
        // Project wrist 3D position to 2D
        cv::Point2f wrist_2d = project_3d_to_2d(hand.wrist_pos_3d, intrinsics);
        
        // Check if on-screen
        if (wrist_2d.x < 0 || wrist_2d.x >= frame.cols ||
            wrist_2d.y < 0 || wrist_2d.y >= frame.rows) {
            continue;
        }
        
        // Calculate pixel radius from meters using camera intrinsics
        // radius_pixels = radius_meters * focal_length / depth
        float depth = hand.wrist_pos_3d.z;
        if (depth <= 0) continue;
        
        // Draw throw distance threshold circle (orange)
        if (viz_settings_.show_throw_distance_threshold) {
            float throw_radius_px = tracking_settings_.throw_distance_threshold * intrinsics.fx / depth;
            cv::circle(frame, wrist_2d, static_cast<int>(throw_radius_px),
                      viz_settings_.throw_distance_color, 2);
            
            // Add label
            std::string label = cv::format("Throw: %.0fcm",
                                          tracking_settings_.throw_distance_threshold * 100);
            cv::Point text_pos(wrist_2d.x + 10, wrist_2d.y - static_cast<int>(throw_radius_px) - 5);
            cv::putText(frame, label, text_pos,
                       cv::FONT_HERSHEY_SIMPLEX, 0.4,
                       viz_settings_.throw_distance_color, 1);
        }
        
        // Draw catch distance threshold circle (green)
        if (viz_settings_.show_catch_distance_threshold) {
            float catch_radius_px = tracking_settings_.catch_distance_threshold * intrinsics.fx / depth;
            cv::circle(frame, wrist_2d, static_cast<int>(catch_radius_px),
                      viz_settings_.catch_distance_color, 2);
            
            // Add label
            std::string label = cv::format("Catch: %.0fcm",
                                          tracking_settings_.catch_distance_threshold * 100);
            cv::Point text_pos(wrist_2d.x + 10, wrist_2d.y + static_cast<int>(catch_radius_px) + 15);
            cv::putText(frame, label, text_pos,
                       cv::FONT_HERSHEY_SIMPLEX, 0.4,
                       viz_settings_.catch_distance_color, 1);
        }
    }
}


void SimpleBallTracker::updateHeldBall(
    SimpleBall& ball,
    const std::vector<SimpleHand>& hands,
    const std::vector<Detection>& yolo_detections,
    const cv::Mat& color_frame,
    const cv::Mat& depth_frame [[maybe_unused]],
    const CameraIntrinsics& intrinsics,
    std::vector<BallEvent>& events) {
    
    DEBUG_LOG(debug_log, {
        OPEN_DEBUG_LOG(debug_log);
        debug_log << "\n[updateHeldBall] Ball " << ball.id << " (" << ball.color_name << ")" << std::endl;
        debug_log << "  held_by_hand_id: " << ball.held_by_hand_id << std::endl;
        debug_log << "  Number of hands: " << hands.size() << std::endl;
        debug_log.close();
    });
    
    // Find color profile for this ball
    const ColorProfile* profile = nullptr;
    for (const auto& p : color_profiles_) {
        if (p.name == ball.color_name && p.enabled) {
            profile = &p;
            break;
        }
    }
    
    if (!profile) {
        ball.tracking_reason = "NO_COLOR_PROFILE";
        return;
    }
    
    // Find the hand holding this ball
    const SimpleHand* hand = nullptr;
    for (const auto& h : hands) {
        if (h.id == ball.held_by_hand_id) {
            hand = &h;
            break;
        }
    }
    
    // CRITICAL FIX: Only auto-assign if ball has NEVER been assigned to a hand
    // Once assigned, preserve the hand ID to prevent incorrect switches
    // This prevents the tracker from jumping between hands when YOLO detection is lost
    if (ball.held_by_hand_id == -1 && !hands.empty()) {
        // Ball has never been assigned - find closest hand
        float min_dist = std::numeric_limits<float>::max();
        int closest_hand_id = -1;
        
        for (const auto& h : hands) {
            if (h.is_visible) {
                float dist = cv::norm(ball.position - h.wrist_pos_3d);
                if (dist < min_dist) {
                    min_dist = dist;
                    closest_hand_id = h.id;
                }
            }
        }
        
        if (closest_hand_id >= 0) {
            ball.held_by_hand_id = closest_hand_id;
            // Find the hand pointer
            for (const auto& h : hands) {
                if (h.id == closest_hand_id) {
                    hand = &h;
                    break;
                }
            }
            
            DEBUG_LOG(debug_log, {
                OPEN_DEBUG_LOG(debug_log);
                debug_log << "  Initial assignment to closest hand: " << closest_hand_id
                          << " (dist=" << min_dist << "m)" << std::endl;
                debug_log.close();
            });
        }
    }
    
    if (!hand || !hand->is_visible) {
        // CRITICAL FALLBACK: Hand not found or offscreen
        // Try to find ANY visible hand and force assignment
        DEBUG_LOG(debug_log, {
            OPEN_DEBUG_LOG(debug_log);
            debug_log << "  Hand " << ball.held_by_hand_id << " not found or offscreen" << std::endl;
            debug_log.close();
        });
        
        // Find any visible hand
        const SimpleHand* any_hand = nullptr;
        for (const auto& h : hands) {
            if (h.is_visible) {
                any_hand = &h;
                break;
            }
        }
        
        if (any_hand) {
            // Force assignment to this hand
            ball.held_by_hand_id = any_hand->id;
            hand = any_hand;
            
            DEBUG_LOG(debug_log, {
                OPEN_DEBUG_LOG(debug_log);
                debug_log << "  FORCED assignment to visible hand " << any_hand->id << std::endl;
                debug_log.close();
            });
        } else {
            // ABSOLUTE LAST RESORT: No hands visible at all
            // Keep ball at last known position or use persisted hand position
            if (!hands.empty()) {
                // Use first persisted hand (not visible but has last known position)
                ball.position = hands[0].wrist_pos_3d;
                ball.pixel_pos = project_3d_to_2d(hands[0].wrist_pos_3d, intrinsics);
                
                // CRITICAL: Always set bbox for visualization
                float bbox_size = 30.0f;
                ball.bbox = cv::Rect_<float>(
                    ball.pixel_pos.x - bbox_size/2,
                    ball.pixel_pos.y - bbox_size/2,
                    bbox_size,
                    bbox_size
                );
                
                // CRITICAL: Set has_yolo_detection to TRUE for visualization
                ball.has_yolo_detection = true;
                ball.yolo_confidence = 0.3f;  // Low confidence for persisted fallback
                ball.yolo_class_id = 1;  // ball_held class
                ball.tracking_reason = "HELD_persisted_hand_fallback";
                ball.held_by_hand_id = hands[0].id;
                
                DEBUG_LOG(debug_log, {
                    OPEN_DEBUG_LOG(debug_log);
                    debug_log << "  ABSOLUTE FALLBACK: Using persisted hand " << hands[0].id << " position" << std::endl;
                    debug_log.close();
                });
            } else {
                // No hands at all - keep ball at last position
                ball.tracking_reason = "HELD_no_hands_available";
                DEBUG_LOG(debug_log, {
                    OPEN_DEBUG_LOG(debug_log);
                    debug_log << "  CRITICAL: No hands available - keeping last position" << std::endl;
                    debug_log.close();
                });
            }
            return;
        }
    }
    
    // HELD BALLS ALWAYS USE WRIST POSITION
    // This ensures stable, predictable tracking for held balls
    ball.position = hand->wrist_pos_3d;
    ball.pixel_pos = project_3d_to_2d(hand->wrist_pos_3d, intrinsics);
    
    // Set bbox for visualization
    float bbox_size = 30.0f;
    ball.bbox = cv::Rect_<float>(
        ball.pixel_pos.x - bbox_size/2,
        ball.pixel_pos.y - bbox_size/2,
        bbox_size,
        bbox_size
    );
    
    // Set detection flags for visualization
    ball.has_yolo_detection = true;
    ball.yolo_confidence = 0.8f;  // High confidence for wrist tracking
    ball.yolo_class_id = 1;  // ball_held class
    ball.tracking_reason = "HELD@wrist";
    
    DEBUG_LOG(debug_log, {
        OPEN_DEBUG_LOG(debug_log);
        debug_log << "  Ball positioned at wrist of hand " << hand->id << std::endl;
        debug_log.close();
    });
    
    // Check for THROW: Look for detection moving away from hand
    // CRITICAL FIX: Use previous ball position to verify actual movement
    // This prevents false throws when ball is just jittering near the hand
    
    // NEW: Calculate hand velocity-based adjustments
    float hand_speed = 0.0f;
    cv::Point3f hand_velocity_direction(0, 0, 0);
    bool hand_is_moving_fast = false;
    
    // CRITICAL: Always reset visualization flag first
    ball.hand_velocity_active = false;
    
    if (tracking_settings_.hand_velocity_enabled && hand->has_valid_velocity) {
        hand_speed = cv::norm(hand->velocity);
        if (hand_speed > tracking_settings_.hand_velocity_threshold) {
            hand_is_moving_fast = true;
            hand_velocity_direction = hand->velocity / hand_speed;  // Normalize
            
            DEBUG_LOG(debug_log, {
                OPEN_DEBUG_LOG(debug_log);
                debug_log << "  [HAND_VELOCITY] Hand " << hand->id << " is moving fast: "
                          << hand_speed << " m/s (threshold: "
                          << tracking_settings_.hand_velocity_threshold << " m/s)" << std::endl;
                debug_log << "    Velocity direction: (" << hand_velocity_direction.x << ", "
                          << hand_velocity_direction.y << ", " << hand_velocity_direction.z << ")" << std::endl;
                debug_log << "    Current hand position: (" << hand->wrist_pos_3d.x << ", "
                          << hand->wrist_pos_3d.y << ", " << hand->wrist_pos_3d.z << ")" << std::endl;
                debug_log << "    Detection zone radius: " << tracking_settings_.hand_velocity_detection_radius << " m" << std::endl;
                debug_log.close();
            });
        }
    }
    
    // CRITICAL: Calculate predicted hand position for velocity-based detection checks
    // This must match the visualization zone center
    // Calculate ONCE and use everywhere to ensure consistency
    float dt = 0.033f;  // Assume 30 FPS for next frame prediction
    cv::Point3f predicted_hand_pos = hand->wrist_pos_3d + (hand_is_moving_fast ? hand->velocity * dt : cv::Point3f(0,0,0));
    
    // Store for visualization AFTER calculating predicted_hand_pos
    if (hand_is_moving_fast) {
        ball.hand_velocity_active = true;
        ball.hand_velocity_center = predicted_hand_pos;  // Use the SAME predicted position
        ball.hand_velocity_direction = hand_velocity_direction;
        ball.hand_velocity_radius = tracking_settings_.hand_velocity_detection_radius;
        
        DEBUG_LOG(debug_log, {
            OPEN_DEBUG_LOG(debug_log);
            debug_log << "  [HAND_VELOCITY] Visualization zone set:" << std::endl;
            debug_log << "    Zone center (predicted): (" << predicted_hand_pos.x << ", "
                      << predicted_hand_pos.y << ", " << predicted_hand_pos.z << ")" << std::endl;
            debug_log.close();
        });
    }
    
    DEBUG_LOG(debug_log, {
        OPEN_DEBUG_LOG(debug_log);
        debug_log << "  [THROW CHECK] Starting detection loop:" << std::endl;
        debug_log << "    Number of YOLO detections: " << yolo_detections.size() << std::endl;
        debug_log << "    Hand velocity zone active: " << (hand_is_moving_fast ? "YES" : "NO") << std::endl;
        if (hand_is_moving_fast) {
            debug_log << "    Hand speed: " << hand_speed << " m/s (threshold: "
                      << tracking_settings_.hand_velocity_threshold << " m/s)" << std::endl;
        }
        debug_log.close();
    });
    
    for (const auto& det : yolo_detections) {
        float dist_from_hand = cv::norm(det.world_pos - hand->wrist_pos_3d);
        float dist_from_ball = cv::norm(det.world_pos - ball.position);
        
        DEBUG_LOG(debug_log, {
            OPEN_DEBUG_LOG(debug_log);
            debug_log << "  [THROW CHECK] Evaluating detection:" << std::endl;
            debug_log << "    det.world_pos: (" << det.world_pos.x << ", " << det.world_pos.y << ", " << det.world_pos.z << ")" << std::endl;
            debug_log << "    det.class_id: " << det.class_id << " (" << (det.class_id == 0 ? "ball" : "ball_held") << ")" << std::endl;
            debug_log << "    det.confidence: " << det.confidence << std::endl;
            debug_log << "    hand.wrist_pos_3d: (" << hand->wrist_pos_3d.x << ", " << hand->wrist_pos_3d.y << ", " << hand->wrist_pos_3d.z << ")" << std::endl;
            debug_log << "    predicted_hand_pos: (" << predicted_hand_pos.x << ", " << predicted_hand_pos.y << ", " << predicted_hand_pos.z << ")" << std::endl;
            debug_log << "    ball.position: (" << ball.position.x << ", " << ball.position.y << ", " << ball.position.z << ")" << std::endl;
            debug_log << "    dist_from_hand: " << dist_from_hand << "m" << std::endl;
            debug_log << "    dist_from_ball: " << dist_from_ball << "m" << std::endl;
            debug_log.close();
        });
        
        // NEW: Check if detection is within the hand velocity zone
        // CRITICAL FIX: Only check if detection is within the zone RADIUS
        // Direction doesn't matter - if it's inside the zone, accept it
        bool in_velocity_direction = false;
        float det_distance_from_predicted = 0.0f;
        if (hand_is_moving_fast) {
            cv::Point3f det_direction = det.world_pos - predicted_hand_pos;
            det_distance_from_predicted = cv::norm(det_direction);
            
            // Detection is in velocity zone if within the detection radius
            // No direction check needed - the zone is a sphere, not a cone
            if (det_distance_from_predicted < tracking_settings_.hand_velocity_detection_radius) {
                in_velocity_direction = true;
                
                DEBUG_LOG(debug_log, {
                    OPEN_DEBUG_LOG(debug_log);
                    debug_log << "    ✓ Detection is INSIDE hand velocity zone!" << std::endl;
                    debug_log << "      Distance from predicted hand pos: " << det_distance_from_predicted << "m < radius: "
                              << tracking_settings_.hand_velocity_detection_radius << "m" << std::endl;
                    debug_log.close();
                });
            } else {
                DEBUG_LOG(debug_log, {
                    OPEN_DEBUG_LOG(debug_log);
                    debug_log << "    ✗ Detection is OUTSIDE hand velocity zone" << std::endl;
                    debug_log << "      Distance from predicted hand pos: " << det_distance_from_predicted << "m >= radius: "
                              << tracking_settings_.hand_velocity_detection_radius << "m" << std::endl;
                    debug_log.close();
                });
            }
        } else {
            DEBUG_LOG(debug_log, {
                OPEN_DEBUG_LOG(debug_log);
                debug_log << "    Hand velocity zone: INACTIVE (hand not moving fast enough)" << std::endl;
                debug_log.close();
            });
        }
        
        // Apply velocity-based threshold adjustments
        float effective_confidence_threshold = tracking_settings_.override_ball_confidence_threshold;
        float effective_color_threshold = tracking_settings_.min_color_match_score;
        bool class_requirement_active = true;
        
        if (in_velocity_direction) {
            // Lower thresholds for detections in the direction of hand movement
            effective_confidence_threshold -= tracking_settings_.hand_velocity_confidence_reduction;
            effective_color_threshold -= tracking_settings_.hand_velocity_confidence_reduction;
            
            // Optionally ignore class requirement
            if (tracking_settings_.hand_velocity_ignore_class) {
                class_requirement_active = false;
            }
            
            DEBUG_LOG(debug_log, {
                OPEN_DEBUG_LOG(debug_log);
                debug_log << "    Applying velocity-based threshold reduction:" << std::endl;
                debug_log << "      Confidence threshold: " << effective_confidence_threshold
                          << " (reduced from " << tracking_settings_.override_ball_confidence_threshold << ")" << std::endl;
                debug_log << "      Color threshold: " << effective_color_threshold
                          << " (reduced from " << tracking_settings_.min_color_match_score << ")" << std::endl;
                debug_log << "      Class requirement: " << (class_requirement_active ? "ACTIVE" : "IGNORED") << std::endl;
                debug_log.close();
            });
        }
        
        DEBUG_LOG(debug_log, {
            OPEN_DEBUG_LOG(debug_log);
            debug_log << "    Class check: det.class_id=" << det.class_id
                      << ", class_requirement_active=" << (class_requirement_active ? "YES" : "NO") << std::endl;
            debug_log.close();
        });
        
        // CRITICAL FIX: Check class requirement AFTER velocity adjustments
        // When hand_velocity_ignore_class is true AND we're in velocity direction,
        // class_requirement_active will be false, so we should accept any class
        bool meets_class_requirement = !class_requirement_active || (det.class_id == 0);
        
        DEBUG_LOG(debug_log, {
            OPEN_DEBUG_LOG(debug_log);
            if (meets_class_requirement) {
                debug_log << "    ✓ Class requirement: PASSED" << std::endl;
                if (!class_requirement_active) {
                    debug_log << "      (class requirement disabled by hand velocity zone)" << std::endl;
                } else {
                    debug_log << "      (det.class_id=" << det.class_id << " is 'ball')" << std::endl;
                }
            } else {
                debug_log << "    ✗ Class requirement: FAILED" << std::endl;
                debug_log << "      (det.class_id=" << det.class_id << " is 'ball_held', requires 'ball')" << std::endl;
            }
            debug_log.close();
        });
        
        // NEW AGGRESSIVE THROW DETECTION: When hand velocity zone is active
        // Check if detection is moving AWAY from ball's current position (at wrist)
        // This allows throw detection even when YOLO detects ball_held class
        // CRITICAL FIX: Don't check meets_class_requirement here - that's the whole point!
        // When hand velocity zone is active and ignore_class is enabled, we should
        // accept ANY class_id (ball or ball_held)
        bool is_aggressive_throw = false;
        bool is_velocity_zone_throw = false;  // Track if this is a velocity zone throw
        if (hand_is_moving_fast && in_velocity_direction) {
            // Detection is in velocity zone - check if it's moving away from wrist
            // The ball is currently at the wrist, so any detection in the velocity direction
            // that's beyond the reduced threshold is considered a throw
            float dist_from_wrist = cv::norm(det.world_pos - hand->wrist_pos_3d);
            
            if (dist_from_wrist > tracking_settings_.hand_velocity_distance_reduction) {
                is_aggressive_throw = true;
                is_velocity_zone_throw = true;  // Mark as velocity zone throw
                
                DEBUG_LOG(debug_log, {
                    OPEN_DEBUG_LOG(debug_log);
                    debug_log << "    AGGRESSIVE THROW DETECTED in velocity zone!" << std::endl;
                    debug_log << "      Detection distance from wrist: " << dist_from_wrist << "m" << std::endl;
                    debug_log << "      Reduced threshold: " << tracking_settings_.hand_velocity_distance_reduction << "m" << std::endl;
                    debug_log << "      Detection class: " << (det.class_id == 0 ? "ball" : "ball_held") << std::endl;
                    debug_log << "      MARKING AS VELOCITY ZONE THROW by hand " << hand->id << std::endl;
                    debug_log << "      Ball can only be caught by other hand OR after 3+ flight frames" << std::endl;
                    debug_log.close();
                });
            }
        }
        
        // STANDARD THROW DETECTION: Detection must be far from hand
        // NEW: When hand velocity zone is active, use reduced hand_distance_threshold
        float effective_hand_distance_threshold = tracking_settings_.hand_distance_threshold;
        if (hand_is_moving_fast && in_velocity_direction) {
            effective_hand_distance_threshold = tracking_settings_.hand_velocity_distance_reduction;
            
            DEBUG_LOG(debug_log, {
                OPEN_DEBUG_LOG(debug_log);
                debug_log << "    Using REDUCED hand_distance_threshold: " << effective_hand_distance_threshold
                          << "m (normal: " << tracking_settings_.hand_distance_threshold << "m)" << std::endl;
                debug_log.close();
            });
        }
        
        bool is_standard_throw = (dist_from_hand > effective_hand_distance_threshold &&
                                  dist_from_ball < tracking_settings_.max_tracker_distance_per_frame &&
                                  meets_class_requirement);
        
        // Accept throw if EITHER aggressive or standard detection
        if (is_aggressive_throw || is_standard_throw) {
            float color_score = matchColor(det, *profile, color_frame);
            
            DEBUG_LOG(debug_log, {
                OPEN_DEBUG_LOG(debug_log);
                debug_log << "    Throw candidate detected (" << (is_aggressive_throw ? "AGGRESSIVE" : "STANDARD") << ")" << std::endl;
                debug_log << "    Checking color and confidence..." << std::endl;
                debug_log << "      color_score: " << color_score << " vs threshold: " << effective_color_threshold << std::endl;
                debug_log << "      det.confidence: " << det.confidence << " vs threshold: " << effective_confidence_threshold << std::endl;
                debug_log.close();
            });
            
            // CRITICAL: When aggressive throw is detected, be MORE lenient with color
            // The ball might have different lighting/angle when held vs in-air
            float aggressive_color_threshold = effective_color_threshold;
            if (is_aggressive_throw) {
                // In aggressive mode, reduce color threshold even further (or skip it)
                // Use half the normal threshold, or minimum 0.15
                aggressive_color_threshold = std::max(0.15f, effective_color_threshold * 0.5f);
                
                DEBUG_LOG(debug_log, {
                    OPEN_DEBUG_LOG(debug_log);
                    debug_log << "    AGGRESSIVE MODE: Using extra-reduced color threshold: "
                              << aggressive_color_threshold << " (normal: " << effective_color_threshold << ")" << std::endl;
                    debug_log.close();
                });
            }
            
            // Use velocity-adjusted thresholds (or aggressive thresholds if in aggressive mode)
            bool color_check_passed = (color_score > aggressive_color_threshold);
            bool confidence_check_passed = (det.confidence >= effective_confidence_threshold);
            
            DEBUG_LOG(debug_log, {
                OPEN_DEBUG_LOG(debug_log);
                if (color_check_passed) {
                    debug_log << "      ✓ Color check: PASSED (" << color_score << " > " << aggressive_color_threshold << ")" << std::endl;
                } else {
                    debug_log << "      ✗ Color check: FAILED (" << color_score << " <= " << aggressive_color_threshold << ")" << std::endl;
                }
                if (confidence_check_passed) {
                    debug_log << "      ✓ Confidence check: PASSED (" << det.confidence << " >= " << effective_confidence_threshold << ")" << std::endl;
                } else {
                    debug_log << "      ✗ Confidence check: FAILED (" << det.confidence << " < " << effective_confidence_threshold << ")" << std::endl;
                }
                debug_log.close();
            });
            
            if (color_check_passed && confidence_check_passed) {
                // CRITICAL FIX: Use dist_from_hand as the distance_moved
                // The ball is currently at the wrist, so we need to check if the detection
                // is far enough from the hand to constitute a throw
                float distance_moved = dist_from_hand;
                
                // CRITICAL: Ball must have moved at least half the throw threshold distance
                // This prevents false throws from small jitter or hand movement
                // NEW: Use the effective threshold (which may be reduced for velocity zone)
                float min_movement_threshold = effective_hand_distance_threshold * 0.5f;
                
                DEBUG_LOG(debug_log, {
                    OPEN_DEBUG_LOG(debug_log);
                    debug_log << "    distance_moved (=dist_from_hand): " << distance_moved << "m" << std::endl;
                    debug_log << "    min_movement_threshold: " << min_movement_threshold << "m" << std::endl;
                    debug_log.close();
                });
                
                bool movement_check_passed = (distance_moved >= min_movement_threshold);
                
                DEBUG_LOG(debug_log, {
                    OPEN_DEBUG_LOG(debug_log);
                    if (movement_check_passed) {
                        debug_log << "      ✓ Movement check: PASSED (" << distance_moved << "m >= " << min_movement_threshold << "m)" << std::endl;
                    } else {
                        debug_log << "      ✗ Movement check: FAILED (" << distance_moved << "m < " << min_movement_threshold << "m)" << std::endl;
                    }
                    debug_log.close();
                });
                
                if (movement_check_passed) {
                    // THROW DETECTED - initiate throw transition
                    DEBUG_LOG(debug_log, {
                        OPEN_DEBUG_LOG(debug_log);
                        debug_log << "  ✓✓✓ THROW DETECTED ✓✓✓" << std::endl;
                        debug_log << "    Detection registered as IN_FLIGHT" << std::endl;
                        debug_log << "    Summary:" << std::endl;
                        debug_log << "      - In velocity zone: " << (in_velocity_direction ? "YES" : "NO") << std::endl;
                        debug_log << "      - Velocity zone throw: " << (is_velocity_zone_throw ? "YES" : "NO") << std::endl;
                        debug_log << "      - Distance from hand: " << dist_from_hand << "m (threshold: " << effective_hand_distance_threshold << "m)" << std::endl;
                        debug_log << "      - Distance from ball: " << dist_from_ball << "m (max: " << tracking_settings_.max_tracker_distance_per_frame << "m)" << std::endl;
                        debug_log << "      - Distance moved: " << distance_moved << "m (min: " << min_movement_threshold << "m)" << std::endl;
                        debug_log << "      - Color score: " << color_score << " (threshold: " << aggressive_color_threshold << ")" << std::endl;
                        debug_log << "      - Confidence: " << det.confidence << " (threshold: " << effective_confidence_threshold << ")" << std::endl;
                        debug_log << "      - Class: " << (det.class_id == 0 ? "ball" : "ball_held") << std::endl;
                        debug_log.close();
                    });
                    
                    // Set the throwing hand ID and reset frame counter (GLOBAL 3-FRAME RULE)
                    ball.last_throwing_hand_id = hand->id;
                    ball.frames_in_flight_since_throw = 0;
                    
                    DEBUG_LOG(debug_log, {
                        OPEN_DEBUG_LOG(debug_log);
                        debug_log << "      Setting last_throwing_hand_id = " << hand->id << std::endl;
                        debug_log << "      Resetting frames_in_flight_since_throw = 0" << std::endl;
                        debug_log << "      GLOBAL RULE: Hand " << hand->id << " cannot catch for 3 frames" << std::endl;
                        debug_log.close();
                    });
                    
                    initiateThrow(ball, det, hand, events);
                    return;
                }
            }
        } else {
            DEBUG_LOG(debug_log, {
                OPEN_DEBUG_LOG(debug_log);
                debug_log << "    ✗ Not a throw candidate:" << std::endl;
                if (dist_from_hand <= effective_hand_distance_threshold) {
                    debug_log << "      - dist_from_hand " << dist_from_hand
                              << "m <= threshold " << effective_hand_distance_threshold << "m" << std::endl;
                }
                if (dist_from_ball >= tracking_settings_.max_tracker_distance_per_frame) {
                    debug_log << "      - dist_from_ball " << dist_from_ball
                              << "m >= max_tracker_distance " << tracking_settings_.max_tracker_distance_per_frame << "m" << std::endl;
                }
                if (!meets_class_requirement) {
                    debug_log << "      - class requirement not met (det.class_id=" << det.class_id << ")" << std::endl;
                }
                debug_log.close();
            });
        }
    }
}
