#pragma once

#include <opencv2/opencv.hpp>
#include <vector>
#include <string>
#include <utility>

// Forward declarations for types used in SimpleBallTracker
struct SimpleBall;
struct BallEvent;
struct SimpleHand;
struct Detection;
struct TrackingSettings;
struct ColorProfile;
struct CameraIntrinsics;

/**
 * @brief Abstract interface for ball tracking systems
 * 
 * This interface allows multiple tracking implementations to be used
 * interchangeably in the Engine. Implementations can use different
 * approaches (depth-based 3D, 2D-only, etc.) while maintaining
 * a consistent API.
 */
class IBallTracker {
public:
    virtual ~IBallTracker() = default;
    
    /**
     * @brief Update tracking with new frame data
     * @param color_image RGB color frame from camera
     * @param depth_image Depth frame from camera (may be ignored by some implementations)
     * @param intrinsics Camera intrinsic parameters
     * @return Pair of tracked balls and ball events (throws/catches)
     */
    virtual std::pair<std::vector<SimpleBall>, std::vector<BallEvent>> 
        update(const cv::Mat& color_image, const cv::Mat& depth_image, 
               const CameraIntrinsics& intrinsics) = 0;
    
    /**
     * @brief Get currently tracked hands
     * @return Vector of tracked hand objects
     */
    virtual const std::vector<SimpleHand>& getHands() const = 0;
    
    /**
     * @brief Get raw YOLO detections from last frame
     * @return Vector of raw detection objects
     */
    virtual const std::vector<Detection>& getLastRawDetections() const = 0;
    
    /**
     * @brief Get current tracking settings (mutable reference for UDP updates)
     * @return Reference to tracking settings
     */
    virtual TrackingSettings& getTrackingSettings() = 0;
    
    /**
     * @brief Get color profiles for ball identification
     * @return Vector of color profiles
     */
    virtual const std::vector<ColorProfile>& getColorProfiles() const = 0;
    
    /**
     * @brief Calibrate a ball color from a clicked point
     * @param color_name Name of the color to calibrate (e.g., "red", "blue")
     * @param click_point Pixel coordinates where user clicked
     * @param error_message Output parameter for error details
     * @return True if calibration succeeded, false otherwise
     */
    virtual bool calibrateColor(const std::string& color_name, 
                                cv::Point click_point, 
                                std::string& error_message) = 0;
    
    /**
     * @brief Set current recording frame number for logging
     * @param frame_num Frame number (-1 if not recording)
     */
    virtual void setRecordingFrameNumber(int frame_num) = 0;
    
    /**
     * @brief Draw hand distance threshold circles for visualization
     * @param frame Image to draw on
     * @param hands Vector of hands to draw thresholds for
     * @param intrinsics Camera intrinsics for projection
     */
    virtual void drawHandThresholds(cv::Mat& frame, 
                                    const std::vector<SimpleHand>& hands,
                                    const CameraIntrinsics& intrinsics) = 0;
    
    /**
     * @brief Evaluate override criteria for detections (for recording visualization)
     * @param detections Vector of detections to evaluate
     * @param color_image Color frame for color sampling
     */
    virtual void evaluateOverrideCriteria(std::vector<Detection>& detections,
                                         const cv::Mat& color_image) = 0;
    
    /**
     * @brief Update a tracking setting
     * @param key Setting name
     * @param value Setting value as string
     * @return True if setting was recognized and updated, false otherwise
     */
    virtual bool updateSetting(const std::string& key, const std::string& value) = 0;
};