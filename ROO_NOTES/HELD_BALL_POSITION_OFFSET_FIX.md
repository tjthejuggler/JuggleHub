# Held Ball Position Uses Held Circle Center

**Date:** 2025-10-27  
**Status:** ✅ Complete

## Overview

Updated the New 3D Kalman Tracking system so that held balls use the center of the held circle (with offset applied) as their position, rather than just the wrist position. This ensures consistency between the ball position and the held circle visualization.

## Problem

Previously, when a ball was in the HELD state:
- The held circle visualization was drawn at the wrist position + offset along the forearm direction
- However, the ball's actual position was set to just the wrist position (without offset)
- This created a visual inconsistency where the ball appeared at the wrist but the held circle was offset

## Solution

Updated two key functions in [`engine/src/New3DTracker.cpp`](engine/src/New3DTracker.cpp):

### 1. `finalizeBallPositions()` (lines ~1226-1286)

Changed from:
```cpp
// Lock ball position to wrist
ball.last_known_position = holding_hand->wrist_pos_3d;
```

To:
```cpp
// Start with previous position as default (maintains continuity if skeleton data unavailable)
cv::Point3f held_position = ball.last_known_position;

// Apply offset along forearm direction if skeleton data is available
if (!holding_hand->keypoints.empty() && holding_hand->keypoints.size() > 10) {
    int elbow_idx = (holding_hand->id == 0) ? 7 : 8;
    int wrist_idx = (holding_hand->id == 0) ? 9 : 10;
    
    if (elbow_idx < holding_hand->keypoints.size() && wrist_idx < holding_hand->keypoints.size()) {
        const cv::Point3f& elbow_pos = holding_hand->keypoints[elbow_idx];
        const cv::Point3f& wrist_pos = holding_hand->keypoints[wrist_idx];
        
        if (elbow_pos.z > 0.1f && wrist_pos.z > 0.1f) {
            cv::Point3f forearm_dir = wrist_pos - elbow_pos;
            float forearm_length = std::sqrt(
                forearm_dir.x * forearm_dir.x +
                forearm_dir.y * forearm_dir.y +
                forearm_dir.z * forearm_dir.z
            );
            
            if (forearm_length > 0.01f) {
                forearm_dir = forearm_dir / forearm_length;
                float offset_m = settings_.held_circle_offset_cm / 100.0f;
                held_position = wrist_pos + forearm_dir * offset_m;
            }
        }
    }
}

// Update ball position (either newly calculated or previous position maintained)
ball.last_known_position = held_position;
```

### 2. `handleUnmatchedBalls()` (lines ~972-1028)

Applied the same offset calculation for held balls that are not currently visible but are still being tracked at the hand position.

Changed from:
```cpp
// Keep ball locked to wrist position even though it's not detected
ball->predicted_position = holding_hand->wrist_pos_3d;
```

To the same offset calculation logic as above, ensuring consistency.

## Implementation Details

The offset calculation:
1. **Starts with the previous frame's position** as the default (ensures smooth continuity)
2. If skeleton keypoints are available (elbow and wrist):
   - Calculates the forearm direction vector (from elbow to wrist)
   - Normalizes the direction vector
   - Applies the `held_circle_offset_cm` setting (converted to meters) along this direction
   - Updates the position to the new calculated held circle center
3. **If skeleton data is unavailable**, keeps the previous frame's position (no fallback to wrist)

This matches exactly the logic used in:
- [`predictHeldBall()`](engine/src/New3DTracker.cpp:205) for Kalman prediction
- [`drawHandThresholds()`](engine/src/New3DTracker.cpp:2223) for visualization

## Benefits

1. **Visual Consistency**: Ball position now matches the held circle visualization
2. **Accurate Tracking**: Ball position reflects the actual held circle area used for catch/throw detection
3. **Configurable Offset**: The `held_circle_offset_cm` setting now affects both visualization and tracking
4. **Unified Logic**: Same offset calculation used throughout the codebase
5. **Smooth Continuity**: When skeleton data is temporarily unavailable, the ball maintains its previous position rather than jumping to the wrist

## Testing

After rebuilding the engine:
```bash
cd engine
./rebuild.sh
```

The ball position for held balls should now:
- Match the center of the yellow held circle visualization
- Move with the held circle when the offset setting is changed
- Maintain consistency across all tracking states

## Related Files

- [`engine/src/New3DTracker.cpp`](engine/src/New3DTracker.cpp) - Main implementation
- [`HELD_CIRCLE_OFFSET_IMPLEMENTATION.md`](HELD_CIRCLE_OFFSET_IMPLEMENTATION.md) - Original offset feature
- [`HELD_CIRCLE_OFFSET_REAL_TIME_VIZ_FIX.md`](HELD_CIRCLE_OFFSET_REAL_TIME_VIZ_FIX.md) - Visualization fix

## Settings

The relevant setting is:
- `held_circle_offset_cm` - Offset distance in centimeters from wrist along forearm direction (default: 10.0)

This can be adjusted in the UI under New 3D Tracker settings.