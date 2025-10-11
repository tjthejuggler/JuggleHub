# Trajectory Prediction for Missing Frames

**Date:** 2025-10-10  
**Status:** ✅ Implemented

## Overview

Enhanced the trajectory prediction system to handle missing detections during flight mode by using predicted trajectory points instead of failing to track the ball.

## Problem

Previously, when a ball was in flight mode and we didn't have a YOLO detection or color blob for a frame, the tracking would fail or produce unreliable results. This could happen due to:
- Temporary occlusions
- Fast ball movement causing motion blur
- Poor lighting conditions
- Ball moving outside camera view temporarily

## Solution

Modified [`updateInFlightBall()`](engine/src/SimpleBallTracker.cpp:1630) to:

1. **Use predicted position when no detection found**: When YOLO detection and color blob search both fail, the ball position is set to the next predicted trajectory point
2. **Don't add predicted positions as verified points**: Only actual detections (YOLO or color blob) are added as verified trajectory points
3. **Continue prediction from last verified point**: The trajectory prediction continues from the last verified point, maintaining accuracy

## Implementation Details

### Key Changes in `updateInFlightBall()`

```cpp
// Step 3: Handle detection result
if (detection) {
    // YOLO detection found and verified
    ball.position = detection->world_pos;
    // ... update ball state
    verified = true;
}
else {
    // No YOLO detection - try color blob fallback
    // ... color blob search
    
    if (!verified) {
        // Use predicted position (NEW: this is now the fallback)
        ball.position = predicted_next;
        ball.pixel_pos = project_3d_to_2d(predicted_next, intrinsics);
        ball.tracking_reason = "IN_FLIGHT_predicted";
    }
}

// Step 4: Add verified point ONLY if we have a real detection
if (verified && ball.position.z > 0) {
    addVerifiedPoint(ball, ball.position, current_timestamp);
    // Recalculate prediction from verified points
}
else if (!verified && ball.position.z > 0) {
    // Using prediction - don't add as verified point
    // Trajectory continues from last verified point
}
```

### Catch Detection

The catch detection logic remains unchanged - a ball is caught when:
- Ball position (whether verified or predicted) is within [`hand_distance_threshold`](engine/include/SimpleBallTracker.hpp:149) of a hand
- Default threshold: 0.30m (30cm)

This means:
- ✅ Hand being at the predicted point triggers a catch
- ✅ Hand being within range of the predicted point triggers a catch
- ✅ Catch detection works with both verified and predicted positions

## Benefits

1. **Continuous tracking**: Ball tracking continues smoothly even when detections are temporarily unavailable
2. **Accurate predictions**: Only verified detections are used to refine trajectory, maintaining prediction accuracy
3. **Robust catch detection**: Catches are detected based on proximity to predicted position, making the system more reliable
4. **Better user experience**: No visual jumps or tracking failures during brief occlusions

## Configuration

Relevant settings in [`TrackingSettings`](engine/include/SimpleBallTracker.hpp:139):

```cpp
float hand_distance_threshold = 0.30f;    // Distance threshold for hand-ball proximity (m)
float traj_search_radius = 0.15f;         // Search radius along trajectory (m)
float traj_max_search_distance = 0.50f;   // Maximum search distance from prediction (m)
```

## Testing Recommendations

1. Test with fast throws where ball may blur in some frames
2. Test with partial occlusions (hand passing in front of ball)
3. Test catch detection with predicted positions
4. Verify trajectory visualization shows both verified (bright) and predicted (darker) points correctly

## Related Files

- [`engine/src/SimpleBallTracker.cpp`](engine/src/SimpleBallTracker.cpp) - Main implementation
- [`engine/include/SimpleBallTracker.hpp`](engine/include/SimpleBallTracker.hpp) - Header with settings
- [`engine/include/GpuTrajectoryPredictor.hpp`](engine/include/GpuTrajectoryPredictor.hpp) - Trajectory prediction logic

## See Also

- [TRAJECTORY_PREDICTION_FIX.md](TRAJECTORY_PREDICTION_FIX.md) - Previous trajectory prediction improvements
- [TRAJECTORY_PREDICTION_IMPLEMENTATION.md](TRAJECTORY_PREDICTION_IMPLEMENTATION.md) - Original implementation