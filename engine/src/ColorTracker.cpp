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
    }
    
    // Try to load settings from file
    loadSettings();
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
    
    // Step 1: Try to associate inactive trackers with ByteTrack detections that match color profiles
    // Track which ByteTrack detections have been assigned to avoid double-assignment
    std::set<int> assigned_bytetrack_ids;
    
    // Log inactive trackers and available detections (INFO level for visibility)
    int inactive_count = 0;
    for (const auto& ball : tracked_balls_) {
        if (!ball.is_active) inactive_count++;
    }
    if (inactive_count > 0 || bytetrack_objects.size() > 0) {
        INFO_LOG("ColorTracker: ", inactive_count, " inactive trackers, ",
                  bytetrack_objects.size(), " ByteTrack detections available");
    }
    
    for (auto& ball : tracked_balls_) {
        if (!ball.is_active) {
            INFO_LOG("ColorTracker: Attempting to reactivate ball ", ball.logical_id,
                     " (previous color: ", (ball.color_name.empty() ? "none" : ball.color_name), ")");
            
            // Strategy: If ball had a previous color, try to find a detection matching that color first
            // This preserves color identity across temporary occlusions
            std::vector<const ColorProfile*> profiles_to_try;
            
            if (!ball.color_name.empty()) {
                // Try previous color first
                for (const auto& profile : color_profiles_) {
                    if (profile.name == ball.color_name) {
                        profiles_to_try.push_back(&profile);
                        break;
                    }
                }
            }
            
            // Then try all other colors
            for (const auto& profile : color_profiles_) {
                if (profile.name != ball.color_name) {
                    profiles_to_try.push_back(&profile);
                }
            }
            
            // Look for ByteTrack objects that match this ball's color profile
            for (const auto& obj : bytetrack_objects) {
                if (obj.class_name != "ball") continue;
                
                // Skip if this detection is already assigned to another ball
                if (assigned_bytetrack_ids.count(obj.id) > 0) continue;
                
                // Get center of bounding box
                cv::Point2f center(obj.box.x + obj.box.width / 2.0f,
                                  obj.box.y + obj.box.height / 2.0f);
                
                // Check color profiles in priority order
                for (const auto* profile : profiles_to_try) {
                    bool matches = matchesColorProfile(hsv_frame, center, *profile);
                    DEBUG_LOG("ColorTracker: Testing detection at (", center.x, ",", center.y,
                              ") against ", profile->name, ": ", (matches ? "MATCH" : "no match"));
                    if (matches) {
                        // Check if another ball is already using this color
                        bool color_already_used = false;
                        for (const auto& other_ball : tracked_balls_) {
                            if (other_ball.is_active && other_ball.color_name == profile->name &&
                                other_ball.logical_id != ball.logical_id) {
                                color_already_used = true;
                                DEBUG_LOG("ColorTracker: Color ", profile->name,
                                         " already used by ball ", other_ball.logical_id);
                                break;
                            }
                        }
                        
                        if (!color_already_used) {
                            DEBUG_LOG("ColorTracker: Reactivating ball ", ball.logical_id,
                                     " with color ", profile->name, " at (", center.x, ",", center.y, ")");
                            // Found a match! Activate this tracker
                            ball.is_active = true;
                            ball.color_name = profile->name;
                            ball.pixel_pos = center;
                            ball.frames_since_seen = 0;
                            ball.associated_wrist_id = -1;
                            assigned_bytetrack_ids.insert(obj.id);
                            
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
                            
                            break; // Break out of color profiles loop
                        }
                    }
                }
                if (ball.is_active) break; // Break out of ByteTrack objects loop
            }
        }
    }
    
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
                if (matchesColorProfile(hsv_frame, center, *profile)) {
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
        
        // Step 2c: If still not found, try simple color tracking around last position
        if (!found_this_frame) {
            cv::Point2f new_pos = findLargestColorBlob(hsv_frame, *profile,
                                                      ball.pixel_pos, WRIST_SEARCH_RADIUS);
            
            if (new_pos.x >= 0 && new_pos.y >= 0) {
                ball.pixel_pos = new_pos;
                ball.frames_since_seen = 0;
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

bool ColorTracker::matchesColorProfile(const cv::Mat& hsv_frame,
                                       const cv::Point2f& center,
                                       const ColorProfile& profile, 
                                       int sample_radius) {
    // Sample area around center
    int x_min = std::max(0, static_cast<int>(center.x - sample_radius));
    int y_min = std::max(0, static_cast<int>(center.y - sample_radius));
    int x_max = std::min(hsv_frame.cols - 1, static_cast<int>(center.x + sample_radius));
    int y_max = std::min(hsv_frame.rows - 1, static_cast<int>(center.y + sample_radius));
    
    if (x_max <= x_min || y_max <= y_min) {
        return false;
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
    
    // Check if enough pixels match the color
    int matching_pixels = cv::countNonZero(mask);
    int total_pixels = sample_area.rows * sample_area.cols;
    float match_ratio = static_cast<float>(matching_pixels) / total_pixels;
    
    // Debug output (only when debug is enabled)
    DEBUG_LOG("ColorTracker: Color match ratio: ", match_ratio, " (threshold: 0.10)");
    
    return match_ratio > 0.10f; // At least 10% of pixels should match (lowered for better reactivation)
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
    // Find the color profile to calibrate
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