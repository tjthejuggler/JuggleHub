#pragma once

#include <opencv2/opencv.hpp>
#include <vector>
#include <string>
#include <memory>
#include <chrono>
#include "json.hpp"

using json = nlohmann::json;

namespace juggler {

/**
 * @brief Represents a single color calibration sample for a ball
 * 
 * Multiple samples can be taken in different lighting conditions to create
 * a robust color profile that works across various environments.
 */
struct ColorSample {
    cv::Scalar mean_hsv;           ///< Average HSV values from sample region
    cv::Scalar std_hsv;            ///< Standard deviation of HSV values
    cv::Scalar min_hsv;            ///< Minimum HSV bounds for this sample
    cv::Scalar max_hsv;            ///< Maximum HSV bounds for this sample
    float weight;                  ///< Confidence weight (0-1), higher = more reliable
    std::string lighting_condition; ///< Description: "bright", "dim", "mixed", etc.
    cv::Point2f sample_location;   ///< Where in the frame the sample was taken
    int64_t timestamp;             ///< When the sample was captured (milliseconds since epoch)
    
    ColorSample()
        : mean_hsv(0, 0, 0), std_hsv(0, 0, 0),
          min_hsv(0, 0, 0), max_hsv(180, 255, 255),
          weight(1.0f), lighting_condition("unknown"),
          sample_location(-1, -1), timestamp(0) {}
    
    // Serialization
    json toJson() const;
    static ColorSample fromJson(const json& j);
};

/**
 * @brief Represents a trackable ball with its color profile and metadata
 * 
 * An ActiveBall can have multiple color samples to handle lighting variations.
 * The aggregate HSV ranges are computed from all samples to create a robust
 * color profile.
 */
struct ActiveBall {
    std::string id;                          ///< Unique identifier (e.g., "ball_001")
    std::string display_name;                ///< User-friendly name (e.g., "Pink Ball #1")
    std::vector<ColorSample> color_samples;  ///< Multiple calibration samples
    cv::Scalar aggregate_min_hsv;            ///< Computed min HSV from all samples
    cv::Scalar aggregate_max_hsv;            ///< Computed max HSV from all samples
    cv::Scalar aggregate_min_hsv2;           ///< For wrap-around colors (red/pink)
    cv::Scalar aggregate_max_hsv2;           ///< For wrap-around colors (red/pink)
    bool is_active;                          ///< Currently being tracked?
    int logical_tracker_id;                  ///< Which tracker slot (0-N), -1 if not assigned
    float min_confidence_threshold;          ///< Minimum confidence to accept detection (0-1)
    float expected_diameter_cm;              ///< Expected ball diameter in centimeters
    int frames_tracked;                      ///< Statistics: total frames tracked
    int frames_lost;                         ///< Statistics: total frames lost
    int64_t created_timestamp;               ///< When ball was created
    int64_t last_seen_timestamp;             ///< When ball was last successfully tracked
    
    ActiveBall()
        : id(""), display_name(""),
          aggregate_min_hsv(0, 0, 0), aggregate_max_hsv(180, 255, 255),
          aggregate_min_hsv2(-1, -1, -1), aggregate_max_hsv2(-1, -1, -1),
          is_active(false), logical_tracker_id(-1),
          min_confidence_threshold(0.65f), expected_diameter_cm(7.0f),
          frames_tracked(0), frames_lost(0),
          created_timestamp(0), last_seen_timestamp(0) {}
    
    // Serialization
    json toJson() const;
    static ActiveBall fromJson(const json& j);
};

/**
 * @brief Manages the registry of all balls (registered and active)
 * 
 * The BallRegistry is the central component for ball management. It handles:
 * - Creating and deleting balls
 * - Activating/deactivating balls for tracking
 * - Adding/removing color samples
 * - Computing aggregate color ranges
 * - Persisting ball configurations to disk
 */
class BallRegistry {
public:
    BallRegistry();
    ~BallRegistry() = default;
    
    // ===== Ball Management =====
    
    /**
     * @brief Create a new ball with the given display name
     * @param display_name User-friendly name for the ball
     * @return Unique ID of the created ball
     */
    std::string createBall(const std::string& display_name);
    
    /**
     * @brief Delete a ball from the registry
     * @param ball_id Unique ID of the ball to delete
     * @return true if ball was deleted, false if not found
     */
    bool deleteBall(const std::string& ball_id);
    
    /**
     * @brief Activate a ball for tracking
     * @param ball_id Unique ID of the ball to activate
     * @return true if ball was activated, false if not found or max active reached
     */
    bool activateBall(const std::string& ball_id);
    
    /**
     * @brief Deactivate a ball (stop tracking it)
     * @param ball_id Unique ID of the ball to deactivate
     * @return true if ball was deactivated, false if not found
     */
    bool deactivateBall(const std::string& ball_id);
    
    // ===== Calibration =====
    
    /**
     * @brief Add a color sample to a ball's profile
     * @param ball_id Unique ID of the ball
     * @param hsv_frame HSV-converted frame
     * @param click_point Point where user clicked to sample
     * @param lighting_condition Description of lighting (e.g., "bright", "dim")
     * @param sample_radius Radius around click point to sample (default: 10 pixels)
     * @return true if sample was added successfully
     */
    bool addColorSample(const std::string& ball_id,
                       const cv::Mat& hsv_frame,
                       const cv::Point& click_point,
                       const std::string& lighting_condition = "unknown",
                       int sample_radius = 10);
    
    /**
     * @brief Remove a color sample from a ball's profile
     * @param ball_id Unique ID of the ball
     * @param sample_index Index of the sample to remove
     * @return true if sample was removed successfully
     */
    bool removeColorSample(const std::string& ball_id, int sample_index);
    
    /**
     * @brief Recompute aggregate HSV ranges from all samples
     * @param ball_id Unique ID of the ball
     */
    void recomputeAggregateRanges(const std::string& ball_id);
    
    // ===== Queries =====
    
    /**
     * @brief Get all currently active balls
     * @return Vector of pointers to active balls
     */
    std::vector<ActiveBall*> getActiveBalls();
    
    /**
     * @brief Get all currently active balls (const version)
     * @return Vector of const pointers to active balls
     */
    std::vector<const ActiveBall*> getActiveBalls() const;
    
    /**
     * @brief Get all registered balls (active and inactive)
     * @return Vector of pointers to all balls
     */
    std::vector<ActiveBall*> getAllBalls();
    
    /**
     * @brief Get all registered balls (const version)
     * @return Vector of const pointers to all balls
     */
    std::vector<const ActiveBall*> getAllBalls() const;
    
    /**
     * @brief Get a specific ball by ID
     * @param ball_id Unique ID of the ball
     * @return Pointer to ball, or nullptr if not found
     */
    ActiveBall* getBall(const std::string& ball_id);
    
    /**
     * @brief Get a specific ball by ID (const version)
     * @param ball_id Unique ID of the ball
     * @return Const pointer to ball, or nullptr if not found
     */
    const ActiveBall* getBall(const std::string& ball_id) const;
    
    /**
     * @brief Get ball by logical tracker ID
     * @param tracker_id Logical tracker ID (0-N)
     * @return Pointer to ball, or nullptr if not found
     */
    ActiveBall* getBallByTrackerId(int tracker_id);
    
    /**
     * @brief Get maximum number of balls that can be active simultaneously
     * @return Maximum active balls
     */
    int getMaxActiveBalls() const { return max_active_balls_; }
    
    /**
     * @brief Set maximum number of balls that can be active simultaneously
     * @param max Maximum active balls (1-10)
     */
    void setMaxActiveBalls(int max);
    
    /**
     * @brief Get number of currently active balls
     * @return Number of active balls
     */
    int getActiveCount() const;
    
    // ===== Persistence =====
    
    /**
     * @brief Save all balls to a JSON file
     * @param filepath Path to save file
     * @return true if save was successful
     */
    bool saveToFile(const std::string& filepath);
    
    /**
     * @brief Load balls from a JSON file
     * @param filepath Path to load file
     * @return true if load was successful
     */
    bool loadFromFile(const std::string& filepath);
    
    /**
     * @brief Export registry to JSON
     * @return JSON representation of all balls
     */
    json toJson() const;
    
    /**
     * @brief Import registry from JSON
     * @param j JSON representation
     */
    void fromJson(const json& j);
    
private:
    std::vector<ActiveBall> registered_balls_;  ///< All registered balls
    int max_active_balls_;                      ///< Maximum simultaneous active balls
    int next_ball_number_;                      ///< Counter for generating unique IDs
    
    /**
     * @brief Generate a unique ball ID
     * @return Unique ID string (e.g., "ball_001")
     */
    std::string generateBallId();
    
    /**
     * @brief Compute aggregate HSV range from multiple samples
     * @param samples Vector of color samples
     * @param min_hsv Output: minimum HSV values
     * @param max_hsv Output: maximum HSV values
     * @param min_hsv2 Output: minimum HSV values for wrap-around (or -1 if not needed)
     * @param max_hsv2 Output: maximum HSV values for wrap-around (or -1 if not needed)
     */
    void computeAggregateRange(const std::vector<ColorSample>& samples,
                              cv::Scalar& min_hsv, cv::Scalar& max_hsv,
                              cv::Scalar& min_hsv2, cv::Scalar& max_hsv2);
    
    /**
     * @brief Check if a hue value indicates a wrap-around color (red/pink)
     * @param samples Vector of color samples
     * @return true if color wraps around HSV hue boundary
     */
    bool isWrapAroundColor(const std::vector<ColorSample>& samples) const;
    
    /**
     * @brief Get current timestamp in milliseconds since epoch
     * @return Timestamp
     */
    int64_t getCurrentTimestamp() const;
};

} // namespace juggler