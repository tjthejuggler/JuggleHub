#pragma once

#include "IBallTracker.hpp"
#include "SimpleBallTracker.hpp"  // Shared types: ColorProfile, SimpleBall, BallEvent, SimpleHand, Detection, TrackingSettings
#include "json.hpp"
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

using json = nlohmann::json;

/**
 * @brief Tunable settings for ColorOnlyTracker.
 *
 * Persisted to hub/calibration_settings_color_only.json (its own file - it never
 * touches the New3D tracker's settings except for READING the shared color
 * profiles that the hub calibration UI writes).
 */
struct ColorOnlySettings {
    // Depth gating (RealSense). Removes background such as walls and the juggler's body.
    bool use_depth_filter = true;
    float min_distance_m = 0.10f;   // 10 cm
    float max_distance_m = 3.00f;   // 300 cm

    // HSV segmentation per calibrated color profile.
    int hue_tolerance = 15;         // +/- around the profile's calibrated avg_hue (OpenCV H: 0-180)
    int sat_minimum = 80;           // 0-255
    int val_minimum = 80;           // 0-255

    // Depth-aware physical blob filters.
    float min_area_cm2 = 2.0f;      // Minimum physical blob surface area
    float max_area_cm2 = 120.0f;    // Maximum physical blob surface area
    float min_circularity = 0.20f;  // 4*pi*A/P^2

    // Safety cap: maximum reported blobs per color per frame.
    int max_balls_per_color = 4;

    bool debug_logging = false;
};

/**
 * @brief Identity-free, color-class ball tracker ("which color ball is where").
 *
 * Design goal: RELIABILITY over identity. This tracker deliberately does NOT
 * attempt to keep per-ball identity across frames. Every frame it:
 *
 *   1. Builds a depth-range mask from the RealSense depth frame (background removal).
 *   2. Converts the color frame to HSV once.
 *   3. For each enabled + calibrated color profile: builds an HSV mask around the
 *      calibrated hue, ANDs it with the depth mask, closes small holes, and finds
 *      contours (blobs).
 *   4. Filters blobs by physical surface area (depth-aware) and circularity.
 *   5. Emits one SimpleBall per surviving blob, labeled with the color name.
 *
 * Two blue balls simply produce two "blue" balls each frame. No Kalman filters,
 * no throw/catch state machines, no YOLO models, no pose model - just fast,
 * predictable OpenCV segmentation on the RealSense RGB+depth stream.
 *
 * Color profiles are READ from hub/calibration_settings_new3d.json so the
 * existing hub calibration UI (click-on-ball calibration) works with this
 * tracker unchanged. Detection tuning lives in its own settings file.
 */
class ColorOnlyTracker : public IBallTracker {
public:
    explicit ColorOnlyTracker(const std::string& settings_file = "hub/calibration_settings_color_only.json");

    // --- IBallTracker interface ---
    std::pair<std::vector<SimpleBall>, std::vector<BallEvent>>
        update(const cv::Mat& color_image, const cv::Mat& depth_image,
               const CameraIntrinsics& intrinsics) override;

    const std::vector<SimpleHand>& getHands() const override { return hands_; }
    const std::vector<Detection>& getLastRawDetections() const override { return last_detections_; }
    TrackingSettings& getTrackingSettings() override { return tracking_settings_; }
    const std::vector<ColorProfile>& getColorProfiles() const override { return color_profiles_; }

    bool calibrateColor(const std::string& color_name,
                        cv::Point click_point,
                        std::string& error_message) override;

    void setRecordingFrameNumber(int frame_num) override { recording_frame_number_ = frame_num; }

    void drawHandThresholds(cv::Mat& frame,
                            const std::vector<SimpleHand>& hands,
                            const CameraIntrinsics& intrinsics,
                            const std::vector<SimpleBall>* balls_override = nullptr) override;

    void evaluateOverrideCriteria(std::vector<Detection>& detections,
                                  const cv::Mat& color_image) override;

    bool updateSetting(const std::string& key, const std::string& value) override;

    // --- Color profile management ---
    /** Reload color profiles from the shared calibration file (hub sends RELOAD_COLOR_PROFILES). */
    void reloadColorProfiles();

    const ColorOnlySettings& getSettings() const { return settings_; }

private:
    bool loadSettings();
    void saveSettings() const;
    void loadColorProfiles();
    cv::Point3f deprojectToWorld(const cv::Point2f& pixel, float depth_m,
                                 const CameraIntrinsics& intrinsics) const;

    ColorOnlySettings settings_;
    TrackingSettings tracking_settings_;       // Interface compatibility (not used for detection)
    std::vector<ColorProfile> color_profiles_; // Shared with New3DTracker calibration file
    std::vector<SimpleHand> hands_;            // Always empty (no pose model)
    std::vector<Detection> last_detections_;   // Blobs found in the last frame
    std::vector<SimpleBall> last_balls_;       // Balls emitted in the last frame

    std::string settings_file_;                // Own tuning settings (color_only)
    std::string color_profiles_file_ = "hub/calibration_settings_new3d.json";  // Shared calibration

    cv::Mat last_color_image_;                 // Cached for click calibration
    int recording_frame_number_ = -1;
};
