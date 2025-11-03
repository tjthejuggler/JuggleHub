#pragma once

#include <opencv2/opencv.hpp>
#include <openvino/openvino.hpp>
#include <vector>
#include <string>
#include "SimpleBallTracker.hpp"  // Include SimpleBallTracker to get shared struct definitions

/**
 * @file Simple2DBallTracker.hpp
 * @brief Simplified 2D-only ball tracking system
 *
 * This tracker shows raw YOLO detections without depth processing, trajectory prediction,
 * or complex state machines. It's designed for debugging and visualization of raw detection
 * data before applying sophisticated tracking algorithms.
 *
 * Key simplifications compared to SimpleBallTracker:
 * - No depth processing (depth_image parameter ignored)
 * - No trajectory prediction or physics simulation
 * - No state machine (HELD/IN_FLIGHT states)
 * - No color matching or calibration
 * - Simple nearest-neighbor tracking by ID
 * - Minimal hand tracking (just for visualization)
 *
 * @date 2025-10-13
 */

// Note: CameraIntrinsics, ColorProfile, Detection, SimpleHand, SimpleBall,
// BallEvent, and TrackingSettings are now defined in SimpleBallTracker.hpp

/**
 * @brief Simple 2D ball representation
 * 
 * Minimal ball structure for 2D-only tracking. Contains only essential
 * information from YOLO detections without depth or trajectory data.
 */
struct Simple2DBall {
    int id;                          // Unique ball ID (assigned by tracker)
    cv::Rect_<float> bbox;           // Bounding box from YOLO
    cv::Point2f center;              // 2D center point (bbox center)
    float confidence;                // YOLO detection confidence
    int class_id;                    // YOLO class ID (0=ball, 1=ball_held)
    int frames_since_seen;           // Frames since last YOLO detection
    
    Simple2DBall() : id(-1), confidence(0.0f), class_id(0), frames_since_seen(0) {}
};

/**
 * @brief Simple 2D Ball Tracker
 * 
 * Implements IBallTracker interface with minimal 2D-only tracking.
 * Shows raw YOLO detections without depth processing or trajectory prediction.
 * 
 * This tracker is useful for:
 * - Debugging YOLO detection quality
 * - Visualizing raw detection data
 * - Testing without depth camera
 * - Baseline comparison for more complex trackers
 */
class Simple2DBallTracker : public IBallTracker {
public:
    /**
     * @brief Constructor
     * @param ball_model_path Path to YOLO ball detection model (.xml)
     * @param pose_model_path Path to YOLO pose estimation model (.xml)
     * @param device_name OpenVINO device (e.g., "CPU", "GPU")
     */
    Simple2DBallTracker(const std::string& ball_model_path,
                        const std::string& pose_model_path,
                        const std::string& device_name);
    
    ~Simple2DBallTracker() = default;
    
    // ========================================================================
    // IBallTracker Interface Implementation
    // ========================================================================
    
    /**
     * @brief Update tracking with new frame
     * 
     * Runs YOLO detection and simple nearest-neighbor tracking.
     * Depth image is ignored in this 2D-only implementation.
     * 
     * @param color_image RGB color frame from camera
     * @param depth_image Depth frame (IGNORED in 2D tracker)
     * @param intrinsics Camera intrinsics (used for projection only)
     * @return Pair of tracked balls and events (events always empty in 2D)
     */
    std::pair<std::vector<SimpleBall>, std::vector<BallEvent>> 
        update(const cv::Mat& color_image, const cv::Mat& depth_image, 
               const CameraIntrinsics& intrinsics) override;
    
    /**
     * @brief Get currently tracked hands
     * @return Vector of tracked hand objects
     */
    const std::vector<SimpleHand>& getHands() const override { return hands_; }
    
    /**
     * @brief Get raw YOLO detections from last frame
     * @return Vector of raw detection objects
     */
    const std::vector<Detection>& getLastRawDetections() const override { 
        return raw_detections_; 
    }
    
    /**
     * @brief Get tracking settings (mutable reference for UDP updates)
     * @return Reference to tracking settings
     */
    TrackingSettings& getTrackingSettings() override { return tracking_settings_; }
    
    /**
     * @brief Get color profiles (always empty in 2D tracker)
     * @return Empty vector (no color matching in 2D)
     */
    const std::vector<ColorProfile>& getColorProfiles() const override { 
        return empty_color_profiles_; 
    }
    
    /**
     * @brief Calibrate color (not supported in 2D tracker)
     * @return Always returns false (not supported)
     */
    bool calibrateColor(const std::string& color_name, 
                       cv::Point click_point, 
                       std::string& error_message) override {
        error_message = "Color calibration not supported in 2D tracker";
        return false;
    }
    
    /**
     * @brief Set recording frame number for logging
     * @param frame_num Frame number (-1 if not recording)
     */
    void setRecordingFrameNumber(int frame_num) override { 
        recording_frame_number_ = frame_num; 
    }
    
    /**
     * @brief Draw hand thresholds (empty in 2D tracker)
     * 
     * No thresholds to draw in 2D-only mode since we don't track
     * hand-ball proximity or state transitions.
     */
    void drawHandThresholds(cv::Mat& frame,
                           const std::vector<SimpleHand>& hands,
                           const CameraIntrinsics& intrinsics,
                           const std::vector<SimpleBall>* balls_override = nullptr) override {
        // No-op: no thresholds in 2D tracker
        (void)balls_override;  // Suppress unused parameter warning
    }
    
    /**
     * @brief Evaluate override criteria (empty in 2D tracker)
     * 
     * No override logic in 2D-only mode since we don't have
     * color matching or trajectory prediction.
     */
    void evaluateOverrideCriteria(std::vector<Detection>& detections,
                                  const cv::Mat& color_image) override {
        // No-op: no override logic in 2D tracker
    }
    
    /**
     * @brief Update a tracking setting
     * @param key Setting name
     * @param value Setting value
     * @return True if setting was recognized and updated
     */
    bool updateSetting(const std::string& key, const std::string& value);

private:
    // ========================================================================
    // YOLO Detection Methods
    // ========================================================================
    
    /**
     * @brief Preprocess image for YOLO inference
     * @param frame Input color frame
     * @param scale_x Output: X scaling factor
     * @param scale_y Output: Y scaling factor
     * @return Preprocessed image ready for YOLO
     */
    cv::Mat preprocess(const cv::Mat& frame, float& scale_x, float& scale_y);
    
    /**
     * @brief Run YOLO ball detection
     * @param preprocessed Preprocessed image (already resized and normalized)
     * @param scale_x X scaling factor for coordinate conversion
     * @param scale_y Y scaling factor for coordinate conversion
     * @return Vector of ball detections
     */
    std::vector<Detection> runBallDetection(const cv::Mat& preprocessed,
                                            float scale_x, float scale_y);
    
    /**
     * @brief Run YOLO pose estimation for hands
     * @param preprocessed Preprocessed image (already resized and normalized)
     * @param scale_x X scaling factor for coordinate conversion
     * @param scale_y Y scaling factor for coordinate conversion
     * @return Vector of detected hands
     */
    std::vector<SimpleHand> runPoseEstimation(const cv::Mat& preprocessed,
                                               float scale_x, float scale_y);
    
    // ========================================================================
    // Simple Tracking Methods
    // ========================================================================
    
    /**
     * @brief Find closest existing ball ID for a detection
     * 
     * Simple nearest-neighbor tracking by 2D distance.
     * 
     * @param detection New detection to match
     * @param max_distance Maximum distance for matching (pixels)
     * @return Ball ID if match found, -1 otherwise
     */
    int findClosestBallId(const Detection& detection, float max_distance = 100.0f);
    
    // ========================================================================
    // OpenVINO Models
    // ========================================================================
    
    ov::Core core_;                  // OpenVINO core
    ov::CompiledModel ball_model_;   // Compiled ball detection model
    ov::InferRequest ball_infer_;    // Ball detection inference request (async)
    ov::CompiledModel pose_model_;   // Compiled pose estimation model
    ov::InferRequest pose_infer_;    // Pose estimation inference request (async)
    
    // Async inference support
    bool use_async_inference_;       // Enable asynchronous inference for performance
    
    // ========================================================================
    // State Vectors
    // ========================================================================
    
    std::vector<Simple2DBall> balls_2d_;        // Tracked 2D balls
    std::vector<SimpleHand> hands_;             // Tracked hands
    std::vector<Detection> raw_detections_;     // Raw YOLO detections
    
    // ========================================================================
    // Tracking State
    // ========================================================================
    
    int next_ball_id_;                          // Counter for assigning ball IDs
    int recording_frame_number_;                // Current recording frame (-1 if not recording)
    CameraIntrinsics camera_intrinsics_;        // Camera intrinsics for 2D->3D conversion
    
    // ========================================================================
    // Settings
    // ========================================================================
    
    TrackingSettings tracking_settings_;        // Tracking configuration
    std::vector<ColorProfile> empty_color_profiles_;  // Empty vector for interface
    
    // ========================================================================
    // Model Parameters
    // ========================================================================
    
    int input_width_;                           // YOLO input width (640)
    int input_height_;                          // YOLO input height (640)
    float ball_confidence_threshold_;           // Confidence threshold for 'ball' class
    float ball_held_confidence_threshold_;      // Confidence threshold for 'ball_held' class
    float nms_threshold_;                       // Non-maximum suppression threshold
    bool enable_ball_detection_;                // Enable/disable YOLO ball detection
    bool enable_pose_detection_;                // Enable/disable YOLO pose detection
    int ball_processing_density_;               // Percentage of frames to process ball detection (10-100%)
    int ball_frame_counter_;                    // Frame counter for ball detection skipping
    int pose_processing_density_;               // Percentage of frames to process pose (10-100%)
    int pose_frame_counter_;                    // Frame counter for pose detection skipping
    
    // Constants
    static constexpr int NUM_CLASSES = 2;       // ball, ball_held
    static constexpr float MAX_TRACKING_DISTANCE = 100.0f;  // Max pixel distance for tracking
    static constexpr int MAX_FRAMES_MISSING = 30;  // Max frames before removing ball
};
