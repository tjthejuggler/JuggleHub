# Kalman Filter Throw Velocity Fix

**Date:** 2025-10-06  
**Issue:** Bad Kalman predictions immediately after throw events

## Problem Analysis

### Observed Behavior
When analyzing the debug images, the Kalman filter made an obviously incorrect prediction:
- **Ball trajectory:** Moving UP and to the RIGHT (Y: 0.51→0.64, X: -0.43→-0.52)
- **Kalman prediction:** Velocity showed (-0.07, **-1.10**, -0.16) m/s
- **Issue:** Y velocity was -1.10 m/s (DOWNWARD) when ball was clearly moving UPWARD

### Root Cause

The problem occurs during the HELD → IN-AIR state transition (throw event):

1. **While held:** Ball position is tracked at hand location with zero or minimal velocity
2. **Throw detected:** State changes from HELD to IN-AIR
3. **Kalman prediction:** Immediately applies gravity (9.81 m/s² downward)
4. **Result:** Kalman predicts ball will go DOWN, even though it was thrown UP

The Kalman filter code in [`engine/src/KalmanFilter3D.cpp`](engine/src/KalmanFilter3D.cpp:75):
```cpp
x_(1) += x_(4) * dt + 0.5 * effective_gravity * dt * dt; // y position
x_(4) += effective_gravity * dt;                        // y velocity
```

Since `effective_gravity` is always positive (pulls downward in camera coordinates), and the Kalman velocity starts at ~0, the first prediction after a throw is always downward.

## Solution

### Implementation
Modified [`engine/src/SimpleBallTracker.cpp`](engine/src/SimpleBallTracker.cpp:785) to initialize Kalman velocity with estimated throw velocity when a throw event is detected:

```cpp
if (old_state_was_held && !now_held) {
    // Was held, now in air = THROW
    
    // CRITICAL FIX: Estimate throw velocity from color predictor history
    auto estimated_velocity = ball.color_predictor.getVelocity();
    if (estimated_velocity.z != 0.0f) {  // Valid velocity estimate
        // Update Kalman velocity with throw velocity
        auto& state = ball.kalman.get_state();
        state(3) = estimated_velocity.x;  // vx
        state(4) = estimated_velocity.y;  // vy
        state(5) = estimated_velocity.z;  // vz
    }
    
    // Generate throw event...
}
```

### How It Works

1. **Color Predictor History:** The `ColorBasedPredictor` maintains a history of recent ball positions (default: 5 frames)

2. **Velocity Estimation:** When a throw is detected, we call `getVelocity()` which:
   - Calculates velocity between consecutive position samples
   - Averages these velocities for a robust estimate
   - Returns the estimated throw velocity vector

3. **Kalman Initialization:** The estimated velocity is directly written to the Kalman state vector:
   - `state(3)` = vx (horizontal velocity)
   - `state(4)` = vy (vertical velocity)  
   - `state(5)` = vz (depth velocity)

4. **Gravity Application:** After initialization, gravity is applied normally, but now it modifies the correct initial velocity instead of starting from zero

### Benefits

- **Accurate predictions:** Kalman now predicts ball trajectory in the correct direction
- **Smooth tracking:** No sudden jumps or wrong-direction predictions
- **Robust:** Uses averaged velocity from multiple frames, not just last two positions
- **Minimal overhead:** Only runs once per throw event, not every frame

## Testing Recommendations

1. **Upward throws:** Verify predictions go UP when ball is thrown upward
2. **Sideways throws:** Check horizontal velocity is captured correctly
3. **Fast throws:** Test with high-velocity throws to ensure velocity estimation is accurate
4. **Slow tosses:** Verify gentle throws still work correctly

## Related Files

- [`engine/src/SimpleBallTracker.cpp`](engine/src/SimpleBallTracker.cpp:785) - Throw event detection and velocity initialization
- [`engine/include/ColorBasedPredictor.hpp`](engine/include/ColorBasedPredictor.hpp:98) - Velocity estimation method
- [`engine/src/KalmanFilter3D.cpp`](engine/src/KalmanFilter3D.cpp:53) - Kalman prediction with gravity
- [`hub/components/ui_settings.py`](hub/components/ui_settings.py:561) - Prediction radius setting (now max 100cm)

## Additional Notes

- The prediction radius was also increased from 30cm to 100cm max to allow tracking of faster/longer throws
- The fix preserves the existing Kalman filter implementation, only modifying the initial velocity on throw events
- Debug logging was added to track velocity initialization for troubleshooting