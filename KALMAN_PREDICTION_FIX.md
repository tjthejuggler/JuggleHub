# Kalman Prediction Snap Bug Fix

**Date:** 2025-10-06  
**Issue:** Kalman prediction circle (P0) was snapping to incorrect positions, moving right when ball was clearly moving up and left

## Root Cause

The color-based predictor's history was becoming stale during certain tracking fallback modes. The predictor uses a deque of recent ball positions to calculate velocity and predict the next position. However, the history was only being updated when YOLO detections were found (line 980).

When the system fell back to alternative tracking methods, the history was NOT updated:
- ✅ Color tracking near hands (line 1046) - WAS updating
- ✅ Color tracking at Kalman prediction (line 1088) - WAS updating  
- ✅ Kalman-only prediction for recent tracking (line 1108) - WAS updating
- ❌ **Trajectory-based flight prediction (line 1188)** - NOT updating ← **BUG**

## The Problem

When the ball had been without YOLO detection for 5+ frames, the code entered trajectory-based validation (lines 1137-1189). This section:

1. Checks if the Kalman-predicted trajectory leads toward a hand
2. If YES: Snaps to hand position (correctly does NOT update predictor - hand positions corrupt velocity)
3. If NO: Uses Kalman prediction for free flight ← **This path was missing the predictor update!**

Without updating the predictor history during free flight, the velocity calculation became stale. The predictor would then use old velocity data from many frames ago, causing the prediction circle to appear in completely wrong locations.

## The Fix

Added color predictor update in the trajectory-based free flight path:

```cpp
// Line 1188 - Trajectory does NOT lead to any hand - ball is in free flight
ball.position = predicted_pos;
ball.yolo_class_id = 0;  // Mark as in-air
ball.held_by_hand_id = -1;
ball.tracking_reason = "Traj→Flight";

// CRITICAL: Update color predictor with flight prediction
// This keeps the history active and velocity calculation accurate
ball.color_predictor.addDetection(ball.position);
```

## Why This Matters

The color-based predictor is the PRIMARY prediction system shown to the user as the P0 circle. It needs continuous history updates to:

1. Calculate accurate velocity from recent positions
2. Apply gravity to predict parabolic trajectories
3. Show where the ball will be in the next frame

When history goes stale, the predictor uses old velocity that no longer matches the ball's current motion, causing the prediction to "snap" to incorrect locations.

## Testing

After this fix, the prediction circle should:
- ✅ Smoothly follow the ball's trajectory during free flight
- ✅ Accurately predict parabolic motion with gravity
- ✅ Never snap to incorrect positions
- ✅ Maintain velocity continuity across all tracking modes

## Related Files

- [`engine/src/SimpleBallTracker.cpp`](engine/src/SimpleBallTracker.cpp:1188) - Fixed trajectory-based flight prediction
- [`engine/include/ColorBasedPredictor.hpp`](engine/include/ColorBasedPredictor.hpp) - Prediction system interface
- [`KALMAN_PREDICTION_SOLUTIONS.md`](KALMAN_PREDICTION_SOLUTIONS.md) - Previous investigation notes

## History Update Locations

All locations where `ball.color_predictor.addDetection()` is called:

1. Line 980: YOLO detection found
2. Line 1046: Color blob found near hand
3. Line 1088: Color blob found at Kalman prediction
4. Line 1108: Kalman-only prediction (recent tracking)
5. **Line 1193: Trajectory-based free flight** ← NEW FIX

Note: Hand snap positions (line 1066) are explicitly NOT added to avoid corrupting velocity calculations.