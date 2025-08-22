#pragma once

#include <opencv2/opencv.hpp>
#include <librealsense2/rs.hpp>
#include <vector>
#include <string>
#include <memory>
#include "json.hpp"

using json = nlohmann::json;

namespace juggler {

// Struct to hold HSV color range
struct ColorRange {
    std::string name;
    cv::Scalar min_hsv;
    cv::Scalar max_hsv;
    cv::Scalar min_hsv2; // For colors that wrap around HSV
    cv::Scalar max_hsv2; // For colors that wrap around HSV
    
    ColorRange(const std::string& n, const cv::Scalar& min, const cv::Scalar& max, 
               const cv::Scalar& min2 = cv::Scalar(-1, -1, -1), 
               const cv::Scalar& max2 = cv::Scalar(-1, -1, -1))
        : name(n), min_hsv(min), max_hsv(max), min_hsv2(min2), max_hsv2(max2) {}
};

// Ball detection result
struct BallDetection {
    std::string id;
    std::string color_name;
    cv::Point2f center;
    float world_x, world_y, world_z;
    float confidence;
    bool is_held = false;
    uint64_t timestamp_us;
};

class BallTracker {
private:
    std::vector<ColorRange> colors_;
    std::string settings_file_;
    
    // Constants
    static constexpr double MIN_CONTOUR_AREA = 100.0;
    static constexpr float MAX_DEPTH = 3.0f;
    static constexpr double MERGE_DISTANCE_THRESHOLD = 80.0;
    
    // Helper methods
    double calculateDistance(const cv::Point2f& p1, const cv::Point2f& p2);
    std::vector<cv::Point2f> mergeNearbyDetections(const std::vector<cv::Point2f>& centers);
    void detectBallsForColor(const cv::Mat& hsv_frame, const ColorRange& color, 
                            std::vector<cv::Point2f>& centers, double downscale_factor = 1.0);
    float getAveragedDepth(const rs2::depth_frame& depth_frame, int x, int y, int patch_size);
    
public:
    explicit BallTracker(const std::string& settings_file = "ball_settings.json");
    ~BallTracker() = default;
    
    // Settings management
    bool loadSettings();
    void saveSettings();
    void resetToDefaults();
    
    // Color calibration
    void calibrateColor(const std::string& color_name, const cv::Mat& hsv_image, 
                       const cv::Point& click_point);
    
    // Ball detection
    std::vector<BallDetection> detectBalls(const cv::Mat& color_image, 
                                          const rs2::depth_frame& depth_frame,
                                          const rs2_intrinsics& intrinsics,
                                          double downscale_factor = 1.0);
    
    // Getters
    const std::vector<ColorRange>& getColorRanges() const { return colors_; }
    std::vector<ColorRange>& getColorRanges() { return colors_; }
};

} // namespace juggler