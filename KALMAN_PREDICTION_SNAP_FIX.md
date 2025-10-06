# Kalman Prediction Sporadic Snapping Fix

**Date**: 2025-10-06  
**Issue**: Frames 203-204 showed sporadic snapping where the ball tracker jumped to incorrect positions despite YOLO detections being available and the color tracker being correctly positioned.

## Root Cause Analysis

### The Problem Chain

1. **Hand Position Corruption** (Primary Issue)
   - When the ball was near a hand without a YOLO detection, the code would "snap" the tracker to the hand's wrist position
   - This hand position was then **fed into the Kalman filter as a measurement** (lines 1117-1118)
   - Hand wrist positions are NOT accurate ball positions - they can be 10-20cm off
   - This corrupted the Kalman filter's internal state (position and velocity estimates)

2. **Prediction Rejection Loop** (Secondary Issue)
   - The corrupted Kalman state caused predictions to be far from the actual ball position
   - When the ball was thrown, the Kalman prediction was 0.64m+ away from reality
   - ALL valid YOLO detections were rejected because they were outside the `prediction_radius_m` (0.50m)
   - Without YOLO corrections, the Kalman continued using its corrupted state

3. **Self-Reinforcing Error** (Tertiary Issue)
   - When no YOLO detection was accepted, the code used the Kalman prediction
   - It then **updated the Kalman filter with its own prediction** (line 1160-1161)
   - This created a circular reasoning loop where errors compounded over time

### Why It Manifested in Frames 203-204

Looking at the debug output:
```
History: last=(-0.58,-0.07,1.46) prev=(-0.58,-0.07,1.46)
Pred: pos=(-0.57,-0.08,1.46)
```

But the actual YOLO detections were at:
```
Det#0: pos=(-0.70, 0.18, 1.49) - REJECTED: Dist 0.64m>0.50m
Det#1: pos=(-0.69, 0.08, 1.49) - REJECTED: Dist 0.68m>0.50m
```

The Kalman filter's history showed it was tracking at (-0.58, -0.07, 1.46), which was **not** where the ball actually was. This incorrect state came from previous frames where hand positions were fed into the Kalman filter.

## The Fixes

### Fix 1: Stop Updating Kalman with Hand Positions

**Location**: Lines 1105-1122 and 1221-1237

**Before**:
```cpp
// Snap to hand position
ball.position = closest_hand_pos;
// Update Kalman with hand position
ball.kalman.update(KalmanFilter3D::MeasurementVector(
    closest_hand_pos.x, closest_hand_pos.y, closest_hand_pos.z));
```

**After**:
```cpp
// Snap to hand position
ball.position = closest_hand_pos;
// CRITICAL FIX: DO NOT update Kalman with hand snap positions!
// Hand positions are not accurate ball positions and corrupt the Kalman state
// Only update Kalman with real YOLO or color detections
```

**Rationale**: Hand wrist positions are approximations, not precise ball measurements. Feeding them into the Kalman filter corrupts its state and causes bad predictions when the ball is thrown.

### Fix 2: Stop Updating Kalman with Its Own Predictions

**Location**: Lines 1149-1162

**Before**:
```cpp
// Use Kalman prediction
ball.position = cv::Point3f(state(0), state(1), state(2));
// Update Kalman filter with this predicted position
ball.kalman.update(KalmanFilter3D::MeasurementVector(
    ball.position.x, ball.position.y, ball.position.z));
```

**After**:
```cpp
// Use Kalman prediction for display
ball.position = cv::Point3f(state(0), state(1), state(2));
// DO NOT update Kalman with its own prediction - this causes drift!
// The Kalman filter should only be updated with real measurements
```

**Rationale**: A Kalman filter should only be updated with real measurements, not its own predictions. Updating with predictions creates circular reasoning and causes errors to compound.

### Fix 3: Always Calculate Kalman Predictions

**Location**: Line 936

**Before**:
```cpp
if (ball.frames_without_yolo < 5) {
    // Calculate prediction
}
```

**After**:
```cpp
if (ball.kalman.get_state()(2) > 0.01f) {  // Only if initialized
    // Calculate prediction
}
```

**Rationale**: The Kalman filter should always predict when initialized. The prediction quality naturally degrades over time through process noise (Q matrix), but it should never be completely disabled. Disabling predictions causes loss of trajectory tracking.

## Expected Behavior After Fix

1. **Kalman filter only learns from real measurements**:
   - YOLO detections (when available and within prediction radius)
   - Color blob detections (when found at predicted or hand locations)
   - Never from hand snap positions
   - Never from its own predictions

2. **Prediction radius enforcement**:
   - YOLO detections outside the prediction radius are rejected
   - This prevents the tracker from jumping to distant false positives
   - The radius naturally grows over time as prediction uncertainty increases

3. **Graceful degradation**:
   - When no measurements are available, the Kalman prediction is used for display
   - The prediction continues following the last known trajectory (physics-based)
   - No circular updates that compound errors

4. **Hand association**:
   - Ball position can snap to hands for display purposes
   - But these snap positions don't corrupt the Kalman filter
   - When the ball is thrown, the Kalman prediction is based on real measurements only

## Testing Recommendations

1. Test with balls held in hands for extended periods (100+ frames)
2. Verify that after a throw, the Kalman prediction follows the expected parabolic trajectory
3. Check that YOLO detections are accepted when they're within the prediction radius
4. Confirm that the tracker doesn't snap to incorrect positions when valid detections exist

## Related Files

- [`engine/src/SimpleBallTracker.cpp`](engine/src/SimpleBallTracker.cpp) - Main tracking logic
- [`engine/src/KalmanFilter3D.cpp`](engine/src/KalmanFilter3D.cpp) - Kalman filter implementation
- [`engine/include/SimpleBallTracker.hpp`](engine/include/SimpleBallTracker.hpp) - Tracker interface

## Technical Notes

### Kalman Filter Measurement Policy

The Kalman filter should follow this strict policy:

**DO update with**:
- ✅ YOLO detections (high confidence, within prediction radius)
- ✅ Color blob detections (found at predicted location)
- ✅ Color blob detections (found near hands, when prediction is near hand)

**DO NOT update with**:
- ❌ Hand wrist positions (approximations, not measurements)
- ❌ Kalman's own predictions (circular reasoning)
- ❌ Interpolated or estimated positions (not real measurements)

### Process Noise Tuning

The Kalman filter's process noise (Q matrix) controls how much the filter trusts its model vs. measurements:
- Higher Q = less trust in model, more responsive to measurements
- Lower Q = more trust in model, smoother but slower to adapt

Current settings (from `KalmanFilter3D.cpp`):
```cpp
Q_ = Eigen::Matrix<float, 6, 6>::Identity() * 0.01;
Q_.topLeftCorner<3, 3>() *= 10.0;  // Higher uncertainty for position
```

This gives position uncertainty of 0.1 and velocity uncertainty of 0.01, which is reasonable for juggling ball tracking.