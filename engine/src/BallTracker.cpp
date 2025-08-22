#include "BallTracker.hpp"
#include <fstream>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <chrono>

namespace juggler {

BallTracker::BallTracker(const std::string& settings_file) 
    : settings_file_(settings_file) {
    // Initialize default color ranges
    colors_ = {
        ColorRange("pink", cv::Scalar(150, 150, 90), cv::Scalar(170, 255, 255)),
        ColorRange("orange", cv::Scalar(5, 150, 120), cv::Scalar(15, 255, 255)),
        ColorRange("green", cv::Scalar(45, 120, 70), cv::Scalar(75, 255, 255)),
        ColorRange("yellow", cv::Scalar(25, 120, 100), cv::Scalar(35, 255, 255))
    };
    
    // Try to load settings from file
    loadSettings();
}

double BallTracker::calculateDistance(const cv::Point2f& p1, const cv::Point2f& p2) {
    return std::sqrt(std::pow(p1.x - p2.x, 2) + std::pow(p1.y - p2.y, 2));
}

std::vector<cv::Point2f> BallTracker::mergeNearbyDetections(const std::vector<cv::Point2f>& centers) {
    if (centers.empty()) return centers;
    
    std::vector<cv::Point2f> merged_centers;
    std::vector<bool> used(centers.size(), false);
    
    for (size_t i = 0; i < centers.size(); ++i) {
        if (used[i]) continue;
        
        // Start a new cluster with this center
        std::vector<cv::Point2f> cluster;
        cluster.push_back(centers[i]);
        used[i] = true;
        
        // Find all nearby centers to merge
        for (size_t j = i + 1; j < centers.size(); ++j) {
            if (used[j]) continue;
            
            // Check if this center is close to any center in the current cluster
            bool should_merge = false;
            for (const auto& cluster_center : cluster) {
                if (calculateDistance(centers[j], cluster_center) < MERGE_DISTANCE_THRESHOLD) {
                    should_merge = true;
                    break;
                }
            }
            
            if (should_merge) {
                cluster.push_back(centers[j]);
                used[j] = true;
            }
        }
        
        // Calculate the centroid of the cluster
        cv::Point2f centroid(0, 0);
        for (const auto& point : cluster) {
            centroid.x += point.x;
            centroid.y += point.y;
        }
        centroid.x /= cluster.size();
        centroid.y /= cluster.size();
        
        merged_centers.push_back(centroid);
    }
    
    return merged_centers;
}

void BallTracker::detectBallsForColor(const cv::Mat& hsv_frame, const ColorRange& color, 
                                     std::vector<cv::Point2f>& centers, double downscale_factor) {
    if (downscale_factor == 1.0) {
        // Original processing path
        cv::Mat mask;
        cv::inRange(hsv_frame, color.min_hsv, color.max_hsv, mask);
        if (color.min_hsv2[0] >= 0) {
            cv::Mat mask2;
            cv::inRange(hsv_frame, color.min_hsv2, color.max_hsv2, mask2);
            cv::bitwise_or(mask, mask2, mask);
        }

        cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5, 5));
        cv::morphologyEx(mask, mask, cv::MORPH_OPEN, kernel);
        cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, kernel);

        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

        std::vector<cv::Point2f> initial_centers;
        for (const auto& contour : contours) {
            if (cv::contourArea(contour) > MIN_CONTOUR_AREA) {
                cv::Moments m = cv::moments(contour);
                if (m.m00 > 0) {
                    initial_centers.push_back(cv::Point2f(m.m10 / m.m00, m.m01 / m.m00));
                }
            }
        }
        centers = mergeNearbyDetections(initial_centers);

    } else {
        // Optimized path with downscaling
        cv::Mat resized_hsv;
        cv::resize(hsv_frame, resized_hsv, cv::Size(), downscale_factor, downscale_factor, cv::INTER_LINEAR);

        cv::Mat mask;
        cv::inRange(resized_hsv, color.min_hsv, color.max_hsv, mask);
        if (color.min_hsv2[0] >= 0) {
            cv::Mat mask2;
            cv::inRange(resized_hsv, color.min_hsv2, color.max_hsv2, mask2);
            cv::bitwise_or(mask, mask2, mask);
        }

        // Use smaller kernel for smaller image
        cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(3, 3));
        cv::morphologyEx(mask, mask, cv::MORPH_OPEN, kernel);
        cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, kernel);

        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

        std::vector<cv::Point2f> initial_centers;
        double scaled_min_area = MIN_CONTOUR_AREA * downscale_factor * downscale_factor;
        for (const auto& contour : contours) {
            if (cv::contourArea(contour) > scaled_min_area) {
                cv::Moments m = cv::moments(contour);
                if (m.m00 > 0) {
                    initial_centers.push_back(cv::Point2f(m.m10 / m.m00, m.m01 / m.m00));
                }
            }
        }
        
        // Scale centers back to original image size
        for (auto& center : initial_centers) {
            center.x /= downscale_factor;
            center.y /= downscale_factor;
        }
        centers = mergeNearbyDetections(initial_centers);
    }
}

float BallTracker::getAveragedDepth(const rs2::depth_frame& depth_frame, int x, int y, int patch_size) {
    float total_depth = 0.0f;
    int valid_pixel_count = 0;
    
    int start_x = std::max(0, x - patch_size / 2);
    int end_x = std::min(depth_frame.get_width() - 1, x + patch_size / 2);
    int start_y = std::max(0, y - patch_size / 2);
    int end_y = std::min(depth_frame.get_height() - 1, y + patch_size / 2);

    for (int cy = start_y; cy <= end_y; ++cy) {
        for (int cx = start_x; cx <= end_x; ++cx) {
            float depth = depth_frame.get_distance(cx, cy);
            if (depth > 0.0f) {
                total_depth += depth;
                valid_pixel_count++;
            }
        }
    }

    return valid_pixel_count > 0 ? total_depth / valid_pixel_count : 0.0f;
}

bool BallTracker::loadSettings() {
    std::ifstream file(settings_file_);
    if (!file.is_open()) {
        std::cerr << "Warning: Settings file not found. Using default values." << std::endl;
        return false;
    }
    
    try {
        json j;
        file >> j;
        for (auto& color : colors_) {
            if (j.contains(color.name)) {
                auto& color_data = j[color.name];
                color.min_hsv = cv::Scalar(color_data["min_hsv"][0], color_data["min_hsv"][1], color_data["min_hsv"][2]);
                color.max_hsv = cv::Scalar(color_data["max_hsv"][0], color_data["max_hsv"][1], color_data["max_hsv"][2]);
                if (color_data.contains("min_hsv2")) {
                    color.min_hsv2 = cv::Scalar(color_data["min_hsv2"][0], color_data["min_hsv2"][1], color_data["min_hsv2"][2]);
                    color.max_hsv2 = cv::Scalar(color_data["max_hsv2"][0], color_data["max_hsv2"][1], color_data["max_hsv2"][2]);
                }
            }
        }
        std::cerr << "Settings loaded from " << settings_file_ << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error loading settings: " << e.what() << ". Using default values." << std::endl;
        return false;
    }
}

void BallTracker::saveSettings() {
    json j;
    for (const auto& color : colors_) {
        j[color.name]["min_hsv"] = {color.min_hsv[0], color.min_hsv[1], color.min_hsv[2]};
        j[color.name]["max_hsv"] = {color.max_hsv[0], color.max_hsv[1], color.max_hsv[2]};
        if (color.min_hsv2[0] >= 0) { // Only save second range if it's valid
            j[color.name]["min_hsv2"] = {color.min_hsv2[0], color.min_hsv2[1], color.min_hsv2[2]};
            j[color.name]["max_hsv2"] = {color.max_hsv2[0], color.max_hsv2[1], color.max_hsv2[2]};
        }
    }
    std::ofstream file(settings_file_);
    file << j.dump(4); // pretty print with 4 spaces
    std::cerr << "Settings saved to " << settings_file_ << std::endl;
}

void BallTracker::resetToDefaults() {
    colors_ = {
        ColorRange("pink", cv::Scalar(150, 150, 90), cv::Scalar(170, 255, 255)),
        ColorRange("orange", cv::Scalar(5, 150, 120), cv::Scalar(15, 255, 255)),
        ColorRange("green", cv::Scalar(45, 120, 70), cv::Scalar(75, 255, 255)),
        ColorRange("yellow", cv::Scalar(25, 120, 100), cv::Scalar(35, 255, 255))
    };
    std::cerr << "Reset to default color values" << std::endl;
}

void BallTracker::calibrateColor(const std::string& color_name, const cv::Mat& hsv_image, 
                                const cv::Point& click_point) {
    // Find the color to calibrate
    auto it = std::find_if(colors_.begin(), colors_.end(), 
                          [&color_name](const ColorRange& c) { return c.name == color_name; });
    
    if (it == colors_.end()) {
        std::cerr << "Error: Color '" << color_name << "' not found" << std::endl;
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
    
    // Calculate mean and standard deviation of HSV values in the sample area
    cv::Scalar mean, stddev;
    cv::meanStdDev(sample_area, mean, stddev);
    
    // Set HSV range based on the sampled values with reduced tolerance for better separation
    int h_tolerance = 8;  // Reduced from 15 to prevent color overlap
    int s_tolerance = 40; // Reduced from 50
    int v_tolerance = 40; // Reduced from 50
    
    int h_mean = (int)mean[0];
    
    // Handle potential HSV wrap-around for colors near red/magenta boundary
    if ((color_name == "pink" && (h_mean <= 10 || h_mean >= 170)) ||
        (color_name == "orange" && h_mean <= 15)) {
        // Handle wrap-around
        if (h_mean <= 15) {
            it->min_hsv = cv::Scalar(
                std::max(0, h_mean - h_tolerance),
                std::max(0, (int)mean[1] - s_tolerance),
                std::max(0, (int)mean[2] - v_tolerance)
            );
            it->max_hsv = cv::Scalar(
                std::min(15, h_mean + h_tolerance),
                std::min(255, (int)mean[1] + s_tolerance),
                std::min(255, (int)mean[2] + v_tolerance)
            );
            it->min_hsv2 = cv::Scalar(
                std::max(165, 180 - h_tolerance),
                std::max(0, (int)mean[1] - s_tolerance),
                std::max(0, (int)mean[2] - v_tolerance)
            );
            it->max_hsv2 = cv::Scalar(
                180,
                std::min(255, (int)mean[1] + s_tolerance),
                std::min(255, (int)mean[2] + v_tolerance)
            );
        } else {
            it->min_hsv = cv::Scalar(
                std::max(165, h_mean - h_tolerance),
                std::max(0, (int)mean[1] - s_tolerance),
                std::max(0, (int)mean[2] - v_tolerance)
            );
            it->max_hsv = cv::Scalar(
                180,
                std::min(255, (int)mean[1] + s_tolerance),
                std::min(255, (int)mean[2] + v_tolerance)
            );
            it->min_hsv2 = cv::Scalar(
                0,
                std::max(0, (int)mean[1] - s_tolerance),
                std::max(0, (int)mean[2] - v_tolerance)
            );
            it->max_hsv2 = cv::Scalar(
                std::min(15, h_tolerance),
                std::min(255, (int)mean[1] + s_tolerance),
                std::min(255, (int)mean[2] + v_tolerance)
            );
        }
    } else {
        // Normal single range
        it->min_hsv = cv::Scalar(
            std::max(0, h_mean - h_tolerance),
            std::max(0, (int)mean[1] - s_tolerance),
            std::max(0, (int)mean[2] - v_tolerance)
        );
        it->max_hsv = cv::Scalar(
            std::min(180, h_mean + h_tolerance),
            std::min(255, (int)mean[1] + s_tolerance),
            std::min(255, (int)mean[2] + v_tolerance)
        );
        it->min_hsv2 = cv::Scalar(-1, -1, -1); // Disable second range
        it->max_hsv2 = cv::Scalar(-1, -1, -1);
    }
    
    std::cerr << "Calibrated " << color_name << " color from click at (" 
              << click_point.x << "," << click_point.y << ")" << std::endl;
    std::cerr << "HSV values - H:" << (int)mean[0] << " S:" << (int)mean[1] << " V:" << (int)mean[2] << std::endl;
}

std::vector<BallDetection> BallTracker::detectBalls(const cv::Mat& color_image, 
                                                   const rs2::depth_frame& depth_frame,
                                                   const rs2_intrinsics& intrinsics,
                                                   double downscale_factor) {
    std::vector<BallDetection> detections;
    
    // Convert to HSV
    cv::Mat hsv_image;
    cv::cvtColor(color_image, hsv_image, cv::COLOR_BGR2HSV);
    
    // Get current timestamp
    auto now = std::chrono::system_clock::now();
    auto timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()).count();
    
    // Detect balls for each color
    for (const auto& color : colors_) {
        std::vector<cv::Point2f> centers;
        detectBallsForColor(hsv_image, color, centers, downscale_factor);
        
        for (const auto& center : centers) {
            int x = static_cast<int>(center.x);
            int y = static_cast<int>(center.y);
            
            if (x >= 0 && x < color_image.cols && y >= 0 && y < color_image.rows) {
                // Get averaged depth
                const int patch_size = 5;
                float depth = getAveragedDepth(depth_frame, x, y, patch_size);
                
                if (depth > 0 && depth < MAX_DEPTH) {
                    // Deproject 2D pixel to 3D point
                    float pixel[2] = {center.x, center.y};
                    float point[3];
                    rs2_deproject_pixel_to_point(point, &intrinsics, pixel, depth);
                    
                    BallDetection detection;
                    detection.id = color.name + "_" + std::to_string(timestamp_us);
                    detection.color_name = color.name;
                    detection.center = center;
                    detection.world_x = point[0];
                    detection.world_y = point[1];
                    detection.world_z = point[2];
                    detection.confidence = 1.0f;
                    detection.is_held = false; // TODO: Implement hand tracking integration
                    detection.timestamp_us = timestamp_us;
                    
                    detections.push_back(detection);
                }
            }
        }
    }
    
    return detections;
}

} // namespace juggler