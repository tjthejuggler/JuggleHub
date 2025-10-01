# Throw and Catch Detection Implementation Plan

**Created:** 2025-10-01 19:38:00 UTC

## Executive Summary

This document outlines the implementation of a robust throw and catch detection system that leverages our new 2-class YOLO model (`ball` and `ball_held`) combined with multi-evidence fusion logic to achieve accurate, real-time event detection.

## Analysis of the LLM's Proposal

### What's Good About the Proposal:

1. **Multi-Evidence Fusion Approach**: The core idea of combining multiple sources of evidence (ML classification, proximity, kinematics, relative velocity) is excellent and aligns with our "glass-box" philosophy.

2. **Layered Architecture**: The three-layer approach (Perception, Tracking & Kinematics, Decision & Fusion) is sound and maps well to our existing architecture.

3. **Explicit Event Conditions**: Defining clear conditions for CATCH and THROW events with multiple criteria provides robustness.

4. **Kinematic Analysis**: Using velocity changes and relative velocity between ball and hand is crucial for accurate detection.

### What Needs Improvement:

1. **Over-Engineering**: The proposal suggests re-labeling the entire dataset and training from scratch. We already have a 2-class model trained, so we should use it.

2. **Redundant Tracking**: The proposal suggests using SORT/DeepSORT when we already have ByteTrack + PersistentTracker with Kalman filters working well.

3. **Missing Integration**: The proposal doesn't account for our existing ColorTracker system that handles occlusion.

4. **Complexity**: The proposal is overly complex for initial implementation. We should start simpler and iterate.

5. **Real-Time Constraints**: The proposal doesn't emphasize computational efficiency enough for real-time operation.

## Our Improved Architecture

### Core Principles:

1. **Leverage Existing Systems**: Build on top of our working ByteTrack + PersistentTracker + ColorTracker infrastructure.

2. **Incremental Enhancement**: Add the 2-class model support without disrupting existing functionality.

3. **Multi-Evidence Fusion**: Combine ML state classification with physics-based analysis.

4. **Real-Time Performance**: Keep computational overhead minimal.

5. **Interpretability**: Make decisions transparent and debuggable.

## System Architecture

### Layer 1: Enhanced Perception (Already Exists)

- **YOLO 2-Class Model**: Detects `ball` (class 0) and `ball_held` (class 1)
- **YOLO-Pose Model**: Provides wrist positions
- **ByteTrack**: Maintains consistent IDs across frames
- **ColorTracker**: Handles complete occlusion cases

### Layer 2: State Tracking (Existing + Enhanced)

Our existing [`PersistentTracker`](engine/include/PersistentTracker.hpp:17) already has:
- Kalman Filter for position/velocity estimation
- Status tracking (TRACKED, PREDICTED, OCCLUDED, LOST)
- Parent hand association

**Enhancements Needed:**
- Add explicit ball state enum: `IN_FLIGHT`, `HELD_LEFT`, `HELD_RIGHT`, `TRANSITIONING`
- Track ML classification confidence over time
- Store recent velocity history for kinematic analysis

### Layer 3: Event Detection & Fusion (NEW)

Create a new `ThrowCatchDetector` class that:

1. **Monitors Multiple Evidence Streams:**
   - ML Classification: `ball` vs `ball_held` from YOLO
   - Proximity: Distance between ball and wrists
   - Kinematics: Ball velocity magnitude and direction
   - Relative Velocity: Velocity difference between ball and hand
   - Temporal Consistency: State must persist for minimum frames

2. **Catch Detection Logic:**
   ```
   CATCH detected when:
   - Ball transitions from IN_FLIGHT to near hand (< 15cm)
   - ML model shows ball_held classification (confidence > 0.6)
   - Ball velocity drops significantly (> 70% reduction)
   - Relative velocity with hand approaches zero (< 0.3 m/s)
   - Conditions persist for 2-3 frames (temporal filtering)
   ```

3. **Throw Detection Logic:**
   ```
   THROW detected when:
   - Ball transitions from HELD to increasing distance from hand (> 20cm)
   - ML model shows ball classification (confidence > 0.6)
   - Ball velocity increases significantly (> 0.5 m/s)
   - Relative velocity with hand increases (> 0.5 m/s)
   - Ball enters ballistic trajectory (acceleration ≈ gravity)
   - Conditions persist for 2-3 frames
   ```

4. **Weighted Evidence Fusion:**
   Each evidence source has a weight:
   - ML Classification: 0.35 (most reliable when visible)
   - Proximity: 0.25 (essential but can be noisy)
   - Kinematics: 0.25 (physics-based, very reliable)
   - Relative Velocity: 0.15 (supporting evidence)
   
   Event triggered when weighted sum > 0.75

## Implementation Strategy

### Phase 1: Update Model Configuration (CURRENT)

1. Update [`DNNTracker`](engine/include/DNNTracker.hpp:51) class_names to support 2-class model:
   ```cpp
   const std::vector<std::string> class_names_ = {"ball", "ball_held"};
   ```

2. Modify postprocessing to extract and store class information per detection

### Phase 2: Enhance PersistentTracker

1. Add ball state enum to [`PersistentTracker`](engine/include/PersistentTracker.hpp:17):
   ```cpp
   enum class BallState {
       IN_FLIGHT,
       HELD_LEFT,
       HELD_RIGHT,
       TRANSITIONING
   };
   ```

2. Add fields for event detection:
   ```cpp
   BallState ball_state = BallState::IN_FLIGHT;
   float ml_held_confidence = 0.0f;
   std::deque<Eigen::Vector3d> velocity_history; // Last 5 frames
   int frames_in_current_state = 0;
   ```

### Phase 3: Create ThrowCatchDetector Class

Create new files:
- `engine/include/ThrowCatchDetector.hpp`
- `engine/src/ThrowCatchDetector.cpp`

Key methods:
```cpp
class ThrowCatchDetector {
public:
    struct EventEvidence {
        float ml_confidence;
        float proximity_score;
        float kinematic_score;
        float relative_velocity_score;
        float total_score;
    };
    
    struct DetectedEvent {
        enum Type { CATCH, THROW };
        Type type;
        int ball_id;
        int hand_id; // 0=left, 1=right
        uint64_t timestamp_us;
        cv::Point3f position;
        EventEvidence evidence;
    };
    
    std::vector<DetectedEvent> detectEvents(
        std::vector<PersistentTracker>& balls,
        std::vector<PersistentTracker>& hands,
        const std::vector<Detection>& raw_detections,
        float dt
    );
    
private:
    EventEvidence evaluateCatchEvidence(
        const PersistentTracker& ball,
        const PersistentTracker& hand,
        const Detection* detection,
        float dt
    );
    
    EventEvidence evaluateThrowEvidence(
        const PersistentTracker& ball,
        const PersistentTracker& hand,
        float dt
    );
    
    bool meetsTemporalRequirement(const PersistentTracker& ball, int required_frames);
};
```

### Phase 4: Integration

1. Instantiate `ThrowCatchDetector` in [`DNNTracker`](engine/include/DNNTracker.hpp:51)

2. Call detector after ByteTrack association in [`update()`](engine/src/DNNTracker.cpp:80)

3. Update ball states based on detected events

4. Log events for downstream pattern recognition

### Phase 5: Maintain ColorTracker Integration

Our existing [`ColorTracker`](engine/include/ColorTracker.hpp:49) handles complete occlusion well:
- When ball is completely hidden, it uses color blob detection near last known position
- If no blob found, assumes ball is at wrist position
- This system continues to work alongside the new throw/catch detection

**Integration Point:**
- When `ThrowCatchDetector` confirms a CATCH, inform `ColorTracker` to start occlusion tracking
- When `ThrowCatchDetector` confirms a THROW, inform `ColorTracker` to resume normal tracking

## Advantages of Our Approach

1. **Builds on Proven Systems**: Leverages existing ByteTrack, PersistentTracker, and ColorTracker
2. **Incremental**: Can be developed and tested in stages
3. **Efficient**: Minimal computational overhead
4. **Robust**: Multiple evidence sources with temporal filtering
5. **Interpretable**: Clear decision logic, easy to debug
6. **Real-Time**: Designed for 30+ FPS operation
7. **Accurate**: Multi-evidence fusion reduces false positives/negatives

## Testing Strategy

1. **Unit Tests**: Test each evidence evaluator independently
2. **Integration Tests**: Test full event detection pipeline
3. **Real-World Validation**: Test with actual juggling footage
4. **Edge Cases**: Test with:
   - Fast throws
   - Slow catches
   - Multiplex patterns
   - Partial occlusions
   - Poor lighting

## Success Metrics

- **Catch Detection Accuracy**: > 95%
- **Throw Detection Accuracy**: > 95%
- **False Positive Rate**: < 5%
- **Latency**: < 33ms (1 frame at 30 FPS)
- **Temporal Precision**: ± 2 frames (±66ms)

## Future Enhancements

1. **IMU Integration**: Use wrist IMU data for additional evidence
2. **Pattern-Aware Detection**: Use pattern context to improve accuracy
3. **Adaptive Thresholds**: Learn optimal thresholds per juggler
4. **Multi-Ball Collision Detection**: Detect intentional mid-air collisions
5. **Drop Detection**: Identify when balls are dropped vs caught

## Conclusion

This implementation plan provides a practical, efficient, and robust approach to throw and catch detection that:
- Leverages our new 2-class YOLO model
- Builds on existing proven systems
- Uses multi-evidence fusion for accuracy
- Maintains real-time performance
- Provides interpretable results

The system will form the foundation for accurate pattern recognition and siteswap calculation.