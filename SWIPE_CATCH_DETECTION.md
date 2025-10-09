# Swipe-Through Catch Detection Enhancement

**Date:** 2025-10-09  
**Status:** Implemented

## Overview

Enhanced the catch inference system to detect fast catches where a hand "swipes through" the ball's trajectory, even when the hand isn't close to the ball's last known position. This solves the problem of missed catch detections during rapid hand movements.

## Problem Statement

Previously, catch detection only worked when:
- Ball vanished from YOLO detection (frames_without_yolo == 1)
- A hand was within 25cm of the ball's last position

This missed fast catches where the hand moved so quickly that it was never close to the ball's last detected position, but instead crossed through the ball's trajectory.

## Solution: Trajectory Crossing Detection

### Algorithm

When a ball vanishes from YOLO, the system now checks for two types of catches:

#### 1. **Swipe-Through Detection** (NEW - Priority)
Detects when a hand crosses the ball's trajectory line:

```cpp
// Calculate ball's movement vector from previous frame
ball_movement = ball_curr_pos - ball_prev_pos

// Check if hand crossed from one side to opposite side
// by comparing dot products (opposite signs = crossed)
dot_prev = ball_movement · (hand_pos - ball_prev_pos)
dot_curr = ball_movement · (hand_pos - ball_curr_pos)
crossed = (dot_prev * dot_curr) < 0

// Verify hand is close to trajectory line (not just crossing in 3D space)
dist_to_trajectory = distance_from_point_to_line(hand_pos, ball_trajectory)

// Catch detected if:
// - Hand crossed trajectory (opposite sides)
// - Hand is within 20cm of trajectory line
if (crossed && dist_to_trajectory < 0.20m) {
    CATCH_DETECTED (swipe-through)
}
```

#### 2. **Proximity Detection** (Existing - Fallback)
Original method: hand within 25cm of ball's last position

### Key Parameters

```cpp
SWIPE_TRAJECTORY_THRESHOLD = 0.20f  // 20cm - max distance from trajectory line
CATCH_INFERENCE_DISTANCE = 0.25f    // 25cm - proximity threshold (fallback)
```

### Requirements

- Ball must have at least 2 frames of position history (for movement vector)
- Ball must be moving (movement magnitude > 0.01m)
- Hand must be visible

## Implementation Details

### Location
File: [`engine/src/SimpleBallTracker.cpp`](engine/src/SimpleBallTracker.cpp:1485-1595)

### Logic Flow

```
Ball vanished from YOLO?
  ├─ YES → Check all visible hands
  │         ├─ For each hand:
  │         │   ├─ Calculate proximity to ball
  │         │   └─ Check trajectory crossing:
  │         │       ├─ Get ball movement vector (curr - prev)
  │         │       ├─ Calculate dot products (prev & curr)
  │         │       ├─ Check if signs opposite (crossed)
  │         │       └─ Calculate distance to trajectory line
  │         │
  │         ├─ PRIORITY 1: Swipe detected?
  │         │   └─ YES → CATCH by swipe hand
  │         │
  │         └─ PRIORITY 2: Hand within 25cm?
  │             └─ YES → CATCH by closest hand
  │
  └─ NO → Continue normal tracking
```

### Tracking Reasons

The system now uses distinct tracking reasons for debugging:

- `CATCH_SWIPE@Hand[L/R]` - Swipe-through catch detected
- `CATCH_PROX@Hand[L/R] d=X.XXm` - Proximity-based catch detected

## Benefits

1. **Catches Fast Movements**: Detects catches even when hand moves faster than frame rate
2. **More Robust**: Works in scenarios where proximity detection fails
3. **Prioritized Logic**: Swipe detection takes priority over proximity
4. **Maintains Backward Compatibility**: Proximity detection still works as fallback

## Debug Logging

Enhanced debug logging for troubleshooting:

```cpp
[SWIPE_DETECTED] Ball 0 | Hand 1 (RIGHT) crossed trajectory 
                | dist_to_trajectory=0.15m | ball_movement_mag=0.08m

[CATCH_INFERENCE_SWIPE] Ball 0 vanished from YOLO with hand 1 (RIGHT) 
                       SWIPING THROUGH trajectory - INFERRING CATCH
```

## Testing Recommendations

1. **Fast Catches**: Throw ball quickly from one hand to other
2. **Swipe Catches**: Catch ball with rapid hand movement
3. **Edge Cases**: Test with hands moving parallel to trajectory (should not trigger)
4. **Multiple Hands**: Verify correct hand is selected when both hands visible

## Future Enhancements

Potential improvements:
1. Store previous hand positions to verify hand actually moved
2. Add velocity-based thresholds (faster ball = larger trajectory threshold)
3. Consider hand orientation/palm direction for catch validation
4. Add temporal smoothing to reduce false positives

## Related Files

- [`engine/src/SimpleBallTracker.cpp`](engine/src/SimpleBallTracker.cpp) - Main implementation
- [`engine/include/SimpleBallTracker.hpp`](engine/include/SimpleBallTracker.hpp) - Class definition
- [`engine/include/ColorBasedPredictor.hpp`](engine/include/ColorBasedPredictor.hpp) - Position history tracking

---

*This enhancement improves catch detection robustness for fast juggling patterns and rapid hand movements.*