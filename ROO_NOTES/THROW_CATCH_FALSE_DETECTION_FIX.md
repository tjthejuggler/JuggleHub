# Throw/Catch False Detection Fix

**Date:** 2025-10-11  
**Issue:** False throw and catch events being registered when ball is still very close to hand during actual throw motion

## Problem Description

When a ball was being thrown from the left hand, the system was incorrectly registering multiple throw and catch events in rapid succession:

1. Ball starts HELD at wrist position
2. Ball moves slightly away → **FALSE THROW** detected
3. Ball snaps back to wrist → **FALSE CATCH** detected  
4. Ball moves away again → Another throw detected
5. Ball snaps back → Another catch detected
6. Finally, actual throw occurs

This created spurious events and incorrect state transitions, making the tracking unreliable.

### Root Cause Analysis

From the logs (Frames 73-81), the issue occurred because:

1. **Throw Detection Too Sensitive**: The throw detection in [`updateHeldBall()`](engine/src/SimpleBallTracker.cpp:2693) only checked:
   - Distance from hand > [`hand_distance_threshold`](engine/include/SimpleBallTracker.hpp:149) (default: 0.30m, previously `throw_distance_threshold`)
   - Distance from ball < `max_tracker_distance_per_frame`
   
   But it didn't verify that the ball had **actually moved significantly** from its previous position. Small jitter or hand movement could trigger false throws.

2. **Catch Detection Too Eager**: The catch detection in [`updateInFlightBall()`](engine/src/SimpleBallTracker.cpp:2018) would immediately catch the ball if it came within [`hand_distance_threshold`](engine/include/SimpleBallTracker.hpp:149) (previously `catch_distance_threshold`) of a hand, even if the ball had just been thrown and was still very close to the throwing hand.

## Solution

### 1. Throw Detection Fix (Lines 2693-2741)

Added an additional check to verify the ball has moved a minimum distance from its previous position:

```cpp
// ADDITIONAL CHECK: Verify ball has actually moved away from its previous position
// Calculate distance moved from last known position
float distance_moved = cv::norm(det.world_pos - ball.position);

// CRITICAL: Ball must have moved at least half the throw threshold distance
// This prevents false throws from small jitter or hand movement
float min_movement_threshold = tracking_settings_.hand_distance_threshold * 0.5f;

if (distance_moved >= min_movement_threshold) {
    // THROW DETECTED - initiate throw transition
    initiateThrow(ball, det, hand, events);
    return;
}
```

**Key Points:**
- Ball must move at least **50% of the throw threshold** (default: 0.10m) from its previous position
- This filters out small jitter and hand movements
- Ensures the ball is actually being thrown, not just moving slightly

### 2. Catch Detection Fix (Lines 2018-2072)

Added a check to ensure the ball has moved significantly away from the throw position before allowing a catch:

```cpp
// CRITICAL FIX: Check if ball has moved significantly away from throw position
// This prevents immediate catch right after throw when ball is still near hand
bool has_moved_away = false;
if (!ball.trajectory.points.empty()) {
    // Get the first trajectory point (throw position)
    cv::Point3f throw_position = ball.trajectory.points[0].position;
    float distance_from_throw = cv::norm(position_for_catch - throw_position);
    
    // Ball must have moved at least the throw threshold distance away
    // This ensures the ball has actually left the hand before we can catch it
    has_moved_away = (distance_from_throw >= tracking_settings_.hand_distance_threshold);
}

if (!has_moved_away) {
    // Skip catch detection - ball hasn't moved far enough
    return;
}
```

**Key Points:**
- Ball must move at least **the full throw threshold** (default: 0.20m) away from throw position
- Uses the first trajectory point as the throw position reference
- Prevents immediate catch right after throw
- Ensures ball has actually left the hand before it can be caught

## Testing

To verify the fix works:

1. Rebuild the engine:
   ```bash
   cd engine
   mkdir -p build && cd build
   cmake .. && make -j$(nproc)
   ```

2. Run the system and perform a single throw from the left hand

3. Check the logs - you should see:
   - Only ONE throw event when ball leaves hand
   - Only ONE catch event when ball returns to hand
   - No spurious state transitions between HELD and IN_FLIGHT

4. Look for these debug messages:
   - `THROW REJECTED: distance_moved X.XXm < min_movement X.XXm` (filtering out false throws)
   - `Skipping catch detection - ball hasn't moved far enough from throw position` (filtering out false catches)

## Configuration

The fix uses existing configuration parameters:

- **`throw_distance_threshold`** (default: 0.20m): Distance ball must be from hand to detect throw
  - Throw detection now requires ball to move at least **50% of this value** from previous position
  - Catch detection requires ball to move at least **100% of this value** from throw position

- **`catch_distance_threshold`** (default: 0.30m): Maximum distance from hand to detect catch

- **`max_tracker_distance_per_frame`** (default: varies): Maximum distance ball can move per frame

## Impact

- ✅ Eliminates false throw/catch events during actual throw motion
- ✅ More reliable state transitions
- ✅ Better tracking accuracy for juggling patterns
- ✅ No performance impact (just additional distance checks)
- ✅ Backward compatible with existing configuration

## Related Files

- [`engine/src/SimpleBallTracker.cpp`](engine/src/SimpleBallTracker.cpp) - Main implementation
- [`engine/include/SimpleBallTracker.hpp`](engine/include/SimpleBallTracker.hpp) - Header file
- [`hub/components/ui_settings.py`](hub/components/ui_settings.py) - UI settings for thresholds

---

**Timestamp:** 2025-10-11T19:33:00Z