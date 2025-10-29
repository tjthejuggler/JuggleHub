# Wrist Snap Velocity Corruption Fix

**Date**: 2025-10-09  
**Issue**: Kalman predictor jumping to wrong hand position due to velocity corruption from wrist snapping

## Problem Description

When a ball lost YOLO detection and the system snapped it to a wrist position, the large position jump (teleportation) was being added to the color predictor history. This created impossible velocities (e.g., 19 m/s) that corrupted the Kalman filter's velocity estimates, causing phantom predictions in wrong locations.

### Example from User's Log (Frame 99-100):
- **Frame 98→99**: Ball snapped from `(-0.328, -0.121, 1.574)` to LEFT hand `(-0.162, 0.373, 1.652)`
  - Jump distance: ~0.50m in one frame
  - Calculated velocity: **19.03 m/s** (impossible for juggling!)
- **Frame 100**: Kalman prediction corrupted, showing phantom position at wrong Z-depth

## Root Cause

Two locations in the code were adding snapped positions to the color predictor:

1. **Lines 2033-2056**: Fallback wrist snapping (Priority 3 fallback)
2. **Lines 2241-2266**: Held ball wrist snapping (Priority 2 for held balls)

Both were calling:
```cpp
ball.color_predictor.addDetection(ball.position);  // ← PROBLEM!
```

This added teleportation events to the history, creating impossible velocity calculations that fed into Kalman predictions.

## Solution Applied

### Fix 1: Fallback Wrist Snapping (Lines 2033-2056)

**Changes**:
1. **Reset Kalman velocity to zero** when snapping (ball is held, not moving)
2. **Remove color predictor update** (don't add teleportation to history)
3. **Update debug logging** to indicate velocity reset

```cpp
// CRITICAL FIX: Reset Kalman velocity when snapping
auto& state = ball.kalman.get_state();
state(3) = 0.0f;  // vx = 0
state(4) = 0.0f;  // vy = 0
state(5) = 0.0f;  // vz = 0

// CRITICAL FIX: DO NOT add snapped positions to color predictor!
// ball.color_predictor.addDetection(ball.position);  // REMOVED
```

### Fix 2: Held Ball Wrist Snapping (Lines 2241-2266)

**Changes**: Same as Fix 1
1. Reset Kalman velocity to zero
2. Remove color predictor update
3. Update debug logging

## Expected Behavior After Fix

1. **No velocity corruption**: Wrist snapping no longer creates impossible velocities
2. **Stable Kalman predictions**: Velocity stays at zero when ball is held
3. **Clean history**: Color predictor history only contains real motion, not teleportation
4. **Correct tracking**: Ball stays at hand position until YOLO recovers, without phantom predictions

## Testing Recommendations

1. Test scenario where ball loses YOLO detection while in hand
2. Verify Kalman predictions stay at hand position (not jumping elsewhere)
3. Check velocity estimates remain realistic (< 10 m/s for juggling)
4. Confirm no phantom positions appear in color predictor history

## Related Code Locations

- **SimpleBallTracker.cpp:2033-2056**: Fallback wrist snapping
- **SimpleBallTracker.cpp:2241-2266**: Held ball wrist snapping
- **ColorBasedPredictor**: Velocity calculation from history (not modified)
- **KalmanFilter3D**: State vector includes velocity components (not modified)

## Notes

The comment in the original code said "DO NOT update Kalman with hand snap positions!" but then proceeded to update the color predictor, which indirectly affects Kalman through velocity estimates. This fix ensures both Kalman and color predictor are handled correctly during wrist snapping.