#include "DNNTracker.hpp"
#include <iostream>
#include <fstream>
#include <algorithm> // Required for std::max and std::min
#include <cmath>     // Required for std::sqrt
#include <set>
#include <limits>
#include <vector>
#include <iomanip>  // for std::setprecision

// --- HELPER FUNCTIONS ---

// Improved depth filtering using median of 5x5 region
static float get_filtered_depth(const cv::Mat& depth_frame, const cv::Point2f& pixel) {
    const int SAMPLE_SIZE = 5;
    const int half_size = SAMPLE_SIZE / 2;
    
    std::vector<float> depth_samples;
    depth_samples.reserve(SAMPLE_SIZE * SAMPLE_SIZE);
    
    int center_x = static_cast<int>(pixel.x);
    int center_y = static_cast<int>(pixel.y);
    
    // Sample 5x5 region around center
    for (int dy = -half_size; dy <= half_size; ++dy) {
        for (int dx = -half_size; dx <= half_size; ++dx) {
            int x = center_x + dx;
            int y = center_y + dy;
            
            // Check bounds
            if (x >= 0 && x < depth_frame.cols && y >= 0 && y < depth_frame.rows) {
                uint16_t depth_mm = depth_frame.at<uint16_t>(y, x);
                float depth_m = depth_mm / 1000.0f;
                
                // Only include valid depths
                if (depth_m > 0.1f && depth_m < 3.0f) {
                    depth_samples.push_back(depth_m);
                }
            }
        }
    }
    
    // Return median depth
    if (depth_samples.empty()) {
        return 0.0f;
    }
    
    std::sort(depth_samples.begin(), depth_samples.end());
    return depth_samples[depth_samples.size() / 2];
}

static cv::Point3f deproject_2d_to_3d(const cv::Point2f& pixel, float depth, const CameraIntrinsics& intrinsics) {
    if (depth > 0) {
        float x = (pixel.x - intrinsics.ppx) * depth / intrinsics.fx;
        float y = (pixel.y - intrinsics.ppy) * depth / intrinsics.fy;
        return cv::Point3f(x, y, depth);
    }
    return cv::Point3f(0.0f, 0.0f, 0.0f);
}

cv::Point2f DNNTracker::project_3d_to_2d(const cv::Point3f& world_pos, const CameraIntrinsics& intrinsics) {
    if (world_pos.z > 0) {
        float x_2d = (world_pos.x * intrinsics.fx) / world_pos.z + intrinsics.ppx;
        float y_2d = (world_pos.y * intrinsics.fy) / world_pos.z + intrinsics.ppy;
        return cv::Point2f(x_2d, y_2d);
    }
    return cv::Point2f(-1, -1); // Invalid point
}

static float calculate_distance(const Eigen::Vector3d& p1, const cv::Point3f& p2) {
    return std::sqrt(std::pow(p1.x() - p2.x, 2) +
                     std::pow(p1.y() - p2.y, 2) +
                     std::pow(p1.z() - p2.z, 2));
}

static float calculate_distance(const cv::Point3f& p1, const cv::Point3f& p2) {
    return std::sqrt(std::pow(p1.x - p2.x, 2) +
                     std::pow(p1.y - p2.y, 2) +
                     std::pow(p1.z - p2.z, 2));
}

// Helper function to compute color channel dominance for relative matching
// Returns scores for ALL colors to determine which detection is "most" of each color
struct ColorScores {
    float green_score = 0.0f;
    float pink_score = 0.0f;
    float orange_score = 0.0f;
    float yellow_score = 0.0f;
    float red_score = 0.0f;
    float blue_score = 0.0f;
    float purple_score = 0.0f;
    float white_score = 0.0f;
};

static ColorScores compute_color_dominance(const cv::Mat& color_frame, const Detection& detection,
                                           const juggler::AdaptiveColorManager* adaptive_manager = nullptr) {
    ColorScores scores;
    
    // Get detection center
    cv::Point2f center(
        detection.box.x + detection.box.width / 2.0f,
        detection.box.y + detection.box.height / 2.0f
    );
    
    // Check if center is within frame bounds
    if (center.x < 0 || center.x >= color_frame.cols ||
        center.y < 0 || center.y >= color_frame.rows) {
        return scores;
    }
    
    // Convert to HSV
    cv::Mat hsv_frame;
    cv::cvtColor(color_frame, hsv_frame, cv::COLOR_BGR2HSV);
    
    // Sample a 7x7 region around the center for better averaging
    const int sample_radius = 7;
    std::vector<cv::Vec3b> samples;
    
    for (int dy = -sample_radius; dy <= sample_radius; dy++) {
        for (int dx = -sample_radius; dx <= sample_radius; dx++) {
            int x = static_cast<int>(center.x) + dx;
            int y = static_cast<int>(center.y) + dy;
            
            if (x >= 0 && x < hsv_frame.cols && y >= 0 && y < hsv_frame.rows) {
                samples.push_back(hsv_frame.at<cv::Vec3b>(y, x));
            }
        }
    }
    
    if (samples.empty()) return scores;
    
    // Compute average HSV
    float avg_h = 0, avg_s = 0, avg_v = 0;
    for (const auto& sample : samples) {
        avg_h += sample[0];
        avg_s += sample[1];
        avg_v += sample[2];
    }
    avg_h /= samples.size();
    avg_s /= samples.size();
    avg_v /= samples.size();
    
    // Color strength = saturation * value (normalized)
    // Higher saturation and value = stronger, more vibrant color
    float color_strength = (avg_s / 255.0f) * (avg_v / 255.0f);
    
    // Use adaptive ranges if available, otherwise use default ranges
    if (adaptive_manager) {
        const auto& profiles = adaptive_manager->getProfiles();
        
        for (const auto& profile : profiles) {
            if (!profile.enabled) continue;
            
            // Check if hue falls within this color's adaptive range
            bool in_range = false;
            
            // Check primary range
            if (avg_h >= profile.min_hsv[0] && avg_h <= profile.max_hsv[0]) {
                in_range = true;
            }
            
            // Check secondary range (for wrap-around colors like red)
            if (profile.min_hsv2[0] >= 0 &&
                avg_h >= profile.min_hsv2[0] && avg_h <= profile.max_hsv2[0]) {
                in_range = true;
            }
            
            if (in_range) {
                // Assign score based on color name
                if (profile.name == "green") scores.green_score = color_strength;
                else if (profile.name == "pink") scores.pink_score = color_strength;
                else if (profile.name == "orange") scores.orange_score = color_strength;
                else if (profile.name == "yellow") scores.yellow_score = color_strength;
                else if (profile.name == "red") scores.red_score = color_strength;
                else if (profile.name == "blue") scores.blue_score = color_strength;
                else if (profile.name == "purple") scores.purple_score = color_strength;
                else if (profile.name == "white") scores.white_score = color_strength;
            }
        }
    } else {
        // Fallback to default fixed ranges if no adaptive manager
        // Green: Hue 45-75 (centered at 60)
        if (avg_h >= 45 && avg_h <= 75) {
            scores.green_score = color_strength;
        }
        
        // Yellow: Hue 20-40 (centered at 30)
        if (avg_h >= 20 && avg_h <= 40) {
            scores.yellow_score = color_strength;
        }
        
        // Orange: Hue 5-20 (centered at 12)
        if (avg_h >= 5 && avg_h <= 20) {
            scores.orange_score = color_strength;
        }
        
        // Red: Hue 0-10 or 170-180 (wraps around at 0/180)
        if ((avg_h >= 0 && avg_h <= 10) || (avg_h >= 170 && avg_h <= 180)) {
            scores.red_score = color_strength;
        }
        
        // Pink: Hue 140-175 (wider range to catch various pink shades)
        if (avg_h >= 140 && avg_h <= 175) {
            scores.pink_score = color_strength;
        }
        
        // Purple: Hue 130-150 (centered at 140)
        if (avg_h >= 130 && avg_h <= 150) {
            scores.purple_score = color_strength;
        }
        
        // Blue: Hue 100-130 (centered at 115)
        if (avg_h >= 100 && avg_h <= 130) {
            scores.blue_score = color_strength;
        }
        
        // White: Low saturation, high value
        if (avg_s < 30 && avg_v > 200) {
            scores.white_score = avg_v / 255.0f;
        }
    }
    
    return scores;
}

// Helper function to get the score for a specific color name
static float get_score_for_color(const ColorScores& scores, const std::string& color_name) {
    if (color_name == "green") return scores.green_score;
    if (color_name == "orange") return scores.orange_score;
    if (color_name == "pink") return scores.pink_score;
    if (color_name == "yellow") return scores.yellow_score;
    if (color_name == "red") return scores.red_score;
    if (color_name == "blue") return scores.blue_score;
    if (color_name == "purple") return scores.purple_score;
    if (color_name == "white") return scores.white_score;
    return 0.0f;
}

// Simple greedy assignment (can be upgraded to full Hungarian later)
static std::vector<std::pair<int, int>> optimal_assignment(
    const std::vector<std::vector<float>>& cost_matrix,
    float max_cost_threshold
) {
    std::vector<std::pair<int, int>> assignments;
    
    if (cost_matrix.empty() || cost_matrix[0].empty()) {
        return assignments;
    }
    
    int n_trackers = cost_matrix.size();
    int n_detections = cost_matrix[0].size();
    
    std::vector<bool> tracker_assigned(n_trackers, false);
    std::vector<bool> detection_assigned(n_detections, false);
    
    // Greedy: repeatedly find minimum cost unassigned pair
    for (int iter = 0; iter < std::min(n_trackers, n_detections); ++iter) {
        float min_cost = max_cost_threshold;
        int best_tracker = -1;
        int best_detection = -1;
        
        for (int i = 0; i < n_trackers; ++i) {
            if (tracker_assigned[i]) continue;
            
            for (int j = 0; j < n_detections; ++j) {
                if (detection_assigned[j]) continue;
                
                if (cost_matrix[i][j] < min_cost) {
                    min_cost = cost_matrix[i][j];
                    best_tracker = i;
                    best_detection = j;
                }
            }
        }
        
        if (best_tracker >= 0 && best_detection >= 0) {
            assignments.push_back({best_tracker, best_detection});
            tracker_assigned[best_tracker] = true;
            detection_assigned[best_detection] = true;
        } else {
            break;
        }
    }
    
    return assignments;
}

static float calculate_iou(const byte_track::Rect<float>& box1, const byte_track::Rect<float>& box2) {
    float xA = std::max(box1.x(), box2.x());
    float yA = std::max(box1.y(), box2.y());
    float xB = std::min(box1.x() + box1.width(), box2.x() + box2.width());
    float yB = std::min(box1.y() + box1.height(), box2.y() + box1.height());
    float intersection_area = std::max(0.0f, xB - xA) * std::max(0.0f, yB - yA);
    float box1_area = box1.width() * box1.height();
    float box2_area = box2.width() * box2.height();
    float union_area = box1_area + box2_area - intersection_area;
    return (union_area > 0) ? intersection_area / union_area : 0.0f;
}

DNNTracker::DNNTracker(const std::string& ball_model_path, const std::string& pose_model_path, const std::string& device_name) {
    std::cout << "Loading OpenVINO ball model: " << ball_model_path << std::endl;
    std::cout << "Compiling ball model for device: " << device_name << std::endl;
    ball_compiled_model = core.compile_model(ball_model_path, device_name);
    ball_infer_request = ball_compiled_model.create_infer_request();
    std::cout << "Ball model loaded successfully." << std::endl;
    
    std::cout << "Loading OpenVINO pose model: " << pose_model_path << std::endl;
    std::cout << "Compiling pose model for device: " << device_name << std::endl;
    pose_compiled_model = core.compile_model(pose_model_path, device_name);
    pose_infer_request = pose_compiled_model.create_infer_request();
    std::cout << "Pose model loaded successfully." << std::endl;

    reinitialize_tracker();
    initialize_logical_trackers();
    
    // Initialize adaptive color manager with default color profiles
    juggler::AdaptationConfig adaptive_config;
    adaptive_config.enabled = true;
    adaptive_color_manager_ = std::make_unique<juggler::AdaptiveColorManager>(adaptive_config);
    
    // Initialize with default color profiles
    std::vector<cv::Scalar> min_hsv_values = {
        cv::Scalar(45, 50, 50),   // green
        cv::Scalar(140, 50, 50),  // pink
        cv::Scalar(5, 50, 50)     // orange
    };
    std::vector<cv::Scalar> max_hsv_values = {
        cv::Scalar(75, 255, 255),   // green
        cv::Scalar(175, 255, 255),  // pink
        cv::Scalar(20, 255, 255)    // orange
    };
    std::vector<std::string> color_names = {"green", "pink", "orange"};
    std::vector<bool> enabled_states = {true, true, true};
    
    adaptive_color_manager_->initializeFromProfiles(min_hsv_values, max_hsv_values,
                                                     color_names, enabled_states);
    
    // Initialize throw/catch detector
    throw_catch_detector_ = std::make_unique<juggler::ThrowCatchDetector>();
    
    last_update_time_ = std::chrono::steady_clock::now();
}

DNNTracker::~DNNTracker() {}

void DNNTracker::initialize_logical_trackers() {
    logical_ball_trackers_.clear();
    for (int i = 0; i < NUM_BALLS; ++i) {
        logical_ball_trackers_.emplace_back(i, "ball");
    }

    logical_hand_trackers_.clear();
    for (int i = 0; i < NUM_HANDS; ++i) {
        logical_hand_trackers_.emplace_back(i + NUM_BALLS, "hand"); // Give hands unique IDs
    }
}

std::pair<std::vector<TrackedObject>, std::vector<TrackedHand>> DNNTracker::update(const cv::Mat& color_frame, const cv::Mat& depth_frame, const CameraIntrinsics& intrinsics) {
    // Store color frame for calibration
    last_color_frame_ = color_frame.clone();
    
    // Clear unmatched detections at the start of each frame
    unmatched_detections_.clear();
    
    // Clear visualization data from previous frame
    predicted_positions_.clear();
    predicted_tracker_labels_.clear();
    filtered_detections_.clear();
    filter_reasons_.clear();
    tracker_associations_.clear();
    association_distances_.clear();
    newly_initialized_tracker_ids_.clear();
    new_tracker_positions_.clear();
    
    auto current_time = std::chrono::steady_clock::now();
    float dt = std::chrono::duration_cast<std::chrono::duration<float>>(current_time - last_update_time_).count();
    last_update_time_ = current_time;

    // --- 1. PREDICT ---
    for (auto& ball : logical_ball_trackers_) {
        if (ball.status != TrackerStatus::LOST) {
            if (ball.is_in_freefall) {
                ball.kf.predict_ball(dt);
            } else {
                ball.kf.predict(dt); // Constant velocity prediction if held or stationary
            }
        }
    }
    for (auto& hand : logical_hand_trackers_) {
        if (hand.status != TrackerStatus::LOST) hand.kf.predict(dt);
    }
    
    // NOTE: Predicted positions for visualization are now stored AFTER the update step
    // (see lines after 3D matching) to show FUTURE predictions instead of current state

    // --- 2. DETECT BALLS ---
    float scale_x, scale_y;
    cv::Mat preprocessed_image = preprocess(color_frame, scale_x, scale_y);
    ov::Tensor input_tensor(ball_compiled_model.input().get_element_type(), ball_compiled_model.input().get_shape(), preprocessed_image.data);
    ball_infer_request.set_input_tensor(input_tensor);
    ball_infer_request.infer();
    const ov::Tensor& output_tensor = ball_infer_request.get_output_tensor();
    last_raw_detections_.clear();
    std::vector<byte_track::Object> detections_for_bytetrack = postprocess_ball_detection(color_frame, depth_frame, intrinsics, output_tensor, scale_x, scale_y, last_raw_detections_);

    std::ofstream debug_log("engine_debug.log", std::ios::app);
    debug_log << "\n========================================" << std::endl;
    debug_log << "3D MATCHING CODE IS RUNNING!" << std::endl;
    debug_log << "Raw detections: " << last_raw_detections_.size() << std::endl;
    debug_log << "========================================\n" << std::endl;
    
    // --- 3. DIRECT 3D DISTANCE-BASED ASSOCIATION ---
    // Build cost matrix: [tracker][detection] = 3D distance
    std::vector<PersistentTracker*> ball_trackers_list;
    for (auto& ball : logical_ball_trackers_) {
        if (ball.status != TrackerStatus::LOST) {
            ball_trackers_list.push_back(&ball);
        }
    }
    
    // Filter valid BALL detections only - exclude hands completely
    std::vector<const Detection*> valid_detections;
    for (const auto& det : last_raw_detections_) {
        // CRITICAL: Only track balls (class_id 0 or 1), never hands (class_id 3)
        if (det.class_id == 3) {
            filtered_detections_.push_back(det);
            filter_reasons_.push_back("Hand detection - excluded from ball tracking");
            continue; // Skip hands entirely
        }
        
        // Check depth validity
        if (det.world_pos.z <= 0.2f) {
            filtered_detections_.push_back(det);
            filter_reasons_.push_back("Too close (<0.2m)");
        } else if (det.world_pos.z >= 2.0f) {
            filtered_detections_.push_back(det);
            filter_reasons_.push_back("Too far (>2.0m)");
        } else if (det.world_pos.z == 0.0f) {
            filtered_detections_.push_back(det);
            filter_reasons_.push_back("No depth");
        } else {
            // Valid ball detection
            valid_detections.push_back(&det);
        }
    }
    
    // Remove duplicate detections (identical or nearly identical bboxes)
    std::vector<const Detection*> unique_detections;
    std::ofstream dedup_log("engine_debug.log", std::ios::app);
    dedup_log << "[DEDUP] Filtering duplicates from " << valid_detections.size() << " detections..." << std::endl;

    for (const auto* det : valid_detections) {
        bool is_duplicate = false;
        for (const auto* existing : unique_detections) {
            // Check if bboxes are nearly identical (within 10 pixels tolerance)
            float bbox_diff = std::abs(det->box.x - existing->box.x) +
                             std::abs(det->box.y - existing->box.y) +
                             std::abs(det->box.width - existing->box.width) +
                             std::abs(det->box.height - existing->box.height);
            
            if (bbox_diff < 10.0f) {  // Sum of differences < 10 pixels = duplicate
                is_duplicate = true;
                dedup_log << "[DEDUP] Rejected duplicate: bbox[" << det->box.x << "," << det->box.y
                          << "," << det->box.width << "," << det->box.height << "] matches existing" << std::endl;
                // Store duplicate for visualization
                filtered_detections_.push_back(*det);
                filter_reasons_.push_back("Duplicate detection");
                break;
            }
        }
        
        if (!is_duplicate) {
            unique_detections.push_back(det);
            dedup_log << "[DEDUP] Kept unique detection: bbox[" << det->box.x << "," << det->box.y
                      << "," << det->box.width << "," << det->box.height << "]" << std::endl;
        }
    }

    dedup_log << "[DEDUP] Result: " << unique_detections.size() << " unique detections from "
              << valid_detections.size() << " total" << std::endl;
    dedup_log.close();

    // Replace valid_detections with unique_detections for all subsequent operations
    valid_detections = unique_detections;
    
    debug_log << "\n[DETECTION DEBUG] ==================" << std::endl;
    debug_log << "[DETECTION DEBUG] Total raw detections: " << last_raw_detections_.size() << std::endl;
    debug_log << "[DETECTION DEBUG] Valid detections after filtering: " << valid_detections.size() << std::endl;
    
    // Log each raw detection's status
    for (size_t i = 0; i < last_raw_detections_.size(); ++i) {
        const auto& det = last_raw_detections_[i];
        bool passed_filter = (det.class_id != 3 && det.world_pos.z > 0.2f && det.world_pos.z < 2.0f);
        debug_log << "[DETECTION DEBUG] Detection " << i
                  << " - class_id: " << det.class_id
                  << ", depth: " << det.world_pos.z << "m"
                  << ", bbox: [" << det.box.x << "," << det.box.y << "," << det.box.width << "," << det.box.height << "]"
                  << ", passed_filter: " << (passed_filter ? "YES" : "NO");
        
        if (!passed_filter) {
            debug_log << " (REJECTED: ";
            if (det.class_id == 3) debug_log << "is_hand ";
            if (det.world_pos.z <= 0.2f) debug_log << "depth_too_close ";
            if (det.world_pos.z >= 2.0f) debug_log << "depth_too_far ";
            if (det.world_pos.z == 0.0f) debug_log << "no_depth ";
            debug_log << ")";
        }
        debug_log << std::endl;
    }
    debug_log << "[DETECTION DEBUG] ==================\n" << std::endl;
    
    debug_log << "[3D Matching] " << ball_trackers_list.size() << " active trackers, "
              << valid_detections.size() << " valid detections" << std::endl;
    
    // Mark all tracked as predicted initially
    for (auto* tracker : ball_trackers_list) {
        if (tracker->status == TrackerStatus::TRACKED) {
            tracker->status = TrackerStatus::PREDICTED;
        }
        tracker->frames_since_seen++;
    }
    
    // Log predicted positions for each tracker
    for (size_t i = 0; i < ball_trackers_list.size(); ++i) {
        Eigen::Vector3d pred = ball_trackers_list[i]->position;
        debug_log << "[3D MATCH] Tracker " << ball_trackers_list[i]->logical_id
                  << " predicted at (" << pred.x() << ", " << pred.y() << ", " << pred.z() << ")"
                  << " status=" << static_cast<int>(ball_trackers_list[i]->status) << std::endl;
    }
    
    // --- NEW: COLOR-DOMINATED ASSIGNMENT ---
    // Instead of using Kalman predictions for matching, use color dominance as PRIMARY identity
    if (!ball_trackers_list.empty() && !valid_detections.empty()) {
        std::ofstream color_log("engine_debug.log", std::ios::app);
        color_log << "\n[COLOR-DOMINATED] Starting color-based assignment" << std::endl;
        color_log << "[COLOR-DOMINATED] " << ball_trackers_list.size() << " active trackers, "
                  << valid_detections.size() << " valid detections" << std::endl;
        
        // Step 1: Compute color dominance scores for ALL detections (do this once)
        std::vector<ColorScores> detection_color_scores;
        std::vector<float> detection_hues;  // Store hue values for adaptive monitoring
        
        for (size_t j = 0; j < valid_detections.size(); ++j) {
            ColorScores scores = compute_color_dominance(last_color_frame_, *valid_detections[j],
                                                         adaptive_color_manager_.get());
            detection_color_scores.push_back(scores);
            
            // Extract hue value for this detection
            cv::Point2f center(
                valid_detections[j]->box.x + valid_detections[j]->box.width / 2.0f,
                valid_detections[j]->box.y + valid_detections[j]->box.height / 2.0f
            );
            
            if (center.x >= 0 && center.x < last_color_frame_.cols &&
                center.y >= 0 && center.y < last_color_frame_.rows) {
                cv::Mat hsv_frame;
                cv::cvtColor(last_color_frame_, hsv_frame, cv::COLOR_BGR2HSV);
                cv::Vec3b hsv_pixel = hsv_frame.at<cv::Vec3b>(static_cast<int>(center.y),
                                                               static_cast<int>(center.x));
                detection_hues.push_back(static_cast<float>(hsv_pixel[0]));
            } else {
                detection_hues.push_back(0.0f);
            }
            
            color_log << "[COLOR-DOMINATED] Detection " << j << " color scores: "
                      << "green=" << scores.green_score << " "
                      << "orange=" << scores.orange_score << " "
                      << "pink=" << scores.pink_score << " "
                      << "yellow=" << scores.yellow_score << " "
                      << "red=" << scores.red_score << " "
                      << "blue=" << scores.blue_score << std::endl;
        }
        
        // Step 2: For each tracker with a color assignment, find the detection with HIGHEST score for that color
        std::set<int> matched_detection_indices;
        
        for (auto* tracker : ball_trackers_list) {
            if (!tracker->has_color_assignment || tracker->assigned_color_name.empty()) {
                color_log << "[COLOR-DOMINATED] Tracker " << tracker->logical_id
                          << " has no color assignment - skipping" << std::endl;
                continue;
            }
            
            color_log << "[COLOR-DOMINATED] Tracker " << tracker->logical_id
                      << " looking for color '" << tracker->assigned_color_name << "'" << std::endl;
            
            // Find detection with HIGHEST RELATIVE score for this tracker's color
            // Key change: We look for the detection that is MOST of this color compared to other colors
            int best_detection_idx = -1;
            float best_relative_score = -1.0f; // Can be negative if color is weak
            
            for (size_t j = 0; j < valid_detections.size(); ++j) {
                // Skip already matched detections
                if (matched_detection_indices.count(j) > 0) continue;
                
                const ColorScores& scores = detection_color_scores[j];
                float target_score = get_score_for_color(scores, tracker->assigned_color_name);
                
                // Calculate how much MORE this detection is of the target color vs other colors
                // This is the key: we want the detection that is MOST pink, not just has SOME pink
                float max_other_score = 0.0f;
                std::vector<std::string> other_colors = {"green", "orange", "pink", "yellow", "red", "blue", "purple", "white"};
                for (const auto& other_color : other_colors) {
                    if (other_color != tracker->assigned_color_name) {
                        float other_score = get_score_for_color(scores, other_color);
                        max_other_score = std::max(max_other_score, other_score);
                    }
                }
                
                // Relative score = how much more of target color than any other color
                float relative_score = target_score - max_other_score;
                
                color_log << "[COLOR-DOMINATED]   Detection " << j << " for " << tracker->assigned_color_name
                          << ": target=" << target_score << ", max_other=" << max_other_score
                          << ", relative=" << relative_score << std::endl;
                
                if (relative_score > best_relative_score) {
                    best_relative_score = relative_score;
                    best_detection_idx = j;
                }
            }
            
            // Lower threshold for relative scoring - even slightly more pink is enough
            float best_score = best_relative_score; // For compatibility with existing code
            
            // If we found a match, assign it
            if (best_detection_idx >= 0) {
                const auto* detection = valid_detections[best_detection_idx];
                
                color_log << "[COLOR-DOMINATED] ✓ Tracker " << tracker->logical_id
                          << " matched to detection " << best_detection_idx
                          << " with relative score " << best_relative_score << std::endl;
                
                // Store association for visualization
                tracker_associations_.push_back({tracker->logical_id, best_detection_idx});
                association_distances_.push_back(0.0f); // Not using distance anymore
                
                // Mark detection as matched
                matched_detection_indices.insert(best_detection_idx);
                
                // Update Kalman filter with detection position (for smoothing only)
                tracker->kf.update(KalmanFilter3D::MeasurementVector(
                    detection->world_pos.x, detection->world_pos.y, detection->world_pos.z));
                tracker->update_from_kf();  // Sync position field with updated Kalman state
                tracker->status = TrackerStatus::TRACKED;
                tracker->box_2d = detection->box;
                tracker->frames_since_seen = 0;
                tracker->parent_id = -1;
                
                debug_log << "[COLOR-DOMINATED] Tracker " << tracker->logical_id
                          << " (" << tracker->assigned_color_name << ") matched to detection "
                          << best_detection_idx << " with color score " << best_score << std::endl;
            } else {
                // No detection found for this color - tracker goes LOST immediately
                color_log << "[COLOR-DOMINATED] ✗ Tracker " << tracker->logical_id
                          << " (" << tracker->assigned_color_name << ") NOT MATCHED (no detection with sufficient score)" << std::endl;
                
                debug_log << "[COLOR-DOMINATED] Tracker " << tracker->logical_id
                          << " NOT MATCHED (best relative score: " << best_relative_score << ")" << std::endl;
            }
        }
        
        // Store unmatched detections for visualization
        for (size_t i = 0; i < valid_detections.size(); ++i) {
            if (matched_detection_indices.find(i) == matched_detection_indices.end()) {
                unmatched_detections_.push_back(*valid_detections[i]);
            }
        }
        
        color_log << "[COLOR-DOMINATED] Assignment complete: " << matched_detection_indices.size()
                  << " detections matched, " << unmatched_detections_.size() << " unmatched" << std::endl;
        
        // --- ADAPTIVE COLOR MONITORING ---
        // Build map of matched colors for monitoring
        std::map<std::string, int> matched_colors;
        for (auto* tracker : ball_trackers_list) {
            if (tracker->status == TrackerStatus::TRACKED && tracker->has_color_assignment) {
                // Find which detection was matched to this tracker
                for (const auto& assoc : tracker_associations_) {
                    if (assoc.first == tracker->logical_id) {
                        matched_colors[tracker->assigned_color_name] = assoc.second;
                        break;
                    }
                }
            }
        }
        
        // Monitor this frame's tracking results
        if (adaptive_color_manager_) {
            adaptive_color_manager_->monitorFrame(matched_colors, detection_hues);
            
            // Periodically adjust ranges based on tracking success
            adaptive_color_manager_->adjustRanges();
            
            // Log success rates for debugging
            auto success_rates = adaptive_color_manager_->getSuccessRates();
            color_log << "[ADAPTIVE] Current success rates:" << std::endl;
            for (const auto& pair : success_rates) {
                color_log << "  " << pair.first << ": " << (pair.second * 100) << "%" << std::endl;
            }
        }
        
        color_log.close();
        debug_log.close();
    }
    
    // --- STORE FUTURE PREDICTIONS FOR VISUALIZATION ---
    // After updates are complete, predict where each tracker will be in the NEXT frame
    // This shows the Kalman filter's trajectory prediction ahead of the current position
    std::ofstream pred_log("engine_debug.log", std::ios::app);
    pred_log << "[PRED VIZ] Checking " << logical_ball_trackers_.size() << " ball trackers for predictions" << std::endl;
    
    for (auto& ball : logical_ball_trackers_) {
        pred_log << "[PRED VIZ]   Ball " << ball.logical_id << " status=" << static_cast<int>(ball.status)
                 << " (0=TRACKED, 1=PREDICTED, 2=OCCLUDED, 3=LOST)" << std::endl;
        
        if (ball.status == TrackerStatus::TRACKED || ball.status == TrackerStatus::PREDICTED) {
            // Get current state after update
            ball.update_from_kf();
            
            // Make a temporary prediction for next frame (for visualization only)
            KalmanFilter3D temp_kf = ball.kf;  // Copy the filter
            if (ball.is_in_freefall) {
                temp_kf.predict_ball(dt);  // Predict with gravity
            } else {
                temp_kf.predict(dt);  // Predict with constant velocity
            }
            
            // Store the FUTURE predicted position
            Eigen::Vector3f future_pos = temp_kf.get_position();
            predicted_positions_.push_back(cv::Point3f(future_pos.x(), future_pos.y(), future_pos.z()));
            predicted_tracker_labels_.push_back("Ball " + std::to_string(ball.logical_id));
            
            pred_log << "[PRED VIZ]     -> Added prediction for Ball " << ball.logical_id
                     << " at (" << future_pos.x() << ", " << future_pos.y() << ", " << future_pos.z() << ")" << std::endl;
        }
    }
    
    pred_log << "[PRED VIZ] Total predictions added: " << predicted_positions_.size() << std::endl;
    pred_log.close();
    
    for (auto& hand : logical_hand_trackers_) {
        if (hand.status == TrackerStatus::TRACKED || hand.status == TrackerStatus::PREDICTED) {
            // Get current state after update
            hand.update_from_kf();
            
            // Make a temporary prediction for next frame (for visualization only)
            KalmanFilter3D temp_kf = hand.kf;  // Copy the filter
            temp_kf.predict(dt);  // Predict with constant velocity
            
            // Store the FUTURE predicted position
            Eigen::Vector3f future_pos = temp_kf.get_position();
            predicted_positions_.push_back(cv::Point3f(future_pos.x(), future_pos.y(), future_pos.z()));
            predicted_tracker_labels_.push_back("Hand " + std::to_string(hand.logical_id));
        }
    }
    
    // Auto-initialize trackers based on enabled color profiles (Step 6: Auto-Init)
    // NEW APPROACH: Create one tracker per enabled color profile, not just when all trackers are lost
    std::ofstream auto_init_log("engine_debug.log", std::ios::app);
    
    // Get enabled color profiles from adaptive manager
    std::vector<std::string> enabled_colors;
    if (adaptive_color_manager_) {
        const auto& profiles = adaptive_color_manager_->getProfiles();
        for (const auto& profile : profiles) {
            if (profile.enabled) {
                enabled_colors.push_back(profile.name);
            }
        }
    }
    
    auto_init_log << "[AUTO-INIT] Enabled color profiles: " << enabled_colors.size() << std::endl;
    for (const auto& color : enabled_colors) {
        auto_init_log << "[AUTO-INIT]   - " << color << std::endl;
    }
    
    // Check which colors need trackers
    std::vector<std::string> colors_needing_trackers;
    for (const auto& color : enabled_colors) {
        bool has_tracker = false;
        for (const auto& ball : logical_ball_trackers_) {
            if (ball.status != TrackerStatus::LOST &&
                ball.has_color_assignment &&
                ball.assigned_color_name == color) {
                has_tracker = true;
                break;
            }
        }
        if (!has_tracker) {
            colors_needing_trackers.push_back(color);
        }
    }
    
    auto_init_log << "[AUTO-INIT] Colors needing trackers: " << colors_needing_trackers.size() << std::endl;
    for (const auto& color : colors_needing_trackers) {
        auto_init_log << "[AUTO-INIT]   - " << color << std::endl;
    }
    
    // NEW APPROACH: Use relative color matching - find which detection is "most" green/pink/orange
    if (!colors_needing_trackers.empty() && !valid_detections.empty()) {
        auto_init_log << "[AUTO-INIT] Using RELATIVE color matching for missing colors..." << std::endl;
        
        // Compute color scores for all unmatched detections
        struct DetectionWithScores {
            const Detection* detection;
            size_t index;
            ColorScores scores;
            bool already_matched;
        };
        
        std::vector<DetectionWithScores> detection_scores;
        for (size_t j = 0; j < valid_detections.size(); ++j) {
            DetectionWithScores dws;
            dws.detection = valid_detections[j];
            dws.index = j;
            dws.scores = compute_color_dominance(last_color_frame_, *valid_detections[j],
                                                 adaptive_color_manager_.get());
            
            // Check if already matched to an active tracker
            dws.already_matched = false;
            for (const auto& ball : logical_ball_trackers_) {
                if (ball.status == TrackerStatus::TRACKED) {
                    float dist = calculate_distance(ball.position, valid_detections[j]->world_pos);
                    if (dist < 0.20f) {
                        dws.already_matched = true;
                        break;
                    }
                }
            }
            
            detection_scores.push_back(dws);
        }
        
        // For each color that needs a tracker, find the detection with highest RELATIVE score
        for (const auto& color : colors_needing_trackers) {
            const Detection* best_detection = nullptr;
            float best_relative_score = -999.0f;
            
            for (const auto& dws : detection_scores) {
                if (dws.already_matched) continue;
                
                float target_score = get_score_for_color(dws.scores, color);
                
                // Calculate relative dominance
                float max_other_score = 0.0f;
                std::vector<std::string> other_colors = {"green", "orange", "pink", "yellow", "red", "blue", "purple", "white"};
                for (const auto& other_color : other_colors) {
                    if (other_color != color) {
                        float other_score = get_score_for_color(dws.scores, other_color);
                        max_other_score = std::max(max_other_score, other_score);
                    }
                }
                
                float relative_score = target_score - max_other_score;
                
                if (relative_score > best_relative_score) {
                    best_relative_score = relative_score;
                    best_detection = dws.detection;
                }
            }
            
            // Initialize if this detection is more of the target color than any other color
            // Even a small positive relative score means it's the "most" of that color
            if (best_detection && best_relative_score > -0.5f) {
                // Find an available (LOST) tracker slot
                PersistentTracker* available_tracker = nullptr;
                for (auto& ball : logical_ball_trackers_) {
                    if (ball.status == TrackerStatus::LOST) {
                        available_tracker = &ball;
                        break;
                    }
                }
                
                if (available_tracker) {
                    // Initialize the tracker
                    available_tracker->kf.init(KalmanFilter3D::MeasurementVector(
                        best_detection->world_pos.x,
                        best_detection->world_pos.y,
                        best_detection->world_pos.z
                    ));
                    
                    available_tracker->status = TrackerStatus::TRACKED;
                    available_tracker->frames_since_seen = 0;
                    available_tracker->box_2d = best_detection->box;
                    available_tracker->parent_id = -1;
                    available_tracker->assigned_color_name = color;
                    available_tracker->has_color_assignment = true;
                    
                    // Mark this detection as matched
                    for (auto& dws : detection_scores) {
                        if (dws.detection == best_detection) {
                            dws.already_matched = true;
                            break;
                        }
                    }
                    
                    // Store for visualization
                    newly_initialized_tracker_ids_.push_back(available_tracker->logical_id);
                    new_tracker_positions_.push_back(best_detection->world_pos);
                    
                    auto_init_log << "[AUTO-INIT] ✅ Initialized tracker " << available_tracker->logical_id
                              << " for color '" << color << "' with relative dominance score " << best_relative_score
                              << " at position (" << best_detection->world_pos.x << ", "
                              << best_detection->world_pos.y << ", " << best_detection->world_pos.z << ")"
                              << std::endl;
                } else {
                    auto_init_log << "[AUTO-INIT] ⚠️ No available tracker slot for color '" << color << "'" << std::endl;
                }
            } else {
                auto_init_log << "[AUTO-INIT] ⚠️ No detection found for color '" << color
                          << "' (best relative score: " << best_relative_score << ")" << std::endl;
            }
        }
    }
    
    auto_init_log.close();
    
    // Handle hand tracking (keep ByteTrack for hands as they move slower)
    std::vector<Detection> hand_detections;
    for (const auto& det : last_raw_detections_) {
        if (det.class_id == 3) {
            hand_detections.push_back(det);
        }
    }
    manage_hand_tracks(hand_detections);

    // --- 4. RUN THROW/CATCH DETECTION ---
    detected_events_ = throw_catch_detector_->detectEvents(
        logical_ball_trackers_, logical_hand_trackers_, last_raw_detections_, dt);
    
    // --- 5. MANAGE HEURISTICS (Legacy occlusion handling) ---
    // Note: manage_ball_occlusion() is now largely replaced by ThrowCatchDetector
    // but we keep it for backward compatibility and edge cases
    manage_ball_occlusion();

    // --- 6. COMPILE FINAL RESULTS ---
    std::vector<PersistentTracker*> all_logical_trackers;
    for(auto& ball : logical_ball_trackers_) all_logical_trackers.push_back(&ball);
    for(auto& hand : logical_hand_trackers_) all_logical_trackers.push_back(&hand);
    
    std::vector<TrackedObject> final_tracked_objects;
    for(auto* tracker : all_logical_trackers) {
        if (tracker->status == TrackerStatus::LOST) continue;
        
        tracker->update_from_kf();
        auto pos = tracker->position;

        final_tracked_objects.push_back({
            tracker->box_2d,
            cv::Point3f(pos.x(), pos.y(), pos.z()),
            tracker->last_seen_bytetrack_id,
            -1, // class id
            tracker->class_name,
            tracker->status,
            tracker->logical_id,
            tracker->is_left_hand
        });
    }

    // --- 7. RUN POSE ESTIMATION ---
    std::vector<TrackedHand> tracked_hands;
    if (pose_model_enabled_) {
        tracked_hands = run_pose_estimation(color_frame, depth_frame, intrinsics);
    }

    return {final_tracked_objects, tracked_hands};
}


void DNNTracker::manage_hand_tracks(const std::vector<Detection>& hand_detections) {
    // This logic is now stateful. It tries to maintain left/right assignment.
    PersistentTracker* left_hand = nullptr;
    PersistentTracker* right_hand = nullptr;
    for(auto& hand : logical_hand_trackers_) {
        if (hand.is_left_hand) left_hand = &hand;
        else right_hand = &hand;
    }

    // Simple assignment based on x-coordinate for now if both are visible
    if (logical_hand_trackers_[0].status == TrackerStatus::TRACKED && logical_hand_trackers_[1].status == TrackerStatus::TRACKED) {
         if (logical_hand_trackers_[0].position.x() < logical_hand_trackers_[1].position.x()) {
            logical_hand_trackers_[0].is_left_hand = true;
            logical_hand_trackers_[1].is_left_hand = false;
         } else {
            logical_hand_trackers_[0].is_left_hand = false;
            logical_hand_trackers_[1].is_left_hand = true;
         }
    }
}


void DNNTracker::manage_ball_occlusion() {
    const float CATCH_THRESHOLD = 0.15f; // 15cm distance threshold for a catch
    const float THROW_THRESHOLD = 0.20f; // 20cm distance threshold for a throw

    PersistentTracker* left_hand = nullptr;
    PersistentTracker* right_hand = nullptr;
    for(auto& hand : logical_hand_trackers_) {
        if (hand.is_left_hand) left_hand = &hand;
        else right_hand = &hand;
    }

    for (auto& ball : logical_ball_trackers_) {
        // --- CATCH LOGIC ---
        // A predicted (unseen) ball is considered caught if it gets close to a tracked hand.
        if (ball.status == TrackerStatus::PREDICTED) {
            ball.update_from_kf();
            cv::Point3f predicted_ball_pos(ball.position.x(), ball.position.y(), ball.position.z());
            
            auto check_catch = [&](PersistentTracker* hand) {
                if (hand && hand->status == TrackerStatus::TRACKED) {
                    hand->update_from_kf();
                    cv::Point3f hand_pos(hand->position.x(), hand->position.y(), hand->position.z());
                    if (calculate_distance(predicted_ball_pos, hand_pos) < CATCH_THRESHOLD) {
                        ball.status = TrackerStatus::OCCLUDED;
                        ball.parent_id = hand->logical_id;
                        ball.is_in_freefall = false; // The ball has been caught, stop gravity.
                        // Snap ball position and velocity to the hand's state
                        ball.position = hand->position;
                        KalmanFilter3D::StateVector hand_state = hand->kf.get_state();
                        KalmanFilter3D::StateVector& ball_state = ball.kf.get_state();
                        ball_state.tail<3>() = hand_state.tail<3>();
                        return true;
                    }
                }
                return false;
            };

            if (check_catch(left_hand)) continue;
            check_catch(right_hand);
        }

        // --- THROW LOGIC ---
        // An occluded ball is considered thrown if it becomes tracked again far from its parent hand.
        if (ball.status == TrackerStatus::TRACKED && ball.parent_id != -1) {
             PersistentTracker* parent_hand = nullptr;
             for(auto& hand : logical_hand_trackers_) {
                 if(hand.logical_id == ball.parent_id) {
                     parent_hand = &hand;
                     break;
                 }
             }

            if (parent_hand && parent_hand->status == TrackerStatus::TRACKED) {
                ball.update_from_kf();
                parent_hand->update_from_kf();
                cv::Point3f ball_pos(ball.position.x(), ball.position.y(), ball.position.z());
                cv::Point3f hand_pos(parent_hand->position.x(), parent_hand->position.y(), parent_hand->position.z());

                if (calculate_distance(ball_pos, hand_pos) > THROW_THRESHOLD) {
                    ball.is_in_freefall = true; // The ball has been thrown, start gravity.
                    ball.parent_id = -1; // It is no longer associated with the hand.
                }
            } else {
                // If the parent hand is lost, the ball is also considered thrown.
                ball.is_in_freefall = true;
                ball.parent_id = -1;
            }
        }
    }
}

void DNNTracker::calibrate_object(int logical_id, const cv::Point2f& pixel_coords, const cv::Mat& depth_frame, const CameraIntrinsics& intrinsics) {
    float min_dist = 20.0f; // 20 pixel search radius
    const Detection* closest_det = nullptr;

    for(const auto& det : last_raw_detections_) {
        cv::Point2f center(det.box.x + det.box.width / 2, det.box.y + det.box.height / 2);
        float dist = cv::norm(pixel_coords - center);
        if (dist < min_dist) {
            min_dist = dist;
            closest_det = &det;
        }
    }

    if (closest_det) {
        // Find the logical tracker
        for (auto& tracker : logical_ball_trackers_) {
            if (tracker.logical_id == logical_id) {
                if (closest_det->world_pos.z > 0.2 && closest_det->world_pos.z < 2.0) {
                     tracker.kf.init(KalmanFilter3D::MeasurementVector(closest_det->world_pos.x, closest_det->world_pos.y, closest_det->world_pos.z));
                     tracker.status = TrackerStatus::TRACKED;
                     tracker.frames_since_seen = 0;
                     tracker.is_in_freefall = false; // When calibrating, assume it's held/stationary.
                     std::cout << "Calibrated Ball " << logical_id << " at " << closest_det->world_pos << std::endl;
                }
                return;
            }
        }
    }
}

bool DNNTracker::calibrate_color(const std::string& color_name, const cv::Point& click_point, std::string& error_message) {
    if (!color_tracker_) {
        error_message = "Color tracker not initialized";
        std::cerr << "DNNTracker: " << error_message << std::endl;
        return false;
    }
    
    if (last_color_frame_.empty()) {
        error_message = "No color frame available for calibration";
        std::cerr << "DNNTracker: " << error_message << std::endl;
        return false;
    }
    
    // Find the YOLO detection box that contains the click point
    const Detection* clicked_detection = nullptr;
    for (const auto& det : last_raw_detections_) {
        // Check if click point is inside this detection box
        if (click_point.x >= det.box.x &&
            click_point.x <= (det.box.x + det.box.width) &&
            click_point.y >= det.box.y &&
            click_point.y <= (det.box.y + det.box.height)) {
            clicked_detection = &det;
            break;
        }
    }
    
    if (!clicked_detection) {
        error_message = "No YOLO detection box found at click location (" +
                       std::to_string(click_point.x) + "," + std::to_string(click_point.y) + "). " +
                       "Please click directly on a detected ball.";
        std::cerr << "DNNTracker: " << error_message << std::endl;
        return false;
    }
    
    std::cout << "DNNTracker: Found YOLO detection box at click location: "
              << "x=" << clicked_detection->box.x << ", y=" << clicked_detection->box.y
              << ", w=" << clicked_detection->box.width << ", h=" << clicked_detection->box.height
              << std::endl;
    
    // Convert current frame to HSV
    cv::Mat hsv_frame;
    cv::cvtColor(last_color_frame_, hsv_frame, cv::COLOR_BGR2HSV);
    
    // Sample colors from the entire YOLO detection box
    // Extract the region of interest (ROI) from the detection box
    cv::Rect roi_rect(
        std::max(0, static_cast<int>(clicked_detection->box.x)),
        std::max(0, static_cast<int>(clicked_detection->box.y)),
        std::min(static_cast<int>(clicked_detection->box.width),
                 hsv_frame.cols - static_cast<int>(clicked_detection->box.x)),
        std::min(static_cast<int>(clicked_detection->box.height),
                 hsv_frame.rows - static_cast<int>(clicked_detection->box.y))
    );
    
    // Ensure ROI is valid
    if (roi_rect.width <= 0 || roi_rect.height <= 0) {
        error_message = "Invalid detection box dimensions";
        std::cerr << "DNNTracker: " << error_message << std::endl;
        return false;
    }
    
    cv::Mat roi = hsv_frame(roi_rect);
    
    // Calculate statistics from the ROI
    std::vector<float> hue_values;
    std::vector<float> sat_values;
    std::vector<float> val_values;
    
    // Sample pixels from the ROI (use every pixel for better accuracy)
    for (int y = 0; y < roi.rows; y++) {
        for (int x = 0; x < roi.cols; x++) {
            cv::Vec3b hsv_pixel = roi.at<cv::Vec3b>(y, x);
            hue_values.push_back(static_cast<float>(hsv_pixel[0]));
            sat_values.push_back(static_cast<float>(hsv_pixel[1]));
            val_values.push_back(static_cast<float>(hsv_pixel[2]));
        }
    }
    
    if (hue_values.empty()) {
        error_message = "No valid pixels found in detection box";
        std::cerr << "DNNTracker: " << error_message << std::endl;
        return false;
    }
    
    // Calculate mean and standard deviation for each channel
    auto calc_stats = [](const std::vector<float>& values) -> std::pair<float, float> {
        float sum = std::accumulate(values.begin(), values.end(), 0.0f);
        float mean = sum / values.size();
        
        float sq_sum = 0.0f;
        for (float val : values) {
            sq_sum += (val - mean) * (val - mean);
        }
        float stddev = std::sqrt(sq_sum / values.size());
        
        return {mean, stddev};
    };
    
    auto [mean_h, stddev_h] = calc_stats(hue_values);
    auto [mean_s, stddev_s] = calc_stats(sat_values);
    auto [mean_v, stddev_v] = calc_stats(val_values);
    
    std::cout << "DNNTracker: Sampled " << hue_values.size() << " pixels from detection box" << std::endl;
    std::cout << "  Hue: mean=" << mean_h << ", stddev=" << stddev_h << std::endl;
    std::cout << "  Sat: mean=" << mean_s << ", stddev=" << stddev_s << std::endl;
    std::cout << "  Val: mean=" << mean_v << ", stddev=" << stddev_v << std::endl;
    
    // Define range based on mean ± 2*stddev (covers ~95% of values)
    // Use a minimum range to avoid too-narrow ranges
    float hue_tolerance = std::max(15.0f, stddev_h * 2.0f);
    float sat_tolerance = std::max(50.0f, stddev_s * 2.0f);
    float val_tolerance = std::max(50.0f, stddev_v * 2.0f);
    
    float min_hue = std::max(0.0f, mean_h - hue_tolerance);
    float max_hue = std::min(180.0f, mean_h + hue_tolerance);
    float min_sat = std::max(0.0f, mean_s - sat_tolerance);
    float max_sat = std::min(255.0f, mean_s + sat_tolerance);
    float min_val = std::max(0.0f, mean_v - val_tolerance);
    float max_val = std::min(255.0f, mean_v + val_tolerance);
    
    std::cout << "DNNTracker: Calculated HSV range for '" << color_name << "':" << std::endl;
    std::cout << "  H: [" << min_hue << ", " << max_hue << "]" << std::endl;
    std::cout << "  S: [" << min_sat << ", " << max_sat << "]" << std::endl;
    std::cout << "  V: [" << min_val << ", " << max_val << "]" << std::endl;
    
    // Update the color profile with the calculated range
    color_tracker_->calibrateColorFromRange(
        color_name,
        cv::Scalar(min_hue, min_sat, min_val),
        cv::Scalar(max_hue, max_sat, max_val)
    );
    
    color_tracker_->saveSettings();
    
    std::cout << "DNNTracker: Successfully calibrated color profile '" << color_name
              << "' from YOLO detection box" << std::endl;
    
    error_message = ""; // Clear error message on success
    return true;
}


void DNNTracker::update_setting(const std::string& key, const std::string& value) {
    try {
        if (key == "confidence_threshold") confidence_threshold_ = std::stof(value);
        else if (key == "nms_threshold") nms_threshold_ = std::stof(value);
        else if (key == "track_buffer") { track_buffer_ = std::stoi(value); reinitialize_tracker(); }
        else if (key == "pose_model_enabled") { pose_model_enabled_ = (value == "true"); }
        else if (key == "track_thresh") { track_thresh_ = std::stof(value); reinitialize_tracker(); }
        else if (key == "high_thresh") { high_thresh_ = std::stof(value); reinitialize_tracker(); }
        else if (key == "match_thresh") { match_thresh_ = std::stof(value); reinitialize_tracker(); }
        // Throw/Catch Detection Settings
        else if (key == "tc_ml_weight") {
            auto config = throw_catch_detector_->getConfig();
            config.ml_weight = std::stof(value);
            throw_catch_detector_->setConfig(config);
        }
        else if (key == "tc_proximity_weight") {
            auto config = throw_catch_detector_->getConfig();
            config.proximity_weight = std::stof(value);
            throw_catch_detector_->setConfig(config);
        }
        else if (key == "tc_kinematic_weight") {
            auto config = throw_catch_detector_->getConfig();
            config.kinematic_weight = std::stof(value);
            throw_catch_detector_->setConfig(config);
        }
        else if (key == "tc_relative_velocity_weight") {
            auto config = throw_catch_detector_->getConfig();
            config.relative_velocity_weight = std::stof(value);
            throw_catch_detector_->setConfig(config);
        }
        else if (key == "tc_catch_threshold") {
            auto config = throw_catch_detector_->getConfig();
            config.catch_threshold = std::stof(value);
            throw_catch_detector_->setConfig(config);
        }
        else if (key == "tc_throw_threshold") {
            auto config = throw_catch_detector_->getConfig();
            config.throw_threshold = std::stof(value);
            throw_catch_detector_->setConfig(config);
        }
        else if (key == "tc_catch_distance") {
            auto config = throw_catch_detector_->getConfig();
            config.catch_distance = std::stof(value);
            throw_catch_detector_->setConfig(config);
        }
        else if (key == "tc_throw_distance") {
            auto config = throw_catch_detector_->getConfig();
            config.throw_distance = std::stof(value);
            throw_catch_detector_->setConfig(config);
        }
        else if (key == "tc_min_frames") {
            auto config = throw_catch_detector_->getConfig();
            config.min_frames_for_event = std::stoi(value);
            throw_catch_detector_->setConfig(config);
        }
        // Adaptive Color Tracking Settings
        else if (key == "adaptive_enabled") {
            if (adaptive_color_manager_) {
                auto config = adaptive_color_manager_->getConfig();
                config.enabled = (value == "true" || value == "1");
                adaptive_color_manager_->setConfig(config);
                std::cout << "Adaptive color tracking " << (config.enabled ? "enabled" : "disabled") << std::endl;
            }
        }
        else if (key == "adaptive_success_threshold") {
            if (adaptive_color_manager_) {
                auto config = adaptive_color_manager_->getConfig();
                config.success_threshold = std::stof(value);
                adaptive_color_manager_->setConfig(config);
                std::cout << "Adaptive success threshold set to " << config.success_threshold << std::endl;
            }
        }
        else if (key == "adaptive_failure_threshold") {
            if (adaptive_color_manager_) {
                auto config = adaptive_color_manager_->getConfig();
                config.failure_threshold = std::stof(value);
                adaptive_color_manager_->setConfig(config);
                std::cout << "Adaptive failure threshold set to " << config.failure_threshold << std::endl;
            }
        }
        else if (key == "adaptive_expansion_step") {
            if (adaptive_color_manager_) {
                auto config = adaptive_color_manager_->getConfig();
                config.expansion_step = std::stof(value);
                adaptive_color_manager_->setConfig(config);
                std::cout << "Adaptive expansion step set to " << config.expansion_step << std::endl;
            }
        }
        else if (key == "adaptive_contraction_step") {
            if (adaptive_color_manager_) {
                auto config = adaptive_color_manager_->getConfig();
                config.contraction_step = std::stof(value);
                adaptive_color_manager_->setConfig(config);
                std::cout << "Adaptive contraction step set to " << config.contraction_step << std::endl;
            }
        }
        else if (key == "adaptive_history_window") {
            if (adaptive_color_manager_) {
                auto config = adaptive_color_manager_->getConfig();
                config.history_window_size = std::stoi(value);
                adaptive_color_manager_->setConfig(config);
                std::cout << "Adaptive history window set to " << config.history_window_size << " frames" << std::endl;
            }
        }
        // Color profile enable/disable settings
        else if (key.find("track_") == 0) {
            // Extract color name from key like "track_green"
            std::string color_name = key.substr(6); // Remove "track_" prefix
            bool enabled = (value == "true" || value == "1");
            
            if (adaptive_color_manager_) {
                adaptive_color_manager_->setProfileEnabled(color_name, enabled);
                std::cout << "Color profile '" << color_name << "' "
                          << (enabled ? "enabled" : "disabled") << std::endl;
            }
        }
        else std::cerr << "Warning: Unknown DNNTracker setting key '" << key << "'" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error updating setting " << key << " with value " << value << ": " << e.what() << std::endl;
    }
}

void DNNTracker::reinitialize_tracker() {
    int frame_rate = 30;
    tracker = std::make_unique<byte_track::BYTETracker>(frame_rate, track_buffer_, track_thresh_, high_thresh_, match_thresh_);
}

cv::Mat DNNTracker::preprocess(const cv::Mat& frame, float& scale_x, float& scale_y) {
    cv::Mat resized_frame;
    cv::resize(frame, resized_frame, cv::Size(input_width_, input_height_));
    scale_x = (float)frame.cols / input_width_;
    scale_y = (float)frame.rows / input_height_;
    cv::Mat float_frame;
    resized_frame.convertTo(float_frame, CV_32F, 1.0 / 255.0);
    return cv::dnn::blobFromImage(float_frame);
}

std::vector<byte_track::Object> DNNTracker::postprocess_ball_detection(const cv::Mat& color_frame, const cv::Mat& depth_frame, const CameraIntrinsics& intrinsics, const ov::Tensor& output_tensor, float scale_x, float scale_y, std::vector<Detection>& raw_detections) {
    raw_detections.clear();
    const float* output_data = output_tensor.data<const float>();
    const int num_channels = 4 + num_classes_;
    
    cv::Mat output_buffer(num_channels, output_tensor.get_shape()[2], CV_32F, (void*)output_data);
    cv::transpose(output_buffer, output_buffer);

    std::vector<cv::Rect> boxes;
    std::vector<float> confidences;
    std::vector<int> class_ids;
    
    float raw_detection_threshold = 0.1f;

    for (int i = 0; i < output_buffer.rows; ++i) {
        cv::Mat class_scores = output_buffer.row(i).colRange(4, num_channels);
        cv::Point class_id_point;
        double max_class_score;
        cv::minMaxLoc(class_scores, nullptr, &max_class_score, nullptr, &class_id_point);

        float confidence = static_cast<float>(max_class_score);
        
        float cx = output_buffer.at<float>(i, 0);
        float cy = output_buffer.at<float>(i, 1);
        float w = output_buffer.at<float>(i, 2);
        float h = output_buffer.at<float>(i, 3);
        int left = static_cast<int>((cx - 0.5 * w) * scale_x);
        int top = static_cast<int>((cy - 0.5 * h) * scale_y);
        int width = static_cast<int>(w * scale_x);
        int height = static_cast<int>(h * scale_y);

        if (confidence > confidence_threshold_) {
            boxes.push_back(cv::Rect(left, top, width, height));
            confidences.push_back(confidence);
            class_ids.push_back(class_id_point.x);
        }

        if (confidence > raw_detection_threshold) {
            cv::Point2f center_pixel(left + width / 2.0f, top + height / 2.0f);
            cv::Point3f world_pos(0,0,0);

            if (center_pixel.x >= 0 && center_pixel.x < depth_frame.cols &&
                center_pixel.y >= 0 && center_pixel.y < depth_frame.rows) {
                float depth_value_m = get_filtered_depth(depth_frame, center_pixel);
                world_pos = deproject_2d_to_3d(center_pixel, depth_value_m, intrinsics);
            }
            raw_detections.push_back({cv::Rect_<float>(left, top, width, height), world_pos, confidence, class_id_point.x});
        }
    }

    std::vector<int> nms_indices;
    cv::dnn::NMSBoxes(boxes, confidences, confidence_threshold_, nms_threshold_, nms_indices);

    std::vector<byte_track::Object> objects;
    for (int index : nms_indices) {
        objects.push_back({
            byte_track::Rect<float>(boxes[index].x, boxes[index].y, boxes[index].width, boxes[index].height),
            class_ids[index],
            confidences[index]
        });
    }
    
    return objects;
}

std::vector<TrackedHand> DNNTracker::run_pose_estimation(const cv::Mat& color_frame, const cv::Mat& depth_frame, const CameraIntrinsics& intrinsics) {
    std::vector<TrackedHand> tracked_hands;
    
    // --- 1. PREPROCESS ---
    float scale_x, scale_y;
    cv::Mat preprocessed_image = preprocess(color_frame, scale_x, scale_y);
    
    // --- 2. RUN INFERENCE ---
    ov::Tensor input_tensor(pose_compiled_model.input().get_element_type(),
                           pose_compiled_model.input().get_shape(),
                           preprocessed_image.data);
    pose_infer_request.set_input_tensor(input_tensor);
    pose_infer_request.infer();
    const ov::Tensor& output_tensor = pose_infer_request.get_output_tensor();
    
    // --- 3. PARSE YOLO-POSE OUTPUT ---
    // YOLO-Pose output format: [1, 56, N] where 56 = 4 (bbox) + 1 (conf) + 17*3 (keypoints x,y,conf)
    const float* output_data = output_tensor.data<const float>();
    const auto& shape = output_tensor.get_shape();
    
    if (shape.size() < 3) {
        std::cerr << "Unexpected pose output tensor shape" << std::endl;
        return tracked_hands;
    }
    
    const int num_channels = shape[1]; // Should be 56
    const int num_detections = shape[2];
    
    // Transpose to [N, 56] for easier processing
    cv::Mat output_buffer(num_channels, num_detections, CV_32F, (void*)output_data);
    cv::transpose(output_buffer, output_buffer);
    
    const float pose_confidence_threshold = 0.3f; // Threshold for person detection
    const float keypoint_confidence_threshold = 0.5f; // Threshold for individual keypoints
    
    // --- 4. PROCESS EACH PERSON DETECTION ---
    for (int i = 0; i < output_buffer.rows; ++i) {
        // Extract bounding box and confidence
        float cx = output_buffer.at<float>(i, 0);
        float cy = output_buffer.at<float>(i, 1);
        float w = output_buffer.at<float>(i, 2);
        float h = output_buffer.at<float>(i, 3);
        float person_confidence = output_buffer.at<float>(i, 4);
        
        if (person_confidence < pose_confidence_threshold) continue;
        
        // Scale back to original image coordinates
        int left = static_cast<int>((cx - 0.5 * w) * scale_x);
        int top = static_cast<int>((cy - 0.5 * h) * scale_y);
        int width = static_cast<int>(w * scale_x);
        int height = static_cast<int>(h * scale_y);
        
        // --- 5. EXTRACT ALL 17 KEYPOINTS ---
        // COCO keypoint order: 0-nose, 1-left_eye, 2-right_eye, 3-left_ear, 4-right_ear,
        // 5-left_shoulder, 6-right_shoulder, 7-left_elbow, 8-right_elbow,
        // 9-left_wrist, 10-right_wrist, 11-left_hip, 12-right_hip,
        // 13-left_knee, 14-right_knee, 15-left_ankle, 16-right_ankle
        
        std::vector<cv::Point3f> keypoints_3d(17, cv::Point3f(0, 0, 0));
        std::vector<float> keypoint_confidences(17, 0.0f);
        
        for (int kp_idx = 0; kp_idx < 17; ++kp_idx) {
            int base_idx = 5 + kp_idx * 3; // Start after bbox(4) + conf(1)
            
            float kp_x = output_buffer.at<float>(i, base_idx + 0) * scale_x;
            float kp_y = output_buffer.at<float>(i, base_idx + 1) * scale_y;
            float kp_conf = output_buffer.at<float>(i, base_idx + 2);
            
            keypoint_confidences[kp_idx] = kp_conf;
            
            // Only deproject keypoints with sufficient confidence
            if (kp_conf > keypoint_confidence_threshold) {
                cv::Point2f pixel(kp_x, kp_y);
                
                // Ensure pixel is within frame bounds
                if (pixel.x >= 0 && pixel.x < depth_frame.cols &&
                    pixel.y >= 0 && pixel.y < depth_frame.rows) {
                    
                    float depth_value_m = get_filtered_depth(depth_frame, pixel);
                    
                    // Deproject to 3D
                    if (depth_value_m > 0.2f && depth_value_m < 3.0f) {
                        keypoints_3d[kp_idx] = deproject_2d_to_3d(pixel, depth_value_m, intrinsics);
                    }
                }
            }
        }
        
        // --- 6. CREATE TRACKED HANDS FROM WRIST KEYPOINTS ---
        // Left wrist is keypoint 9, right wrist is keypoint 10
        
        // Left hand
        if (keypoint_confidences[9] > keypoint_confidence_threshold &&
            keypoints_3d[9].z > 0.2f && keypoints_3d[9].z < 3.0f) {
            
            TrackedHand left_hand;
            left_hand.wrist_pos_3d = keypoints_3d[9];
            left_hand.confidence = keypoint_confidences[9];
            left_hand.id = 0; // Left hand ID
            left_hand.keypoints = keypoints_3d; // Store all keypoints
            tracked_hands.push_back(left_hand);
        }
        
        // Right hand
        if (keypoint_confidences[10] > keypoint_confidence_threshold &&
            keypoints_3d[10].z > 0.2f && keypoints_3d[10].z < 3.0f) {
            
            TrackedHand right_hand;
            right_hand.wrist_pos_3d = keypoints_3d[10];
            right_hand.confidence = keypoint_confidences[10];
            right_hand.id = 1; // Right hand ID
            right_hand.keypoints = keypoints_3d; // Store all keypoints
            tracked_hands.push_back(right_hand);
        }
        
        // Note: We only process the first person detected for now
        // In a multi-person scenario, you would need to track multiple people
        break;
    }
    
    return tracked_hands;
}