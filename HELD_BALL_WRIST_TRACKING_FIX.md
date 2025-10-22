# Held Ball Wrist Tracking Fix

**Date:** 2025-10-22
**Component:** New 3D Kalman Tracking System
**Issue:** Balls held in hands were not staying locked to wrist when not visible, and balls within held_radius were incorrectly marked as IN_FLIGHT

## Problem Description

There were two critical issues:

1. **Invisible Held Balls**: When a ball was held in a hand and became invisible (not detected by YOLO), the tracker would lose track of the ball's position. The ball would remain in the HELD state but wouldn't follow the hand's movement.

2. **Incorrect State Assignment**: When a ball was re-acquired after being lost, it was being set to IN_FLIGHT even when clearly within `held_radius` of a hand. This was because the re-acquisition logic only set balls to HELD if they were already HELD before losing detection.

## Root Cause

The tracking system had four areas where held balls needed fixes:

1. **`handleUnmatchedBalls()`**: When a ball was not matched to any detection, it would only increment the `frames_since_seen` counter without updating the position for HELD balls
2. **`predictAllBalls()`**: Missing logging for when a hand wasn't found
3. **`finalizeBallPositions()`**: Not updating pixel position for visualization when ball was held but not detected
4. **`createNewTracks()` (CRITICAL)**: Re-acquisition logic was too restrictive - only setting balls to HELD if they were already HELD before, instead of checking distance to hands

## Solution

### 1. Enhanced `handleUnmatchedBalls()` (lines 905-955)

**Key Changes:**
- Added state-aware handling for unmatched balls
- For HELD balls: Lock position to wrist even when not detected
- For IN_FLIGHT balls: Keep using Kalman prediction
- Transition HELD balls to IN_FLIGHT if their associated hand is lost

```cpp
if (ball->state == HELD) {
    // Find the hand holding this ball
    const SimpleHand* holding_hand = nullptr;
    for (const auto& hand : hands_) {
        if (hand.id == ball->associated_hand_id) {
            holding_hand = &hand;
            break;
        }
    }
    
    if (holding_hand) {
        // Keep ball locked to wrist position even though it's not detected
        ball->predicted_position = holding_hand->wrist_pos_3d;
        ball->tracking_reason = "HELD (not visible, tracking wrist) - " + 
                               std::to_string(ball->frames_since_seen) + " frames";
    } else {
        // Hand lost - transition to IN_FLIGHT
        ball->state = IN_FLIGHT;
        ball->associated_hand_id = -1;
    }
}
```

### 2. Improved `predictAllBalls()` (lines 176-200)

**Key Changes:**
- Added debug logging when hand is not found for a HELD ball
- Clarified that keeping last position is intentional behavior

```cpp
if (holding_hand) {
    predictHeldBall(ball, *holding_hand, dt);
} else {
    // Hand not found, but ball is marked as held
    // This can happen if hand detection temporarily fails
    // Keep last known position (which should be the wrist from previous frame)
    ball.predicted_position = ball.last_known_position;
    logDebug("  Ball ", ball.id, " (", ball.color_name, ") is HELD but hand ", 
             ball.associated_hand_id, " not detected - keeping last position");
}
```

### 3. Enhanced `finalizeBallPositions()` (lines 1095-1148)

**Key Changes:**
- Updated function signature to accept `CameraIntrinsics` for pixel projection
- Always lock HELD balls to wrist position, even when not detected
- Update pixel position for proper visualization

```cpp
void New3DTracker::finalizeBallPositions(const std::vector<SimpleHand>& hands, 
                                         const CameraIntrinsics& intrinsics) {
    // ...
    if (ball.state == HELD) {
        if (holding_hand) {
            // Lock ball position to wrist - this is the key fix
            ball.last_known_position = holding_hand->wrist_pos_3d;
            
            // Also update pixel position for visualization
            ball.pixel_pos = project3DTo2D(holding_hand->wrist_pos_3d, intrinsics);
        }
    }
    // ...
}
```

### 4. Fixed Re-Acquisition Logic in `createNewTracks()` (lines 1010-1047) **CRITICAL FIX**

**Key Changes:**
- Removed the restrictive check that only set balls to HELD if they were already HELD
- Now properly checks distance to hands and sets state based on proximity
- Any ball within `held_radius` of a hand is set to HELD, regardless of previous state

**Before (BROKEN):**
```cpp
// Update state - CRITICAL: Only set to HELD if ball was already HELD before losing detection
if (near_hand && ball->state == HELD) {
    ball->state = HELD;
    ball->associated_hand_id = closest_hand_id;
} else {
    ball->state = IN_FLIGHT;  // <-- WRONG! Ball is near hand but marked as IN_FLIGHT
}
```

**After (FIXED):**
```cpp
// Update state - CRITICAL FIX: Set to HELD if ball is within held_radius of any hand
if (near_hand) {
    // Ball is within held_radius of a hand - set to HELD
    ball->state = HELD;
    ball->associated_hand_id = closest_hand_id;
    ball->tracking_reason = "Re-acquired (HELD, distance=" +
                           std::to_string(min_distance) + "m)";
} else {
    // Ball is not near any hand - set as IN_FLIGHT
    ball->state = IN_FLIGHT;
    ball->associated_hand_id = -1;
}
```

### 5. Updated Header File

**File:** [`engine/include/New3DTracker.hpp`](engine/include/New3DTracker.hpp:485)

Updated function signature to match implementation:
```cpp
void finalizeBallPositions(const std::vector<SimpleHand>& hands,
                          const CameraIntrinsics& intrinsics);
```

## Behavior After Fix

### When Ball is HELD and Not Detected:

1. **Position Tracking**: Ball position is locked to the wrist of the associated hand
2. **State Maintenance**: Ball remains in HELD state as long as the hand is detected
3. **Visual Feedback**: Tracker displays at wrist position with appropriate tracking reason
4. **Frame Counter**: `frames_since_seen` increments but doesn't affect position
5. **Hand Loss**: If hand is lost, ball transitions to IN_FLIGHT state

### Tracking Reasons:

- `"HELD (not visible, tracking wrist) - N frames"` - Ball is held but not detected
- `"Hand lost (not detected for N frames)"` - Hand was lost, transitioning to IN_FLIGHT
- `"HELD - locked to hand X wrist: (x, y, z) m"` - Normal HELD state with detection

## Testing Recommendations

1. **Hold a ball in hand and move it around** - Tracker should follow wrist
2. **Occlude the ball completely** - Tracker should stay on wrist
3. **Move hand while ball is occluded** - Tracker should follow hand movement
4. **Release ball while occluded** - Should detect throw when ball becomes visible again
5. **Lose hand detection temporarily** - Ball should transition to IN_FLIGHT

## Files Modified

1. [`engine/src/New3DTracker.cpp`](engine/src/New3DTracker.cpp)
   - `handleUnmatchedBalls()` (lines 905-955)
   - `predictAllBalls()` (lines 176-200)
   - `finalizeBallPositions()` (lines 1095-1148)
   - `updateNew3D()` (line 1703) - Updated function call

2. [`engine/include/New3DTracker.hpp`](engine/include/New3DTracker.hpp:485)
   - Updated `finalizeBallPositions()` signature

## Impact

- **Improved User Experience**: Tracker no longer appears frozen when ball is held but not visible
- **Better Juggling Support**: Handles common scenarios where balls are temporarily occluded in hands
- **Consistent Behavior**: Ball position always reflects the actual state (wrist for HELD, prediction for IN_FLIGHT)
- **No Breaking Changes**: All existing functionality preserved, only enhanced behavior for edge cases

## Related Documentation

- [New 3D Tracker Architecture](NEW_3D_TRACKER_ARCHITECTURE.md)
- [New 3D Tracker Implementation](NEW_3D_TRACKER_IMPLEMENTATION_COMPLETE.md)
- [Persistent Ball Architecture](PERSISTENT_BALL_ARCHITECTURE_IMPLEMENTATION.md)