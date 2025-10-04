#pragma once

#include <opencv2/opencv.hpp>
#include <string>
#include <vector>
#include <deque>
#include <map>
#include <memory>

namespace juggler {

// Forward declaration
struct Detection;
struct ColorScores;

/**
 * @brief Configuration parameters for adaptive color range adjustment
 */
struct AdaptationConfig {
    // Monitoring parameters
    int history_window_size = 60;      // Track last 60 frames (1 second at 60fps)
    float success_threshold = 0.7f;    // 70% success = "well tracked"
    float failure_threshold = 0.3f;    // 30% success = "poorly tracked"
    
    // Adjustment parameters
    float expansion_step = 2.0f;       // Expand by 2 hue degrees per adjustment
    float contraction_step = 1.0f;     // Contract by 1 hue degree per adjustment
    float shift_step = 1.0f;           // Shift center by 1 degree per adjustment
    
    // Adjustment frequency
    int frames_between_adjustments = 30;  // Adjust every 30 frames (0.5s at 60fps)
    
    // Safety limits
    float min_separation = 5.0f;       // Minimum hue separation between colors
    float max_range_width = 40.0f;     // Maximum hue range width
    float min_range_width = 10.0f;     // Minimum hue range width
    
    // Enable/disable adaptive system
    bool enabled = true;
};

/**
 * @brief Adaptive color profile with dynamic range adjustment
 */
struct AdaptiveColorProfile {
    std::string name;
    bool enabled;
    
    // Current HSV range (dynamic)
    cv::Scalar min_hsv;
    cv::Scalar max_hsv;
    cv::Scalar min_hsv2;  // For wrap-around colors (like red)
    cv::Scalar max_hsv2;
    
    // Adaptation state
    float center_hue;           // Current center of hue range
    float hue_range_width;      // Current width of hue range
    
    // Tracking statistics (rolling window)
    std::deque<bool> recent_matches;  // Last N frames: matched or not
    int frames_tracked;         // Consecutive frames with successful match
    int frames_unmatched;       // Consecutive frames without match
    float success_rate;         // Percentage of recent successful matches
    
    // Constraints
    float min_hue_width;        // Minimum range width
    float max_hue_width;        // Maximum range width
    
    // Observed color data
    std::vector<float> observed_hues;  // Actual hue values when matched
    float mean_observed_hue;           // Average of observed hues
    
    // Constructor
    AdaptiveColorProfile(const std::string& name_, bool enabled_,
                        const cv::Scalar& min_hsv_, const cv::Scalar& max_hsv_,
                        const cv::Scalar& min_hsv2_ = cv::Scalar(-1, -1, -1),
                        const cv::Scalar& max_hsv2_ = cv::Scalar(-1, -1, -1));
};

/**
 * @brief Manages adaptive color range adjustment for ball tracking
 * 
 * This class monitors tracking success for each color and dynamically adjusts
 * HSV ranges to minimize unmatched detections. It expands ranges for poorly-tracked
 * colors and contracts ranges for well-tracked colors, while preventing overlap.
 */
class AdaptiveColorManager {
public:
    /**
     * @brief Constructor
     * @param config Adaptation configuration parameters
     */
    explicit AdaptiveColorManager(const AdaptationConfig& config = AdaptationConfig());
    
    /**
     * @brief Initialize adaptive profiles from existing color profiles
     * @param color_profiles Vector of color profiles to adapt
     */
    void initializeFromProfiles(const std::vector<cv::Scalar>& min_hsv_values,
                               const std::vector<cv::Scalar>& max_hsv_values,
                               const std::vector<std::string>& color_names,
                               const std::vector<bool>& enabled_states);
    
    /**
     * @brief Monitor tracking results for this frame
     * @param matched_colors Map of color name to detection index for matched colors
     * @param detection_hues Vector of hue values for each detection
     */
    void monitorFrame(const std::map<std::string, int>& matched_colors,
                     const std::vector<float>& detection_hues);
    
    /**
     * @brief Adjust color ranges based on tracking statistics
     * Called periodically (every N frames) to update ranges
     */
    void adjustRanges();
    
    /**
     * @brief Get current adaptive profiles
     * @return Vector of adaptive color profiles
     */
    const std::vector<AdaptiveColorProfile>& getProfiles() const { return adaptive_profiles_; }
    
    /**
     * @brief Get a specific profile by name
     * @param color_name Name of the color profile
     * @return Pointer to profile, or nullptr if not found
     */
    const AdaptiveColorProfile* getProfile(const std::string& color_name) const;
    
    /**
     * @brief Enable or disable a color profile
     * @param color_name Name of the color profile
     * @param enabled True to enable, false to disable
     */
    void setProfileEnabled(const std::string& color_name, bool enabled);
    
    /**
     * @brief Get current configuration
     * @return Adaptation configuration
     */
    const AdaptationConfig& getConfig() const { return config_; }
    
    /**
     * @brief Update configuration
     * @param config New configuration
     */
    void setConfig(const AdaptationConfig& config) { config_ = config; }
    
    /**
     * @brief Reset all profiles to their initial state
     */
    void reset();
    
    /**
     * @brief Get tracking statistics for debugging
     * @return Map of color name to success rate
     */
    std::map<std::string, float> getSuccessRates() const;

private:
    // Configuration
    AdaptationConfig config_;
    
    // Adaptive profiles
    std::vector<AdaptiveColorProfile> adaptive_profiles_;
    
    // Frame counter for adjustment timing
    int frame_count_;
    
    // Private helper methods
    
    /**
     * @brief Expand range for a poorly-tracked color
     * @param profile Profile to expand
     */
    void expandRange(AdaptiveColorProfile& profile);
    
    /**
     * @brief Contract range for a well-tracked color
     * @param profile Profile to contract
     */
    void contractRange(AdaptiveColorProfile& profile);
    
    /**
     * @brief Shift center toward observed hue values
     * @param profile Profile to shift
     * @param target_hue Target hue to shift toward
     */
    void shiftCenterToward(AdaptiveColorProfile& profile, float target_hue);
    
    /**
     * @brief Update min/max HSV from center and width
     * @param profile Profile to update
     */
    void updateRangeFromCenter(AdaptiveColorProfile& profile);
    
    /**
     * @brief Calculate overlap between two profiles
     * @param profile1 First profile
     * @param profile2 Second profile
     * @return Overlap amount in hue degrees
     */
    float calculateOverlap(const AdaptiveColorProfile& profile1,
                          const AdaptiveColorProfile& profile2) const;
    
    /**
     * @brief Resolve conflicts between overlapping ranges
     * @param failing_colors Profiles with low success rates
     * @param succeeding_colors Profiles with high success rates
     */
    void resolveConflicts(const std::vector<AdaptiveColorProfile*>& failing_colors,
                         const std::vector<AdaptiveColorProfile*>& succeeding_colors);
    
    /**
     * @brief Ensure minimum separation between all enabled colors
     */
    void ensureMinimumSeparation();
    
    /**
     * @brief Calculate mean of a vector of values
     * @param values Vector of float values
     * @return Mean value
     */
    float calculateMean(const std::vector<float>& values) const;
    
    /**
     * @brief Normalize hue to 0-180 range (handle wrap-around)
     * @param hue Hue value
     * @return Normalized hue
     */
    float normalizeHue(float hue) const;
    
    /**
     * @brief Calculate angular distance between two hues (handles wrap-around)
     * @param hue1 First hue
     * @param hue2 Second hue
     * @return Angular distance
     */
    float hueDistance(float hue1, float hue2) const;
};

} // namespace juggler