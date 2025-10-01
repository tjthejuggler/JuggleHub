#pragma once

#include <opencv2/opencv.hpp>
#include <librealsense2/rs.hpp>
#include <vector>
#include <string>
#include <map>
#include "json.hpp"

using json = nlohmann::json;

// Forward declarations to avoid circular dependency
struct TrackedObject;
struct TrackedHand;

namespace juggler {

// Struct to hold HSV color range
struct ColorProfile {
    std::string name;
    cv::Scalar min_hsv;
    cv::Scalar max_hsv;
    cv::Scalar min_hsv2; // For colors that wrap around HSV (like red)
    cv::Scalar max_hsv2;
    
    ColorProfile(const std::string& n = "", 
                 const cv::Scalar& min = cv::Scalar(0, 0, 0), 
                 const cv::Scalar& max = cv::Scalar(180, 255, 255),
                 const cv::Scalar& min2 = cv::Scalar(-1, -1, -1), 
                 const cv::Scalar& max2 = cv::Scalar(-1, -1, -1))
        : name(n), min_hsv(min), max_hsv(max), min_hsv2(min2), max_hsv2(max2) {}
};

// Color-tracked ball state
struct ColorTrackedBall {
    int logical_id;                    // Persistent ID (0, 1, 2 for 3 balls)
    std::string color_name;            // Associated color profile name
    cv::Point2f pixel_pos;             // Current 2D position
    cv::Point3f world_pos;             // Current 3D position
    bool is_active;                    // Whether this tracker is currently tracking
    int associated_wrist_id;           // -1 if not associated, 0=left, 1=right if associated
    int frames_since_seen;             // Counter for tracking loss
    
    ColorTrackedBall() 
        : logical_id(-1), color_name(""), pixel_pos(-1, -1), world_pos(0, 0, 0),
          is_active(false), associated_wrist_id(-1), frames_since_seen(0) {}
};

class ColorTracker {
public:
    explicit ColorTracker(const std::string& settings_file = "ball_settings.json");
    ~ColorTracker() = default;
    
    // Main update function - integrates with ByteTrack and skeleton tracking
    std::vector<ColorTrackedBall> update(
        const cv::Mat& color_frame,
        const cv::Mat& depth_frame,
        const rs2_intrinsics& intrinsics,
        const std::vector<TrackedObject>& bytetrack_objects,
        const std::vector<TrackedHand>& tracked_hands
    );
    
    // Settings management
    bool loadSettings();
    void saveSettings();
    void resetToDefaults();
    bool updateSetting(const std::string& key, const std::string& value);
    
    // Color calibration
    void calibrateColor(const std::string& color_name, const cv::Mat& hsv_image,
                       const cv::Point& click_point);
    
    // Getters
    const std::vector<ColorProfile>& getColorProfiles() const { return color_profiles_; }
    std::vector<ColorProfile>& getColorProfiles() { return color_profiles_; }
    
private:
    // Helper methods
    cv::Point2f findLargestColorBlob(const cv::Mat& hsv_frame, const ColorProfile& profile,
                                     const cv::Point2f& search_center, int search_radius);
    bool matchesColorProfile(const cv::Mat& hsv_frame, const cv::Point2f& center,
                            const ColorProfile& profile, int sample_radius = 5);
    float getDepthAtPoint(const rs2::depth_frame& depth_frame, const cv::Point2f& point,
                         int patch_size = 5);
    cv::Point3f deprojectToWorld(const cv::Point2f& pixel, float depth,
                                const rs2_intrinsics& intrinsics);
    
    // State
    std::vector<ColorProfile> color_profiles_;
    std::vector<ColorTrackedBall> tracked_balls_;
    std::string settings_file_;
    
    // Parameters
    static constexpr int NUM_BALLS = 3;
    static constexpr float WRIST_ASSOCIATION_DISTANCE = 0.15f; // 15cm
    static constexpr int WRIST_SEARCH_RADIUS = 100; // pixels
    static constexpr int MAX_FRAMES_LOST = 30; // ~1 second at 30fps
    static constexpr float MIN_DEPTH = 0.2f;
    static constexpr float MAX_DEPTH = 3.0f;
    static constexpr double MIN_BLOB_AREA = 50.0;
};

} // namespace juggler