#pragma once

#include <opencv2/opencv.hpp>
#include <openvino/openvino.hpp>
#include <vector>
#include <string>
#include <memory>
#include <chrono>
#include <unordered_set>
#include "json.hpp"
#include "GpuHsvConverter.hpp"
#include "IBallTracker.hpp"
#include "SimpleBallTracker.hpp"  // For shared type definitions (ColorProfile, BallState, etc.)

using json = nlohmann::json;

/**
 * @brief 3D Ball structure for New 3D Tracker
 * 
 * Contains all state information for a tracked ball including:
 * - Identity (ID, color)
 * - State (HELD/IN_FLIGHT, associated hand)
 * - Physics (Kalman filter, positions, velocities)
 * - Tracking quality metrics
 * - Visualization data
 * - Position history for pattern analysis
 */
struct New3DBall {
    // === IDENTITY ===
    long long id;                    // Unique permanent ID
    std::string color_name;          // "red", "green", "blue", etc.
    ColorProfile color_profile;      // Color matching profile
    
    // === STATE ===
    BallState state;                 // HELD or IN_FLIGHT (using existing BallState enum)
    int associated_hand_id;          // -1=none, 0=left, 1=right
    
    // === PHYSICS (Kalman Filter) ===
    cv::KalmanFilter kf;             // 6-state [x,y,z,vx,vy,vz]
    cv::Point3f last_known_position; // Official position from previous frame
    cv::Point3f predicted_position;  // Kalman prediction for this frame
    cv::Point3f last_detection_position; // Last ACTUAL detection (for color search region)
    
    // === TRACKING QUALITY ===
    int frames_since_seen;           // Counter for deletion
    int consecutive_frames_seen;     // Counter for confirmation
    bool color_locked;               // True after min_frames_for_color_lock
    
    // === VISUALIZATION ===
    cv::Point2f pixel_pos;           // 2D position for display
    cv::Rect_<float> bbox;           // Bounding box
    float yolo_confidence;           // Detection confidence
    float color_match_score;         // Color matching score
    std::string tracking_reason;     // Debug info
    
    // === HISTORY (optional for pattern analysis) ===
    std::vector<cv::Point3f> position_history;
    std::vector<uint64_t> timestamp_history;
    
    // Constructor with default values
    New3DBall() : id(-1), state(HELD), associated_hand_id(-1),
                  frames_since_seen(0), consecutive_frames_seen(0),
                  color_locked(false), yolo_confidence(0.0f),
                  color_match_score(0.0f),
                  last_detection_position(0.0f, 0.0f, 0.0f) {
        std::cout << "[New3DBall] Default constructor called" << std::endl;
    }
    
    // Destructor with logging
    ~New3DBall() {
        std::cout << "[New3DBall] Destructor called for ball ID=" << id << " color=" << color_name << std::endl;
    }
    
    // Copy constructor - properly handle Kalman filter
    New3DBall(const New3DBall& other)
        : id(other.id), color_name(other.color_name), color_profile(other.color_profile),
          state(other.state), associated_hand_id(other.associated_hand_id),
          last_known_position(other.last_known_position), predicted_position(other.predicted_position),
          last_detection_position(other.last_detection_position),
          frames_since_seen(other.frames_since_seen), consecutive_frames_seen(other.consecutive_frames_seen),
          color_locked(other.color_locked), pixel_pos(other.pixel_pos), bbox(other.bbox),
          yolo_confidence(other.yolo_confidence), color_match_score(other.color_match_score),
          tracking_reason(other.tracking_reason), position_history(other.position_history),
          timestamp_history(other.timestamp_history) {
        std::cout << "[New3DBall] Copy constructor called: copying ball ID=" << other.id
                  << " color=" << other.color_name << std::endl;
        
        // Deep copy the Kalman filter - must initialize first, then copy
        std::cout << "[New3DBall] Initializing new Kalman filter..." << std::endl;
        kf = cv::KalmanFilter(6, 3, 0);  // Initialize with same dimensions
        std::cout << "[New3DBall] Kalman filter initialized, now copying matrices..." << std::endl;
        
        // Now copy all matrices
        if (!other.kf.statePre.empty()) other.kf.statePre.copyTo(kf.statePre);
        if (!other.kf.statePost.empty()) other.kf.statePost.copyTo(kf.statePost);
        if (!other.kf.transitionMatrix.empty()) other.kf.transitionMatrix.copyTo(kf.transitionMatrix);
        if (!other.kf.controlMatrix.empty()) other.kf.controlMatrix.copyTo(kf.controlMatrix);
        if (!other.kf.measurementMatrix.empty()) other.kf.measurementMatrix.copyTo(kf.measurementMatrix);
        if (!other.kf.processNoiseCov.empty()) other.kf.processNoiseCov.copyTo(kf.processNoiseCov);
        if (!other.kf.measurementNoiseCov.empty()) other.kf.measurementNoiseCov.copyTo(kf.measurementNoiseCov);
        if (!other.kf.errorCovPre.empty()) other.kf.errorCovPre.copyTo(kf.errorCovPre);
        if (!other.kf.gain.empty()) other.kf.gain.copyTo(kf.gain);
        if (!other.kf.errorCovPost.empty()) other.kf.errorCovPost.copyTo(kf.errorCovPost);
        
        std::cout << "[New3DBall] Copy constructor completed for ball ID=" << id << std::endl;
    }
    
    // Move constructor - transfer ownership without copying
    New3DBall(New3DBall&& other) noexcept
        : id(other.id), color_name(std::move(other.color_name)), color_profile(other.color_profile),
          state(other.state), associated_hand_id(other.associated_hand_id),
          kf(std::move(other.kf)),  // Move the Kalman filter
          last_known_position(other.last_known_position), predicted_position(other.predicted_position),
          last_detection_position(other.last_detection_position),
          frames_since_seen(other.frames_since_seen), consecutive_frames_seen(other.consecutive_frames_seen),
          color_locked(other.color_locked), pixel_pos(other.pixel_pos), bbox(other.bbox),
          yolo_confidence(other.yolo_confidence), color_match_score(other.color_match_score),
          tracking_reason(std::move(other.tracking_reason)),
          position_history(std::move(other.position_history)),
          timestamp_history(std::move(other.timestamp_history)) {
        std::cout << "[New3DBall] Move constructor called for ball ID=" << id << " color=" << color_name << std::endl;
    }
    
    // Assignment operator - properly handle Kalman filter
    New3DBall& operator=(const New3DBall& other) {
        if (this != &other) {
            id = other.id;
            color_name = other.color_name;
            color_profile = other.color_profile;
            state = other.state;
            associated_hand_id = other.associated_hand_id;
            last_known_position = other.last_known_position;
            predicted_position = other.predicted_position;
            last_detection_position = other.last_detection_position;
            frames_since_seen = other.frames_since_seen;
            consecutive_frames_seen = other.consecutive_frames_seen;
            color_locked = other.color_locked;
            pixel_pos = other.pixel_pos;
            bbox = other.bbox;
            yolo_confidence = other.yolo_confidence;
            color_match_score = other.color_match_score;
            tracking_reason = other.tracking_reason;
            position_history = other.position_history;
            timestamp_history = other.timestamp_history;
            
            // Deep copy the Kalman filter - reinitialize first
            kf = cv::KalmanFilter(6, 3, 0);  // Reinitialize with same dimensions
            
            // Now copy all matrices
            if (!other.kf.statePre.empty()) other.kf.statePre.copyTo(kf.statePre);
            if (!other.kf.statePost.empty()) other.kf.statePost.copyTo(kf.statePost);
            if (!other.kf.transitionMatrix.empty()) other.kf.transitionMatrix.copyTo(kf.transitionMatrix);
            if (!other.kf.controlMatrix.empty()) other.kf.controlMatrix.copyTo(kf.controlMatrix);
            if (!other.kf.measurementMatrix.empty()) other.kf.measurementMatrix.copyTo(kf.measurementMatrix);
            if (!other.kf.processNoiseCov.empty()) other.kf.processNoiseCov.copyTo(kf.processNoiseCov);
            if (!other.kf.measurementNoiseCov.empty()) other.kf.measurementNoiseCov.copyTo(kf.measurementNoiseCov);
            if (!other.kf.errorCovPre.empty()) other.kf.errorCovPre.copyTo(kf.errorCovPre);
            if (!other.kf.gain.empty()) other.kf.gain.copyTo(kf.gain);
            if (!other.kf.errorCovPost.empty()) other.kf.errorCovPost.copyTo(kf.errorCovPost);
        }
        return *this;
    }
};

/**
 * @brief Settings structure for New 3D Tracker
 * 
 * Contains all configurable parameters for the tracker including:
 * - Geometry & distance thresholds
 * - Physics parameters (gravity, velocity thresholds)
 * - Tracking logic (frame counts, confirmation thresholds)
 * - Color tracking settings
 * - YOLO integration parameters
 * - Hand velocity settings
 * - Visualization options
 */
struct New3DTrackerSettings {
    // === GEOMETRY & DISTANCE (meters) ===
    float held_radius_m = 0.12f;                    // 12cm radius for "held" detection
    float held_circle_offset_cm = 5.0f;             // Offset distance from wrist towards hand center (cm)
    float association_max_distance_m = 0.50f;       // Max distance for detection matching
    float color_mismatch_penalty_m = 1.0f;          // Distance penalty for color mismatch (meters)
    
    // === PHYSICS & DYNAMICS ===
    float throw_velocity_threshold_mps = 0.50f;     // Min relative velocity for throw
    cv::Point3f gravity_mps2 = {0.0f, -9.81f, 0.0f}; // Gravity vector (Y-down in camera space)
    
    // === TRACKING LOGIC (frames) ===
    // NOTE: max_frames_unseen removed - balls are now persistent and never deleted
    int min_frames_for_new_track = 3;               // Confirm new track after 3 frames
    int min_frames_for_color_lock = 5;              // Lock color after 5 frames
    
    // === COLOR TRACKING ===
    bool use_color_tracking = true;                 // Enable color-based identification
    float color_match_threshold = 0.50f;            // Min color match score
    int color_sample_radius = 1;                    // Pixel radius for color sampling
    int min_saturation_threshold = 50;              // Min saturation to include pixel in color sampling (0-255)
    
    // === YOLO INTEGRATION ===
    bool enable_ball_detection = true;              // Enable/disable YOLO ball detection
    bool enable_pose_estimation = true;             // Enable/disable YOLO pose estimation
    int ball_processing_density = 50;               // Percentage of frames to process ball detection (10-100%)
    int pose_processing_density = 50;               // Percentage of frames to process pose (10-100%)
    float ball_confidence_threshold = 0.25f;        // Min confidence for 'ball' class
    float ball_held_confidence_threshold = 0.25f;   // Min confidence for 'ball_held' class
    bool ignore_class = false;                      // Treat ball/ball_held same
    
    // === DEPTH BLOB DETECTION (alternative to YOLO) ===
    bool enable_depth_blob_detection = false;       // Enable depth-based blob detection
    float depth_blob_min_distance_m = 0.30f;        // Min depth distance (meters)
    float depth_blob_max_distance_m = 1.50f;        // Max depth distance (meters)
    int depth_blob_min_area_px = 50;                // Min blob physical surface area (cm²) - DEPTH-AWARE
    int depth_blob_max_area_px = 2000;              // Max blob physical surface area (cm²) - DEPTH-AWARE
    float depth_blob_min_circularity = 0.65f;       // Min circularity (0.0-1.0, 1.0=perfect circle)
    int depth_blob_min_brightness = 0;              // Min average brightness (0-255, for LED balls)
    int depth_blob_max_whiteness = 255;             // Max whiteness for color sampling (0-255, filters bright pixels)
    bool show_depth_filtered_pixels = true;         // Show filtered depth pixels in visualization
    
    // === HAND VELOCITY (for throw prediction) ===
    bool hand_velocity_enabled = true;              // Enable velocity-based throw detection
    float hand_velocity_threshold = 1.0f;           // Min hand speed (m/s) for enhanced detection
    
    // === VISUALIZATION ===
    bool show_kalman_prediction = true;             // Show predicted position
    bool show_held_radius = true;                   // Show held detection radius
    bool show_association_lines = true;             // Show detection-to-track associations
    bool show_depth_globs = true;                   // Show depth glob detections
    bool show_color_search_region = true;           // Show color search region circles
};

/**
 * @brief Pose3D structure for storing previous frame hand positions
 * 
 * Used to calculate hand velocities between frames for throw detection.
 * Stores 3D wrist positions and validity flags for both hands.
 */
struct Pose3D {
    cv::Point3f left_wrist_pos;
    cv::Point3f right_wrist_pos;
    bool is_left_wrist_valid;
    bool is_right_wrist_valid;
    
    Pose3D() : is_left_wrist_valid(false), is_right_wrist_valid(false) {}
};

/**
 * @brief New 3D Tracker - Physics-Based Kalman Filter Tracker
 * 
 * This tracker implements a clean state machine with physics-based prediction:
 * - HELD State: Ball position locked to hand, tracks hand movement
 * - IN_FLIGHT State: Ball follows Kalman-predicted trajectory with gravity
 * - State Transitions: Based on distance thresholds and hand velocity
 * - Association: Hungarian algorithm for detection-to-track matching
 * - Robustness: Multi-frame confirmation for state changes
 * 
 * The tracker uses a 6-state Kalman filter [x,y,z,vx,vy,vz] with gravity
 * applied to the velocity components for realistic trajectory prediction.
 * 
 * Main update loop follows these steps:
 * 1. PREDICTION: Predict all ball positions using Kalman filter
 * 2. ASSOCIATION: Match detections to tracks using Hungarian algorithm
 * 3. UPDATE MATCHED: Update matched balls with new measurements
 * 4. HANDLE UNMATCHED BALLS: Increment unseen counters, delete old tracks
 * 5. HANDLE UNMATCHED DETECTIONS: Create new tracks for unmatched detections
 * 6. FINALIZE: Update final positions and prepare for next frame
 */
class New3DTracker : public IBallTracker {
public:
    /**
     * @brief Constructor
     * @param ball_model_path Path to YOLO ball detection model
     * @param pose_model_path Path to pose estimation model
     * @param device_name OpenVINO device (CPU, GPU, NPU, etc.)
     * @param settings_file Path to settings JSON file
     */
    New3DTracker(const std::string& ball_model_path,
                 const std::string& pose_model_path,
                 const std::string& device_name,
                 const std::string& settings_file = "new_3d_settings.json");
    
    ~New3DTracker() = default;
    
    // === MAIN UPDATE FUNCTION ===
    /**
     * @brief Main tracking update function
     * 
     * Processes a new frame and updates all tracked balls.
     * Returns tracked balls and any events (throws/catches) that occurred.
     * 
     * @param color_frame RGB color image
     * @param depth_frame Depth image (aligned to color)
     * @param intrinsics Camera intrinsic parameters
     * @return Pair of tracked balls and ball events
     */
    std::pair<std::vector<New3DBall>, std::vector<BallEvent>> updateNew3D(
        const cv::Mat& color_frame,
        const cv::Mat& depth_frame,
        const CameraIntrinsics& intrinsics
    );
    
    // === IBallTracker INTERFACE IMPLEMENTATION ===
    std::pair<std::vector<SimpleBall>, std::vector<BallEvent>> update(
        const cv::Mat& color_image,
        const cv::Mat& depth_image,
        const CameraIntrinsics& intrinsics
    ) override;
    
    const std::vector<SimpleHand>& getHands() const override { return hands_; }
    const std::vector<Detection>& getLastRawDetections() const override { return last_raw_detections_; }
    TrackingSettings& getTrackingSettings() override;
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
    
    // === SETTINGS MANAGEMENT ===
    bool loadSettings();
    void saveSettings();
    
    /**
     * @brief Reload color profiles and reinitialize persistent balls
     *
     * Called when color profiles are updated via UI. Reloads the color
     * profiles from the settings file and reinitializes the persistent
     * ball roster to match the new enabled/disabled states.
     */
    void reloadColorProfiles();
    
    // === PERSISTENT BALL INITIALIZATION ===
    /**
     * @brief Initialize one persistent ball per enabled color profile
     *
     * Creates permanent ball objects that are never deleted. Each enabled
     * color profile gets exactly one ball that persists for the entire
     * tracking session. When detections come in, they are matched to these
     * persistent balls by color only.
     */
    void initializePersistentBalls();
    
    // === GETTERS (New3D-specific) ===
    const std::vector<New3DBall>& getBalls() const { return tracked_balls_; }
    New3DTrackerSettings& getNew3DSettings() { return settings_; }
    std::vector<ColorProfile>& getColorProfilesMutable() { return color_profiles_; }
    
    // === EXCLUSION ZONES ===
    /**
     * @brief Set exclusion zones for filtering out false positive detections
     * @param zones Vector of rectangles defining areas to exclude from detection
     */
    void setExclusionZones(const std::vector<cv::Rect>& zones);
    
    /**
     * @brief Get current exclusion zones
     * @return Vector of exclusion zone rectangles
     */
    const std::vector<cv::Rect>& getExclusionZones() const { return exclusion_zones_; }

private:
    // ========================================================================
    // STEP 1: PREDICTION
    // ========================================================================
    
    /**
     * @brief Predict all ball positions for current frame
     * @param dt Time delta since last frame (seconds)
     */
    void predictAllBalls(float dt);
    
    /**
     * @brief Predict position of a held ball (locked to hand)
     * @param ball Ball to predict
     * @param hand Hand holding the ball
     * @param dt Time delta
     */
    void predictHeldBall(New3DBall& ball, const SimpleHand& hand, float dt);
    
    /**
     * @brief Predict position of an in-flight ball (Kalman + gravity)
     * @param ball Ball to predict
     * @param dt Time delta
     */
    void predictInFlightBall(New3DBall& ball, float dt);
    
    // ========================================================================
    // STEP 2: ASSOCIATION
    // ========================================================================
    
    /**
     * @brief Match pair structure for association
     */
    struct MatchPair {
        New3DBall* ball;
        const Detection* detection;
        float distance;
    };
    
    /**
     * @brief Association result structure
     */
    struct AssociationResult {
        std::vector<MatchPair> matched_pairs;
        std::vector<New3DBall*> unmatched_balls;
        std::vector<const Detection*> unmatched_detections;
    };
    
    /**
     * @brief Associate detections to tracked balls using color-aware cost
     * @param balls Current tracked balls
     * @param detections New detections from YOLO
     * @param max_distance Maximum distance for valid association
     * @param color_frame Color image for color determination
     * @return Association result with matched pairs and unmatched items
     */
    AssociationResult associateDetections(
        std::vector<New3DBall>& balls,
        const std::vector<Detection>& detections,
        float max_distance,
        const cv::Mat& color_frame
    );
    
    // ========================================================================
    // STEP 3: UPDATE MATCHED
    // ========================================================================
    
    /**
     * @brief Update all matched balls with new measurements
     * @param matches Matched detection-ball pairs
     * @param hands Current hand detections
     * @param previous_pose Previous frame hand positions
     * @param dt Time delta
     * @param events Output vector for ball events
     */
    void updateMatchedBalls(
        const std::vector<MatchPair>& matches,
        const std::vector<SimpleHand>& hands,
        const Pose3D& previous_pose,
        float dt,
        std::vector<BallEvent>& events
    );
    
    /**
     * @brief Handle update for a ball in HELD state
     * @param ball Ball to update
     * @param detection New detection
     * @param current_hands Current hand detections
     * @param previous_pose Previous frame hand positions
     * @param dt Time delta
     * @param events Output vector for ball events
     */
    void handleHeldStateUpdate(
        New3DBall& ball,
        const Detection& detection,
        const std::vector<SimpleHand>& current_hands,
        const Pose3D& previous_pose,
        float dt,
        std::vector<BallEvent>& events
    );
    
    /**
     * @brief Handle update for a ball in IN_FLIGHT state
     * @param ball Ball to update
     * @param detection New detection
     * @param current_hands Current hand detections
     * @param events Output vector for ball events
     */
    void handleInFlightStateUpdate(
        New3DBall& ball,
        const Detection& detection,
        const std::vector<SimpleHand>& current_hands,
        std::vector<BallEvent>& events
    );
    
    // ========================================================================
    // STEP 4: HANDLE UNMATCHED BALLS
    // ========================================================================
    
    /**
     * @brief Handle balls that were not matched to any detection
     * @param unmatched_balls Balls without matching detections
     */
    void handleUnmatchedBalls(
        const std::vector<New3DBall*>& unmatched_balls
    );

    /**
     * @brief For IN_FLIGHT balls that were not seen, check if they are near a hand.
     *
     * This handles cases where a held ball is occluded and the hand is lost
     * temporarily. When the hand reappears, this function re-establishes the
     * HELD state without needing a visual detection of the ball.
     *
     * @param unmatched_balls A list of balls that were not matched to any detection.
     *                        This list will be modified in-place; re-acquired balls
     *                        will be removed from the list.
     * @param hands Current hand detections.
     * @param events Output vector for ball events (e.g., CATCH).
     */
    void reacquireHeldBallsByProximity(
        std::vector<New3DBall*>& unmatched_balls,
        const std::vector<SimpleHand>& hands,
        std::vector<BallEvent>& events
    );
    
    // ========================================================================
    // STEP 5: HANDLE UNMATCHED DETECTIONS
    // ========================================================================
    
    /**
     * @brief Create new tracks for unmatched detections (with re-acquisition logic)
     * @param unmatched_detections Detections without matching balls
     * @param unmatched_balls Balls without matching detections (for re-acquisition)
     * @param color_frame Color image for color sampling
     */
    void createNewTracks(
        std::vector<const Detection*>& unmatched_detections,
        std::vector<New3DBall*>& unmatched_balls,
        const cv::Mat& color_frame
    );
    
    // ========================================================================
    // STEP 6: FINALIZE
    // ========================================================================
    
    /**
     * @brief Finalize ball positions and prepare for next frame
     * @param hands Current hand detections
     * @param intrinsics Camera intrinsics for pixel position projection
     */
    void finalizeBallPositions(const std::vector<SimpleHand>& hands,
                              const CameraIntrinsics& intrinsics);
    
    // ========================================================================
    // HELPER METHODS
    // ========================================================================
    
    /**
     * @brief Create a new Kalman filter initialized at a position
     * @param initial_pos Initial 3D position
     * @return Configured Kalman filter
     */
    cv::KalmanFilter createKalmanFilter(const cv::Point3f& initial_pos);
    
    /**
     * @brief Calculate hand velocity from previous frame
     * @param hand Current hand
     * @param previous_pose Previous frame pose
     * @param dt Time delta
     * @return Hand velocity vector (m/s)
     */
    cv::Point3f calculateHandVelocity(const SimpleHand& hand, const Pose3D& previous_pose, float dt);
    
    // REMOVED: isHandAvailable() - hands can now hold multiple balls simultaneously
    
    /**
     * @brief Match detection color to a color profile
     * @param det Detection to match
     * @param profile Color profile to match against
     * @param color_frame Color image for sampling
     * @return Color match score [0-1]
     */
    float matchColor(const Detection& det, const ColorProfile& profile, const cv::Mat& color_frame);
    
    /**
     * @brief Determine color of a detection
     * @param det Detection to analyze
     * @param color_frame Color image for sampling
     * @return Color name (e.g., "red", "blue")
     */
    std::string determineColor(const Detection& det, const cv::Mat& color_frame);
    
    /**
     * @brief Sample detected BGR color at detection center using configured sampling parameters
     * @param det Detection to sample color from
     * @param color_frame Color image to sample from
     * @return BGR color vector (median-filtered, saturation-thresholded)
     */
    cv::Vec3b sampleDetectedColor(const Detection& det, const cv::Mat& color_frame);
    
    /**
     * @brief Create Pose3D from current hands
     * @param hands Current hand detections
     * @return Pose3D structure
     */
    Pose3D createPose3D(const std::vector<SimpleHand>& hands);
    
    // ========================================================================
    // YOLO DETECTION
    // ========================================================================
    
    /**
     * @brief Preprocess frame for YOLO inference
     * @param frame Input frame
     * @param scale_x Output X scale factor
     * @param scale_y Output Y scale factor
     * @return Preprocessed frame
     */
    cv::Mat preprocess(const cv::Mat& frame, float& scale_x, float& scale_y);
    
    /**
     * @brief Run ball detection with YOLO
     * @param preprocessed Preprocessed frame
     * @param scale_x X scale factor
     * @param scale_y Y scale factor
     * @param color_frame Original color frame
     * @param depth_frame Depth frame
     * @param intrinsics Camera intrinsics
     * @return Vector of ball detections
     */
    std::vector<Detection> runBallDetection(
        const cv::Mat& preprocessed,
        float scale_x, float scale_y,
        const cv::Mat& color_frame,
        const cv::Mat& depth_frame,
        const CameraIntrinsics& intrinsics
    );
    
    /**
     * @brief Run depth-based blob detection (alternative to YOLO)
     * @param color_frame Original color frame
     * @param depth_frame Depth frame
     * @param intrinsics Camera intrinsics
     * @return Vector of ball detections from depth blobs
     */
    std::vector<Detection> runDepthBlobDetection(
        const cv::Mat& color_frame,
        const cv::Mat& depth_frame,
        const CameraIntrinsics& intrinsics
    );
    
    /**
     * @brief Run pose estimation with YOLO
     * @param preprocessed Preprocessed frame
     * @param scale_x X scale factor
     * @param scale_y Y scale factor
     * @param color_frame Original color frame
     * @param depth_frame Depth frame
     * @param intrinsics Camera intrinsics
     * @return Vector of detected hands
     */
    std::vector<SimpleHand> runPoseEstimation(
        const cv::Mat& preprocessed,
        float scale_x, float scale_y,
        const cv::Mat& color_frame,
        const cv::Mat& depth_frame,
        const CameraIntrinsics& intrinsics
    );
    
    // ========================================================================
    // VISUALIZATION
    // ========================================================================
    
    /**
     * @brief Draw a tracked ball on the frame
     * @param frame Image to draw on
     * @param ball Ball to draw
     * @param intrinsics Camera intrinsics for projection
     */
    void drawBall(cv::Mat& frame, const New3DBall& ball, const CameraIntrinsics& intrinsics);
    
    /**
     * @brief Draw association lines between detections and tracks
     * @param frame Image to draw on
     * @param matches Matched pairs to visualize
     * @param intrinsics Camera intrinsics for projection
     */
    void drawAssociations(cv::Mat& frame, const std::vector<MatchPair>& matches,
                         const CameraIntrinsics& intrinsics);
    
    // ========================================================================
    // UTILITY
    // ========================================================================
    
    /**
     * @brief Get depth value at a pixel location
     * @param depth_frame Depth image
     * @param point Pixel coordinates
     * @return Depth value in meters
     */
    float getDepthAtPoint(const cv::Mat& depth_frame, const cv::Point2f& point);
    
    /**
     * @brief Deproject 2D pixel + depth to 3D world coordinates
     * @param pixel 2D pixel coordinates
     * @param depth Depth value in meters
     * @param intrinsics Camera intrinsics
     * @return 3D world position
     */
    cv::Point3f deprojectToWorld(const cv::Point2f& pixel, float depth,
                                const CameraIntrinsics& intrinsics);
    
    /**
     * @brief Project 3D world coordinates to 2D pixel
     * @param world_pos 3D world position
     * @param intrinsics Camera intrinsics
     * @return 2D pixel coordinates
     */
    static cv::Point2f project3DTo2D(const cv::Point3f& world_pos,
                                     const CameraIntrinsics& intrinsics);
    
    /**
     * @brief Clamp 3D position to stay within camera field of view
     * @param world_pos 3D world position to clamp
     * @param intrinsics Camera intrinsics
     * @param frame_width Frame width in pixels
     * @param frame_height Frame height in pixels
     * @return Clamped 3D position that projects within frame bounds
     */
    cv::Point3f clampToFieldOfView(const cv::Point3f& world_pos,
                                    const CameraIntrinsics& intrinsics,
                                    int frame_width,
                                    int frame_height);
    
    /**
     * @brief Convert New3DBall to SimpleBall for interface compatibility
     * @param new_ball New3DBall to convert
     * @return SimpleBall structure
     */
    SimpleBall convertToSimpleBall(const New3DBall& new_ball);
    
    // ========================================================================
    // STATE
    // ========================================================================
    
    std::vector<New3DBall> tracked_balls_;          // Currently tracked balls
    std::vector<SimpleHand> hands_;                 // Currently detected hands
    Pose3D previous_frame_pose_;                    // Previous frame hand positions
    std::vector<Detection> last_raw_detections_;    // Last raw YOLO detections
    std::vector<ColorProfile> color_profiles_;      // Color profiles for identification
    std::unordered_set<std::string> active_track_colors_;  // Colors currently being tracked
    long long next_track_id_ = 0;                   // Next available track ID
    int recording_frame_number_ = -1;               // Current recording frame (-1 if not recording)
    cv::Mat current_color_image_;                   // Current frame color image for color sampling
    cv::Mat depth_filtered_mask_;                   // Mask of filtered depth pixels for visualization
    std::vector<cv::Rect> exclusion_zones_;         // Exclusion zones for filtering false positives
    
public:
    // Get the depth filtered mask for visualization
    cv::Mat getDepthFilteredMask() const { return depth_filtered_mask_; }
    
    // Get current tracker settings
    const New3DTrackerSettings& getSettings() const { return settings_; }
    
private:
    // ========================================================================
    // SETTINGS
    // ========================================================================
    
    New3DTrackerSettings settings_;                 // Tracker settings
    std::string settings_file_;                     // Settings file path
    
    // ========================================================================
    // OPENVINO
    // ========================================================================
    
    ov::Core core_;                                 // OpenVINO core
    ov::CompiledModel ball_model_;                  // Compiled ball detection model
    ov::InferRequest ball_infer_;                   // Ball inference request
    ov::CompiledModel pose_model_;                  // Compiled pose estimation model
    ov::InferRequest pose_infer_;                   // Pose inference request
    
    // ========================================================================
    // GPU ACCELERATION
    // ========================================================================
    
    std::unique_ptr<GpuHsvConverter> gpu_hsv_converter_;  // GPU HSV conversion
    
    // ========================================================================
    // TIMING & FRAME TRACKING
    // ========================================================================
    
    std::chrono::steady_clock::time_point last_update_time_;  // Last update timestamp
    int frame_counter_ = 0;                                   // Frame counter for debug logging
    int ball_frame_counter_ = 0;                              // Frame counter for ball detection skipping
    int pose_frame_counter_ = 0;                              // Frame counter for pose detection skipping
    
    // ========================================================================
    // MODEL PARAMETERS
    // ========================================================================
    
    int input_width_ = 640;                         // Model input width
    int input_height_ = 640;                        // Model input height
    float nms_threshold_ = 0.5f;                    // NMS threshold
    const int num_classes_ = 2;                     // Number of classes (ball, ball_held)
};