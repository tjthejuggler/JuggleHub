// RenderVisualizations.cpp - Visualization rendering for recordings
// This file contains the large renderVisualizationsOnFrame function
// extracted from Engine.cpp to keep the main engine file manageable.

#include "RecordingManager.hpp"
#include "CameraIntrinsics.hpp"
#include "IBallTracker.hpp"
#include "SimpleBallTracker.hpp"
#include "json.hpp"
#include <opencv2/opencv.hpp>
#include <fstream>
#include <cmath>

// Standalone function for rendering visualizations on recorded frames
cv::Mat renderVisualizationsOnFrame(const cv::Mat& frame, 
                                    const RecordingFrame& rec_frame,
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
            // If loading fails, continue without color profiles
        }
        
        for (const auto& det : rec_frame.raw_detections) {
            // Darker red for raw detections
            cv::Scalar box_color(0, 0, 139);  // Dark red
            
            // Draw thicker box for raw detections
            cv::rectangle(temp_result, det.box, box_color, 3);
            
            // Calculate center of detection
            int center_x = static_cast<int>(det.box.x + det.box.width / 2);
            int center_y = static_cast<int>(det.box.y + det.box.height / 2);
            
            // Sample BGR color at center
            cv::Vec3b bgr_pixel(0, 0, 0);
            if (center_x >= 0 && center_x < frame.cols && center_y >= 0 && center_y < frame.rows) {
                bgr_pixel = frame.at<cv::Vec3b>(center_y, center_x);
            }
            
            // Convert BGR to HSV for color matching
            cv::Mat bgr_mat(1, 1, CV_8UC3, bgr_pixel);
            cv::Mat hsv_mat;
            cv::cvtColor(bgr_mat, hsv_mat, cv::COLOR_BGR2HSV);
            cv::Vec3b hsv_pixel = hsv_mat.at<cv::Vec3b>(0, 0);
            
            // Find closest color profile
            std::string closest_color = "UNKNOWN";
            float min_distance = std::numeric_limits<float>::max();
            
            for (const auto& profile : color_profiles) {
                float hue_diff = std::abs(static_cast<float>(hsv_pixel[0]) - profile.avg_hue);
                if (hue_diff > 90) hue_diff = 180 - hue_diff;  // Wrap around hue circle
                
                float sat_diff = std::abs(static_cast<float>(hsv_pixel[1]) - profile.avg_saturation);
                
                float distance = std::sqrt(hue_diff * hue_diff + sat_diff * sat_diff);
                
                if (distance < min_distance) {
                    min_distance = distance;
                    closest_color = profile.name;
                }
            }
            
            // Create label with class, confidence, and detected color
            std::string class_name = det.class_id == 0 ? "ball" : "ball_held";
            char label[128];
            snprintf(label, sizeof(label), "%s %.0f%% (%s)", 
                    class_name.c_str(), det.confidence * 100, closest_color.c_str());
            
            // Draw label background
            int baseline = 0;
            cv::Size text_size = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX, 0.6, 2, &baseline);
            cv::rectangle(temp_result,
                         cv::Point(det.box.x, det.box.y - text_size.height - 5),
                         cv::Point(det.box.x + text_size.width, det.box.y),
                         box_color, -1);
            
            // Draw label text
            cv::putText(temp_result, label,
                       cv::Point(det.box.x, det.box.y - 5),
                       cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(255, 255, 255), 2);
            
            // Draw center point
            cv::circle(temp_result, cv::Point(center_x, center_y), 3, box_color, -1);
        }
        
        info_lines.push_back("Raw YOLO Detections: " + std::to_string(rec_frame.raw_detections.size()));
        info_colors.push_back(cv::Scalar(0, 0, 139));
    }
    
    // Draw depth glob detections - cyan boxes with confidence scores
    // Note: When depth blob detection is enabled, raw_detections contains depth globs
    // The toggle controls whether to visualize them
    if (viz.show_depth_globs() && !rec_frame.raw_detections.empty()) {
        for (const auto& det : rec_frame.raw_detections) {
            // Draw cyan bounding box
            cv::rectangle(temp_result, det.box, cv::Scalar(255, 255, 0), 2);
            
            // Draw center point
            cv::Point2f center(det.box.x + det.box.width / 2.0f,
                              det.box.y + det.box.height / 2.0f);
            cv::circle(temp_result, center, 3, cv::Scalar(255, 255, 0), -1);
            
            // Create label with confidence
            std::string label = cv::format("Glob %.2f", det.confidence);
            
            // Draw label background
            int baseline = 0;
            cv::Size text_size = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX, 0.5, 2, &baseline);
            cv::rectangle(temp_result,
                         cv::Point(det.box.x, det.box.y - text_size.height - 5),
                         cv::Point(det.box.x + text_size.width, det.box.y),
                         cv::Scalar(255, 255, 0), -1);
            
            // Draw label text
            cv::putText(temp_result, label,
                       cv::Point(det.box.x, det.box.y - 5),
                       cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0, 0), 2);
        }
        
        info_lines.push_back("Depth Globs: " + std::to_string(rec_frame.raw_detections.size()));
        info_colors.push_back(cv::Scalar(255, 255, 0));
    }
    
    // Draw filtered detections (after filtering) - bright red, normal boxes
    if (viz.show_filtered_detections()) {
        // This would show detections after filtering logic
        // For now, we'll skip this as it's not in the current tracking system
    }
    
    // Draw YOLO Color Calibration Squares
    if (viz.show_yolo_color_calibration()) {
        for (const auto& det : rec_frame.raw_detections) {
            // Sample the detected BGR color
            cv::Vec3b bgr_pixel = det.detected_bgr_color;
            cv::Scalar actual_color(bgr_pixel[0], bgr_pixel[1], bgr_pixel[2]);
            
            // Draw small square at top-left of detection box
            int square_x = static_cast<int>(det.box.x);
            int square_y = static_cast<int>(det.box.y);
            int square_size = 8;
            
            // Draw filled square with the actual detected color
            cv::rectangle(temp_result,
                         cv::Rect(square_x, square_y, square_size, square_size),
                         actual_color, -1);
            
            // Draw black border around square for visibility
            cv::rectangle(temp_result,
                         cv::Rect(square_x, square_y, square_size, square_size),
                         cv::Scalar(0, 0, 0), 1);
        }
    }
    
    // Draw hand tracking visualization
    if (viz.show_hand_tracking()) {
        for (const auto& hand : rec_frame.tracked_hands_simple) {
            if (!hand.is_visible) continue;
            
            // Project wrist to 2D
            if (hand.wrist_pos_3d.z > 0) {
                int wrist_x = static_cast<int>((hand.wrist_pos_3d.x * camera_intrinsics.fx) / hand.wrist_pos_3d.z + camera_intrinsics.ppx);
                int wrist_y = static_cast<int>((hand.wrist_pos_3d.y * camera_intrinsics.fy) / hand.wrist_pos_3d.z + camera_intrinsics.ppy);
                
                // Color: Magenta for left, Cyan for right
                cv::Scalar hand_color = hand.id == 0 ? cv::Scalar(255, 0, 255) : cv::Scalar(255, 255, 0);
                
                // Draw wrist circle
                cv::circle(temp_result, cv::Point(wrist_x, wrist_y), 8, hand_color, -1);
                cv::circle(temp_result, cv::Point(wrist_x, wrist_y), 8, cv::Scalar(0, 0, 0), 2);
                
                // Draw hand label
                std::string hand_label = hand.id == 0 ? "L" : "R";
                cv::putText(temp_result, hand_label,
                           cv::Point(wrist_x + 12, wrist_y + 5),
                           cv::FONT_HERSHEY_SIMPLEX, 0.7, hand_color, 2);
                
                // Draw skeleton if enabled
                if (viz.show_skeleton() && !hand.keypoints.empty()) {
                    // Draw keypoints
                    for (const auto& kp : hand.keypoints) {
                        if (kp.z > 0) {
                            int kp_x = static_cast<int>((kp.x * camera_intrinsics.fx) / kp.z + camera_intrinsics.ppx);
                            int kp_y = static_cast<int>((kp.y * camera_intrinsics.fy) / kp.z + camera_intrinsics.ppy);
                            cv::circle(temp_result, cv::Point(kp_x, kp_y), 3, hand_color, -1);
                        }
                    }
                    
                    // Draw connections between keypoints (simplified skeleton)
                    // Connect wrist to each finger base
                    for (size_t i = 0; i < hand.keypoints.size() && i < 5; i++) {
                        const auto& kp = hand.keypoints[i];
                        if (kp.z > 0 && hand.wrist_pos_3d.z > 0) {
                            int kp_x = static_cast<int>((kp.x * camera_intrinsics.fx) / kp.z + camera_intrinsics.ppx);
                            int kp_y = static_cast<int>((kp.y * camera_intrinsics.fy) / kp.z + camera_intrinsics.ppy);
                            cv::line(temp_result, cv::Point(wrist_x, wrist_y), cv::Point(kp_x, kp_y), hand_color, 2);
                        }
                    }
                }
            }
        }
        
        info_lines.push_back("Hands Tracked: " + std::to_string(rec_frame.tracked_hands_simple.size()));
        info_colors.push_back(cv::Scalar(255, 0, 255));
    }
    
    // Draw color-tracked balls (final tracking result)
    if (viz.show_color_tracker()) {
        // Load color profiles to get display colors
        std::map<std::string, cv::Scalar> color_map;
        try {
            std::ifstream color_file("hub/config/color_profiles.json");
            if (color_file.is_open()) {
                nlohmann::json color_profiles;
                color_file >> color_profiles;
                
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
            // Get display color for this ball
            cv::Scalar display_color = cv::Scalar(0, 255, 255);  // Default yellow
            if (color_map.find(ball.color_name) != color_map.end()) {
                display_color = color_map[ball.color_name];
            }
            
            // Draw circle at ball position
            cv::Point2f pixel_pos = ball.pixel_pos;
            cv::circle(temp_result, pixel_pos, 12, display_color, 2);
            cv::circle(temp_result, pixel_pos, 12, cv::Scalar(0, 0, 0), 3);
            
            // Draw ball ID and color name
            char label[64];
            snprintf(label, sizeof(label), "ID:%d %s", ball.id, ball.color_name.c_str());
            
            // Draw label background
            int baseline = 0;
            cv::Size text_size = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX, 0.5, 2, &baseline);
            cv::rectangle(temp_result,
                         cv::Point(pixel_pos.x + 15, pixel_pos.y - text_size.height - 5),
                         cv::Point(pixel_pos.x + 15 + text_size.width, pixel_pos.y),
                         cv::Scalar(0, 0, 0), -1);
            
            // Draw label text
            cv::putText(temp_result, label,
                       cv::Point(pixel_pos.x + 15, pixel_pos.y - 5),
                       cv::FONT_HERSHEY_SIMPLEX, 0.5, display_color, 2);
        }
        
        info_lines.push_back("Color Tracked Balls: " + std::to_string(rec_frame.tracked_balls.size()));
        info_colors.push_back(cv::Scalar(0, 255, 255));
    }
    
    // Draw final tracked boxes (simplified - just boxes around balls)
    if (viz.show_tracked_boxes()) {
        for (const auto& ball : rec_frame.tracked_balls) {
            cv::Scalar box_color = ball.is_held ? cv::Scalar(255, 165, 0) : cv::Scalar(0, 255, 0);
            cv::rectangle(temp_result, ball.bbox, box_color, 2);
        }
    }
    
    // Draw ball states
    if (viz.show_ball_states()) {
        int y_offset = 30;
        for (const auto& ball : rec_frame.tracked_balls) {
            std::string state_text = "Ball " + std::to_string(ball.id) + ": ";
            state_text += ball.is_held ? "HELD" : "IN_FLIGHT";
            if (!ball.is_held && ball.trajectory.verified_point_count > 0) {
                state_text += " (traj:" + std::to_string(ball.trajectory.verified_point_count) + ")";
            }
            
            cv::Scalar state_color = ball.is_held ? cv::Scalar(255, 165, 0) : cv::Scalar(0, 255, 0);
            
            // Draw background
            int baseline = 0;
            cv::Size text_size = cv::getTextSize(state_text, cv::FONT_HERSHEY_SIMPLEX, 0.6, 2, &baseline);
            cv::rectangle(temp_result,
                         cv::Point(8, y_offset - text_size.height - 2),
                         cv::Point(12 + text_size.width, y_offset + 2),
                         cv::Scalar(0, 0, 0), -1);
            
            // Draw text
            cv::putText(temp_result, state_text,
                       cv::Point(10, y_offset),
                       cv::FONT_HERSHEY_SIMPLEX, 0.6, state_color, 2);
            y_offset += 25;
        }
    }
    
    // Draw trajectory points
    if (viz.show_trajectory()) {
        for (const auto& ball : rec_frame.tracked_balls) {
            if (ball.trajectory.points.empty()) continue;
            
            // Get display color
            cv::Scalar traj_color = cv::Scalar(0, 165, 255);  // Orange
            
            // Draw trajectory path
            for (size_t i = 1; i < ball.trajectory.points.size(); i++) {
                const auto& prev_point = ball.trajectory.points[i-1];
                const auto& curr_point = ball.trajectory.points[i];
                
                if (!prev_point.verified || !curr_point.verified) continue;
                if (prev_point.position.z <= 0 || curr_point.position.z <= 0) continue;
                
                // Project to 2D
                float x1_2d = (prev_point.position.x * camera_intrinsics.fx) / prev_point.position.z + camera_intrinsics.ppx;
                float y1_2d = (prev_point.position.y * camera_intrinsics.fy) / prev_point.position.z + camera_intrinsics.ppy;
                float x2_2d = (curr_point.position.x * camera_intrinsics.fx) / curr_point.position.z + camera_intrinsics.ppx;
                float y2_2d = (curr_point.position.y * camera_intrinsics.fy) / curr_point.position.z + camera_intrinsics.ppy;
                
                cv::line(temp_result, 
                        cv::Point(x1_2d, y1_2d),
                        cv::Point(x2_2d, y2_2d),
                        traj_color, 2);
            }
            
            // Draw trajectory points
            for (const auto& traj_point : ball.trajectory.points) {
                if (!traj_point.verified || traj_point.position.z <= 0) continue;
                
                float x_2d = (traj_point.position.x * camera_intrinsics.fx) / traj_point.position.z + camera_intrinsics.ppx;
                float y_2d = (traj_point.position.y * camera_intrinsics.fy) / traj_point.position.z + camera_intrinsics.ppy;
                
                cv::circle(temp_result, cv::Point(x_2d, y_2d), 3, traj_color, -1);
            }
        }
    }
    
    // Draw trajectory predictions
    if (viz.show_trajectory_predictions()) {
        for (const auto& ball : rec_frame.tracked_balls) {
            if (ball.trajectory.predicted_path.empty() || !ball.trajectory.prediction_valid) continue;
            
            cv::Scalar pred_color = cv::Scalar(255, 0, 255);  // Magenta for predictions
            
            // Draw predicted path
            for (size_t i = 1; i < ball.trajectory.predicted_path.size(); i++) {
                const auto& prev_point = ball.trajectory.predicted_path[i-1];
                const auto& curr_point = ball.trajectory.predicted_path[i];
                
                if (prev_point.z <= 0 || curr_point.z <= 0) continue;
                
                float x1_2d = (prev_point.x * camera_intrinsics.fx) / prev_point.z + camera_intrinsics.ppx;
                float y1_2d = (prev_point.y * camera_intrinsics.fy) / prev_point.z + camera_intrinsics.ppy;
                float x2_2d = (curr_point.x * camera_intrinsics.fx) / curr_point.z + camera_intrinsics.ppx;
                float y2_2d = (curr_point.y * camera_intrinsics.fy) / curr_point.z + camera_intrinsics.ppy;
                
                cv::line(temp_result,
                        cv::Point(x1_2d, y1_2d),
                        cv::Point(x2_2d, y2_2d),
                        pred_color, 2, cv::LINE_AA);
            }
            
            // Draw predicted points
            for (const auto& point_3d : ball.trajectory.predicted_path) {
                if (point_3d.z <= 0) continue;
                
                float x_2d = (point_3d.x * camera_intrinsics.fx) / point_3d.z + camera_intrinsics.ppx;
                float y_2d = (point_3d.y * camera_intrinsics.fy) / point_3d.z + camera_intrinsics.ppy;
                
                cv::circle(temp_result, cv::Point(x_2d, y_2d), 4, pred_color, -1);
            }
        }
    }
    
    // Draw tails (motion history)
    if (viz.show_tails()) {
        for (const auto& ball : rec_frame.tracked_balls) {
            if (ball.trajectory.points.size() < 2) continue;
            
            cv::Scalar tail_color = cv::Scalar(100, 100, 255);  // Light red
            
            // Draw last N points as a tail
            size_t tail_length = std::min(size_t(10), ball.trajectory.points.size());
            for (size_t i = ball.trajectory.points.size() - tail_length; i < ball.trajectory.points.size() - 1; i++) {
                const auto& curr_point = ball.trajectory.points[i];
                const auto& next_point = ball.trajectory.points[i + 1];
                
                if (!curr_point.verified || !next_point.verified) continue;
                if (curr_point.position.z <= 0 || next_point.position.z <= 0) continue;
                
                float x1_2d = (curr_point.position.x * camera_intrinsics.fx) / curr_point.position.z + camera_intrinsics.ppx;
                float y1_2d = (curr_point.position.y * camera_intrinsics.fy) / curr_point.position.z + camera_intrinsics.ppy;
                float x2_2d = (next_point.position.x * camera_intrinsics.fx) / next_point.position.z + camera_intrinsics.ppx;
                float y2_2d = (next_point.position.y * camera_intrinsics.fy) / next_point.position.z + camera_intrinsics.ppy;
                
                // Fade the tail
                float alpha = static_cast<float>(i - (ball.trajectory.points.size() - tail_length)) / tail_length;
                cv::Scalar faded_color = tail_color * alpha;
                
                cv::line(temp_result,
                        cv::Point(x1_2d, y1_2d),
                        cv::Point(x2_2d, y2_2d),
                        faded_color, 2);
            }
        }
    }
    
    // Draw hand velocity zones
    if (viz.show_hand_velocity_zone()) {
        for (const auto& hand : rec_frame.tracked_hands_simple) {
            if (!hand.is_visible || !hand.has_valid_velocity) continue;
            if (hand.wrist_pos_3d.z <= 0) continue;
            
            // Project wrist to 2D
            int center_x = static_cast<int>((hand.wrist_pos_3d.x * camera_intrinsics.fx) / hand.wrist_pos_3d.z + camera_intrinsics.ppx);
            int center_y = static_cast<int>((hand.wrist_pos_3d.y * camera_intrinsics.fy) / hand.wrist_pos_3d.z + camera_intrinsics.ppy);
            
            // Calculate velocity magnitude
            float vel_mag = std::sqrt(hand.velocity.x * hand.velocity.x + 
                                     hand.velocity.y * hand.velocity.y + 
                                     hand.velocity.z * hand.velocity.z);
            
            // Draw velocity vector
            if (vel_mag > 0.1f) {  // Only draw if moving
                // Scale velocity for visualization
                float scale = 50.0f;
                int end_x = center_x + static_cast<int>(hand.velocity.x * scale);
                int end_y = center_y + static_cast<int>(hand.velocity.y * scale);
                
                cv::Scalar vel_color = hand.id == 0 ? cv::Scalar(255, 0, 255) : cv::Scalar(255, 255, 0);
                cv::arrowedLine(temp_result, 
                               cv::Point(center_x, center_y),
                               cv::Point(end_x, end_y),
                               vel_color, 2, cv::LINE_AA, 0, 0.3);
                
                // Draw velocity magnitude text
                char vel_text[32];
                snprintf(vel_text, sizeof(vel_text), "%.2f m/s", vel_mag);
                cv::putText(temp_result, vel_text,
                           cv::Point(center_x + 15, center_y - 15),
                           cv::FONT_HERSHEY_SIMPLEX, 0.5, vel_color, 2);
            }
        }
    }
    
    // Draw hand thresholds (catch/throw zones) and/or color search regions
    // Check if either visualization is enabled before calling drawHandThresholds
    // CRITICAL: Pass recorded balls data so color search regions use correct positions
    if (tracker && (viz.show_hand_threshold() || viz.show_color_search())) {
        tracker->drawHandThresholds(temp_result, rec_frame.tracked_hands_simple, camera_intrinsics, &rec_frame.tracked_balls);
    }
    
    // Draw unmatched detections
    if (viz.show_unmatched_detections()) {
        // This would show YOLO detections that weren't matched to tracked balls
        // For now, we'll skip this as it requires tracking state
    }
    
    // Create info panel if we have info lines
    if (!info_lines.empty()) {
        // Calculate panel dimensions
        int line_height = 25;
        int panel_height = (info_lines.size() + 1) * line_height;
        int panel_width = temp_result.cols;
        
        // Create semi-transparent overlay
        cv::Mat overlay = temp_result.clone();
        cv::rectangle(overlay,
                     cv::Point(0, 0),
                     cv::Point(panel_width, panel_height),
                     cv::Scalar(0, 0, 0), -1);
        
        // Blend overlay with result
        cv::addWeighted(overlay, 0.6, temp_result, 0.4, 0, temp_result);
        
        // Draw info lines
        for (size_t i = 0; i < info_lines.size(); i++) {
            cv::Scalar text_color = i < info_colors.size() ? info_colors[i] : cv::Scalar(255, 255, 255);
            cv::putText(temp_result, info_lines[i],
                       cv::Point(10, (i + 1) * line_height),
                       cv::FONT_HERSHEY_SIMPLEX, 0.6, text_color, 2);
        }
    }
    
    return temp_result;
}
