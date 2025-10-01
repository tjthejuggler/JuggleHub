#pragma once

#include "PersistentTracker.hpp"
#include <vector>
#include <opencv2/core/types.hpp>

// Forward declaration
struct Detection;

namespace juggler {

/**
 * @brief Multi-evidence fusion system for detecting throw and catch events
 * 
 * This class analyzes multiple sources of evidence to accurately detect when
 * balls are thrown and caught:
 * - ML Classification: YOLO model's ball vs ball_held classification
 * - Proximity: Distance between ball and wrist positions
 * - Kinematics: Ball velocity changes and trajectory analysis
 * - Relative Velocity: Velocity difference between ball and hand
 * - Temporal Consistency: Events must persist across multiple frames
 */
class ThrowCatchDetector {
public:
    /**
     * @brief Evidence scores from different detection methods
     */
    struct EventEvidence {
        float ml_confidence;           // ML model confidence (0-1)
        float proximity_score;         // Proximity-based score (0-1)
        float kinematic_score;         // Kinematics-based score (0-1)
        float relative_velocity_score; // Relative velocity score (0-1)
        float total_score;             // Weighted sum of all scores (0-1)
        
        EventEvidence() 
            : ml_confidence(0.0f), proximity_score(0.0f), 
              kinematic_score(0.0f), relative_velocity_score(0.0f), 
              total_score(0.0f) {}
    };
    
    /**
     * @brief Detected throw or catch event
     */
    struct DetectedEvent {
        enum Type { CATCH, THROW };
        
        Type type;                  // Event type
        int ball_id;                // Logical ID of the ball
        int hand_id;                // Hand ID (0=left, 1=right)
        uint64_t timestamp_us;      // Event timestamp in microseconds
        cv::Point3f position;       // 3D position where event occurred
        EventEvidence evidence;     // Evidence scores that triggered the event
        
        DetectedEvent(Type t, int bid, int hid, uint64_t ts, const cv::Point3f& pos)
            : type(t), ball_id(bid), hand_id(hid), timestamp_us(ts), position(pos) {}
    };
    
    /**
     * @brief Configuration parameters for event detection
     */
    struct Config {
        // Evidence weights (must sum to 1.0)
        float ml_weight = 0.35f;
        float proximity_weight = 0.25f;
        float kinematic_weight = 0.25f;
        float relative_velocity_weight = 0.15f;
        
        // Detection thresholds
        float catch_threshold = 0.75f;      // Total score needed for catch
        float throw_threshold = 0.75f;      // Total score needed for throw
        float ml_confidence_min = 0.6f;     // Minimum ML confidence
        
        // Distance thresholds (meters)
        float catch_distance = 0.15f;       // Max distance for catch (15cm)
        float throw_distance = 0.20f;       // Min distance for throw (20cm)
        
        // Velocity thresholds (m/s)
        float catch_velocity_drop = 0.70f;  // Velocity must drop by 70%
        float throw_velocity_min = 0.5f;    // Min velocity increase for throw
        float relative_velocity_catch = 0.3f; // Max relative velocity for catch
        float relative_velocity_throw = 0.5f; // Min relative velocity for throw
        
        // Temporal filtering
        int min_frames_for_event = 2;       // Event must persist for N frames
        int max_transition_frames = 5;      // Max frames in TRANSITIONING state
    };
    
    ThrowCatchDetector();
    explicit ThrowCatchDetector(const Config& config);
    ~ThrowCatchDetector() = default;
    
    /**
     * @brief Detect throw and catch events for current frame
     * 
     * @param balls Vector of ball trackers
     * @param hands Vector of hand trackers
     * @param raw_detections Raw detections from YOLO (includes class_id)
     * @param dt Time delta since last frame (seconds)
     * @return Vector of detected events
     */
    std::vector<DetectedEvent> detectEvents(
        std::vector<PersistentTracker>& balls,
        std::vector<PersistentTracker>& hands,
        const std::vector<Detection>& raw_detections,
        float dt
    );
    
    /**
     * @brief Update configuration parameters
     */
    void setConfig(const Config& config) { config_ = config; }
    const Config& getConfig() const { return config_; }
    
private:
    Config config_;
    
    /**
     * @brief Evaluate evidence for a potential catch event
     */
    EventEvidence evaluateCatchEvidence(
        const PersistentTracker& ball,
        const PersistentTracker& hand,
        const Detection* detection,
        float dt
    );
    
    /**
     * @brief Evaluate evidence for a potential throw event
     */
    EventEvidence evaluateThrowEvidence(
        const PersistentTracker& ball,
        const PersistentTracker& hand,
        float dt
    );
    
    /**
     * @brief Check if ball has been in current state long enough
     */
    bool meetsTemporalRequirement(const PersistentTracker& ball, int required_frames) const;
    
    /**
     * @brief Calculate 3D distance between two points
     */
    float calculateDistance(const Eigen::Vector3d& p1, const Eigen::Vector3d& p2) const;
    
    /**
     * @brief Calculate velocity magnitude
     */
    float calculateVelocityMagnitude(const Eigen::Vector3d& velocity) const;
    
    /**
     * @brief Find the detection that best matches a tracker
     */
    const Detection* findMatchingDetection(
        const PersistentTracker& tracker,
        const std::vector<Detection>& detections
    ) const;
    
    /**
     * @brief Update ball's velocity history
     */
    void updateVelocityHistory(PersistentTracker& ball);
    
    /**
     * @brief Get average velocity from history
     */
    Eigen::Vector3d getAverageVelocity(const PersistentTracker& ball) const;
};

} // namespace juggler