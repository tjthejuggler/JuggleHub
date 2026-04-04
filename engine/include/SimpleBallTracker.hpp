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
#include "IBallTracker.hpp"

using json = nlohmann::json;

// Use shared CameraIntrinsics definition
#include "CameraIntrinsics.hpp"

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

// Detection from YOLO or depth blob detection
struct Detection {
    cv::Rect_<float> box;
    cv::Point3f world_pos;
    float confidence;
    int class_id;
    int index;  // Index in detection array
    cv::Vec3b detected_bgr_color;  // Sampled BGR color (median-filtered, saturation-thresholded)
    std::string color_name;  // Pre-identified color name (set by color-first detection, empty otherwise)
    
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
    
    // Hand velocity tracking (for throw prediction)
    std::vector<cv::Point3f> position_history;  // Last 3 positions for velocity calculation
    cv::Point3f velocity;                        // Current velocity vector (m/s)
    bool has_valid_velocity;                     // True if we have enough history to calculate velocity
    uint64_t last_update_timestamp;              // Timestamp of last position update
    
    SimpleHand() : id(-1), is_visible(false), confidence(0.0f),
                   velocity(0, 0, 0), has_valid_velocity(false),
                   last_update_timestamp(0) {}
};

// Detection candidate for throw validation
struct DetectionCandidate {
    cv::Point3f position;
    float distance_from_hand;
    float color_score;
    float confidence;
    uint64_t timestamp;
    int frame_number;
    
    DetectionCandidate() : distance_from_hand(0.0f), color_score(0.0f),
                          confidence(0.0f), timestamp(0), frame_number(0) {}
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
    
    // Throw tracking (NEW: prevent immediate re-catch by throwing hand)
    int was_just_thrown_by_hand_id;  // -1 if not just thrown, 0=left, 1=right (lasts 1 frame)
    int last_throwing_hand_id;  // Hand ID that last threw this ball (-1 if never thrown)
    int frames_in_flight_since_throw;  // Counter for frames in flight since last throw
    
    // Sequential throw detection (NEW: track detection history for robust throw detection)
    std::vector<DetectionCandidate> throw_candidates;  // Last 5 frames of detection candidates
    int consecutive_valid_detections;                   // Counter for consecutive valid detections
    
    // Potential throw location tracking (NEW: Fix catch-22 problem)
    bool has_potential_throw_location;                  // True if we detected a potential throw location
    cv::Point3f potential_throw_location;               // Location where ball was detected near hand
    int potential_throw_frame_age;                      // Frames since potential throw was detected
    
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
    
    // YOLO detected color (for logging and visualization)
    cv::Vec3b detected_bgr_color;    // Actual BGR color detected by YOLO at detection center
    
    // Hand velocity visualization (for debugging)
    bool hand_velocity_active;       // True if hand velocity detection is active this frame
    cv::Point3f hand_velocity_center; // Center of velocity detection zone
    cv::Point3f hand_velocity_direction; // Direction of hand movement
    float hand_velocity_radius;      // Radius of detection zone
    
    SimpleBall() : id(-1), state(HELD), is_held(false),
                   held_by_hand_id(-1), previous_held_by_hand_id(-1),
                   was_just_thrown_by_hand_id(-1),
                   last_throwing_hand_id(-1),
                   frames_in_flight_since_throw(0),
                   consecutive_valid_detections(0),
                   has_potential_throw_location(false),
                   potential_throw_location(0, 0, 0),
                   potential_throw_frame_age(0),
                   has_yolo_detection(false),
                   frames_without_verified_detection(0),
                   unverified_trajectory_points(0),
                   yolo_confidence(0.0f), color_match_score(0.0f),
                   yolo_class_id(0),
                   matched_detection_confidence(0.0f),
                   matched_detection_color_score(0.0f),
                   tracking_reason(""),
                   detected_bgr_color(0, 0, 0),
                   hand_velocity_active(false),
                   hand_velocity_center(0, 0, 0),
                   hand_velocity_direction(0, 0, 0),
                   hand_velocity_radius(0.0f) {}
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
    // UNIFIED: Single threshold for hand-ball distance (replaces separate catch/throw thresholds)
    float hand_distance_threshold = 0.30f;    // Distance threshold for hand-ball proximity (m)
    
    // DEPRECATED: Legacy thresholds kept for backward compatibility
    float throw_distance_threshold = 0.20f;   // DEPRECATED: Use hand_distance_threshold instead
    float catch_distance_threshold = 0.30f;   // DEPRECATED: Use hand_distance_threshold instead
    
    int min_frames_for_transition = 2;        // Debouncing for state changes
    int min_frames_before_catch = 3;          // Minimum frames in flight before same hand can catch (0-10)
    
    // Sequential throw detection settings (NEW: robust multi-frame validation)
    int throw_min_sequential_detections = 2;      // Require 2-3 consecutive frames with valid detections
    float throw_min_movement_per_frame = 0.05f;   // 5cm minimum movement between frames
    float throw_min_velocity = 0.5f;              // 0.5 m/s minimum throw velocity
    float throw_max_velocity = 10.0f;             // 10 m/s maximum throw velocity
    float throw_direction_consistency = 0.5f;     // Dot product threshold for direction consistency (60° max angle)
    float throw_max_detection_distance = 0.5f;    // Max distance from hand to consider detection valid
    int throw_candidate_history_size = 5;         // Number of frames to keep in detection history
    
    // Class filtering
    bool ignore_class = false;                // If true, ignore ML class distinctions (treat ball/ball_held same)
    
    // Hand velocity tracking settings (for throw prediction)
    bool hand_velocity_enabled = true;                    // Enable hand velocity-based throw prediction
    float hand_velocity_threshold = 1.0f;                 // Minimum hand velocity to trigger enhanced detection (m/s)
    float hand_velocity_confidence_reduction = 0.3f;      // Reduce confidence threshold by this amount (0.0-1.0)
    bool hand_velocity_ignore_class = false;              // If true, ignore class requirement when hand is moving fast
    float hand_velocity_detection_radius = 0.15f;         // Radius of detection zone in direction of hand movement (m)
    float hand_velocity_distance_reduction = 0.10f;       // Reduce hand_distance_threshold to this value when velocity zone is active (m)
    
    // Trajectory parameters
    float traj_gravity = 9.81f;                        // Gravitational acceleration (m/s²)
    float traj_time_step = 0.033f;                     // Time between predicted points (s)
    float traj_max_time = 3.0f;                        // Maximum trajectory duration (s)
    
    // Search parameters
    float traj_search_radius = 0.15f;                  // Search radius along trajectory (m)
    float traj_min_points_for_prediction = 3;          // Points needed before using full physics
    float traj_color_match_threshold = 0.50f;          // Color match threshold for verification
    float traj_yolo_confidence_threshold = 0.70f;      // YOLO confidence for IN_FLIGHT trajectory tracking
    float throw_yolo_confidence_threshold = 0.50f;     // YOLO confidence for throw detection (HELD→IN_FLIGHT)
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
    
    // Ball separation and hand change detection (NEW: Fix #5)
    float min_color_confidence_override = 0.35f;  // Minimum color match confidence required for override (0.0-1.0)
    float min_ball_separation = 0.15f;            // Minimum separation between balls in meters (except same hand)
    float min_hand_change_distance = 0.25f;       // Minimum movement distance for hand change detection (meters)
    
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
    bool show_hand_distance_threshold = true;  // Show unified hand distance threshold circles around hands
    bool show_hand_velocity_zone = false;  // Show hand velocity detection zone (purple circle)
    // DEPRECATED: Keep for backward compatibility
    bool show_throw_distance_threshold = true;  // DEPRECATED: Use show_hand_distance_threshold instead
    bool show_catch_distance_threshold = true;  // DEPRECATED: Use show_hand_distance_threshold instead
    
    // Colors (BGR format for OpenCV)
    cv::Scalar trajectory_color = cv::Scalar(255, 255, 0);      // Cyan
    cv::Scalar verified_point_color = cv::Scalar(0, 255, 0);    // Green
    cv::Scalar predicted_point_color = cv::Scalar(0, 255, 255); // Yellow
    cv::Scalar search_radius_color = cv::Scalar(255, 0, 255);   // Magenta
    cv::Scalar max_distance_color = cv::Scalar(0, 0, 255);      // Red
    cv::Scalar throw_distance_color = cv::Scalar(0, 165, 255);  // Orange (BGR)
    cv::Scalar catch_distance_color = cv::Scalar(0, 255, 0);    // Green (BGR)
    cv::Scalar hand_velocity_zone_color = cv::Scalar(255, 0, 255); // Purple/Magenta (BGR)
    
    // Sizes
    int trajectory_thickness = 2;
    int point_radius = 5;
    float trajectory_point_spacing = 0.05f;  // 5cm between drawn points
    
    TrajectoryVisualizationSettings() = default;
};


class SimpleBallTracker : public IBallTracker {
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
    
    // Color calibration (override from IBallTracker)
    bool calibrateColor(const std::string& color_name,
                       cv::Point click_point,
                       std::string& error_message) override;
    
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
    * @param balls_override Optional override for balls data (for recordings)
    */
   void drawHandThresholds(cv::Mat& frame, const std::vector<SimpleHand>& hands, const CameraIntrinsics& intrinsics, const std::vector<SimpleBall>* balls_override = nullptr) override;
   
   const TrackingSettings& getTrackingSettings() const { return tracking_settings_; }
    TrackingSettings& getTrackingSettings() override { return tracking_settings_; }
    void setTrackingSettings(const TrackingSettings& settings) { tracking_settings_ = settings; }
    
    // Utility for projection
    static cv::Point2f project_3d_to_2d(const cv::Point3f& world_pos, const CameraIntrinsics& intrinsics);

private:
    // YOLO detection
    cv::Mat preprocess(const cv::Mat& frame, float& scale_x, float& scale_y);
    std::vector<Detection> runBallDetection(const cv::Mat& preprocessed,
                                           float scale_x,
                                           float scale_y,
                                           const cv::Mat& color_frame,
                                           const cv::Mat& depth_frame,
                                           const CameraIntrinsics& intrinsics);
    std::vector<SimpleHand> runPoseEstimation(const cv::Mat& preprocessed,
                                             float scale_x,
                                             float scale_y,
                                             const cv::Mat& color_frame,
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
    
    // Sequential throw detection helper (NEW: validates multi-frame detection sequence)
    bool validateThrowSequence(const std::vector<DetectionCandidate>& candidates,
                              const SimpleHand* hand);
    
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
    
    // Hand velocity tracking
    void updateHandVelocity(SimpleHand& hand, uint64_t current_timestamp);
    
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
    
    // Recording frame counter (set by Engine when recording is active)
    int recording_frame_number_ = -1;  // -1 means not recording
    
public:
    // Set the current recording frame number (called by Engine during recording)
    void setRecordingFrameNumber(int frame_num) { recording_frame_number_ = frame_num; }
    
private:
    
    // Model parameters
    int input_width_ = 640;
    int input_height_ = 640;
    float ball_confidence_threshold_ = 0.25f;      // Confidence threshold for 'ball' class (class_id=0)
    float ball_held_confidence_threshold_ = 0.25f; // Confidence threshold for 'ball_held' class (class_id=1)
    float nms_threshold_ = 0.5f;
    bool show_raw_yolo_detections_ = false;        // Toggle for showing raw YOLO detections in visualization
    bool enable_ball_detection_ = true;            // Toggle for enabling/disabling YOLO ball detection
    bool enable_pose_detection_ = true;            // Toggle for enabling/disabling YOLO pose detection
    int ball_processing_density_ = 50;             // Percentage of frames to process ball detection (10-100%)
    int ball_frame_counter_ = 0;                   // Frame counter for ball detection skipping
    int pose_processing_density_ = 50;             // Percentage of frames to process pose (10-100%)
    int pose_frame_counter_ = 0;                   // Frame counter for pose detection skipping
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
