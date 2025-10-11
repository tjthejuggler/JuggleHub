#pragma once

#include <opencv2/opencv.hpp>
#include <openvino/openvino.hpp>
#include <vector>
#include <string>
#include <memory>
#include <set>
#include "json.hpp"
#include "GpuHsvConverter.hpp"
#include "GpuTrajectoryPredictor.hpp"

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

// Ball state enum for simplified state machine
enum BallState {
    HELD,       // Ball is in hand, tracker at wrist
    IN_FLIGHT   // Ball is airborne, tracker on trajectory
};

// Detection from YOLO
struct Detection {
    cv::Rect_<float> box;
    cv::Point3f world_pos;
    float confidence;
    int class_id;
    int index;  // Index in detection array
    
    // Override evaluation (calculated per-ball during tracking)
    struct OverrideEval {
        std::string ball_color;
        float color_score;
        bool meets_confidence_threshold;
        bool meets_color_threshold;
        bool meets_class_requirement;
        bool would_override;
        std::string reason;
        
        OverrideEval() : color_score(0.0f), meets_confidence_threshold(false),
                        meets_color_threshold(false), meets_class_requirement(false),
                        would_override(false) {}
    };
    std::vector<OverrideEval> override_evals;  // One per ball color
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
    
    // State (NEW: simplified state machine)
    BallState state;                 // HELD or IN_FLIGHT
    BallState previous_state;        // Previous frame's state (for detecting state transitions)
    bool is_held;                    // In hand or in flight (kept for compatibility)
    int held_by_hand_id;             // -1 if not held, 0=left, 1=right
    int previous_held_by_hand_id;    // Previous frame's hand ID (for detecting hand switches)
    
    // Trajectory (NEW: replaces Kalman and ColorBasedPredictor in Phase 3)
    BallTrajectory trajectory;       // Only valid when IN_FLIGHT
    cv::Point3f last_held_position;  // Position when last held (for velocity estimation)
    
    // Tracking
    bool has_yolo_detection;         // True if YOLO sees it this frame
    
    // Lockup prevention (NEW: track frames without verified detection)
    int frames_without_verified_detection;  // Counter for frames without real detection
    int unverified_trajectory_points;       // Counter for unverified points added
    
    // Confidence scores (for UI display and override detection)
    float yolo_confidence;           // YOLO detection confidence (0.0-1.0)
    float color_match_score;         // How well it matches assigned color (0.0-1.0)
    int yolo_class_id;               // 0=ball, 1=ball_held
    float matched_detection_confidence;  // Confidence of the YOLO detection used for this tracker
    float matched_detection_color_score; // Color match score of the YOLO detection used for this tracker
    
    // Debug info for visualization
    std::string tracking_reason;     // Why this position was chosen (for debugging)
    
    SimpleBall() : id(-1), state(HELD), is_held(false),
                   held_by_hand_id(-1), previous_held_by_hand_id(-1),
                   has_yolo_detection(false),
                   frames_without_verified_detection(0),
                   unverified_trajectory_points(0),
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

// Tracking settings for trajectory-based tracking
struct TrackingSettings {
    // State transition thresholds
    float throw_distance_threshold = 0.20f;   // Min distance to detect throw (m)
    float catch_distance_threshold = 0.30f;   // Max distance to detect catch (m) - increased to match typical juggling catches
    int min_frames_for_transition = 2;        // Debouncing for state changes
    
    // Trajectory parameters
    float traj_gravity = 9.81f;                        // Gravitational acceleration (m/s²)
    float traj_time_step = 0.033f;                     // Time between predicted points (s)
    float traj_max_time = 3.0f;                        // Maximum trajectory duration (s)
    
    // Search parameters
    float traj_search_radius = 0.15f;                  // Search radius along trajectory (m)
    float traj_min_points_for_prediction = 3;          // Points needed before using full physics
    float traj_color_match_threshold = 0.50f;          // Color match threshold for verification
    float traj_velocity_estimation_time = 0.1f;        // Time window for velocity estimation (s)
    float traj_max_search_distance = 0.50f;            // Maximum search distance from prediction (m)
    
    // Legacy trajectory parameters (kept for backward compatibility)
    float gravity = 9.81f;                    // Gravitational acceleration (m/s²)
    float trajectory_time_step = 0.033f;      // Time between predicted points (s)
    float max_trajectory_time = 3.0f;         // Maximum trajectory duration (s)
    float initial_search_radius = 0.30f;      // Wide search initially (m)
    float min_search_radius = 0.10f;          // Tight search when confident (m)
    float min_color_match_score = 0.50f;      // Color verification threshold
    int points_for_full_confidence = 5;       // Points needed for 100% confidence
    
    // LEGACY SETTINGS (kept for backward compatibility during transition)
    // These will be removed in Phase 3
    float undetected_near_hand_threshold = 0.20f;
    int min_frames_for_state_change = 2;
    float min_throw_distance = 0.20f;
    int prediction_history_frames = 5;
    float prediction_radius_m = 0.15f;
    float yolo_confidence_weight = 2.0f;
    float yolo_class_weight = 3.0f;
    float color_match_weight = 1.0f;
    int color_sample_radius = 1;
    float min_yolo_score_threshold = 0.0f;
    float override_confidence_threshold = 0.7f;  // DEPRECATED: kept for backward compatibility
    float override_color_threshold = 0.8f;       // DEPRECATED: kept for backward compatibility
    
    // NEW: Separate override thresholds for ball (class_id=0) and ball_held (class_id=1)
    float override_ball_confidence_threshold = 0.7f;      // Override confidence threshold for 'ball' detections
    float override_ball_color_threshold = 0.8f;           // Override color threshold for 'ball' detections
    float override_ball_held_confidence_threshold = 0.7f; // Override confidence threshold for 'ball_held' detections
    float override_ball_held_color_threshold = 0.8f;      // Override color threshold for 'ball_held' detections
    
    bool override_require_ball_class = true;
    float max_tracker_distance_per_frame = 0.50f;
    float temporal_consistency_bonus = 0.25f;
    float spatial_threshold = 0.40f;
    float override_min_confidence_tracked = 0.50f;
    float override_min_color_score_tracked = 0.60f;
    float override_min_confidence_missing = 0.70f;
    float override_min_color_score_missing = 0.80f;
    int held_color_search_radius = 120;
    float held_color_min_score = 0.30f;
    float held_color_max_distance = 0.25f;
    bool color_blob_search_enabled = true;
    int color_blob_search_radius = 100;
    float color_blob_min_color_score = 0.50f;
    float color_blob_max_depth_diff = 0.30f;
    float max_euclidean_distance = 0.15f;
    float min_euclidean_color_score = 0.30f;
    float max_depth_jump_strict = 0.20f;
    
    TrackingSettings() = default;
};
// Trajectory visualization settings
struct TrajectoryVisualizationSettings {
    bool show_trajectory = true;           // Toggle trajectory display
    bool show_verified_points = true;      // Show confirmed points
    bool show_predicted_path = true;       // Show full predicted path
    bool show_search_radius = true;        // Show current search area
    bool show_confidence = true;           // Show confidence indicator
    bool show_max_distance = true;         // Show max tracker distance circle
    bool show_throw_distance_threshold = true;  // Show throw distance threshold circles around hands
    bool show_catch_distance_threshold = true;  // Show catch distance threshold circles around hands
    
    // Colors (BGR format for OpenCV)
    cv::Scalar trajectory_color = cv::Scalar(255, 255, 0);      // Cyan
    cv::Scalar verified_point_color = cv::Scalar(0, 255, 0);    // Green
    cv::Scalar predicted_point_color = cv::Scalar(0, 255, 255); // Yellow
    cv::Scalar search_radius_color = cv::Scalar(255, 0, 255);   // Magenta
    cv::Scalar max_distance_color = cv::Scalar(0, 0, 255);      // Red
    cv::Scalar throw_distance_color = cv::Scalar(0, 165, 255);  // Orange (BGR)
    cv::Scalar catch_distance_color = cv::Scalar(0, 255, 0);    // Green (BGR)
    
    // Sizes
    int trajectory_thickness = 2;
    int point_radius = 5;
    float trajectory_point_spacing = 0.05f;  // 5cm between drawn points
    
    TrajectoryVisualizationSettings() = default;
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
    
    // Override evaluation - calculates override criteria for all detections
    // PERFORMANCE NOTE: This is expensive (detections × colors), only call when recording
    void evaluateOverrideCriteria(std::vector<Detection>& detections, const cv::Mat& color_frame);
    
    // Getters
    const std::vector<ColorProfile>& getColorProfiles() const { return color_profiles_; }
    std::vector<ColorProfile>& getColorProfiles() { return color_profiles_; }
    const std::vector<SimpleBall>& getBalls() const { return balls_; }
    const std::vector<SimpleHand>& getHands() const { return hands_; }
    const std::vector<Detection>& getLastRawDetections() const { return last_raw_detections_; }
    
    // Tracking settings
    
    // Trajectory visualization settings
    const TrajectoryVisualizationSettings& getVizSettings() const { return viz_settings_; }
    void setVizSettings(const TrajectoryVisualizationSettings& settings) { viz_settings_ = settings; }
    
    /**
     * Draw trajectory visualization on frame
     * 
     * @param frame Frame to draw on (modified in-place)
     * @param ball Ball to visualize
     * @param intrinsics Camera intrinsics for 3D-to-2D projection
     */
    void drawTrajectory(cv::Mat& frame, const SimpleBall& ball, const CameraIntrinsics& intrinsics) const;
   
   /**
    * Draw hand threshold circles on frame
    *
    * @param frame Frame to draw on (modified in-place)
    * @param hands Hands to draw thresholds around
    * @param intrinsics Camera intrinsics for 3D-to-2D projection
    */
   void drawHandThresholds(cv::Mat& frame, const std::vector<SimpleHand>& hands, const CameraIntrinsics& intrinsics) const;
   
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
    std::vector<BallEvent> detectStatesAndEvents(std::vector<SimpleBall>& balls,
                                                 const std::vector<SimpleHand>& hands);
    
    // NEW: Trajectory-based tracking methods
    void updateHeldBall(SimpleBall& ball, const std::vector<SimpleHand>& hands,
                       const std::vector<Detection>& yolo_detections,
                       const cv::Mat& color_frame, const cv::Mat& depth_frame,
                       const CameraIntrinsics& intrinsics, std::vector<BallEvent>& events);
    void updateInFlightBall(SimpleBall& ball, const std::vector<Detection>& yolo_detections,
                           const cv::Mat& color_frame, const cv::Mat& depth_frame,
                           const CameraIntrinsics& intrinsics, std::vector<BallEvent>& events);
    void initiateThrow(SimpleBall& ball, const Detection& first_detection,
                      const SimpleHand* hand, std::vector<BallEvent>& events);
    void initiateCatch(SimpleBall& ball, const SimpleHand& hand, std::vector<BallEvent>& events);
    void addVerifiedPoint(SimpleBall& ball, const cv::Point3f& position, uint64_t timestamp);
    
    // Trajectory prediction helper methods
    cv::Point3f predictWithTwoPoints(SimpleBall& ball);
    std::vector<cv::Point3f> predictFullTrajectory(SimpleBall& ball);
    const Detection* searchAlongPredictionLine(const cv::Point3f& predicted_pos,
                                               float search_radius,
                                               const std::vector<Detection>& yolo_detections,
                                               const cv::Mat& color_frame,
                                               const std::string& ball_color);
    
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
    TrajectoryVisualizationSettings viz_settings_;  // Trajectory visualization configuration
    ov::CompiledModel pose_model_;
    ov::InferRequest pose_infer_;
    
    // GPU-accelerated components
    std::unique_ptr<GpuHsvConverter> gpu_hsv_converter_;
    std::unique_ptr<GpuTrajectoryPredictor> gpu_trajectory_predictor_;
    
    // State
    std::vector<ColorProfile> color_profiles_;
    std::vector<SimpleBall> balls_;
    std::vector<SimpleHand> hands_;
    std::vector<SimpleHand> last_known_hands_;  // Store last known hand positions for persistence
    std::vector<Detection> last_raw_detections_;  // Filtered detections (after confidence threshold, before NMS)
    std::string settings_file_;
    cv::Mat last_color_frame_;  // For calibration
    TrackingSettings tracking_settings_;  // Tracking configuration
    
    // PERFORMANCE: HSV frame cache to avoid redundant GPU conversions
    cv::Mat cached_hsv_frame_;      // Full frame HSV conversion (cached per frame)
    cv::Mat cached_color_frame_;    // Reference to validate cache
    
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
