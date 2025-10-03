#pragma once

#include <opencv2/opencv.hpp>
#include <vector>

// Forward declaration
struct TrackedHand;

namespace juggler {

/**
 * @brief Filters out skin-colored regions to prevent false ball detections
 * 
 * The SkinToneFilter maintains a set of HSV ranges that correspond to human
 * skin tones across different ethnicities and lighting conditions. It can
 * check if a region matches skin tone and if it's near a detected hand.
 */
class SkinToneFilter {
public:
    SkinToneFilter();
    ~SkinToneFilter() = default;
    
    /**
     * @brief Check if a region in the frame matches skin tone
     * @param hsv_frame HSV-converted frame
     * @param center Center point to check
     * @param radius Radius around center to sample (default: 10 pixels)
     * @return true if region matches skin tone
     */
    bool isSkinTone(const cv::Mat& hsv_frame, const cv::Point2f& center, int radius = 10) const;
    
    /**
     * @brief Check if a point is near any detected hand
     * @param point Point to check (in pixel coordinates)
     * @param hands Vector of tracked hands
     * @param distance_threshold_3d Maximum 3D distance in meters (default: 0.15m = 15cm)
     * @return true if point is near a hand
     */
    bool isNearHand(const cv::Point2f& point,
                   const std::vector<TrackedHand>& hands,
                   float distance_threshold_3d = 0.15f) const;
    
    /**
     * @brief Get skin rejection confidence score
     * @param hsv_frame HSV-converted frame
     * @param center Center point to check
     * @param hands Vector of tracked hands
     * @param radius Radius around center to sample
     * @return Score from 0 (definitely skin) to 1 (definitely not skin)
     */
    float getSkinRejectionScore(const cv::Mat& hsv_frame,
                               const cv::Point2f& center,
                               const std::vector<TrackedHand>& hands,
                               int radius = 10) const;
    
    /**
     * @brief Add a custom skin tone range
     * @param min_hsv Minimum HSV values
     * @param max_hsv Maximum HSV values
     */
    void addSkinToneRange(const cv::Scalar& min_hsv, const cv::Scalar& max_hsv);
    
    /**
     * @brief Clear all skin tone ranges
     */
    void clearSkinToneRanges();
    
    /**
     * @brief Reset to default skin tone ranges
     */
    void resetToDefaults();
    
    /**
     * @brief Get all skin tone ranges
     * @return Vector of (min_hsv, max_hsv) pairs
     */
    std::vector<std::pair<cv::Scalar, cv::Scalar>> getSkinToneRanges() const;
    
    /**
     * @brief Enable/disable skin tone filtering
     * @param enabled true to enable, false to disable
     */
    void setEnabled(bool enabled) { enabled_ = enabled; }
    
    /**
     * @brief Check if skin tone filtering is enabled
     * @return true if enabled
     */
    bool isEnabled() const { return enabled_; }
    
private:
    struct SkinToneRange {
        cv::Scalar min_hsv;
        cv::Scalar max_hsv;
        
        SkinToneRange(const cv::Scalar& min, const cv::Scalar& max)
            : min_hsv(min), max_hsv(max) {}
    };
    
    std::vector<SkinToneRange> skin_ranges_;
    bool enabled_;
    
    /**
     * @brief Initialize default skin tone ranges covering various ethnicities
     */
    void initializeDefaultSkinRanges();
    
    /**
     * @brief Check if HSV values fall within a skin tone range
     * @param hsv HSV values to check
     * @param range Skin tone range
     * @return true if within range
     */
    bool matchesSkinRange(const cv::Scalar& hsv, const SkinToneRange& range) const;
};

} // namespace juggler