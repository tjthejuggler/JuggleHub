#include "SkinToneFilter.hpp"
#include "DNNTracker.hpp"  // For TrackedHand
#include "DebugLog.hpp"
#include <cmath>

namespace juggler {

SkinToneFilter::SkinToneFilter()
    : enabled_(true) {
    initializeDefaultSkinRanges();
    INFO_LOG("SkinToneFilter: Initialized with ", skin_ranges_.size(), " default skin tone ranges");
}

void SkinToneFilter::initializeDefaultSkinRanges() {
    skin_ranges_.clear();
    
    // Light skin tones (typical Caucasian, East Asian)
    // Hue: 0-25° (red-orange-yellow range)
    // Saturation: 20-150 (low to medium saturation)
    // Value: 80-255 (medium to high brightness)
    skin_ranges_.emplace_back(
        cv::Scalar(0, 20, 80),    // min HSV
        cv::Scalar(25, 150, 255)  // max HSV
    );
    
    // Medium skin tones (typical South Asian, Hispanic, Mediterranean)
    // Slightly higher hue, similar saturation
    skin_ranges_.emplace_back(
        cv::Scalar(10, 20, 80),
        cv::Scalar(30, 150, 255)
    );
    
    // Dark skin tones (typical African, South Indian)
    // Similar hue range but lower value (brightness)
    skin_ranges_.emplace_back(
        cv::Scalar(15, 20, 60),
        cv::Scalar(35, 150, 220)
    );
    
    // Additional range for very light skin in bright lighting
    skin_ranges_.emplace_back(
        cv::Scalar(0, 10, 150),
        cv::Scalar(20, 100, 255)
    );
}

bool SkinToneFilter::isSkinTone(const cv::Mat& hsv_frame, const cv::Point2f& center, int radius) const {
    if (!enabled_) {
        return false;
    }
    
    // Validate center point
    if (center.x < 0 || center.x >= hsv_frame.cols ||
        center.y < 0 || center.y >= hsv_frame.rows) {
        return false;
    }
    
    // Define sample region
    int x_min = std::max(0, static_cast<int>(center.x - radius));
    int y_min = std::max(0, static_cast<int>(center.y - radius));
    int x_max = std::min(hsv_frame.cols - 1, static_cast<int>(center.x + radius));
    int y_max = std::min(hsv_frame.rows - 1, static_cast<int>(center.y + radius));
    
    if (x_max <= x_min || y_max <= y_min) {
        return false;
    }
    
    cv::Rect sample_rect(x_min, y_min, x_max - x_min + 1, y_max - y_min + 1);
    cv::Mat sample_region = hsv_frame(sample_rect);
    
    // Compute mean HSV
    cv::Scalar mean_hsv = cv::mean(sample_region);
    
    // Check if mean matches any skin tone range
    for (const auto& range : skin_ranges_) {
        if (matchesSkinRange(mean_hsv, range)) {
            DEBUG_LOG("SkinToneFilter: Detected skin tone at (", center.x, ",", center.y, 
                     ") - H:", static_cast<int>(mean_hsv[0]), 
                     " S:", static_cast<int>(mean_hsv[1]), 
                     " V:", static_cast<int>(mean_hsv[2]));
            return true;
        }
    }
    
    return false;
}

bool SkinToneFilter::isNearHand(const cv::Point2f& point,
                               const std::vector<TrackedHand>& hands,
                               float distance_threshold_3d) const {
    if (!enabled_ || hands.empty()) {
        return false;
    }
    
    // Check distance to each hand's wrist position
    for (const auto& hand : hands) {
        // We only have 2D pixel coordinates for the point, but hands have 3D positions
        // We need to check if the point is close in 2D space to the hand
        // This is a simplified check - ideally we'd deproject the point to 3D
        
        // For now, we'll use a simple 2D distance check
        // This is a heuristic: if a detection is within ~100 pixels of a wrist in 2D,
        // it's likely near the hand
        
        // Note: This is a placeholder. The actual implementation should use
        // camera intrinsics to project the hand's 3D position to 2D and compare.
        // For now, we'll return false to avoid false rejections.
        
        // TODO: Implement proper 2D projection of hand position
    }
    
    return false;
}

float SkinToneFilter::getSkinRejectionScore(const cv::Mat& hsv_frame,
                                           const cv::Point2f& center,
                                           const std::vector<TrackedHand>& hands,
                                           int radius) const {
    if (!enabled_) {
        return 1.0f;  // Not skin (filtering disabled)
    }
    
    // Check if it's skin tone
    bool is_skin = isSkinTone(hsv_frame, center, radius);
    
    // Check if near hand
    bool near_hand = isNearHand(center, hands);
    
    // Compute rejection score
    // 0.0 = definitely skin (reject)
    // 1.0 = definitely not skin (accept)
    
    if (is_skin && near_hand) {
        // Very likely skin (near hand and matches skin color)
        return 0.0f;
    } else if (is_skin) {
        // Likely skin (matches skin color but not near hand)
        // Could be skin elsewhere in frame
        return 0.3f;
    } else if (near_hand) {
        // Near hand but doesn't match skin color
        // Could be a ball held in hand
        return 0.7f;
    } else {
        // Not skin color and not near hand
        return 1.0f;
    }
}

void SkinToneFilter::addSkinToneRange(const cv::Scalar& min_hsv, const cv::Scalar& max_hsv) {
    skin_ranges_.emplace_back(min_hsv, max_hsv);
    INFO_LOG("SkinToneFilter: Added custom skin tone range - H:[", 
             static_cast<int>(min_hsv[0]), "-", static_cast<int>(max_hsv[0]), "] ",
             "S:[", static_cast<int>(min_hsv[1]), "-", static_cast<int>(max_hsv[1]), "] ",
             "V:[", static_cast<int>(min_hsv[2]), "-", static_cast<int>(max_hsv[2]), "]");
}

void SkinToneFilter::clearSkinToneRanges() {
    skin_ranges_.clear();
    INFO_LOG("SkinToneFilter: Cleared all skin tone ranges");
}

void SkinToneFilter::resetToDefaults() {
    initializeDefaultSkinRanges();
    INFO_LOG("SkinToneFilter: Reset to default skin tone ranges");
}

std::vector<std::pair<cv::Scalar, cv::Scalar>> SkinToneFilter::getSkinToneRanges() const {
    std::vector<std::pair<cv::Scalar, cv::Scalar>> ranges;
    for (const auto& range : skin_ranges_) {
        ranges.emplace_back(range.min_hsv, range.max_hsv);
    }
    return ranges;
}

bool SkinToneFilter::matchesSkinRange(const cv::Scalar& hsv, const SkinToneRange& range) const {
    // Check if HSV values fall within the range
    return (hsv[0] >= range.min_hsv[0] && hsv[0] <= range.max_hsv[0] &&
            hsv[1] >= range.min_hsv[1] && hsv[1] <= range.max_hsv[1] &&
            hsv[2] >= range.min_hsv[2] && hsv[2] <= range.max_hsv[2]);
}

} // namespace juggler