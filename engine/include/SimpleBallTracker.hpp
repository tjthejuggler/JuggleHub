#pragma once

#include <opencv2/opencv.hpp>
#include <openvino/openvino.hpp>
#include <vector>
#include <string>
#include <memory>
#include <set>
#include "json.hpp"
#include "KalmanFilter3D.hpp"
#include "ColorBasedPredictor.hpp"
#include "GpuHsvConverter.hpp"

using json = nlohmann::json;

// Simple struct to hold camera intrinsics
struct CameraIntrinsics {
    float fx, fy;
    float ppx, ppy;
};

// Color profile for ball identification
struct ColorProfile {
    std::string name;
    bool enabled;
    
    // NEW: Average hue and saturation from calibration
    float avg_hue;        // Average hue value (0-180)
    float avg_saturation; // Average saturation value (0-255)
    
    // LEGACY: Keep old min/max ranges for backward compatibility
    cv::Scalar min_hsv;
    cv::Scalar max_hsv;
    cv::Scalar min_hsv2;  // For wrap-around colors like red
    cv::Scalar max_hsv2;
    
    ColorProfile(const std::string& n = "",
                 float avg_h = -1.0f,
                 float avg_s = -1.0f,
                 const cv::Scalar& min = cv::Scalar(0, 0, 0),
                 const cv::Scalar& max = cv::Scalar(180, 255, 255),
                 const cv::Scalar& min2 = cv::Scalar(-1, -1, -1),
                 const cv::Scalar& max2 = cv::Scalar(-1, -1, -1),
                 bool en = true)
        : name(n), enabled(en), avg_hue(avg_h), avg_saturation(avg_s),
          min_hsv(min), max_hsv(max), min_hsv2(min2), max_hsv2(max2) {}
};

// Detection from YOLO
struct Detection {
    cv::Rect_<float> box;
    cv::Point3f world_pos;
    float confidence;
    int class_id;
    int index;  // Index in detection array
};

// Simple hand state
struct SimpleHand {
    int id;                    // 0=left, 1=right
    cv::Point3f wrist_pos_3d;  // Wrist position from pose model (named for compatibility)
    bool is_visible;           // Detected this frame
    float confidence;          // Detection confidence
    std::vector<cv::Point3f> keypoints;  // All pose keypoints
};

// Simple ball state
struct SimpleBall {
    int id;                          // 0, 1, 2 (based on color order)
    std::string color_name;          // "green", "pink", "orange", etc.
    cv::Point3f position;            // Current 3D position
    cv::Point2f pixel_pos;           // Current 2D position
    cv::Rect_<float> bbox;           // Bounding box
    
    // State
    bool is_held;                    // In hand or in flight
    bool previous_is_held;           // Previous frame state
    int held_by_hand_id;             // -1 if not held, 0=left, 1=right
    int previous_held_by_hand_id;    // Previous frame's hand ID (for detecting hand switches)
    int state_change_counter;        // For debouncing state changes
    float distance_to_nearest_wrist; // Distance to nearest wrist in meters
    
    // Tracking
    bool has_yolo_detection;         // True if YOLO sees it this frame
    int frames_without_yolo;         // Counter for fallback logic
    KalmanFilter3D kalman;           // Only used when YOLO fails (legacy)
    ColorBasedPredictor color_predictor;  // NEW: Color-based prediction for visualization
    
    // Confidence scores (for UI display and override detection)
    float yolo_confidence;           // YOLO detection confidence (0.0-1.0)
    float color_match_score;         // How well it matches assigned color (0.0-1.0)
    int yolo_class_id;               // 0=ball, 1=ball_held
    float matched_detection_confidence;  // Confidence of the YOLO detection used for this tracker
    float matched_detection_color_score; // Color match score of the YOLO detection used for this tracker
    
    // Debug info for visualization
    std::string tracking_reason;     // Why this position was chosen (for debugging)
    
    SimpleBall() : id(-1), is_held(false), previous_is_held(false),
                   held_by_hand_id(-1), previous_held_by_hand_id(-1),
                   state_change_counter(0),
                   distance_to_nearest_wrist(-1.0f),
                   has_yolo_detection(false), frames_without_yolo(0),
                   yolo_confidence(0.0f), color_match_score(0.0f),
                   yolo_class_id(0),
                   matched_detection_confidence(0.0f),
                   matched_detection_color_score(0.0f),
                   tracking_reason("") {}
};

// Ball event (throw/catch)
struct BallEvent {
    enum Type { THROW, CATCH };
    Type type;
    int ball_id;
    int hand_id;
    uint64_t timestamp;
};

// Tracking settings for state detection
struct TrackingSettings {
    // Weights for held/in-air detection (when YOLO detects the ball)
    float ml_ball_weight = 0.3f;           // Weight for ML "ball" (in-air) classification
    float ml_ball_held_weight = 0.3f;      // Weight for ML "ball_held" classification
    float wrist_proximity_weight = 0.4f;   // Weight for wrist proximity detection (INCREASED - proximity is more reliable)
    
    // Distance thresholds
    float wrist_proximity_threshold = 0.15f;      // 15cm - distance to consider detected ball as held
    float undetected_near_hand_threshold = 0.20f; // 20cm - distance to consider undetected ball as held (occluded)
    
    // State change parameters
    int min_frames_for_state_change = 2;   // Frames needed to confirm state change (REDUCED from 3 to 2)
    
    // Color-based prediction settings
    int prediction_history_frames = 5;     // Number of frames to use for prediction
    float prediction_radius_m = 0.15f;     // Radius of prediction circle in meters (15cm)
    
    // Color tracker matching weights (for choosing which YOLO detection to assign to each ball)
    float yolo_confidence_weight = 2.0f;   // Weight for YOLO detection confidence
    float yolo_class_weight = 3.0f;        // Weight for YOLO class (ball vs ball_held)
    float color_match_weight = 1.0f;       // Weight for color matching score
    float kalman_proximity_weight = 0.0f;  // Weight for proximity to Kalman prediction (0=disabled)
    int color_sample_radius = 1;           // Radius for color sampling from detection center (1=3x3, 2=5x5, etc.)
    
    // Minimum score threshold for using YOLO detection as color tracker
    // If best YOLO detection score is below this, use Kalman prediction instead
    float min_yolo_score_threshold = 0.0f;  // 0.0 = always use YOLO if available (default behavior)
    
    // Color tracker override settings - force use of YOLO even if score is below threshold
    // when these conditions are met (helps unstick Kalman prediction)
    float override_confidence_threshold = 0.7f;  // Minimum YOLO confidence for override
    float override_color_threshold = 0.8f;       // Minimum color match score for override
    bool override_require_ball_class = true;     // Only override if ML class is 'ball' (not 'ball_held')
    
    // Maximum distance a ball tracker can move between frames (prevents flickering to far away balls)
    float max_tracker_distance_per_frame = 0.50f;  // 50cm - maximum distance ball can move in one frame
    
    // Euclidean color matching temporal consistency settings
    float temporal_consistency_bonus = 0.25f;  // Bonus to reduce distance for detections near previous position (prevents identity swaps)
    float spatial_threshold = 0.40f;  // Maximum distance (m) to apply temporal consistency bonus
    
    // Kalman prediction bonus - STRONGEST signal for tracker placement
    float kalman_prediction_bonus = 0.50f;  // Huge bonus for detections near Kalman prediction (default: 0.50)
    float kalman_prediction_threshold = 0.30f;  // Maximum distance (m) from Kalman prediction to apply bonus (default: 0.30m)
    
    // Override detection thresholds - force tracker placement when conditions are met
    // These settings ensure trackers never disappear when high-confidence detections exist
    
    // When tracker EXISTS (ball currently being tracked):
    float override_min_confidence_tracked = 0.50f;     // Minimum YOLO confidence to force tracker placement (default: 0.50)
    float override_min_color_score_tracked = 0.60f;    // Minimum color match score to force tracker placement (default: 0.60)
    
    // When tracker MISSING (no tracker for this color currently):
    float override_min_confidence_missing = 0.70f;     // Minimum YOLO confidence to create tracker (default: 0.70)
    float override_min_color_score_missing = 0.80f;    // Minimum color match score to create tracker (default: 0.80)
    
    // Held ball color blob detection settings
    // These control how the system searches for color blobs when a ball is marked as held
    int held_color_search_radius = 120;                // Search radius in pixels around hand when ball is held (default: 120px)
    float held_color_min_score = 0.30f;                // Minimum color match score to accept color blob when held (default: 0.30)
    float held_color_max_distance = 0.25f;             // Maximum distance (m) from hand to accept color blob when held (default: 0.25m)
    
    // Kalman glob detection settings
    // These control color blob search near Kalman prediction when YOLO detection is missing
    bool kalman_glob_detection_enabled = true;         // Enable color blob search at Kalman prediction (default: true)
    int kalman_glob_search_radius = 100;               // Search radius in pixels around Kalman prediction (default: 100px)
    float kalman_glob_min_color_score = 0.50f;         // Minimum color match score to accept color blob at Kalman prediction (default: 0.50)
    float kalman_glob_max_depth_diff = 0.30f;          // Maximum depth difference (m) from Kalman prediction to accept blob (default: 0.30m)
    
    TrackingSettings() = default;
};

class SimpleBallTracker {
public:
    SimpleBallTracker(const std::string& ball_model_path,
                     const std::string& pose_model_path,
                     const std::string& device_name,
                     const std::string& settings_file = "ball_settings.json");
    ~SimpleBallTracker() = default;
    
    // Main update function - now includes YOLO detection internally
    std::pair<std::vector<SimpleBall>, std::vector<BallEvent>> update(
        const cv::Mat& color_frame,
        const cv::Mat& depth_frame,
        const CameraIntrinsics& intrinsics
    );
    
    // Settings management
    bool loadSettings();
    void saveSettings();
    bool updateSetting(const std::string& key, const std::string& value);
    
    // Color calibration
    bool calibrateColor(const std::string& color_name, 
                       const cv::Point& click_point,
                       std::string& error_message);
    
    // Getters
    const std::vector<ColorProfile>& getColorProfiles() const { return color_profiles_; }
    std::vector<ColorProfile>& getColorProfiles() { return color_profiles_; }
    const std::vector<SimpleBall>& getBalls() const { return balls_; }
    const std::vector<SimpleHand>& getHands() const { return hands_; }
    const std::vector<Detection>& getLastRawDetections() const { return last_raw_detections_; }
    
    // Tracking settings
    const TrackingSettings& getTrackingSettings() const { return tracking_settings_; }
    void setTrackingSettings(const TrackingSettings& settings) { tracking_settings_ = settings; }
    
    // Utility for projection
    static cv::Point2f project_3d_to_2d(const cv::Point3f& world_pos, const CameraIntrinsics& intrinsics);

private:
    // YOLO detection
    cv::Mat preprocess(const cv::Mat& frame, float& scale_x, float& scale_y);
    std::vector<Detection> runBallDetection(const cv::Mat& color_frame, 
                                           const cv::Mat& depth_frame,
                                           const CameraIntrinsics& intrinsics);
    std::vector<SimpleHand> runPoseEstimation(const cv::Mat& color_frame,
                                             const cv::Mat& depth_frame,
                                             const CameraIntrinsics& intrinsics);
    
    // Color matching (OPTIMIZED: now takes color_frame and converts only ROIs to HSV)
    float matchColor(const Detection& det, const ColorProfile& profile, const cv::Mat& color_frame);
    
    // State detection
    bool isBallHeld(SimpleBall& ball, const std::vector<SimpleHand>& hands);
    std::vector<BallEvent> detectStatesAndEvents(std::vector<SimpleBall>& balls,
                                                 const std::vector<SimpleHand>& hands);
    
    // Fallback tracking (OPTIMIZED: now takes color_frame and converts only ROI to HSV)
    cv::Point2f searchForColorBlob(const cv::Mat& color_frame,
                                   const ColorProfile& profile,
                                   const cv::Point2f& search_center,
                                   int radius);
    
    // Utility
    float getDepthAtPoint(const cv::Mat& depth_frame, const cv::Point2f& point);
    cv::Point3f deprojectToWorld(const cv::Point2f& pixel, float depth, 
                                const CameraIntrinsics& intrinsics);
    uint64_t getCurrentTimestamp();
    
    // OpenVINO models
    ov::Core core_;
    ov::CompiledModel ball_model_;
    ov::InferRequest ball_infer_;
    ov::CompiledModel pose_model_;
    ov::InferRequest pose_infer_;
    
    // GPU-accelerated HSV converter
    std::unique_ptr<GpuHsvConverter> gpu_hsv_converter_;
    
    // State
    std::vector<ColorProfile> color_profiles_;
    std::vector<SimpleBall> balls_;
    std::vector<SimpleHand> hands_;
    std::vector<SimpleHand> last_known_hands_;  // Store last known hand positions for persistence
    std::vector<Detection> last_raw_detections_;
    std::string settings_file_;
    cv::Mat last_color_frame_;  // For calibration
    TrackingSettings tracking_settings_;  // Tracking configuration
    
    // Timing
    std::chrono::steady_clock::time_point last_update_time_;
    
    // Frame counter for debug logging
    uint64_t frame_counter_ = 0;
    
    // Model parameters
    int input_width_ = 640;
    int input_height_ = 640;
    float ball_confidence_threshold_ = 0.25f;      // Confidence threshold for 'ball' class (class_id=0)
    float ball_held_confidence_threshold_ = 0.25f; // Confidence threshold for 'ball_held' class (class_id=1)
    float nms_threshold_ = 0.5f;
    bool show_raw_yolo_detections_ = false;        // Toggle for showing raw YOLO detections in visualization
    const int num_classes_ = 2;  // ball, ball_held
    
    // Parameters
    static constexpr float WRIST_PROXIMITY_THRESHOLD = 0.15f;  // 15cm
    static constexpr int COLOR_SEARCH_RADIUS = 100;            // pixels
    static constexpr int MAX_FRAMES_WITHOUT_YOLO = 30;         // ~1 second at 30fps
    static constexpr int MIN_FRAMES_FOR_STATE_CHANGE = 3;      // Debounce state changes
    static constexpr float MIN_COLOR_MATCH_SCORE = 0.5f;       // 50% match required
    static constexpr float MIN_DEPTH = 0.2f;
    static constexpr float MAX_DEPTH = 4.0f;  // Increased from 2.0m to allow tracking at greater distances
};
