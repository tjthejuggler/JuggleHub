#include "ColorTracker.hpp"
#include "DNNTracker.hpp"
#include "DebugLog.hpp"
#include <fstream>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <set>

namespace juggler {

ColorTracker::ColorTracker(const std::string& settings_file)
    : settings_file_(settings_file) {
    
    // Initialize default color profiles (matching Python hub profiles)
    color_profiles_ = {
        ColorProfile("pink", cv::Scalar(150, 150, 90), cv::Scalar(170, 255, 255)),
        ColorProfile("orange", cv::Scalar(5, 150, 120), cv::Scalar(15, 255, 255)),
        ColorProfile("yellow", cv::Scalar(25, 120, 100), cv::Scalar(35, 255, 255)),
        ColorProfile("green", cv::Scalar(45, 120, 70), cv::Scalar(75, 255, 255)),
        ColorProfile("red", cv::Scalar(0, 150, 100), cv::Scalar(10, 255, 255)),
        ColorProfile("blue", cv::Scalar(100, 150, 100), cv::Scalar(130, 255, 255)),
        ColorProfile("purple", cv::Scalar(130, 150, 100), cv::Scalar(160, 255, 255)),
        ColorProfile("white", cv::Scalar(0, 0, 200), cv::Scalar(180, 30, 255))
    };
    
    // Initialize tracked balls
    tracked_balls_.resize(NUM_BALLS);
    for (int i = 0; i < NUM_BALLS; ++i) {
        tracked_balls_[i].logical_id = i;
        tracked_balls_[i].is_active = false;
        tracked_balls_[i].frames_since_deactivated = 999; // Start high to allow immediate activation
    }
    
    // Try to load settings from file
    loadSettings();
    
    INFO_LOG("ColorTracker: Initialized with legacy color tracking");
}

std::vector<ColorTrackedBall> ColorTracker::update(
    const cv::Mat& color_frame,
    const cv::Mat& depth_frame,
    const rs2_intrinsics& intrinsics,
    const std::vector<TrackedObject>& bytetrack_objects,
    const std::vector<TrackedHand>& tracked_hands) {
    
    // Convert to HSV once for all operations
    cv::Mat hsv_frame;
    cv::cvtColor(color_frame, hsv_frame, cv::COLOR_BGR2HSV);
    
    // Step 1: GLOBAL ASSIGNMENT - Associate inactive trackers with ByteTrack detections
    // This replaces the sequential loop to prevent the "first ball claims everything" problem
    
    // Step 1a: Build list of inactive balls that need reactivation
    std::vector<int> inactive_ball_indices;
    for (size_t i = 0; i < tracked_balls_.size(); ++i) {
        auto& ball = tracked_balls_[i];
        if (!ball.is_active) {
            // Increment frames_since_deactivated for inactive balls
            ball.frames_since_deactivated++;
            
            // Only consider balls that have been inactive for at least 10 frames
            if (ball.frames_since_deactivated >= 10) {
                inactive_ball_indices.push_back(i);
            } else {
                DEBUG_LOG("ColorTracker: Skipping ball ", ball.logical_id,
                         " - recently deactivated (", ball.frames_since_deactivated, " frames ago)");
            }
        }
    }
    
    // Step 1b: Build list of available ByteTrack detections (balls only)
    std::vector<int> available_detection_indices;
    for (size_t i = 0; i < bytetrack_objects.size(); ++i) {
        if (bytetrack_objects[i].class_name == "ball") {
            available_detection_indices.push_back(i);
        }
    }
    
    // Log status
    INFO_LOG("ColorTracker: Global assignment - ", inactive_ball_indices.size(),
             " inactive balls, ", available_detection_indices.size(), " ball detections");
    
    // Step 1c: Create scoring matrix [ball_index][detection_index] = confidence score
    std::vector<std::vector<float>> score_matrix(
        inactive_ball_indices.size(),
        std::vector<float>(available_detection_indices.size(), 0.0f)
    );
    
    // Also track which color matched best for each (ball, detection) pair
    std::vector<std::vector<std::string>> color_matrix(
        inactive_ball_indices.size(),
        std::vector<std::string>(available_detection_indices.size(), "")
    );
    
    // Step 1d: Fill scoring matrix
    for (size_t i = 0; i < inactive_ball_indices.size(); ++i) {
        auto& ball = tracked_balls_[inactive_ball_indices[i]];
        
        INFO_LOG("ColorTracker: Scoring ball ", ball.logical_id,
                 " (previous color: ", (ball.color_name.empty() ? "none" : ball.color_name), ")");
        
        // Determine which color profiles to try
        std::vector<const ColorProfile*> profiles_to_try;
        if (!ball.color_name.empty()) {
            // Try previous color first (if enabled)
            for (const auto& profile : color_profiles_) {
                if (profile.name == ball.color_name && profile.enabled) {
                    profiles_to_try.push_back(&profile);
                    break;
                }
            }
        }
        // Then try all other enabled colors
        for (const auto& profile : color_profiles_) {
            if (profile.name != ball.color_name && profile.enabled) {
                profiles_to_try.push_back(&profile);
            }
        }
        
        // Score each detection
        for (size_t j = 0; j < available_detection_indices.size(); ++j) {
            const auto& obj = bytetrack_objects[available_detection_indices[j]];
            cv::Point2f center(obj.box.x + obj.box.width / 2.0f,
                              obj.box.y + obj.box.height / 2.0f);
            
            float best_confidence = 0.0f;
            std::string best_color;
            
            // Try each color profile
            for (const auto* profile : profiles_to_try) {
                float confidence = matchesColorProfile(hsv_frame, center, *profile);
                if (confidence > best_confidence) {
                    best_confidence = confidence;
                    best_color = profile->name;
                }
            }
            
            // Apply bonus if this matches the ball's previous color (color consistency)
            if (!ball.color_name.empty() && best_color == ball.color_name) {
                best_confidence *= 1.5f; // 50% bonus for color consistency
            }
            
            score_matrix[i][j] = best_confidence;
            color_matrix[i][j] = best_color;
            
            DEBUG_LOG("  Detection ", j, " at (", center.x, ",", center.y,
                     "): best_color=", best_color, " confidence=", best_confidence);
        }
    }
    
    // Step 1e: Greedy assignment - repeatedly find and assign the best (ball, detection) pair
    std::set<int> assigned_ball_indices;
    std::set<int> assigned_detection_indices;
    
    INFO_LOG("ColorTracker: Starting greedy assignment");
    
    while (assigned_ball_indices.size() < inactive_ball_indices.size() &&
           assigned_detection_indices.size() < available_detection_indices.size()) {
        
        // Find the best unassigned (ball, detection) pair
        float best_score = 0.10f; // Minimum threshold
        int best_ball_idx = -1;
        int best_det_idx = -1;
        
        for (size_t i = 0; i < inactive_ball_indices.size(); ++i) {
            if (assigned_ball_indices.count(i) > 0) continue;
            
            for (size_t j = 0; j < available_detection_indices.size(); ++j) {
                if (assigned_detection_indices.count(j) > 0) continue;
                
                if (score_matrix[i][j] > best_score) {
                    best_score = score_matrix[i][j];
                    best_ball_idx = i;
                    best_det_idx = j;
                }
            }
        }
        
        // If no valid assignment found, stop
        if (best_ball_idx == -1) {
            INFO_LOG("ColorTracker: No more valid assignments (best score below threshold)");
            break;
        }
        
        // Assign this pair
        assigned_ball_indices.insert(best_ball_idx);
        assigned_detection_indices.insert(best_det_idx);
        
        // Activate the ball
        auto& ball = tracked_balls_[inactive_ball_indices[best_ball_idx]];
        const auto& obj = bytetrack_objects[available_detection_indices[best_det_idx]];
        
        cv::Point2f center(obj.box.x + obj.box.width / 2.0f,
                          obj.box.y + obj.box.height / 2.0f);
        
        std::string matched_color = color_matrix[best_ball_idx][best_det_idx];
        float matched_confidence = score_matrix[best_ball_idx][best_det_idx];
        
        // Check if another active ball is already tracking this location (deduplication)
        bool location_already_tracked = false;
        for (const auto& other_ball : tracked_balls_) {
            if (other_ball.is_active && other_ball.logical_id != ball.logical_id) {
                float pixel_dist = std::sqrt(
                    std::pow(center.x - other_ball.pixel_pos.x, 2) +
                    std::pow(center.y - other_ball.pixel_pos.y, 2)
                );
                
                if (pixel_dist < 50.0f) {
                    location_already_tracked = true;
                    INFO_LOG("ColorTracker: Skipping assignment - location already tracked by ball ",
                             other_ball.logical_id, " (distance: ", pixel_dist, " pixels)");
                    break;
                }
            }
        }
        
        if (location_already_tracked) {
            continue; // Skip this assignment
        }
        
        // Activate the ball
        ball.is_active = true;
        ball.color_name = matched_color;
        ball.pixel_pos = center;
        ball.frames_since_seen = 0;
        ball.frames_since_deactivated = 999;
        ball.associated_wrist_id = -1;
        ball.color_match_confidence = matched_confidence;
        
        INFO_LOG("ColorTracker: ✓ Assigned ball ", ball.logical_id,
                 " to detection at (", center.x, ",", center.y,
                 ") with color ", matched_color, " (confidence: ", matched_confidence, ")");
        
        // Get depth and world position
        if (center.x >= 0 && center.x < depth_frame.cols &&
            center.y >= 0 && center.y < depth_frame.rows) {
            uint16_t depth_mm = depth_frame.at<uint16_t>(
                static_cast<int>(center.y), static_cast<int>(center.x));
            float depth_m = depth_mm / 1000.0f;
            
            if (depth_m > MIN_DEPTH && depth_m < MAX_DEPTH) {
                ball.world_pos = deprojectToWorld(center, depth_m, intrinsics);
            }
        }
    }
    
    INFO_LOG("ColorTracker: Global assignment complete - ", assigned_ball_indices.size(),
             " balls activated");
    
    // Step 2: Update active trackers
    for (auto& ball : tracked_balls_) {
        if (!ball.is_active) continue;
        
        // Find the color profile for this ball
        const ColorProfile* profile = nullptr;
        for (const auto& p : color_profiles_) {
            if (p.name == ball.color_name) {
                profile = &p;
                break;
            }
        }
        if (!profile) continue;
        
        // CRITICAL: If the profile is disabled, deactivate this ball immediately
        if (!profile->enabled) {
            INFO_LOG("ColorTracker: Deactivating ball ", ball.logical_id,
                     " because color '", ball.color_name, "' is now disabled");
            ball.is_active = false;
            ball.associated_wrist_id = -1;
            continue;
        }
        
        bool found_this_frame = false;
        
        // Step 2a: Check if ball is near a wrist (for wrist association)
        for (const auto& hand : tracked_hands) {
            cv::Point2f wrist_2d(hand.wrist_pos_3d.x * intrinsics.fx / hand.wrist_pos_3d.z + intrinsics.ppx,
                                hand.wrist_pos_3d.y * intrinsics.fy / hand.wrist_pos_3d.z + intrinsics.ppy);
            
            // Calculate 3D distance between ball and wrist
            float dist_3d = std::sqrt(
                std::pow(ball.world_pos.x - hand.wrist_pos_3d.x, 2) +
                std::pow(ball.world_pos.y - hand.wrist_pos_3d.y, 2) +
                std::pow(ball.world_pos.z - hand.wrist_pos_3d.z, 2)
            );
            
            // If ball is close to wrist, associate it
            if (dist_3d < WRIST_ASSOCIATION_DISTANCE) {
                ball.associated_wrist_id = hand.id;
                
                // When associated with wrist, ALWAYS try color blob detection first
                // Try to find the closest color blob in a reasonable radius around wrist
                cv::Point2f close_blob = findClosestColorBlob(hsv_frame, *profile,
                                                              wrist_2d, WRIST_SEARCH_RADIUS);
                
                if (close_blob.x >= 0 && close_blob.y >= 0) {
                    // Found a color blob near wrist - track that
                    ball.pixel_pos = close_blob;
                    ball.frames_since_seen = 0;
                    ball.color_match_confidence = matchesColorProfile(hsv_frame, close_blob, *profile);
                    found_this_frame = true;
                    
                    // Update world position
                    if (close_blob.x >= 0 && close_blob.x < depth_frame.cols &&
                        close_blob.y >= 0 && close_blob.y < depth_frame.rows) {
                        uint16_t depth_mm = depth_frame.at<uint16_t>(
                            static_cast<int>(close_blob.y), static_cast<int>(close_blob.x));
                        float depth_m = depth_mm / 1000.0f;
                        
                        if (depth_m > MIN_DEPTH && depth_m < MAX_DEPTH) {
                            ball.world_pos = deprojectToWorld(close_blob, depth_m, intrinsics);
                        }
                    }
                } else {
                    // No color visible at all - fall back to wrist position as last resort
                    ball.pixel_pos = wrist_2d;
                    ball.world_pos = hand.wrist_pos_3d;
                    ball.frames_since_seen = 0;  // Reset counter since we're tracking via wrist
                    found_this_frame = true;
                }
                break;
            }
        }
        
        // Step 2b: If not associated with wrist, check ByteTrack detections
        if (!found_this_frame && ball.associated_wrist_id == -1) {
            // Look for ByteTrack detection near last known position
            float min_dist = 100.0f; // pixels
            cv::Point2f best_match(-1, -1);
            
            for (const auto& obj : bytetrack_objects) {
                if (obj.class_name != "ball") continue;
                
                cv::Point2f center(obj.box.x + obj.box.width / 2.0f,
                                  obj.box.y + obj.box.height / 2.0f);
                
                // Check if this detection matches our color profile
                float confidence = matchesColorProfile(hsv_frame, center, *profile);
                if (confidence > 0.10f) {
                    float dist = std::sqrt(
                        std::pow(center.x - ball.pixel_pos.x, 2) +
                        std::pow(center.y - ball.pixel_pos.y, 2)
                    );
                    
                    if (dist < min_dist) {
                        min_dist = dist;
                        best_match = center;
                    }
                }
            }
            
            if (best_match.x >= 0) {
                ball.pixel_pos = best_match;
                ball.frames_since_seen = 0;
                ball.color_match_confidence = matchesColorProfile(hsv_frame, best_match, *profile);
                found_this_frame = true;
                
                // Update world position
                if (best_match.x >= 0 && best_match.x < depth_frame.cols &&
                    best_match.y >= 0 && best_match.y < depth_frame.rows) {
                    uint16_t depth_mm = depth_frame.at<uint16_t>(
                        static_cast<int>(best_match.y), static_cast<int>(best_match.x));
                    float depth_m = depth_mm / 1000.0f;
                    
                    if (depth_m > MIN_DEPTH && depth_m < MAX_DEPTH) {
                        ball.world_pos = deprojectToWorld(best_match, depth_m, intrinsics);
                    }
                }
            }
        }
        
        // Step 2c: If still not found, try simple color tracking around last known position
        if (!found_this_frame) {
            cv::Point2f search_center = ball.pixel_pos; // Use last known position
            
            cv::Point2f new_pos = findLargestColorBlob(hsv_frame, *profile,
                                                      search_center, WRIST_SEARCH_RADIUS);
            
            if (new_pos.x >= 0 && new_pos.y >= 0) {
                ball.pixel_pos = new_pos;
                ball.frames_since_seen = 0;
                ball.color_match_confidence = matchesColorProfile(hsv_frame, new_pos, *profile);
                found_this_frame = true;
                
                // Update world position
                if (new_pos.x >= 0 && new_pos.x < depth_frame.cols &&
                    new_pos.y >= 0 && new_pos.y < depth_frame.rows) {
                    uint16_t depth_mm = depth_frame.at<uint16_t>(
                        static_cast<int>(new_pos.y), static_cast<int>(new_pos.x));
                    float depth_m = depth_mm / 1000.0f;
                    
                    if (depth_m > MIN_DEPTH && depth_m < MAX_DEPTH) {
                        ball.world_pos = deprojectToWorld(new_pos, depth_m, intrinsics);
                    }
                }
            }
        }
        
        // Update tracking state
        if (!found_this_frame) {
            ball.frames_since_seen++;
            
            // If lost for too long, deactivate
            if (ball.frames_since_seen > MAX_FRAMES_LOST) {
                INFO_LOG("ColorTracker: Deactivating ball ", ball.logical_id,
                         " (color: ", ball.color_name, ") after ",
                         ball.frames_since_seen, " frames lost");
                ball.is_active = false;
                ball.frames_since_deactivated = 0; // Reset counter
                ball.associated_wrist_id = -1;
                // CRITICAL: Preserve color_name so ball can reactivate with same color
                // ball.color_name is intentionally NOT cleared here
            }
        } else {
            // Reset wrist association if ball moved away
            if (ball.associated_wrist_id >= 0) {
                bool still_near_wrist = false;
                for (const auto& hand : tracked_hands) {
                    if (hand.id == ball.associated_wrist_id) {
                        float dist_3d = std::sqrt(
                            std::pow(ball.world_pos.x - hand.wrist_pos_3d.x, 2) +
                            std::pow(ball.world_pos.y - hand.wrist_pos_3d.y, 2) +
                            std::pow(ball.world_pos.z - hand.wrist_pos_3d.z, 2)
                        );
                        if (dist_3d < WRIST_ASSOCIATION_DISTANCE * 1.5f) { // Hysteresis
                            still_near_wrist = true;
                        }
                        break;
                    }
                }
                if (!still_near_wrist) {
                    ball.associated_wrist_id = -1;
                }
            }
        }
    }
    
    // Step 3: Deduplicate - If two balls are at the same location, keep the one with better color match
    INFO_LOG("ColorTracker: Starting deduplication check");
    for (const auto& ball : tracked_balls_) {
        if (ball.is_active) {
            INFO_LOG("  Ball ", ball.logical_id, " (", ball.color_name,
                    ") active at pixel(", ball.pixel_pos.x, ",", ball.pixel_pos.y,
                    ") world(", ball.world_pos.x, ",", ball.world_pos.y, ",", ball.world_pos.z,
                    ") confidence=", ball.color_match_confidence);
        }
    }
    
    for (size_t i = 0; i < tracked_balls_.size(); ++i) {
        if (!tracked_balls_[i].is_active) continue;
        
        for (size_t j = i + 1; j < tracked_balls_.size(); ++j) {
            if (!tracked_balls_[j].is_active) continue;
            
            // Calculate 2D pixel distance
            float pixel_dist = std::sqrt(
                std::pow(tracked_balls_[i].pixel_pos.x - tracked_balls_[j].pixel_pos.x, 2) +
                std::pow(tracked_balls_[i].pixel_pos.y - tracked_balls_[j].pixel_pos.y, 2)
            );
            
            // Calculate 3D distance between balls (only if both have valid depth)
            float dist_3d = 999.0f;
            if (tracked_balls_[i].world_pos.z > 0 && tracked_balls_[j].world_pos.z > 0) {
                dist_3d = std::sqrt(
                    std::pow(tracked_balls_[i].world_pos.x - tracked_balls_[j].world_pos.x, 2) +
                    std::pow(tracked_balls_[i].world_pos.y - tracked_balls_[j].world_pos.y, 2) +
                    std::pow(tracked_balls_[i].world_pos.z - tracked_balls_[j].world_pos.z, 2)
                );
            }
            
            DEBUG_LOG("ColorTracker: Checking balls ", tracked_balls_[i].logical_id, " and ",
                     tracked_balls_[j].logical_id, " - pixel_dist=", pixel_dist,
                     " 3d_dist=", dist_3d * 100, "cm");
            
            // If balls are too close (within 10cm in 3D OR 50 pixels in 2D), they're likely the same physical ball
            if (dist_3d < 0.10f || pixel_dist < 50.0f) {
                INFO_LOG("ColorTracker: Detected duplicate tracking - Ball ", tracked_balls_[i].logical_id,
                         " (", tracked_balls_[i].color_name, ", confidence=", tracked_balls_[i].color_match_confidence,
                         ") and Ball ", tracked_balls_[j].logical_id,
                         " (", tracked_balls_[j].color_name, ", confidence=", tracked_balls_[j].color_match_confidence,
                         ") are ", dist_3d * 100, "cm apart (", pixel_dist, " pixels)");
                
                // Deactivate the one with lower confidence
                if (tracked_balls_[i].color_match_confidence < tracked_balls_[j].color_match_confidence) {
                    INFO_LOG("ColorTracker: Deactivating ball ", tracked_balls_[i].logical_id,
                             " (lower confidence: ", tracked_balls_[i].color_match_confidence, ")");
                    tracked_balls_[i].is_active = false;
                    tracked_balls_[i].frames_since_deactivated = 0; // Reset counter
                    tracked_balls_[i].associated_wrist_id = -1;
                } else {
                    INFO_LOG("ColorTracker: Deactivating ball ", tracked_balls_[j].logical_id,
                             " (lower confidence: ", tracked_balls_[j].color_match_confidence, ")");
                    tracked_balls_[j].is_active = false;
                    tracked_balls_[j].frames_since_deactivated = 0; // Reset counter
                    tracked_balls_[j].associated_wrist_id = -1;
                }
            }
        }
    }
    
    return tracked_balls_;
}

cv::Point2f ColorTracker::findLargestColorBlob(const cv::Mat& hsv_frame, 
                                               const ColorProfile& profile,
                                               const cv::Point2f& search_center, 
                                               int search_radius) {
    // Create ROI around search center
    int x_min = std::max(0, static_cast<int>(search_center.x - search_radius));
    int y_min = std::max(0, static_cast<int>(search_center.y - search_radius));
    int x_max = std::min(hsv_frame.cols, static_cast<int>(search_center.x + search_radius));
    int y_max = std::min(hsv_frame.rows, static_cast<int>(search_center.y + search_radius));
    
    if (x_max <= x_min || y_max <= y_min) {
        return cv::Point2f(-1, -1);
    }
    
    cv::Rect roi(x_min, y_min, x_max - x_min, y_max - y_min);
    cv::Mat hsv_roi = hsv_frame(roi);
    
    // Create mask for color
    cv::Mat mask;
    cv::inRange(hsv_roi, profile.min_hsv, profile.max_hsv, mask);
    
    // Handle wrap-around colors (like red)
    if (profile.min_hsv2[0] >= 0) {
        cv::Mat mask2;
        cv::inRange(hsv_roi, profile.min_hsv2, profile.max_hsv2, mask2);
        cv::bitwise_or(mask, mask2, mask);
    }
    
    // Apply morphological operations to clean up
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(3, 3));
    cv::morphologyEx(mask, mask, cv::MORPH_OPEN, kernel);
    cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, kernel);
    
    // Find contours
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    
    // Find largest contour
    double max_area = MIN_BLOB_AREA;
    cv::Point2f best_center(-1, -1);
    
    for (const auto& contour : contours) {
        double area = cv::contourArea(contour);
        if (area > max_area) {
            cv::Moments m = cv::moments(contour);
            if (m.m00 > 0) {
                max_area = area;
                // Convert back to full frame coordinates
                best_center.x = x_min + m.m10 / m.m00;
                best_center.y = y_min + m.m01 / m.m00;
            }
        }
    }
    
    return best_center;
}

cv::Point2f ColorTracker::findClosestColorBlob(const cv::Mat& hsv_frame,
                                               const ColorProfile& profile,
                                               const cv::Point2f& search_center,
                                               int search_radius) {
    // Create ROI around search center
    int x_min = std::max(0, static_cast<int>(search_center.x - search_radius));
    int y_min = std::max(0, static_cast<int>(search_center.y - search_radius));
    int x_max = std::min(hsv_frame.cols, static_cast<int>(search_center.x + search_radius));
    int y_max = std::min(hsv_frame.rows, static_cast<int>(search_center.y + search_radius));
    
    if (x_max <= x_min || y_max <= y_min) {
        return cv::Point2f(-1, -1);
    }
    
    cv::Rect roi(x_min, y_min, x_max - x_min, y_max - y_min);
    cv::Mat hsv_roi = hsv_frame(roi);
    
    // Create mask for color
    cv::Mat mask;
    cv::inRange(hsv_roi, profile.min_hsv, profile.max_hsv, mask);
    
    // Handle wrap-around colors (like red)
    if (profile.min_hsv2[0] >= 0) {
        cv::Mat mask2;
        cv::inRange(hsv_roi, profile.min_hsv2, profile.max_hsv2, mask2);
        cv::bitwise_or(mask, mask2, mask);
    }
    
    // Apply morphological operations to clean up
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(3, 3));
    cv::morphologyEx(mask, mask, cv::MORPH_OPEN, kernel);
    cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, kernel);
    
    // Find contours
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    
    // Find closest contour to search center (with minimum area threshold)
    double min_area = 10.0; // Lower threshold for partial visibility
    float min_distance = std::numeric_limits<float>::max();
    cv::Point2f closest_center(-1, -1);
    
    for (const auto& contour : contours) {
        double area = cv::contourArea(contour);
        if (area > min_area) {
            cv::Moments m = cv::moments(contour);
            if (m.m00 > 0) {
                // Get center in full frame coordinates
                cv::Point2f center(x_min + m.m10 / m.m00, y_min + m.m01 / m.m00);
                
                // Calculate distance to search center
                float dist = std::sqrt(
                    std::pow(center.x - search_center.x, 2) +
                    std::pow(center.y - search_center.y, 2)
                );
                
                if (dist < min_distance) {
                    min_distance = dist;
                    closest_center = center;
                }
            }
        }
    }
    
    return closest_center;
}

float ColorTracker::matchesColorProfile(const cv::Mat& hsv_frame,
                                        const cv::Point2f& center,
                                        const ColorProfile& profile,
                                        int sample_radius) {
    // CRITICAL: Immediately reject if profile is disabled
    if (!profile.enabled) {
        DEBUG_LOG("ColorTracker: Rejecting disabled profile '", profile.name, "'");
        return 0.0f;
    }
    
    // Sample area around center
    int x_min = std::max(0, static_cast<int>(center.x - sample_radius));
    int y_min = std::max(0, static_cast<int>(center.y - sample_radius));
    int x_max = std::min(hsv_frame.cols - 1, static_cast<int>(center.x + sample_radius));
    int y_max = std::min(hsv_frame.rows - 1, static_cast<int>(center.y + sample_radius));
    
    if (x_max <= x_min || y_max <= y_min) {
        return 0.0f;
    }
    
    cv::Rect sample_rect(x_min, y_min, x_max - x_min, y_max - y_min);
    cv::Mat sample_area = hsv_frame(sample_rect);
    
    // Create mask
    cv::Mat mask;
    cv::inRange(sample_area, profile.min_hsv, profile.max_hsv, mask);
    
    if (profile.min_hsv2[0] >= 0) {
        cv::Mat mask2;
        cv::inRange(sample_area, profile.min_hsv2, profile.max_hsv2, mask2);
        cv::bitwise_or(mask, mask2, mask);
    }
    
    // Calculate match ratio as confidence score
    int matching_pixels = cv::countNonZero(mask);
    int total_pixels = sample_area.rows * sample_area.cols;
    float match_ratio = static_cast<float>(matching_pixels) / total_pixels;
    
    // Debug output (only when debug is enabled)
    DEBUG_LOG("ColorTracker: Color match confidence: ", match_ratio, " (threshold: 0.10)");
    
    return match_ratio; // Return confidence score (0.0-1.0)
}

cv::Point3f ColorTracker::deprojectToWorld(const cv::Point2f& pixel, float depth,
                                          const rs2_intrinsics& intrinsics) {
    if (depth > 0) {
        float x = (pixel.x - intrinsics.ppx) * depth / intrinsics.fx;
        float y = (pixel.y - intrinsics.ppy) * depth / intrinsics.fy;
        return cv::Point3f(x, y, depth);
    }
    return cv::Point3f(0.0f, 0.0f, 0.0f);
}

bool ColorTracker::loadSettings() {
    std::ifstream file(settings_file_);
    if (!file.is_open()) {
        WARN_LOG("ColorTracker: Settings file not found. Using default values.");
        return false;
    }
    
    try {
        json j;
        file >> j;
        
        for (auto& profile : color_profiles_) {
            if (j.contains(profile.name)) {
                auto& color_data = j[profile.name];
                
                // Load enabled state (default to true if not present)
                if (color_data.contains("enabled")) {
                    profile.enabled = color_data["enabled"].get<bool>();
                } else {
                    profile.enabled = true;
                }
                
                profile.min_hsv = cv::Scalar(
                    color_data["min_hsv"][0],
                    color_data["min_hsv"][1],
                    color_data["min_hsv"][2]
                );
                profile.max_hsv = cv::Scalar(
                    color_data["max_hsv"][0],
                    color_data["max_hsv"][1],
                    color_data["max_hsv"][2]
                );
                
                if (color_data.contains("min_hsv2")) {
                    profile.min_hsv2 = cv::Scalar(
                        color_data["min_hsv2"][0],
                        color_data["min_hsv2"][1],
                        color_data["min_hsv2"][2]
                    );
                    profile.max_hsv2 = cv::Scalar(
                        color_data["max_hsv2"][0],
                        color_data["max_hsv2"][1],
                        color_data["max_hsv2"][2]
                    );
                }
            }
        }
        
        INFO_LOG("ColorTracker: Settings loaded from ", settings_file_);
        return true;
    } catch (const std::exception& e) {
        ERROR_LOG("ColorTracker: Error loading settings: ", e.what(),
                  ". Using default values.");
        return false;
    }
}

void ColorTracker::saveSettings() {
    json j;
    for (const auto& profile : color_profiles_) {
        j[profile.name]["enabled"] = profile.enabled;
        j[profile.name]["min_hsv"] = {
            profile.min_hsv[0],
            profile.min_hsv[1],
            profile.min_hsv[2]
        };
        j[profile.name]["max_hsv"] = {
            profile.max_hsv[0],
            profile.max_hsv[1],
            profile.max_hsv[2]
        };
        
        if (profile.min_hsv2[0] >= 0) {
            j[profile.name]["min_hsv2"] = {
                profile.min_hsv2[0],
                profile.min_hsv2[1],
                profile.min_hsv2[2]
            };
            j[profile.name]["max_hsv2"] = {
                profile.max_hsv2[0],
                profile.max_hsv2[1],
                profile.max_hsv2[2]
            };
        }
    }
    
    std::ofstream file(settings_file_);
    file << j.dump(4);
    INFO_LOG("ColorTracker: Settings saved to ", settings_file_);
}

void ColorTracker::resetToDefaults() {
    color_profiles_ = {
        ColorProfile("pink", cv::Scalar(150, 150, 90), cv::Scalar(170, 255, 255)),
        ColorProfile("orange", cv::Scalar(5, 150, 120), cv::Scalar(15, 255, 255)),
        ColorProfile("yellow", cv::Scalar(25, 120, 100), cv::Scalar(35, 255, 255)),
        ColorProfile("green", cv::Scalar(45, 120, 70), cv::Scalar(75, 255, 255)),
        ColorProfile("red", cv::Scalar(0, 150, 100), cv::Scalar(10, 255, 255)),
        ColorProfile("blue", cv::Scalar(100, 150, 100), cv::Scalar(130, 255, 255)),
        ColorProfile("purple", cv::Scalar(130, 150, 100), cv::Scalar(160, 255, 255)),
        ColorProfile("white", cv::Scalar(0, 0, 200), cv::Scalar(180, 30, 255))
    };
    INFO_LOG("ColorTracker: Reset to default color values");
}

void ColorTracker::calibrateColor(const std::string& color_name,
                                  const cv::Mat& hsv_image,
                                  const cv::Point& click_point) {
    
    // Update color profile
    auto it = std::find_if(color_profiles_.begin(), color_profiles_.end(),
                          [&color_name](const ColorProfile& p) {
                              return p.name == color_name;
                          });
    
    if (it == color_profiles_.end()) {
        ERROR_LOG("ColorTracker: Color '", color_name, "' not found");
        return;
    }
    
    // Sample a 5x5 area around the click point
    int sample_size = 5;
    int start_x = std::max(0, click_point.x - sample_size/2);
    int start_y = std::max(0, click_point.y - sample_size/2);
    int end_x = std::min(hsv_image.cols - 1, click_point.x + sample_size/2);
    int end_y = std::min(hsv_image.rows - 1, click_point.y + sample_size/2);
    
    cv::Rect sample_rect(start_x, start_y, end_x - start_x, end_y - start_y);
    cv::Mat sample_area = hsv_image(sample_rect);
    
    // Calculate mean and standard deviation
    cv::Scalar mean, stddev;
    cv::meanStdDev(sample_area, mean, stddev);
    
    // Set HSV range with reduced tolerance
    int h_tolerance = 8;
    int s_tolerance = 40;
    int v_tolerance = 40;
    
    int h_mean = static_cast<int>(mean[0]);
    
    // Handle HSV wrap-around for red/pink colors
    if ((color_name == "pink" && (h_mean <= 10 || h_mean >= 170)) ||
        (color_name == "orange" && h_mean <= 15)) {
        if (h_mean <= 15) {
            it->min_hsv = cv::Scalar(
                std::max(0, h_mean - h_tolerance),
                std::max(0, static_cast<int>(mean[1]) - s_tolerance),
                std::max(0, static_cast<int>(mean[2]) - v_tolerance)
            );
            it->max_hsv = cv::Scalar(
                std::min(15, h_mean + h_tolerance),
                std::min(255, static_cast<int>(mean[1]) + s_tolerance),
                std::min(255, static_cast<int>(mean[2]) + v_tolerance)
            );
            it->min_hsv2 = cv::Scalar(
                std::max(165, 180 - h_tolerance),
                std::max(0, static_cast<int>(mean[1]) - s_tolerance),
                std::max(0, static_cast<int>(mean[2]) - v_tolerance)
            );
            it->max_hsv2 = cv::Scalar(
                180,
                std::min(255, static_cast<int>(mean[1]) + s_tolerance),
                std::min(255, static_cast<int>(mean[2]) + v_tolerance)
            );
        } else {
            it->min_hsv = cv::Scalar(
                std::max(165, h_mean - h_tolerance),
                std::max(0, static_cast<int>(mean[1]) - s_tolerance),
                std::max(0, static_cast<int>(mean[2]) - v_tolerance)
            );
            it->max_hsv = cv::Scalar(
                180,
                std::min(255, static_cast<int>(mean[1]) + s_tolerance),
                std::min(255, static_cast<int>(mean[2]) + v_tolerance)
            );
            it->min_hsv2 = cv::Scalar(
                0,
                std::max(0, static_cast<int>(mean[1]) - s_tolerance),
                std::max(0, static_cast<int>(mean[2]) - v_tolerance)
            );
            it->max_hsv2 = cv::Scalar(
                std::min(15, h_tolerance),
                std::min(255, static_cast<int>(mean[1]) + s_tolerance),
                std::min(255, static_cast<int>(mean[2]) + v_tolerance)
            );
        }
    } else {
        // Normal single range
        it->min_hsv = cv::Scalar(
            std::max(0, h_mean - h_tolerance),
            std::max(0, static_cast<int>(mean[1]) - s_tolerance),
            std::max(0, static_cast<int>(mean[2]) - v_tolerance)
        );
        it->max_hsv = cv::Scalar(
            std::min(180, h_mean + h_tolerance),
            std::min(255, static_cast<int>(mean[1]) + s_tolerance),
            std::min(255, static_cast<int>(mean[2]) + v_tolerance)
        );
        it->min_hsv2 = cv::Scalar(-1, -1, -1);
        it->max_hsv2 = cv::Scalar(-1, -1, -1);
    }
    
    INFO_LOG("ColorTracker: Calibrated ", color_name, " at (",
             click_point.x, ",", click_point.y, ")");
    INFO_LOG("HSV values - H:", static_cast<int>(mean[0]),
             " S:", static_cast<int>(mean[1]),
             " V:", static_cast<int>(mean[2]));
}

bool ColorTracker::updateSetting(const std::string& key, const std::string& value) {
    try {
        // Check if this is an enabled/disabled setting (format: "track_colorname")
        if (key.find("track_") == 0) {
            std::string color_name = key.substr(6); // Remove "track_" prefix
            
            auto it = std::find_if(color_profiles_.begin(), color_profiles_.end(),
                                  [&](const ColorProfile& p) { return p.name == color_name; });
            
            if (it == color_profiles_.end()) {
                ERROR_LOG("ColorTracker: Color '", color_name, "' not found");
                return false;
            }
            
            bool enabled = (std::stoi(value) != 0);
            it->enabled = enabled;
            INFO_LOG("ColorTracker: ", (enabled ? "Enabled" : "Disabled"), " tracking for ", color_name);
            saveSettings(); // Save to persist the change
            
            // Log all profile states for debugging
            INFO_LOG("ColorTracker: Current profile states:");
            for (const auto& p : color_profiles_) {
                INFO_LOG("  ", p.name, ": ", (p.enabled ? "ENABLED" : "DISABLED"));
            }
            
            return true;
        }
        
        // Parse key format: colorname_minmax_hsv (e.g., "pink_min_h")
        size_t pos1 = key.find('_');
        size_t pos2 = key.find('_', pos1 + 1);
        
        if (pos1 == std::string::npos || pos2 == std::string::npos) {
            ERROR_LOG("ColorTracker: Unknown setting key '", key, "'");
            return false;
        }
        
        std::string color_name = key.substr(0, pos1);
        std::string range_type = key.substr(pos1 + 1, pos2 - pos1 - 1); // min or max
        std::string hsv_comp = key.substr(pos2 + 1); // h, s, or v
        
        auto it = std::find_if(color_profiles_.begin(), color_profiles_.end(),
                              [&](const ColorProfile& p) { return p.name == color_name; });
        
        if (it == color_profiles_.end()) {
            ERROR_LOG("ColorTracker: Color '", color_name, "' not found");
            return false;
        }
        
        int val = std::stoi(value);
        int comp_idx = (hsv_comp == "h" ? 0 : (hsv_comp == "s" ? 1 : 2));
        
        if (range_type == "min") {
            it->min_hsv[comp_idx] = val;
        } else if (range_type == "max") {
            it->max_hsv[comp_idx] = val;
        } else {
            ERROR_LOG("ColorTracker: Unknown range type '", range_type, "'");
            return false;
        }
        
        INFO_LOG("ColorTracker: Updated ", key, " to ", value);
        return true;
    } catch (const std::exception& e) {
        ERROR_LOG("ColorTracker: Error updating setting: ", e.what());
        return false;
    }
}

} // namespace juggler