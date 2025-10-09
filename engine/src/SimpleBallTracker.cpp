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
        else if (key == "ml_ball_weight") {
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
        else if (key == "kalman_prediction_bonus") {
            tracking_settings_.kalman_prediction_bonus = std::stof(value);
            return true;
        }
        else if (key == "kalman_prediction_threshold") {
            tracking_settings_.kalman_prediction_threshold = std::stof(value);
            return true;
        }
        // Override detection thresholds
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
        // Kalman glob detection settings
        else if (key == "kalman_glob_detection_enabled") {
            tracking_settings_.kalman_glob_detection_enabled = (value == "true" || value == "1");
            return true;
        }
        else if (key == "kalman_glob_search_radius") {
            tracking_settings_.kalman_glob_search_radius = std::stoi(value);
            return true;
        }
        else if (key == "kalman_glob_min_color_score") {
            tracking_settings_.kalman_glob_min_color_score = std::stof(value);
            return true;
        }
        else if (key == "kalman_glob_max_depth_diff") {
            tracking_settings_.kalman_glob_max_depth_diff = std::stof(value);
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
    
    // GPU-ACCELERATED: Convert only the ROI around detection center to HSV using GPU
    // Sample region: 5x5 for calibrated, 15x15 for legacy (radius of 2 vs 7)
    const int max_sample_radius = 7;  // Legacy mode uses larger radius
    int roi_x = std::max(0, static_cast<int>(center.x) - max_sample_radius);
    int roi_y = std::max(0, static_cast<int>(center.y) - max_sample_radius);
    int roi_width = std::min(color_frame.cols - roi_x, max_sample_radius * 2 + 1);
    int roi_height = std::min(color_frame.rows - roi_y, max_sample_radius * 2 + 1);
    
    cv::Rect roi(roi_x, roi_y, roi_width, roi_height);
    cv::Mat hsv_roi = gpu_hsv_converter_->convertRoiToHsv(color_frame, roi);
    
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
    for (auto& ball : balls_) {
        ball.has_yolo_detection = false;
        ball.frames_without_yolo++;
    }
    
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
                
                // GPU-ACCELERATED: Convert only small ROI for sampling using GPU
                const int sample_radius = tracking_settings_.color_sample_radius;
                int roi_x = std::max(0, static_cast<int>(center.x) - sample_radius);
                int roi_y = std::max(0, static_cast<int>(center.y) - sample_radius);
                int roi_width = std::min(color_frame.cols - roi_x, sample_radius * 2 + 1);
                int roi_height = std::min(color_frame.rows - roi_y, sample_radius * 2 + 1);
                
                cv::Rect roi(roi_x, roi_y, roi_width, roi_height);
                cv::Mat hsv_roi = gpu_hsv_converter_->convertRoiToHsv(color_frame, roi);
                
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
        
        // NEW: KALMAN PREDICTION BONUS - Give HUGE bonus to detections near Kalman prediction
        // This is the PRIMARY signal for where the ball should be when YOLO fails temporarily
        const float KALMAN_PREDICTION_BONUS = tracking_settings_.kalman_prediction_bonus;
        const float KALMAN_PREDICTION_THRESHOLD = tracking_settings_.kalman_prediction_threshold;
        
        DEBUG_LOG(euclidean_log, {
            OPEN_DEBUG_LOG(euclidean_log);
            euclidean_log << "\nApplying bonuses:" << std::endl;
            euclidean_log << "  Temporal consistency bonus=" << TEMPORAL_CONSISTENCY_BONUS
                         << ", spatial_threshold=" << SPATIAL_THRESHOLD << "m" << std::endl;
            euclidean_log << "  Kalman prediction bonus=" << KALMAN_PREDICTION_BONUS
                         << ", kalman_threshold=" << KALMAN_PREDICTION_THRESHOLD << "m" << std::endl;
        });
        
        for (auto& match : matches) {
            auto& ball = balls_[match.ball_idx];
            const auto& det = yolo_detections[match.det_idx];
            float original_dist = match.euclidean_distance;
            
            // PRIORITY 1: KALMAN PREDICTION BONUS (strongest signal)
            // If ball has valid Kalman prediction and detection is near it, apply huge bonus
            if (ball.kalman.get_state()(2) > 0.01f) {  // Kalman is initialized
                auto kalman_state = ball.kalman.get_state();
                cv::Point3f kalman_pred(kalman_state(0), kalman_state(1), kalman_state(2));
                
                // Calculate distance from detection to Kalman prediction
                float dx = det.world_pos.x - kalman_pred.x;
                float dy = det.world_pos.y - kalman_pred.y;
                float dz = det.world_pos.z - kalman_pred.z;
                float kalman_dist = std::sqrt(dx*dx + dy*dy + dz*dz);
                
                if (kalman_dist < KALMAN_PREDICTION_THRESHOLD) {
                    // Apply scaled bonus: closer to prediction = stronger bonus
                    float proximity_factor = 1.0f - (kalman_dist / KALMAN_PREDICTION_THRESHOLD);
                    float scaled_bonus = KALMAN_PREDICTION_BONUS * proximity_factor;
                    match.euclidean_distance -= scaled_bonus;
                    
                    DEBUG_LOG(euclidean_log, {
                        OPEN_DEBUG_LOG(euclidean_log);
                        euclidean_log << "  Ball[" << match.ball_idx << "](" << match.ball_color
                                     << ") <-> Det[" << match.det_idx << "]: KALMAN BONUS"
                                     << " | kalman_dist=" << kalman_dist << "m"
                                     << ", proximity_factor=" << proximity_factor
                                     << ", bonus=" << scaled_bonus
                                     << " | " << original_dist << " -> " << match.euclidean_distance << std::endl;
                    });
                    original_dist = match.euclidean_distance;  // Update for next bonus
                }
            }
            
            // PRIORITY 2: Temporal consistency bonus (prevents identity swaps)
            // Check if this ball had a YOLO detection in the previous frame
            if (ball.has_yolo_detection && ball.frames_without_yolo == 0) {
                // Calculate 3D distance from current detection to ball's previous position
                float dx = det.world_pos.x - ball.position.x;
                float dy = det.world_pos.y - ball.position.y;
                float dz = det.world_pos.z - ball.position.z;
                float spatial_dist = std::sqrt(dx*dx + dy*dy + dz*dz);
                
                // If detection is close to where the ball was, apply bonus
                if (spatial_dist < SPATIAL_THRESHOLD) {
                    float proximity_factor = 1.0f - (spatial_dist / SPATIAL_THRESHOLD);
                    float scaled_bonus = TEMPORAL_CONSISTENCY_BONUS * proximity_factor;
                    match.euclidean_distance -= scaled_bonus;
                    
                    DEBUG_LOG(euclidean_log, {
                        OPEN_DEBUG_LOG(euclidean_log);
                        euclidean_log << "  Ball[" << match.ball_idx << "](" << match.ball_color
                                     << ") <-> Det[" << match.det_idx << "]: TEMPORAL BONUS"
                                     << " | spatial_dist=" << spatial_dist << "m"
                                     << ", proximity_factor=" << proximity_factor
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
            
            // Store matched detection confidence scores for UI display
            ball.matched_detection_confidence = det.confidence;
            ball.matched_detection_color_score = ball.color_match_score;
            
            char reason[128];
            snprintf(reason, sizeof(reason), "Euclidean dist=%.3f", match.euclidean_distance);
            ball.tracking_reason = reason;
            
            // CRITICAL: Validate detection before updating Kalman to prevent corruption
            bool should_update_kalman = true;
            
            // Check for suspicious depth jumps (likely sensor errors)
            if (ball.kalman.get_state()(2) > 0.01f) {  // Kalman is initialized
                float prev_depth = ball.kalman.get_state()(2);
                float depth_change = std::abs(det.world_pos.z - prev_depth);
                const float MAX_DEPTH_JUMP = 0.30f;  // 30cm max depth change per frame
                
                if (depth_change > MAX_DEPTH_JUMP) {
                    should_update_kalman = false;
                    DEBUG_LOG(depth_jump_reject_log, {
                        OPEN_DEBUG_LOG(depth_jump_reject_log);
                        depth_jump_reject_log << "\n[DEPTH_JUMP_REJECT] Ball " << ball.id
                                             << " | Detection rejected for Kalman update"
                                             << " | depth_change=" << depth_change << "m"
                                             << " | prev_depth=" << prev_depth << "m"
                                             << " | new_depth=" << det.world_pos.z << "m"
                                             << " | Exceeds MAX_DEPTH_JUMP=" << MAX_DEPTH_JUMP << "m" << std::endl;
                    });
                }
            }
            
            // Update Kalman only if detection passes validation
            if (should_update_kalman) {
                ball.kalman.update(KalmanFilter3D::MeasurementVector(
                    ball.position.x, ball.position.y, ball.position.z));
            }
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
    
    // OVERRIDE DETECTION: Force tracker placement for high-confidence detections
    // This ensures trackers never disappear when good detections exist
    DEBUG_LOG(override_log, {
        OPEN_DEBUG_LOG(override_log);
        override_log << "\n=== FRAME " << frame_counter_ << " - OVERRIDE DETECTION CHECK ===" << std::endl;
    });
    
    for (size_t ball_idx = 0; ball_idx < balls_.size(); ++ball_idx) {
        auto& ball = balls_[ball_idx];
        
        // Determine which thresholds to use based on whether ball currently has a tracker
        bool ball_has_tracker = (ball.position.z > 0.01f);  // Ball has valid position
        float min_confidence = ball_has_tracker ?
            tracking_settings_.override_min_confidence_tracked :
            tracking_settings_.override_min_confidence_missing;
        float min_color_score = ball_has_tracker ?
            tracking_settings_.override_min_color_score_tracked :
            tracking_settings_.override_min_color_score_missing;
        
        DEBUG_LOG(override_log, {
            OPEN_DEBUG_LOG(override_log);
            override_log << "\nBall[" << ball_idx << "] '" << ball.color_name << "' "
                        << (ball_has_tracker ? "HAS tracker" : "MISSING tracker") << std::endl;
            override_log << "  Thresholds: conf>=" << min_confidence << ", color>=" << min_color_score << std::endl;
        });
        
        // Skip if ball was already matched by euclidean system
        if (ball.has_yolo_detection && ball.frames_without_yolo == 0) {
            DEBUG_LOG(override_log, {
                OPEN_DEBUG_LOG(override_log);
                override_log << "  SKIP: Already matched by euclidean system" << std::endl;
            });
            continue;
        }
        
        // Find color profile
        const ColorProfile* profile = nullptr;
        for (const auto& p : color_profiles_) {
            if (p.name == ball.color_name && p.enabled) {
                profile = &p;
                break;
            }
        }
        
        if (!profile) continue;
        
        // Search for high-confidence detection matching this ball's color
        float best_score = -1.0f;
        const Detection* best_det = nullptr;
        
        for (const auto& det : yolo_detections) {
            // Skip if already used
            if (used_detections.find(det.index) != used_detections.end()) continue;
            
            // Skip if invalid depth
            if (det.world_pos.z < MIN_DEPTH || det.world_pos.z > MAX_DEPTH) continue;
            
            // Check confidence threshold
            if (det.confidence < min_confidence) continue;
            
            // Calculate color match score
            float color_score = matchColor(det, *profile, color_frame);
            
            // Check color threshold
            if (color_score < min_color_score) continue;
            
            // Calculate combined score (confidence * color_score)
            float combined_score = det.confidence * color_score;
            
            DEBUG_LOG(override_log, {
                OPEN_DEBUG_LOG(override_log);
                override_log << "  Det#" << det.index << ": conf=" << det.confidence
                            << ", color=" << color_score << ", combined=" << combined_score << std::endl;
            });
            
            if (combined_score > best_score) {
                best_score = combined_score;
                best_det = &det;
            }
        }
        
        if (best_det) {
            DEBUG_LOG(override_log, {
                OPEN_DEBUG_LOG(override_log);
                override_log << "  *** OVERRIDE: Forcing tracker to Det#" << best_det->index
                            << " (conf=" << best_det->confidence << ", color="
                            << matchColor(*best_det, *profile, color_frame) << ") ***" << std::endl;
            });
            
            // Force tracker to this detection
            ball.position = best_det->world_pos;
            ball.pixel_pos = cv::Point2f(best_det->box.x + best_det->box.width / 2.0f,
                                         best_det->box.y + best_det->box.height / 2.0f);
            ball.bbox = best_det->box;
            ball.has_yolo_detection = true;
            ball.frames_without_yolo = 0;
            ball.yolo_confidence = best_det->confidence;
            ball.yolo_class_id = best_det->class_id;
            ball.color_match_score = matchColor(*best_det, *profile, color_frame);
            
            // Store matched detection confidence scores
            ball.matched_detection_confidence = best_det->confidence;
            ball.matched_detection_color_score = ball.color_match_score;
            
            char reason[128];
            snprintf(reason, sizeof(reason), "OVERRIDE conf=%.2f color=%.2f",
                     best_det->confidence, ball.color_match_score);
            ball.tracking_reason = reason;
            
            // CRITICAL: Validate detection before updating Kalman to prevent corruption
            bool should_update_kalman = true;
            
            // Check for suspicious depth jumps (likely sensor errors)
            if (ball.kalman.get_state()(2) > 0.01f) {  // Kalman is initialized
                float prev_depth = ball.kalman.get_state()(2);
                float depth_change = std::abs(best_det->world_pos.z - prev_depth);
                const float MAX_DEPTH_JUMP = 0.30f;  // 30cm max depth change per frame
                
                if (depth_change > MAX_DEPTH_JUMP) {
                    should_update_kalman = false;
                    DEBUG_LOG(depth_jump_reject_log, {
                        OPEN_DEBUG_LOG(depth_jump_reject_log);
                        depth_jump_reject_log << "\n[DEPTH_JUMP_REJECT_OVERRIDE] Ball " << ball.id
                                             << " | Override detection rejected for Kalman update"
                                             << " | depth_change=" << depth_change << "m"
                                             << " | prev_depth=" << prev_depth << "m"
                                             << " | new_depth=" << best_det->world_pos.z << "m"
                                             << " | Exceeds MAX_DEPTH_JUMP=" << MAX_DEPTH_JUMP << "m" << std::endl;
                    });
                }
            }
            
            // Update Kalman only if detection passes validation
            if (should_update_kalman) {
                ball.kalman.update(KalmanFilter3D::MeasurementVector(
                    ball.position.x, ball.position.y, ball.position.z));
            }
            ball.color_predictor.addDetection(ball.position);
            
            // Mark detection as used
            used_detections.insert(best_det->index);
        } else {
            DEBUG_LOG(override_log, {
                OPEN_DEBUG_LOG(override_log);
                override_log << "  No override detection found" << std::endl;
            });
        }
    }
    
    DEBUG_LOG(override_log, {
        OPEN_DEBUG_LOG(override_log);
        override_log << "=== END OVERRIDE DETECTION ===" << std::endl;
        override_log.close();
    });
    
    // Handle unmatched balls with fallback strategies (Kalman prediction, color tracking, hand snapping)
    for (auto& ball : balls_) {
        // Skip if already matched by euclidean system
        if (ball.has_yolo_detection && ball.frames_without_yolo == 0) {
            continue;
        }
        
        // Ball was not matched - mark as undetected and increment counter
        ball.has_yolo_detection = false;
        ball.frames_without_yolo++;
        
        DEBUG_LOG(fallback_log, {
            OPEN_DEBUG_LOG(fallback_log);
            fallback_log << "\n[FALLBACK] Ball " << ball.id << " (" << ball.color_name
                        << ") lost YOLO detection (frames_without_yolo=" << ball.frames_without_yolo << ")" << std::endl;
        });
        
        // Get Kalman prediction for fallback tracking
        cv::Point3f kalman_pred(0, 0, 0);
        bool has_prediction = false;
        
        if (ball.kalman.get_state()(2) > 0.01f) {  // Only if Kalman has been initialized (z > 0)
            // SAVE the current Kalman state before prediction
            auto saved_state = ball.kalman.get_state();
            
            // CRITICAL: Clamp velocity to realistic juggling speeds before prediction
            // This prevents corrupted Kalman states from causing wild predictions
            const float MAX_VELOCITY = 8.0f;  // 8 m/s max velocity (realistic for juggling)
            float vx = saved_state(3);
            float vy = saved_state(4);
            float vz = saved_state(5);
            float velocity_mag = std::sqrt(vx*vx + vy*vy + vz*vz);
            
            if (velocity_mag > MAX_VELOCITY) {
                // Scale velocity down to max
                float scale = MAX_VELOCITY / velocity_mag;
                saved_state(3) = vx * scale;
                saved_state(4) = vy * scale;
                saved_state(5) = vz * scale;
                ball.kalman.get_state() = saved_state;
                
                DEBUG_LOG(velocity_clamp_log, {
                    OPEN_DEBUG_LOG(velocity_clamp_log);
                    velocity_clamp_log << "\n[VELOCITY_CLAMP] Ball " << ball.id
                                      << " | Velocity clamped from " << velocity_mag << " m/s"
                                      << " to " << MAX_VELOCITY << " m/s"
                                      << " | Original: (" << vx << ", " << vy << ", " << vz << ")"
                                      << " | Clamped: (" << saved_state(3) << ", " << saved_state(4) << ", " << saved_state(5) << ")" << std::endl;
                });
            }
            
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
            
            // SWIPE-THROUGH DETECTION FOR VISIBLE BALLS:
            // Check if hand is swiping through ball's trajectory even when ball is still visible
            // This catches fast catches where YOLO still detects the ball but hand has intercepted it
            if (ball.has_yolo_detection && ball.frames_without_yolo == 0 && !ball.is_held) {
                // Check if any hand is swiping through the ball's current trajectory
                bool swipe_detected = false;
                int swipe_hand_id = -1;
                cv::Point3f swipe_hand_pos(0, 0, 0);
                
                // Need at least 2 frames of history to detect movement
                if (ball.color_predictor.getHistorySize() >= 2) {
                    auto history = ball.color_predictor.getHistory();
                    cv::Point3f ball_prev_pos = history[history.size() - 2].position;
                    cv::Point3f ball_curr_pos = ball.position;
                    
                    // Calculate ball's movement vector
                    cv::Point3f ball_movement = ball_curr_pos - ball_prev_pos;
                    float ball_movement_mag = cv::norm(ball_movement);
                    
                    if (ball_movement_mag > 0.01f) {  // Ball is moving
                        for (const auto& hand : hands) {
                            if (!hand.is_visible) continue;
                            
                            // Vector from previous ball position to current hand position
                            cv::Point3f prev_to_hand = hand.wrist_pos_3d - ball_prev_pos;
                            
                            // Vector from current ball position to current hand position
                            cv::Point3f curr_to_hand = hand.wrist_pos_3d - ball_curr_pos;
                            
                            // Check if hand crossed the ball's trajectory
                            float dot_prev = ball_movement.dot(prev_to_hand);
                            float dot_curr = ball_movement.dot(curr_to_hand);
                            
                            // If signs are opposite, hand crossed the trajectory
                            bool crossed_trajectory = (dot_prev * dot_curr) < 0;
                            
                            // Distance from hand to trajectory line
                            cv::Point3f ball_dir = ball_movement / ball_movement_mag;
                            cv::Point3f to_hand = hand.wrist_pos_3d - ball_prev_pos;
                            float proj_length = to_hand.dot(ball_dir);
                            cv::Point3f closest_point = ball_prev_pos + ball_dir * proj_length;
                            float dist_to_trajectory = cv::norm(hand.wrist_pos_3d - closest_point);
                            
                            // Also check current distance to ball
                            float dist_to_ball = cv::norm(hand.wrist_pos_3d - ball_curr_pos);
                            
                            const float SWIPE_TRAJECTORY_THRESHOLD = 0.20f;  // 20cm from trajectory
                            const float SWIPE_BALL_THRESHOLD = 0.25f;        // 25cm from ball
                            
                            // Detect swipe if hand crossed trajectory AND is close to ball
                            if (crossed_trajectory &&
                                (dist_to_trajectory < SWIPE_TRAJECTORY_THRESHOLD || dist_to_ball < SWIPE_BALL_THRESHOLD)) {
                                swipe_detected = true;
                                swipe_hand_id = hand.id;
                                swipe_hand_pos = hand.wrist_pos_3d;
                                
                                DEBUG_LOG(swipe_visible_log, {
                                    OPEN_DEBUG_LOG(swipe_visible_log);
                                    swipe_visible_log << "\n[SWIPE_VISIBLE] Ball " << ball.id
                                                     << " | Hand " << hand.id << " (" << (hand.id == 0 ? "LEFT" : "RIGHT") << ")"
                                                     << " swiped through visible ball"
                                                     << " | dist_to_trajectory=" << dist_to_trajectory << "m"
                                                     << " | dist_to_ball=" << dist_to_ball << "m"
                                                     << " | ball_movement_mag=" << ball_movement_mag << "m" << std::endl;
                                });
                                break;  // Use first detected swipe
                            }
                        }
                    }
                }
                
                if (swipe_detected) {
                    // SWIPE-THROUGH CATCH DETECTED ON VISIBLE BALL
                    DEBUG_LOG(catch_inference_log, {
                        OPEN_DEBUG_LOG(catch_inference_log);
                        catch_inference_log << "\n[CATCH_SWIPE_VISIBLE] Ball " << ball.id
                                           << " still visible but hand " << swipe_hand_id
                                           << " (" << (swipe_hand_id == 0 ? "LEFT" : "RIGHT") << ")"
                                           << " SWIPED THROUGH - INFERRING CATCH" << std::endl;
                    });
                    
                    // CRITICAL: Mark ball as HELD so it follows the hand
                    ball.is_held = true;
                    ball.held_by_hand_id = swipe_hand_id;
                    ball.previous_held_by_hand_id = swipe_hand_id;
                    ball.yolo_class_id = 1;  // Mark as held
                    ball.position = swipe_hand_pos;
                    ball.pixel_pos = project_3d_to_2d(swipe_hand_pos, intrinsics);
                    ball.has_yolo_detection = false;  // Override YOLO detection
                    ball.frames_without_yolo = 1;     // Mark as just lost
                    
                    char reason[128];
                    snprintf(reason, sizeof(reason), "CATCH_SWIPE_VIS@Hand[%c]",
                             swipe_hand_id == 0 ? 'L' : 'R');
                    ball.tracking_reason = reason;
                    
                    // Update color predictor with hand position
                    ball.color_predictor.addDetection(ball.position);
                    
                    // Continue to next ball - skip normal fallback logic
                    continue;
                }
            }
            
            // CATCH DETECTION: If ball recently vanished from YOLO (frames_without_yolo <= 3)
            // and a hand is nearby OR a hand swiped through the ball's position, infer catch
            // Extended window catches balls that were lost for a few frames before snapping to hand
            if (ball.frames_without_yolo >= 1 && ball.frames_without_yolo <= 3 && !ball.is_held) {
                // Check if any hand is near the ball's last known position
                float min_hand_dist = std::numeric_limits<float>::max();
                int closest_hand_id = -1;
                cv::Point3f closest_hand_pos(0, 0, 0);
                
                // Also check for "swipe-through" detection
                bool swipe_detected = false;
                int swipe_hand_id = -1;
                cv::Point3f swipe_hand_pos(0, 0, 0);
                
                for (const auto& hand : hands) {
                    if (!hand.is_visible) continue;
                    
                    // Standard proximity check
                    float dist = cv::norm(ball.position - hand.wrist_pos_3d);
                    if (dist < min_hand_dist) {
                        min_hand_dist = dist;
                        closest_hand_id = hand.id;
                        closest_hand_pos = hand.wrist_pos_3d;
                    }
                    
                    // SWIPE-THROUGH DETECTION: Check if hand moved from one side of ball to opposite side
                    // This catches fast catches where hand swipes through ball's trajectory
                    // We need at least 2 frames of history to detect movement
                    if (ball.color_predictor.getHistorySize() >= 2) {
                        auto history = ball.color_predictor.getHistory();
                        cv::Point3f ball_prev_pos = history[history.size() - 2].position;
                        cv::Point3f ball_curr_pos = ball.position;
                        
                        // Get hand's previous position from stored hands (if available)
                        // For now, we'll use a simpler approach: check if hand is now on opposite side
                        // of ball compared to where ball was moving
                        
                        // Calculate ball's movement vector
                        cv::Point3f ball_movement = ball_curr_pos - ball_prev_pos;
                        float ball_movement_mag = cv::norm(ball_movement);
                        
                        if (ball_movement_mag > 0.01f) {  // Ball was moving
                            // Vector from previous ball position to current hand position
                            cv::Point3f prev_to_hand = hand.wrist_pos_3d - ball_prev_pos;
                            
                            // Vector from current ball position to current hand position
                            cv::Point3f curr_to_hand = hand.wrist_pos_3d - ball_curr_pos;
                            
                            // Check if hand crossed the ball's trajectory
                            // This happens when the dot products have opposite signs
                            // (hand was on one side, now on opposite side)
                            float dot_prev = ball_movement.dot(prev_to_hand);
                            float dot_curr = ball_movement.dot(curr_to_hand);
                            
                            // If signs are opposite, hand crossed the trajectory
                            bool crossed_trajectory = (dot_prev * dot_curr) < 0;
                            
                            // Also check that hand is reasonably close to the trajectory line
                            // Distance from point to line: ||(p - a) - ((p - a) · n) * n||
                            // where n is normalized direction vector
                            cv::Point3f ball_dir = ball_movement / ball_movement_mag;
                            cv::Point3f to_hand = hand.wrist_pos_3d - ball_prev_pos;
                            float proj_length = to_hand.dot(ball_dir);
                            cv::Point3f closest_point = ball_prev_pos + ball_dir * proj_length;
                            float dist_to_trajectory = cv::norm(hand.wrist_pos_3d - closest_point);
                            
                            const float SWIPE_TRAJECTORY_THRESHOLD = 0.20f;  // 20cm from trajectory line
                            
                            if (crossed_trajectory && dist_to_trajectory < SWIPE_TRAJECTORY_THRESHOLD) {
                                swipe_detected = true;
                                swipe_hand_id = hand.id;
                                swipe_hand_pos = hand.wrist_pos_3d;
                                
                                DEBUG_LOG(swipe_log, {
                                    OPEN_DEBUG_LOG(swipe_log);
                                    swipe_log << "\n[SWIPE_DETECTED] Ball " << ball.id
                                             << " | Hand " << hand.id << " (" << (hand.id == 0 ? "LEFT" : "RIGHT") << ")"
                                             << " crossed trajectory"
                                             << " | dist_to_trajectory=" << dist_to_trajectory << "m"
                                             << " | ball_movement_mag=" << ball_movement_mag << "m" << std::endl;
                                });
                                break;  // Use first detected swipe
                            }
                        }
                    }
                }
                
                // Prioritize swipe detection over proximity
                const float CATCH_INFERENCE_DISTANCE = 0.25f;  // 25cm - generous for catch detection
                
                if (swipe_detected) {
                    // SWIPE-THROUGH CATCH DETECTED
                    DEBUG_LOG(catch_inference_log, {
                        OPEN_DEBUG_LOG(catch_inference_log);
                        catch_inference_log << "\n[CATCH_INFERENCE_SWIPE] Ball " << ball.id
                                           << " vanished from YOLO with hand " << swipe_hand_id
                                           << " (" << (swipe_hand_id == 0 ? "LEFT" : "RIGHT") << ")"
                                           << " SWIPING THROUGH trajectory"
                                           << " - INFERRING CATCH" << std::endl;
                    });
                    
                    // CRITICAL: Mark ball as HELD so it follows the hand
                    ball.is_held = true;
                    ball.held_by_hand_id = swipe_hand_id;
                    ball.previous_held_by_hand_id = swipe_hand_id;
                    ball.yolo_class_id = 1;  // Mark as held
                    ball.position = swipe_hand_pos;
                    ball.pixel_pos = project_3d_to_2d(swipe_hand_pos, intrinsics);
                    
                    char reason[128];
                    snprintf(reason, sizeof(reason), "CATCH_SWIPE@Hand[%c]",
                             swipe_hand_id == 0 ? 'L' : 'R');
                    ball.tracking_reason = reason;
                    
                    // Update color predictor with hand position
                    ball.color_predictor.addDetection(ball.position);
                    
                    // Continue to next ball - skip normal fallback logic
                    continue;
                }
                else if (min_hand_dist < CATCH_INFERENCE_DISTANCE) {
                    // PROXIMITY-BASED CATCH DETECTED
                    DEBUG_LOG(catch_inference_log, {
                        OPEN_DEBUG_LOG(catch_inference_log);
                        catch_inference_log << "\n[CATCH_INFERENCE_PROXIMITY] Ball " << ball.id
                                           << " vanished from YOLO with hand " << closest_hand_id
                                           << " (" << (closest_hand_id == 0 ? "LEFT" : "RIGHT") << ")"
                                           << " at distance " << min_hand_dist << "m"
                                           << " - INFERRING CATCH" << std::endl;
                    });
                    
                    // CRITICAL: Mark ball as HELD so it follows the hand
                    ball.is_held = true;
                    ball.held_by_hand_id = closest_hand_id;
                    ball.previous_held_by_hand_id = closest_hand_id;
                    ball.yolo_class_id = 1;  // Mark as held
                    ball.position = closest_hand_pos;
                    ball.pixel_pos = project_3d_to_2d(closest_hand_pos, intrinsics);
                    
                    char reason[128];
                    snprintf(reason, sizeof(reason), "CATCH_PROX@Hand[%c] d=%.2fm",
                             closest_hand_id == 0 ? 'L' : 'R', min_hand_dist);
                    ball.tracking_reason = reason;
                    
                    // Update color predictor with hand position
                    ball.color_predictor.addDetection(ball.position);
                    
                    // Continue to next ball - skip normal fallback logic
                    continue;
                }
            }
            
            // CRITICAL FIX: Always try fallback strategies regardless of frames_without_yolo
            // Tracker should only disappear when ball goes off-screen, not after a time threshold
            if (has_prediction) {
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
                
                // CRITICAL FIX: Prioritize color blob search near wrist FIRST
                // Check if prediction is near any hand
                if (min_hand_dist < tracking_settings_.wrist_proximity_threshold) {
                    // Project hand position to 2D
                    cv::Point2f hand_2d = project_3d_to_2d(closest_hand_pos, intrinsics);
                    
                    // PRIORITY 1: Search for color blob near the hand (larger radius for better detection)
                    cv::Point2f color_blob = searchForColorBlob(color_frame, *profile, hand_2d, 120);
                    
                    if (color_blob.x > 0 && color_blob.y > 0) {
                        // Found color blob near hand - use it!
                        float depth = getDepthAtPoint(depth_frame, color_blob);
                        if (depth > MIN_DEPTH && depth < MAX_DEPTH) {
                            cv::Point3f color_pos = deprojectToWorld(color_blob, depth, intrinsics);
                            
                            // Validate position is on-screen
                            cv::Point2f pixel_check = project_3d_to_2d(color_pos, intrinsics);
                            if (pixel_check.x >= 0 && pixel_check.x < color_frame.cols &&
                                pixel_check.y >= 0 && pixel_check.y < color_frame.rows) {
                                
                                ball.position = color_pos;
                                ball.pixel_pos = color_blob;
                                ball.held_by_hand_id = closest_hand_id;
                                ball.yolo_class_id = 1;  // Mark as held
                                char reason[128];
                                snprintf(reason, sizeof(reason), "Color@Hand[%c] d=%.2fm",
                                         closest_hand_id == 0 ? 'L' : 'R', min_hand_dist);
                                ball.tracking_reason = reason;
                                
                                // CRITICAL: Validate detection before updating Kalman
                                bool should_update_kalman = true;
                                
                                if (ball.kalman.get_state()(2) > 0.01f) {
                                    float prev_depth = ball.kalman.get_state()(2);
                                    float depth_change = std::abs(color_pos.z - prev_depth);
                                    const float MAX_DEPTH_JUMP = 0.30f;
                                    
                                    if (depth_change > MAX_DEPTH_JUMP) {
                                        should_update_kalman = false;
                                        DEBUG_LOG(depth_jump_reject_log, {
                                            OPEN_DEBUG_LOG(depth_jump_reject_log);
                                            depth_jump_reject_log << "\n[DEPTH_JUMP_REJECT_COLOR] Ball " << ball.id
                                                                 << " | Color blob rejected for Kalman update"
                                                                 << " | depth_change=" << depth_change << "m" << std::endl;
                                        });
                                    }
                                }
                                
                                // Update Kalman only if detection passes validation
                                if (should_update_kalman) {
                                    ball.kalman.update(KalmanFilter3D::MeasurementVector(
                                        color_pos.x, color_pos.y, color_pos.z));
                                }
                                ball.color_predictor.addDetection(color_pos);
                                
                                DEBUG_LOG(fallback_log, {
                                    OPEN_DEBUG_LOG(fallback_log);
                                    fallback_log << "  -> Found color blob near hand at (" << color_pos.x << ", "
                                                << color_pos.y << ", " << color_pos.z << ")" << std::endl;
                                });
                                continue;
                            }
                        }
                    }
                    
                    // PRIORITY 2: Check for ML-detected ball_held near hand
                    float min_ml_dist = std::numeric_limits<float>::max();
                    const Detection* closest_ml_det = nullptr;
                    
                    for (const auto& det : yolo_detections) {
                        if (det.class_id == 1) {  // ball_held class
                            float dist = cv::norm(det.world_pos - closest_hand_pos);
                            if (dist < min_ml_dist && dist < 0.25f) {  // Within 25cm of hand
                                min_ml_dist = dist;
                                closest_ml_det = &det;
                            }
                        }
                    }
                    
                    if (closest_ml_det) {
                        // Found ML ball_held near hand - use it!
                        ball.position = closest_ml_det->world_pos;
                        ball.pixel_pos = cv::Point2f(closest_ml_det->box.x + closest_ml_det->box.width / 2.0f,
                                                     closest_ml_det->box.y + closest_ml_det->box.height / 2.0f);
                        ball.held_by_hand_id = closest_hand_id;
                        ball.yolo_class_id = 1;
                        char reason[128];
                        snprintf(reason, sizeof(reason), "ML_held@Hand[%c] d=%.2fm",
                                 closest_hand_id == 0 ? 'L' : 'R', min_ml_dist);
                        ball.tracking_reason = reason;
                        
                        ball.kalman.update(KalmanFilter3D::MeasurementVector(
                            ball.position.x, ball.position.y, ball.position.z));
                        ball.color_predictor.addDetection(ball.position);
                        
                        DEBUG_LOG(fallback_log, {
                            OPEN_DEBUG_LOG(fallback_log);
                            fallback_log << "  -> Found ML ball_held near hand" << std::endl;
                        });
                        continue;
                    }
                    
                    // PRIORITY 3: Snap to wrist as last resort (only if hand is on-screen)
                    cv::Point2f hand_pixel = project_3d_to_2d(closest_hand_pos, intrinsics);
                    if (hand_pixel.x >= 0 && hand_pixel.x < color_frame.cols &&
                        hand_pixel.y >= 0 && hand_pixel.y < color_frame.rows) {
                        
                        ball.position = closest_hand_pos;
                        ball.pixel_pos = hand_pixel;
                        ball.held_by_hand_id = closest_hand_id;
                        ball.yolo_class_id = 1;  // Mark as held
                        
                        // CRITICAL FIX: Immediately set is_held state when snapping to hand
                        // This bypasses debouncing and ensures catch is detected immediately
                        bool was_not_held = !ball.is_held;
                        if (was_not_held) {
                            ball.is_held = true;
                            ball.state_change_counter = 0;  // Reset debouncing counter
                            ball.previous_held_by_hand_id = closest_hand_id;
                            
                            // CRITICAL: Generate CATCH event immediately when snapping to hand
                            // This ensures catches are never missed when ball snaps to wrist
                            std::vector<BallEvent> snap_events;
                            snap_events.push_back({
                                BallEvent::CATCH,
                                ball.id,
                                closest_hand_id,
                                getCurrentTimestamp()
                            });
                            
                            DEBUG_LOG(snap_catch_log, {
                                OPEN_DEBUG_LOG(snap_catch_log);
                                snap_catch_log << "\n[SNAP_CATCH] Ball " << ball.id
                                              << " snapped to hand " << closest_hand_id
                                              << " (" << (closest_hand_id == 0 ? "LEFT" : "RIGHT") << ")"
                                              << " - IMMEDIATE state change to HELD + CATCH EVENT" << std::endl;
                            });
                        }
                        
                        char reason[128];
                        snprintf(reason, sizeof(reason), "Snap→Hand[%c] d=%.2fm",
                                     closest_hand_id == 0 ? 'L' : 'R', min_hand_dist);
                        ball.tracking_reason = reason;
                        
                        // CRITICAL FIX: Reset Kalman velocity when snapping to prevent velocity corruption
                        // Snapping creates teleportation, not real motion - velocity should be zero
                        auto& state = ball.kalman.get_state();
                        state(3) = 0.0f;  // vx = 0
                        state(4) = 0.0f;  // vy = 0
                        state(5) = 0.0f;  // vz = 0
                        
                        // CRITICAL FIX: DO NOT add snapped positions to color predictor!
                        // Snapping creates teleportation, not real motion
                        // This corrupts velocity estimates used for predictions
                        // ball.color_predictor.addDetection(ball.position);  // REMOVED
                        
                        DEBUG_LOG(fallback_log, {
                            OPEN_DEBUG_LOG(fallback_log);
                            fallback_log << "  -> Snapped to wrist (last resort) - velocity reset to zero" << std::endl;
                        });
                        continue;
                    }
                }
                
                // Prediction is NOT near a hand - try Kalman glob detection if enabled
                if (tracking_settings_.kalman_glob_detection_enabled) {
                    cv::Point2f pred_2d = project_3d_to_2d(kalman_pred, intrinsics);
                    cv::Point2f color_blob = searchForColorBlob(color_frame, *profile, pred_2d,
                                                                tracking_settings_.kalman_glob_search_radius);
                    
                    if (color_blob.x > 0 && color_blob.y > 0) {
                        // Found color blob at predicted location - validate it
                        float depth = getDepthAtPoint(depth_frame, color_blob);
                        if (depth > MIN_DEPTH && depth < MAX_DEPTH) {
                            cv::Point3f color_pos = deprojectToWorld(color_blob, depth, intrinsics);
                            
                            // CRITICAL: Validate depth is close to Kalman prediction
                            float depth_diff = std::abs(color_pos.z - kalman_pred.z);
                            
                            // CRITICAL: Validate color match score
                            Detection temp_det;
                            temp_det.box = cv::Rect_<float>(color_blob.x - 15, color_blob.y - 15, 30, 30);
                            float color_score = matchColor(temp_det, *profile, color_frame);
                            
                            DEBUG_LOG(kalman_glob_log, {
                                OPEN_DEBUG_LOG(kalman_glob_log);
                                kalman_glob_log << "\n[KALMAN_GLOB] Ball " << ball.id << " (" << ball.color_name << ")"
                                               << " | blob found at (" << color_pos.x << ", " << color_pos.y << ", " << color_pos.z << ")"
                                               << " | Kalman pred depth: " << kalman_pred.z << "m"
                                               << " | depth_diff: " << depth_diff << "m (max=" << tracking_settings_.kalman_glob_max_depth_diff << "m)"
                                               << " | color_score: " << color_score << " (min=" << tracking_settings_.kalman_glob_min_color_score << ")"
                                               << std::endl;
                            });
                            
                            // Accept blob only if depth and color match are good
                            if (depth_diff <= tracking_settings_.kalman_glob_max_depth_diff &&
                                color_score >= tracking_settings_.kalman_glob_min_color_score) {
                                
                                ball.position = color_pos;
                                ball.pixel_pos = color_blob;
                                ball.color_match_score = color_score;
                                char reason[128];
                                snprintf(reason, sizeof(reason), "KalmanGlob(c=%.2f,d=%.2fm)",
                                        color_score, depth_diff);
                                ball.tracking_reason = reason;
                                
                                // CRITICAL: Validate detection before updating Kalman
                                bool should_update_kalman_glob = true;
                                
                                if (ball.kalman.get_state()(2) > 0.01f) {
                                    float prev_depth = ball.kalman.get_state()(2);
                                    float depth_change = std::abs(color_pos.z - prev_depth);
                                    const float MAX_DEPTH_JUMP = 0.30f;
                                    
                                    if (depth_change > MAX_DEPTH_JUMP) {
                                        should_update_kalman_glob = false;
                                        DEBUG_LOG(depth_jump_reject_log, {
                                            OPEN_DEBUG_LOG(depth_jump_reject_log);
                                            depth_jump_reject_log << "\n[DEPTH_JUMP_REJECT_KALMAN_GLOB] Ball " << ball.id
                                                                 << " | Kalman glob rejected for Kalman update"
                                                                 << " | depth_change=" << depth_change << "m" << std::endl;
                                        });
                                    }
                                }
                                
                                // Update Kalman only if detection passes validation
                                if (should_update_kalman_glob) {
                                    ball.kalman.update(KalmanFilter3D::MeasurementVector(
                                        color_pos.x, color_pos.y, color_pos.z));
                                }
                                ball.color_predictor.addDetection(color_pos);
                                ball.frames_without_yolo = 0;
                                
                                DEBUG_LOG(kalman_glob_accept_log, {
                                    OPEN_DEBUG_LOG(kalman_glob_accept_log);
                                    kalman_glob_accept_log << "[KALMAN_GLOB_ACCEPT] Ball " << ball.id
                                                          << " using color blob at Kalman prediction" << std::endl;
                                });
                                continue;
                            } else {
                                DEBUG_LOG(kalman_glob_reject_log, {
                                    OPEN_DEBUG_LOG(kalman_glob_reject_log);
                                    kalman_glob_reject_log << "[KALMAN_GLOB_REJECT] Ball " << ball.id
                                                          << " rejected: depth_diff=" << depth_diff
                                                          << "m > max=" << tracking_settings_.kalman_glob_max_depth_diff
                                                          << "m OR color_score=" << color_score
                                                          << " < min=" << tracking_settings_.kalman_glob_min_color_score
                                                          << std::endl;
                                });
                            }
                        }
                    }
                }
                
                // If color tracking failed, fall through to Kalman-only prediction below
            }
            
            // CRITICAL FIX: Always use Kalman prediction if available (no time limit)
            // Tracker should only disappear when ball goes off-screen
            if (has_prediction) {
                // Check if Kalman prediction is on-screen
                cv::Point2f pred_pixel = project_3d_to_2d(kalman_pred, intrinsics);
                bool is_on_screen = (pred_pixel.x >= 0 && pred_pixel.x < color_frame.cols &&
                                    pred_pixel.y >= 0 && pred_pixel.y < color_frame.rows);
                
                if (!is_on_screen) {
                    // Ball has gone off-screen - this is the ONLY reason to stop tracking
                    DEBUG_LOG(fallback_log, {
                        OPEN_DEBUG_LOG(fallback_log);
                        fallback_log << "  -> Ball went OFF-SCREEN at pixel (" << pred_pixel.x << ", "
                                    << pred_pixel.y << ") - STOPPING TRACKER" << std::endl;
                    });
                    // Mark ball as invalid by setting position to zero
                    ball.position = cv::Point3f(0, 0, 0);
                    ball.pixel_pos = cv::Point2f(-1, -1);
                    ball.tracking_reason = "OFF-SCREEN";
                    continue;
                }
                
                // Use Kalman prediction as the ball position for display
                auto state = ball.kalman.get_state();
                ball.position = cv::Point3f(state(0), state(1), state(2));
                
                // DO NOT update Kalman with its own prediction - this causes drift!
                // The Kalman filter should only be updated with real measurements
                // The prediction uncertainty (P matrix) naturally grows over time
                
                // CRITICAL FIX: Validate position before adding to color predictor
                // Only add if position change is reasonable (not a teleportation)
                if (ball.color_predictor.getHistorySize() > 0) {
                    auto history = ball.color_predictor.getHistory();
                    cv::Point3f last_pos = history.back().position;
                    float dx = ball.position.x - last_pos.x;
                    float dy = ball.position.y - last_pos.y;
                    float dz = ball.position.z - last_pos.z;
                    float distance = std::sqrt(dx*dx + dy*dy + dz*dz);
                    
                    // Only add if movement is reasonable (< 0.5m per frame)
                    const float MAX_POSITION_JUMP = 0.5f;
                    if (distance < MAX_POSITION_JUMP) {
                        ball.color_predictor.addDetection(ball.position);
                    } else {
                        DEBUG_LOG(color_pred_reject_log, {
                            OPEN_DEBUG_LOG(color_pred_reject_log);
                            color_pred_reject_log << "\n[COLOR_PRED_REJECT] Ball " << ball.id
                                                 << " | Position jump " << distance << "m rejected"
                                                 << " | Exceeds MAX_POSITION_JUMP=" << MAX_POSITION_JUMP << "m" << std::endl;
                        });
                    }
                } else {
                    // First position, always add
                    ball.color_predictor.addDetection(ball.position);
                }
                
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
                
                DEBUG_LOG(fallback_log, {
                    OPEN_DEBUG_LOG(fallback_log);
                    fallback_log << "  -> Using Kalman prediction at (" << ball.position.x << ", "
                                << ball.position.y << ", " << ball.position.z << ")" << std::endl;
                });
            }
        }
    }
    
    // CRITICAL: Update held ball positions to follow hands
    // If a ball is marked as held, it must move with the hand
    // PRIORITY: 1) Color blob near hand, 2) Snap to wrist
    // IMPORTANT: Only override if ball doesn't have a good YOLO detection from euclidean matching
    for (auto& ball : balls_) {
        if (ball.is_held && ball.held_by_hand_id >= 0) {
            // CRITICAL FIX: Don't override good euclidean matches!
            // Only search for color blobs if:
            // 1. Ball has NO YOLO detection, OR
            // 2. YOLO confidence is low (< 0.5), OR
            // 3. Color match score is poor (< 0.3)
            bool needs_fallback_tracking = !ball.has_yolo_detection ||
                                          ball.yolo_confidence < 0.5f ||
                                          ball.color_match_score < 0.3f;
            
            if (!needs_fallback_tracking) {
                // Ball has a good euclidean match - trust it and skip held ball override
                DEBUG_LOG(held_skip_log, {
                    OPEN_DEBUG_LOG(held_skip_log);
                    held_skip_log << "\n[HELD_SKIP] Ball " << ball.id << " has good euclidean match"
                                 << " | yolo_conf=" << ball.yolo_confidence
                                 << ", color_score=" << ball.color_match_score
                                 << " - SKIPPING held ball override" << std::endl;
                });
                continue;  // Skip to next ball
            }
            
            // Find the hand that's holding this ball
            for (const auto& hand : hands) {
                if (hand.id == ball.held_by_hand_id && hand.is_visible) {
                    // Find color profile for this ball
                    const ColorProfile* profile = nullptr;
                    for (const auto& p : color_profiles_) {
                        if (p.name == ball.color_name && p.enabled) {
                            profile = &p;
                            break;
                        }
                    }
                    
                    if (profile) {
                        // Project hand position to 2D
                        cv::Point2f hand_2d = project_3d_to_2d(hand.wrist_pos_3d, intrinsics);
                        
                        // Check if hand is on-screen
                        bool hand_on_screen = (hand_2d.x >= 0 && hand_2d.x < color_frame.cols &&
                                              hand_2d.y >= 0 && hand_2d.y < color_frame.rows);
                        
                        if (hand_on_screen) {
                            // PRIORITY 1: Search for color blob near the hand (using configurable radius)
                            cv::Point2f color_blob = searchForColorBlob(color_frame, *profile, hand_2d,
                                                                       tracking_settings_.held_color_search_radius);
                            
                            if (color_blob.x > 0 && color_blob.y > 0) {
                                // Found color blob near hand - validate it before using
                                float depth = getDepthAtPoint(depth_frame, color_blob);
                                if (depth > MIN_DEPTH && depth < MAX_DEPTH) {
                                    cv::Point3f color_pos = deprojectToWorld(color_blob, depth, intrinsics);
                                    
                                    // Validate position is on-screen
                                    cv::Point2f pixel_check = project_3d_to_2d(color_pos, intrinsics);
                                    if (pixel_check.x >= 0 && pixel_check.x < color_frame.cols &&
                                        pixel_check.y >= 0 && pixel_check.y < color_frame.rows) {
                                        
                                        // CRITICAL: Validate color match score to prevent tracking wrong objects
                                        Detection temp_det;
                                        temp_det.box = cv::Rect_<float>(color_blob.x - 15, color_blob.y - 15, 30, 30);
                                        float color_score = matchColor(temp_det, *profile, color_frame);
                                        
                                        // CRITICAL: Validate distance from hand to prevent tracking distant objects
                                        float dist_from_hand = cv::norm(color_pos - hand.wrist_pos_3d);
                                        
                                        // Only accept if color match is good AND blob is close to hand
                                        if (color_score >= tracking_settings_.held_color_min_score &&
                                            dist_from_hand <= tracking_settings_.held_color_max_distance) {
                                            
                                            ball.position = color_pos;
                                            ball.pixel_pos = color_blob;
                                            ball.color_match_score = color_score;
                                            char reason[128];
                                            snprintf(reason, sizeof(reason), "Held_Color@Hand(%.2f,%.2fm)",
                                                    color_score, dist_from_hand);
                                            ball.tracking_reason = reason;
                                            
                                            // CRITICAL: Validate before adding to color predictor
                                            if (ball.color_predictor.getHistorySize() > 0) {
                                                auto history = ball.color_predictor.getHistory();
                                                cv::Point3f last_pos = history.back().position;
                                                float distance = cv::norm(color_pos - last_pos);
                                                const float MAX_POSITION_JUMP = 0.5f;
                                                
                                                if (distance < MAX_POSITION_JUMP) {
                                                    ball.color_predictor.addDetection(color_pos);
                                                }
                                            } else {
                                                ball.color_predictor.addDetection(color_pos);
                                            }
                                            
                                            DEBUG_LOG(held_color_log, {
                                                OPEN_DEBUG_LOG(held_color_log);
                                                held_color_log << "\n[HELD_COLOR] Ball " << ball.id << " found color blob near hand " << hand.id
                                                              << " at (" << color_pos.x << ", " << color_pos.y << ", " << color_pos.z << ")"
                                                              << " | color_score=" << color_score << ", dist=" << dist_from_hand << "m" << std::endl;
                                            });
                                            break;  // Found valid color blob, done with this ball
                                        } else {
                                            DEBUG_LOG(held_color_reject_log, {
                                                OPEN_DEBUG_LOG(held_color_reject_log);
                                                held_color_reject_log << "\n[HELD_COLOR_REJECT] Ball " << ball.id
                                                                     << " rejected color blob near hand " << hand.id
                                                                     << " | color_score=" << color_score
                                                                     << " (min=" << tracking_settings_.held_color_min_score << ")"
                                                                     << " | dist=" << dist_from_hand << "m"
                                                                     << " (max=" << tracking_settings_.held_color_max_distance << "m)" << std::endl;
                                            });
                                        }
                                    }
                                }
                            }
                            
                            // PRIORITY 2: No color blob found - snap to wrist ONLY if no good YOLO detection exists
                            // CRITICAL FIX: Don't override valid YOLO detections during hand-to-hand throws!
                            // Only snap to wrist when ball is truly occluded (no YOLO detection)
                            if (!ball.has_yolo_detection || ball.yolo_confidence < 0.5f) {
                                ball.position = hand.wrist_pos_3d;
                                ball.pixel_pos = hand_2d;
                                ball.tracking_reason = "Held_Snap@Wrist";
                                
                                // CRITICAL FIX: Reset Kalman velocity when snapping to prevent velocity corruption
                                // Snapping creates teleportation, not real motion - velocity should be zero
                                auto& state = ball.kalman.get_state();
                                state(3) = 0.0f;  // vx = 0
                                state(4) = 0.0f;  // vy = 0
                                state(5) = 0.0f;  // vz = 0
                                
                                // CRITICAL FIX: DO NOT add snapped positions to color predictor!
                                // Snapping creates teleportation, not real motion
                                // This corrupts velocity estimates used for predictions
                                // ball.color_predictor.addDetection(ball.position);  // REMOVED
                                
                                DEBUG_LOG(held_snap_log, {
                                    OPEN_DEBUG_LOG(held_snap_log);
                                    held_snap_log << "\n[HELD_SNAP] Ball " << ball.id << " snapped to wrist of hand " << hand.id
                                                 << " at (" << ball.position.x << ", " << ball.position.y << ", "
                                                 << ball.position.z << ") - velocity reset to zero" << std::endl;
                                });
                            } else {
                                DEBUG_LOG(held_snap_reject_log, {
                                    OPEN_DEBUG_LOG(held_snap_reject_log);
                                    held_snap_reject_log << "\n[HELD_SNAP_REJECT] Ball " << ball.id
                                                        << " NOT snapped to wrist - YOLO has good detection"
                                                        << " | yolo_conf=" << ball.yolo_confidence
                                                        << " | Trusting YOLO position instead" << std::endl;
                                });
                            }
                        } else {
                            // Hand is off-screen - mark ball as off-screen too
                            ball.position = cv::Point3f(0, 0, 0);
                            ball.pixel_pos = cv::Point2f(-1, -1);
                            ball.tracking_reason = "Held_OFF-SCREEN";
                            
                            DEBUG_LOG(held_offscreen_log, {
                                OPEN_DEBUG_LOG(held_offscreen_log);
                                held_offscreen_log << "\n[HELD_OFFSCREEN] Ball " << ball.id << " held by hand " << hand.id
                                                  << " which is off-screen" << std::endl;
                            });
                        }
                    } else {
                        // No color profile found - fallback to wrist snap
                        cv::Point2f hand_2d = project_3d_to_2d(hand.wrist_pos_3d, intrinsics);
                        bool hand_on_screen = (hand_2d.x >= 0 && hand_2d.x < color_frame.cols &&
                                              hand_2d.y >= 0 && hand_2d.y < color_frame.rows);
                        
                        if (hand_on_screen) {
                            ball.position = hand.wrist_pos_3d;
                            ball.pixel_pos = hand_2d;
                            ball.tracking_reason = "Held_NoProfile@Wrist";
                            // DO NOT add snapped positions to color predictor - causes corruption
                        } else {
                            ball.position = cv::Point3f(0, 0, 0);
                            ball.pixel_pos = cv::Point2f(-1, -1);
                            ball.tracking_reason = "Held_OFF-SCREEN";
                        }
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


