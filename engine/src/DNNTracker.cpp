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
    
    // Initialize color tracker
    color_tracker_ = std::make_unique<juggler::ColorTracker>("ball_settings.json");
    
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
    
    // Filter valid ball detections and store filtered ones for visualization (Step 4: Filtered Detections)
    std::vector<const Detection*> valid_detections;
    for (const auto& det : last_raw_detections_) {
        // Check various filter conditions
        if (det.class_id == 3) {
            filtered_detections_.push_back(det);
            filter_reasons_.push_back("Hand detection");
        } else if (det.world_pos.z <= 0.2f) {
            filtered_detections_.push_back(det);
            filter_reasons_.push_back("Too close (<0.2m)");
        } else if (det.world_pos.z >= 2.0f) {
            filtered_detections_.push_back(det);
            filter_reasons_.push_back("Too far (>2.0m)");
        } else if (det.world_pos.z == 0.0f) {
            filtered_detections_.push_back(det);
            filter_reasons_.push_back("No depth");
        } else {
            // TEMPORARY: Accept ALL detections to diagnose class_id issue
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
    
    if (!ball_trackers_list.empty() && !valid_detections.empty()) {
        // Build cost matrix
        std::vector<std::vector<float>> cost_matrix(
            ball_trackers_list.size(),
            std::vector<float>(valid_detections.size(), std::numeric_limits<float>::max())
        );
        
        for (size_t i = 0; i < ball_trackers_list.size(); ++i) {
            ball_trackers_list[i]->update_from_kf();
            Eigen::Vector3d predicted_pos = ball_trackers_list[i]->position;
            
            for (size_t j = 0; j < valid_detections.size(); ++j) {
                float dist = calculate_distance(predicted_pos, valid_detections[j]->world_pos);
                cost_matrix[i][j] = dist;
            }
        }
        
        // Optimal assignment with 30cm threshold
        const float MAX_ASSOCIATION_DISTANCE = 0.30f;
        
        // Log the complete cost matrix
        debug_log << "[3D MATCH] Cost Matrix (" << ball_trackers_list.size()
                  << " trackers x " << valid_detections.size() << " detections):" << std::endl;
        for (size_t i = 0; i < ball_trackers_list.size(); ++i) {
            debug_log << "[3D MATCH]   Tracker " << ball_trackers_list[i]->logical_id << ": ";
            for (size_t j = 0; j < valid_detections.size(); ++j) {
                debug_log << std::fixed << std::setprecision(3) << cost_matrix[i][j] << "m ";
            }
            debug_log << std::endl;
        }
        debug_log << "[3D MATCH] Distance threshold: " << MAX_ASSOCIATION_DISTANCE << "m" << std::endl;
        auto assignments = optimal_assignment(cost_matrix, MAX_ASSOCIATION_DISTANCE);
        
        debug_log << "[3D Matching] Made " << assignments.size() << " assignments" << std::endl;
        
        // Log assignment details
        debug_log << "[3D MATCH] Assignments made: " << assignments.size() << std::endl;
        for (const auto& [tracker_idx, detection_idx] : assignments) {
            debug_log << "[3D MATCH]   Tracker " << ball_trackers_list[tracker_idx]->logical_id
                      << " <- Detection " << detection_idx
                      << " (distance: " << cost_matrix[tracker_idx][detection_idx] << "m)" << std::endl;
        }
        
        // Log which trackers were NOT matched
        for (size_t i = 0; i < ball_trackers_list.size(); ++i) {
            bool matched = false;
            for (const auto& [t_idx, d_idx] : assignments) {
                if (t_idx == i) { matched = true; break; }
            }
            if (!matched) {
                debug_log << "[3D MATCH]   Tracker " << ball_trackers_list[i]->logical_id
                          << " NOT MATCHED (all distances > threshold)" << std::endl;
            }
        }
        
        // Apply assignments and store for visualization (Step 5: 3D Matching)
        std::set<int> matched_detection_indices;
        for (const auto& [tracker_idx, detection_idx] : assignments) {
            auto* tracker = ball_trackers_list[tracker_idx];
            const auto* detection = valid_detections[detection_idx];
            
            // Store association for visualization
            tracker_associations_.push_back({tracker->logical_id, detection_idx});
            association_distances_.push_back(cost_matrix[tracker_idx][detection_idx]);
            
            // Track which detections were matched
            matched_detection_indices.insert(detection_idx);
            
            // Update Kalman filter
            tracker->kf.update(KalmanFilter3D::MeasurementVector(
                detection->world_pos.x, detection->world_pos.y, detection->world_pos.z));
            tracker->update_from_kf();  // Sync position field with updated Kalman state
            tracker->status = TrackerStatus::TRACKED;
            tracker->box_2d = detection->box;
            tracker->frames_since_seen = 0;
            tracker->parent_id = -1;
            
            debug_log << "[3D Matching] Tracker " << tracker->logical_id
                      << " matched to detection at distance "
                      << cost_matrix[tracker_idx][detection_idx] << "m" << std::endl;
        }
        
        // Store unmatched detections for visualization
        for (size_t i = 0; i < valid_detections.size(); ++i) {
            if (matched_detection_indices.find(i) == matched_detection_indices.end()) {
                unmatched_detections_.push_back(*valid_detections[i]);
            }
        }
        
        debug_log << "[3D Matching] Unmatched detections: " << unmatched_detections_.size() << std::endl;
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
    
    // Auto-initialize trackers from unmatched detections if no active trackers (Step 6: Auto-Init)
    if (ball_trackers_list.empty() && !valid_detections.empty()) {
        std::ofstream debug_log("engine_debug.log", std::ios::app);
        debug_log << "[AUTO-INIT] No active trackers, initializing from detections..." << std::endl;
        
        size_t num_to_init = std::min(valid_detections.size(), logical_ball_trackers_.size());
        for (size_t i = 0; i < num_to_init; ++i) {
            auto& tracker = logical_ball_trackers_[i];
            const auto* det = valid_detections[i];
            
            // Initialize Kalman filter with detection position
            tracker.kf.init(KalmanFilter3D::MeasurementVector(
                det->world_pos.x, det->world_pos.y, det->world_pos.z));
            
            tracker.status = TrackerStatus::TRACKED;
            tracker.frames_since_seen = 0;
            tracker.box_2d = det->box;
            tracker.parent_id = -1;
            
            // Store newly initialized tracker for visualization
            newly_initialized_tracker_ids_.push_back(tracker.logical_id);
            new_tracker_positions_.push_back(det->world_pos);
            
            debug_log << "[AUTO-INIT] Initialized tracker " << tracker.logical_id
                      << " at position (" << det->world_pos.x << ", "
                      << det->world_pos.y << ", " << det->world_pos.z << ")" << std::endl;
        }
        
        debug_log << "[AUTO-INIT] Initialized " << num_to_init << " trackers" << std::endl;
        debug_log.close();
    }
    
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

    // --- 8. RUN COLOR TRACKING ---
    // Convert depth_frame cv::Mat to rs2_intrinsics for ColorTracker
    rs2_intrinsics rs_intrinsics;
    rs_intrinsics.fx = intrinsics.fx;
    rs_intrinsics.fy = intrinsics.fy;
    rs_intrinsics.ppx = intrinsics.ppx;
    rs_intrinsics.ppy = intrinsics.ppy;
    rs_intrinsics.width = color_frame.cols;
    rs_intrinsics.height = color_frame.rows;
    rs_intrinsics.model = RS2_DISTORTION_BROWN_CONRADY;
    for (int i = 0; i < 5; i++) rs_intrinsics.coeffs[i] = 0.0f;
    
    color_tracked_balls_ = color_tracker_->update(color_frame, depth_frame, rs_intrinsics,
                                                   final_tracked_objects, tracked_hands);

    // --- 9. FUSE COLOR TRACKING MEASUREMENTS INTO PERSISTENT TRACKERS ---
    // Fuse color tracking measurements into persistent trackers
    for (const auto& color_ball : color_tracked_balls_) {
        if (!color_ball.is_active) continue;
        
        // Find corresponding persistent tracker
        for (auto& ball : logical_ball_trackers_) {
            if (ball.logical_id == color_ball.logical_id && ball.status != TrackerStatus::LOST) {
                // Use color position as additional measurement
                if (color_ball.world_pos.z > 0.2f && color_ball.world_pos.z < 2.0f) {
                    ball.kf.update(KalmanFilter3D::MeasurementVector(
                        color_ball.world_pos.x, color_ball.world_pos.y, color_ball.world_pos.z));
                    std::ofstream debug_log("engine_debug.log", std::ios::app);
                    debug_log << "[Color Fusion] Updated tracker " << ball.logical_id
                              << " with color measurement at ("
                              << color_ball.world_pos.x << ", "
                              << color_ball.world_pos.y << ", "
                              << color_ball.world_pos.z << ")" << std::endl;
                    debug_log.close();
                }
                break;
            }
        }
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

void DNNTracker::calibrate_color(const std::string& color_name, const cv::Point& click_point) {
    if (!color_tracker_) {
        std::cerr << "DNNTracker: Color tracker not initialized" << std::endl;
        return;
    }
    
    // Convert current frame to HSV
    cv::Mat hsv_frame;
    if (!last_color_frame_.empty()) {
        cv::cvtColor(last_color_frame_, hsv_frame, cv::COLOR_BGR2HSV);
        color_tracker_->calibrateColor(color_name, hsv_frame, click_point);
        color_tracker_->saveSettings();
        std::cout << "DNNTracker: Calibrated color profile '" << color_name << "' at ("
                  << click_point.x << "," << click_point.y << ")" << std::endl;
    } else {
        std::cerr << "DNNTracker: No color frame available for calibration" << std::endl;
    }
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
        // Forward color tracker settings (track_*, color hue settings)
        else if (key.find("track_") == 0 || key.find("_min_hue") != std::string::npos || key.find("_max_hue") != std::string::npos) {
            if (color_tracker_) {
                color_tracker_->updateSetting(key, value);
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