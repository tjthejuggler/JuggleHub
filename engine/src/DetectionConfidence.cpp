#include "DetectionConfidence.hpp"
#include "BallRegistry.hpp"
#include "SkinToneFilter.hpp"
#include "DNNTracker.hpp"  // For TrackedHand and CameraIntrinsics
#include "DebugLog.hpp"
#include <cmath>
#include <algorithm>

namespace juggler {

ConfidenceScorer::ConfidenceScorer(const BallRegistry& registry, const SkinToneFilter& skin_filter)
    : registry_(registry), skin_filter_(skin_filter), min_confidence_threshold_(0.65f) {
    INFO_LOG("ConfidenceScorer: Initialized with threshold ", min_confidence_threshold_);
}

DetectionConfidence ConfidenceScorer::computeConfidence(
    const cv::Mat& hsv_frame,
    const cv::Mat& depth_frame,
    const cv::Point2f& center,
    const ActiveBall& ball,
    const std::vector<TrackedHand>& hands,
    const CameraIntrinsics& intrinsics,
    float dnn_confidence) {
    
    DetectionConfidence confidence;
    
    // 1. Color Match Score
    confidence.color_match_score = scoreColorMatch(hsv_frame, center, ball);
    
    // 2. Shape Score (circularity)
    confidence.shape_score = scoreShape(hsv_frame, center, ball);
    
    // 3. Size Score
    confidence.size_score = scoreSize(depth_frame, center, ball, intrinsics);
    
    // 4. Texture Score (uniformity)
    confidence.texture_score = scoreTexture(hsv_frame, center);
    
    // 5. Temporal Score
    confidence.temporal_score = scoreTemporal(center, ball);
    
    // 6. Skin Rejection Score
    confidence.skin_rejection_score = skin_filter_.getSkinRejectionScore(hsv_frame, center, hands);
    
    // 7. DNN Confidence
    confidence.dnn_confidence = dnn_confidence;
    
    // 8. Spatial Consistency (placeholder for now)
    confidence.spatial_consistency = 1.0f;
    
    DEBUG_LOG("ConfidenceScorer: ", confidence.toString());
    
    return confidence;
}

float ConfidenceScorer::scoreColorMatch(const cv::Mat& hsv_frame, const cv::Point2f& center,
                                       const ActiveBall& ball) {
    if (ball.color_samples.empty()) {
        WARN_LOG("ConfidenceScorer: Ball '", ball.display_name, "' has no color samples");
        return 0.0f;
    }
    
    // Sample region around center
    int radius = 10;
    int x_min = std::max(0, static_cast<int>(center.x - radius));
    int y_min = std::max(0, static_cast<int>(center.y - radius));
    int x_max = std::min(hsv_frame.cols - 1, static_cast<int>(center.x + radius));
    int y_max = std::min(hsv_frame.rows - 1, static_cast<int>(center.y + radius));
    
    if (x_max <= x_min || y_max <= y_min) {
        return 0.0f;
    }
    
    cv::Rect sample_rect(x_min, y_min, x_max - x_min + 1, y_max - y_min + 1);
    cv::Mat sample_region = hsv_frame(sample_rect);
    
    // Create mask using aggregate ranges
    cv::Mat mask;
    cv::inRange(sample_region, ball.aggregate_min_hsv, ball.aggregate_max_hsv, mask);
    
    // Handle wrap-around colors (red/pink)
    if (ball.aggregate_min_hsv2[0] >= 0) {
        cv::Mat mask2;
        cv::inRange(sample_region, ball.aggregate_min_hsv2, ball.aggregate_max_hsv2, mask2);
        cv::bitwise_or(mask, mask2, mask);
    }
    
    // Compute match ratio
    int matching_pixels = cv::countNonZero(mask);
    int total_pixels = sample_region.rows * sample_region.cols;
    float match_ratio = static_cast<float>(matching_pixels) / total_pixels;
    
    // Score: 0% match = 0.0, 50% match = 0.5, 100% match = 1.0
    // But we want to be more lenient - 30% match should give decent score
    // Use a sigmoid-like curve
    float score = std::min(1.0f, match_ratio * 2.0f);  // 50% match = 1.0 score
    
    return score;
}

float ConfidenceScorer::scoreShape(const cv::Mat& hsv_frame, const cv::Point2f& center,
                                  const ActiveBall& ball) {
    // Create a mask of the ball's color in a region around the center
    int search_radius = 50;
    int x_min = std::max(0, static_cast<int>(center.x - search_radius));
    int y_min = std::max(0, static_cast<int>(center.y - search_radius));
    int x_max = std::min(hsv_frame.cols - 1, static_cast<int>(center.x + search_radius));
    int y_max = std::min(hsv_frame.rows - 1, static_cast<int>(center.y + search_radius));
    
    if (x_max <= x_min || y_max <= y_min) {
        return 0.5f;  // Neutral score if we can't check
    }
    
    cv::Rect roi(x_min, y_min, x_max - x_min + 1, y_max - y_min + 1);
    cv::Mat hsv_roi = hsv_frame(roi);
    
    // Create mask
    cv::Mat mask;
    cv::inRange(hsv_roi, ball.aggregate_min_hsv, ball.aggregate_max_hsv, mask);
    
    if (ball.aggregate_min_hsv2[0] >= 0) {
        cv::Mat mask2;
        cv::inRange(hsv_roi, ball.aggregate_min_hsv2, ball.aggregate_max_hsv2, mask2);
        cv::bitwise_or(mask, mask2, mask);
    }
    
    // Clean up mask
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(3, 3));
    cv::morphologyEx(mask, mask, cv::MORPH_OPEN, kernel);
    cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, kernel);
    
    // Find contours
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    
    if (contours.empty()) {
        return 0.0f;
    }
    
    // Find contour closest to center
    float min_dist = std::numeric_limits<float>::max();
    const std::vector<cv::Point>* closest_contour = nullptr;
    
    for (const auto& contour : contours) {
        cv::Moments m = cv::moments(contour);
        if (m.m00 > 0) {
            cv::Point2f contour_center(x_min + m.m10 / m.m00, y_min + m.m01 / m.m00);
            float dist = cv::norm(contour_center - center);
            if (dist < min_dist) {
                min_dist = dist;
                closest_contour = &contour;
            }
        }
    }
    
    if (!closest_contour || closest_contour->size() < 5) {
        return 0.5f;  // Neutral score
    }
    
    // Compute circularity
    float circularity = computeCircularity(*closest_contour);
    
    return circularity;
}

float ConfidenceScorer::scoreSize(const cv::Mat& depth_frame, const cv::Point2f& center,
                                 const ActiveBall& ball, const CameraIntrinsics& intrinsics) {
    // Get depth at center
    if (center.x < 0 || center.x >= depth_frame.cols ||
        center.y < 0 || center.y >= depth_frame.rows) {
        return 0.5f;  // Neutral score
    }
    
    uint16_t depth_mm = depth_frame.at<uint16_t>(static_cast<int>(center.y), static_cast<int>(center.x));
    float depth_m = depth_mm / 1000.0f;
    
    if (depth_m < 0.2f || depth_m > 3.0f) {
        return 0.0f;  // Invalid depth
    }
    
    // Compute expected pixel size of ball at this depth
    // Ball diameter in meters
    float ball_diameter_m = ball.expected_diameter_cm / 100.0f;
    
    // Expected pixel size (approximate)
    float expected_pixel_diameter = (ball_diameter_m * intrinsics.fx) / depth_m;
    
    // We don't have the actual detected size here, so we'll use a heuristic
    // For now, if depth is valid, give a decent score
    // TODO: Pass actual detection size and compare
    
    // Heuristic: balls should be 10-50 pixels in diameter at typical juggling distance (1-2m)
    if (expected_pixel_diameter >= 10.0f && expected_pixel_diameter <= 50.0f) {
        return 1.0f;
    } else if (expected_pixel_diameter >= 5.0f && expected_pixel_diameter <= 100.0f) {
        return 0.7f;
    } else {
        return 0.3f;
    }
}

float ConfidenceScorer::scoreTexture(const cv::Mat& hsv_frame, const cv::Point2f& center) {
    // Sample region around center
    int radius = 10;
    int x_min = std::max(0, static_cast<int>(center.x - radius));
    int y_min = std::max(0, static_cast<int>(center.y - radius));
    int x_max = std::min(hsv_frame.cols - 1, static_cast<int>(center.x + radius));
    int y_max = std::min(hsv_frame.rows - 1, static_cast<int>(center.y + radius));
    
    if (x_max <= x_min || y_max <= y_min) {
        return 0.5f;
    }
    
    cv::Rect sample_rect(x_min, y_min, x_max - x_min + 1, y_max - y_min + 1);
    cv::Mat sample_region = hsv_frame(sample_rect);
    
    // Compute standard deviation of each channel
    cv::Scalar mean, stddev;
    cv::meanStdDev(sample_region, mean, stddev);
    
    // Uniform texture should have low standard deviation
    // Hue: expect stddev < 10
    // Saturation: expect stddev < 30
    // Value: expect stddev < 30
    
    float h_score = 1.0f - std::min(1.0f, static_cast<float>(stddev[0]) / 10.0f);
    float s_score = 1.0f - std::min(1.0f, static_cast<float>(stddev[1]) / 30.0f);
    float v_score = 1.0f - std::min(1.0f, static_cast<float>(stddev[2]) / 30.0f);
    
    // Average the scores
    float texture_score = (h_score + s_score + v_score) / 3.0f;
    
    return texture_score;
}

float ConfidenceScorer::scoreTemporal(const cv::Point2f& center, const ActiveBall& ball) {
    // Check if ball is currently being tracked
    if (!ball.is_active || ball.logical_tracker_id < 0) {
        // Ball is not active, so no temporal history
        return 0.5f;  // Neutral score
    }
    
    // For now, we don't have access to the ball's last known position
    // This would require storing position history in ActiveBall
    // TODO: Add position history to ActiveBall and implement proper temporal scoring
    
    // Placeholder: if ball is active and was recently seen, give good score
    if (ball.frames_tracked > 0) {
        return 0.8f;
    } else {
        return 0.5f;
    }
}

bool ConfidenceScorer::matchesBallColor(const cv::Mat& hsv_frame, const cv::Point2f& center,
                                       const ActiveBall& ball, int radius) {
    int x_min = std::max(0, static_cast<int>(center.x - radius));
    int y_min = std::max(0, static_cast<int>(center.y - radius));
    int x_max = std::min(hsv_frame.cols - 1, static_cast<int>(center.x + radius));
    int y_max = std::min(hsv_frame.rows - 1, static_cast<int>(center.y + radius));
    
    if (x_max <= x_min || y_max <= y_min) {
        return false;
    }
    
    cv::Rect sample_rect(x_min, y_min, x_max - x_min + 1, y_max - y_min + 1);
    cv::Mat sample_region = hsv_frame(sample_rect);
    
    cv::Mat mask;
    cv::inRange(sample_region, ball.aggregate_min_hsv, ball.aggregate_max_hsv, mask);
    
    if (ball.aggregate_min_hsv2[0] >= 0) {
        cv::Mat mask2;
        cv::inRange(sample_region, ball.aggregate_min_hsv2, ball.aggregate_max_hsv2, mask2);
        cv::bitwise_or(mask, mask2, mask);
    }
    
    int matching_pixels = cv::countNonZero(mask);
    int total_pixels = sample_region.rows * sample_region.cols;
    float match_ratio = static_cast<float>(matching_pixels) / total_pixels;
    
    return match_ratio > 0.3f;  // At least 30% of pixels should match
}

float ConfidenceScorer::computeCircularity(const std::vector<cv::Point>& contour) {
    if (contour.size() < 5) {
        return 0.0f;
    }
    
    // Circularity = 4π * area / perimeter²
    // Perfect circle = 1.0
    // Square ≈ 0.785
    // Lower values = less circular
    
    double area = cv::contourArea(contour);
    double perimeter = cv::arcLength(contour, true);
    
    if (perimeter == 0) {
        return 0.0f;
    }
    
    float circularity = static_cast<float>((4.0 * M_PI * area) / (perimeter * perimeter));
    
    // Clamp to [0, 1]
    circularity = std::min(1.0f, std::max(0.0f, circularity));
    
    return circularity;
}

} // namespace juggler