# Held Ball Hand Assignment Fix

**Date:** 2025-10-10  
**Status:** ✅ Fixed  
**Severity:** Critical - Prevents incorrect hand switches

## Problem

When a ball was in HELD state and YOLO detection was temporarily lost, the tracker would incorrectly switch the ball to a different hand. This caused:

1. **Visual jumps**: Ball position would jump ~0.45m between hands
2. **Trajectory corruption**: Trajectory points would be cleared on state transitions
3. **Tracking instability**: Ball would bounce between hands for several frames

### Example from Logs

```
Frame 194-195: Ball correctly at RIGHT hand (-0.33, 0.45, 1.94)
Frame 196-198: Ball INCORRECTLY jumps to LEFT hand (0.10, 0.39, 1.87)  ← 0.45m jump!
Frame 199:     Ball returns to RIGHT hand (-0.41, 0.51, 1.98)
```

## Root Cause

In [`updateHeldBall()`](engine/src/SimpleBallTracker.cpp:2289), when YOLO detection was lost and wrist fallback was used, the code would **auto-assign to the first visible hand**:

```cpp
// OLD CODE (DANGEROUS):
if (!hand && !hands.empty()) {
    for (const auto& h : hands) {
        if (h.is_visible) {
            ball.held_by_hand_id = h.id;  // ← Assigns to FIRST hand, not correct hand!
            hand = &h;
            break;
        }
    }
}
```

This logic would:
1. Iterate through hands in order (LEFT=0, RIGHT=1)
2. Assign to the **first** visible hand (always LEFT if both visible)
3. Ignore which hand was actually holding the ball

## Solution

**Preserve hand assignment once set, only auto-assign on initial assignment:**

```cpp
// NEW CODE (SAFE):
if (ball.held_by_hand_id == -1 && !hands.empty()) {
    // Ball has NEVER been assigned - find closest hand
    float min_dist = std::numeric_limits<float>::max();
    int closest_hand_id = -1;
    
    for (const auto& h : hands) {
        if (h.is_visible) {
            float dist = cv::norm(ball.position - h.wrist_pos_3d);
            if (dist < min_dist) {
                min_dist = dist;
                closest_hand_id = h.id;
            }
        }
    }
    
    if (closest_hand_id >= 0) {
        ball.held_by_hand_id = closest_hand_id;
        // ... assign hand pointer
    }
}
```

### Key Changes

1. **Only auto-assign if `held_by_hand_id == -1`** (never been assigned)
2. **Find CLOSEST hand** instead of first hand
3. **Preserve hand ID** once assigned, even when YOLO detection is lost
4. **Prevent hand switches** during temporary detection loss

## How It Works

### Initial Assignment (First Frame)
- Ball starts with `held_by_hand_id = -1`
- System finds closest hand to ball position
- Assigns ball to that hand
- Hand ID is preserved from this point forward

### Subsequent Frames
- Ball keeps its `held_by_hand_id`
- Even if YOLO detection is lost, hand ID is preserved
- Wrist fallback uses the **correct** hand's position
- No incorrect hand switches occur

### Hand Changes (Legitimate Catches)
- Hand ID only changes through catch detection in [`initiateCatch()`](engine/src/SimpleBallTracker.cpp:1934)
- Catch detection requires ball to be within [`hand_distance_threshold`](engine/include/SimpleBallTracker.hpp:149) of a hand
- This is the ONLY legitimate way for hand ID to change

## Benefits

1. **Stability**: Ball stays with correct hand even during detection loss
2. **No jumps**: Position doesn't jump between hands
3. **Trajectory preservation**: Trajectory data isn't corrupted by false hand switches
4. **Correct tracking**: Ball follows the hand that's actually holding it

## Edge Cases Handled

### Case 1: YOLO Detection Lost for Several Frames
- **Before**: Ball would switch to first visible hand (usually LEFT)
- **After**: Ball stays with assigned hand, uses wrist fallback

### Case 2: Both Hands Visible
- **Before**: Would always assign to LEFT hand (first in iteration)
- **After**: Assigns to closest hand on initial assignment, then preserves

### Case 3: Hand Temporarily Occluded
- **Before**: Might reassign when hand becomes visible again
- **After**: Preserves hand ID, no reassignment

### Case 4: Legitimate Hand Switch (Catch)
- **Before**: Worked correctly through catch detection
- **After**: Still works correctly - catch detection is the only way to change hands

## Testing Recommendations

1. **Hold ball in one hand** - verify it doesn't switch to other hand
2. **Move hands close together** - verify no incorrect switches
3. **Occlude ball briefly** - verify it returns to correct hand
4. **Pass ball between hands** - verify catch detection still works
5. **Start with ball in either hand** - verify initial assignment is correct

## Related Code

- [`updateHeldBall()`](engine/src/SimpleBallTracker.cpp:2289) - Main fix location
- [`initiateCatch()`](engine/src/SimpleBallTracker.cpp:1934) - Legitimate hand changes
- [`SimpleBall::held_by_hand_id`](engine/include/SimpleBallTracker.hpp:99) - Hand assignment storage

## See Also

- [TRAJECTORY_PREDICTION_MISSING_FRAMES.md](TRAJECTORY_PREDICTION_MISSING_FRAMES.md) - Related trajectory fixes
- [TRAJECTORY_THROW_CATCH_THRESHOLDS_UI.md](TRAJECTORY_THROW_CATCH_THRESHOLDS_UI.md) - Catch detection thresholds