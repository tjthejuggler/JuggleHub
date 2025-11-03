# Color Search Region Fix Documentation

**Date**: 2025-11-03  
**Author**: Roo (AI Assistant)  
**Status**: Completed

---

## Table of Contents

1. [Problem Summary](#problem-summary)
2. [Root Causes](#root-causes)
3. [Solution Overview](#solution-overview)
4. [Implementation Details](#implementation-details)
5. [Testing Recommendations](#testing-recommendations)
6. [Files Modified](#files-modified)

---

## Problem Summary

The color search region visualization system had **4 critical bugs** that caused incorrect behavior when tracking balls:

1. **Search regions used predicted positions instead of actual detections** - The color search region was centered on `predicted_position` (Kalman filter prediction) rather than `last_detection_position` (actual detection), causing the search region to drift away from where the ball was actually seen.

2. **Search regions disappeared for lost balls** - When a ball was lost (not detected for several frames), the search region would disappear entirely, making it impossible to see where the tracker was searching for the ball.

3. **Stale Kalman filter velocities after re-acquisition** - When a ball was re-acquired after being lost, the old velocity estimates in the Kalman filter caused immediate divergence, making tracking unstable.

4. **Search region color didn't indicate lost state** - The visualization used the ball's color profile color even when the ball was lost, providing no visual feedback about tracking quality.

These issues were particularly evident in frames 933-937 of the debug logs, where balls would be lost and the search regions would behave incorrectly.

---

## Root Causes

### 1. Search Region Position Bug

**Location**: `engine/src/New3DTracker.cpp:2699-2701`

**Problem**: The visualization code was using `ball.predicted_position` to center the search region instead of the last actual detection position.

**Why this is wrong**: 
- `predicted_position` is updated every frame by the Kalman filter, even when the ball isn't detected
- This causes the search region to "chase" the prediction, which may be wrong
- The search region should stay anchored to where the ball was **actually seen**

### 2. Search Region Visibility Bug

**Location**: `engine/src/New3DTracker.cpp:2697-2698`

**Problem**: The condition for showing search regions was too restrictive - only showing for balls unseen for less than 5 frames.

**Why this is wrong**:
- Search regions would disappear after 5 frames of not seeing the ball
- This is exactly when you NEED to see the search region most
- Makes debugging tracking issues impossible

### 3. Kalman Filter State Bug

**Location**: `engine/src/New3DTracker.cpp:1376-1378` (before fix)

**Problem**: When re-acquiring a lost ball, the code would update positions but not reset the Kalman filter.

**Why this is wrong**:
- The Kalman filter still has old velocity estimates from before the ball was lost
- These stale velocities cause the prediction to immediately diverge
- The filter needs to be reset to start fresh with zero velocities

### 4. `last_detection_position` Not Updated Correctly

**Location**: `engine/src/New3DTracker.cpp:1485-1488`

**Problem**: In `finalizeBallPositions()`, the code was updating `last_known_position` but not properly managing `last_detection_position`.

**Why this is wrong**:
- For IN_FLIGHT balls without detections, `last_known_position` gets the prediction
- But `last_detection_position` should ONLY be updated when we get an actual detection
- This ensures the search region stays anchored to real detections

---

## Solution Overview

The fix implements a **detection-anchored search region system** with the following principles:

1. **New field**: Added `last_detection_position` to `New3DBall` structure
2. **Detection-only updates**: `last_detection_position` is ONLY updated when we get an actual detection
3. **Always visible**: Search regions are shown for ALL IN_FLIGHT balls, regardless of `frames_since_seen`
4. **Visual feedback**: Search region color changes to RED when ball has been lost for ≥5 frames
5. **Kalman reset**: Kalman filter is reset (velocities zeroed) when re-acquiring a lost ball
6. **Off-screen clamping**: Search regions are clamped to frame bounds so they remain visible

---

## Implementation Details

### Fix 1: Add `last_detection_position` Field

**File**: `engine/include/New3DTracker.hpp:42`

Added new field to `New3DBall` structure to track the last actual detection position separately from predictions.

**Initialization**: `engine/src/New3DTracker.cpp:100`

### Fix 2: Update `last_detection_position` on Detection

**File**: `engine/src/New3DTracker.cpp:1062-1069`

In `handleInFlightStateUpdate()`, both `last_known_position` and `last_detection_position` are updated when we get an actual detection. This ensures the color search region stays centered on real detections, not predictions.

### Fix 3: Don't Update `last_detection_position` on Prediction

**File**: `engine/src/New3DTracker.cpp:1485-1495`

In `finalizeBallPositions()`, explicitly documented why we DON'T update `last_detection_position` for IN_FLIGHT balls. The field should ONLY be updated when we get an actual detection, ensuring the color search region stays anchored to where the ball was actually seen.

### Fix 4: Reset Kalman Filter on Re-acquisition

**File**: `engine/src/New3DTracker.cpp:1353-1378`

In `createNewTracks()`, when re-acquiring a lost ball:
- Zero out velocities (vx, vy, vz) to start fresh
- Reset error covariance to initial uncertainty
- Then update with new measurement
- Update all three position fields: `last_known_position`, `predicted_position`, and `last_detection_position`

This prevents stale velocity estimates from causing immediate divergence after re-acquisition.

### Fix 5: Always Show Search Region for IN_FLIGHT Balls

**File**: `engine/src/New3DTracker.cpp:2697-2707`

Simplified condition to show search regions for ALL IN_FLIGHT balls with valid `last_detection_position`, regardless of `frames_since_seen`. Uses `last_detection_position` instead of `predicted_position` for the search center.

### Fix 6: Visual Feedback for Lost Balls

**File**: `engine/src/New3DTracker.cpp:2731-2737`

Search region color changes based on tracking quality:
- Normal ball color when `frames_since_seen < 5`
- RED when `frames_since_seen >= 5` (lost state)

This provides immediate visual feedback about tracking quality.

### Fix 7: Clamp Search Region to Frame Bounds

**File**: `engine/src/New3DTracker.cpp:2712-2718`

Clamps the search region position to frame bounds before visualization, ensuring the search region remains visible even when the ball goes off-screen. An "OFF-SCREEN" indicator is added to the label when clamping occurs.

**Helper Function**: `engine/src/New3DTracker.cpp:2010-2039`

The `clampToFieldOfView()` function projects the 3D position to 2D, clamps to frame bounds with a 10-pixel margin, then back-projects to 3D at the same depth.

---

## Testing Recommendations

### 1. Visual Verification

Enable color search region visualization:
```cpp
settings_.show_color_search_region = true;
```

**Expected behavior**:
- Search regions appear for all IN_FLIGHT balls
- Search regions stay centered on last detection, not drift with predictions
- Search regions turn RED when ball is lost for ≥5 frames
- Search regions remain visible even when ball goes off-screen (clamped to edge)

### 2. Re-acquisition Testing

Temporarily occlude a ball, then bring it back into view.

**Expected behavior**:
- Ball should be re-acquired smoothly without divergence
- Kalman filter should start fresh with zero velocities
- Search region should immediately update to new detection position

### 3. Log Analysis

Review debug logs for frames where balls are lost and re-acquired.

**Look for**:
- `last_detection_position` updates only on actual detections
- `last_known_position` updates every frame (prediction or detection)
- Kalman filter reset messages on re-acquisition
- Search region position logs showing correct anchoring

### 4. Edge Cases

Test scenarios:
1. Ball goes completely off-screen → Search region should clamp to edge
2. Ball is lost for >30 frames → Search region should stay RED and visible
3. Multiple balls lost simultaneously → Each should have independent search regions
4. Ball re-acquired near hand → Should transition to HELD correctly

---

## Files Modified

### Header Files

1. **`engine/include/New3DTracker.hpp`**
   - Line 42: Added `last_detection_position` field to `New3DBall` struct
   - Lines 75-106: Updated copy constructor to handle new field
   - Lines 108-122: Updated move constructor to handle new field
   - Lines 124-162: Updated assignment operator to handle new field

### Implementation Files

2. **`engine/src/New3DTracker.cpp`**
   - Line 100: Initialize `last_detection_position` in `initializePersistentBalls()`
   - Lines 1062-1069: Update `last_detection_position` in `handleInFlightStateUpdate()`
   - Lines 1353-1378: Reset Kalman filter on re-acquisition in `createNewTracks()`
   - Lines 1485-1495: Document why NOT to update `last_detection_position` in `finalizeBallPositions()`
   - Lines 2010-2039: Add `clampToFieldOfView()` helper function
   - Lines 2697-2763: Fix search region visualization in `drawHandThresholds()`

### Settings Files

3. **`engine/src/New3DTracker.cpp`** (Settings)
   - Lines 507-509: Load `show_color_search_region` setting
   - Lines 609-610: Save `show_color_search_region` setting
   - Lines 2922-2923: Handle `show_color_search_region` in `updateSetting()`

---

## Summary

This fix implements a robust **detection-anchored search region system** that:

1. ✅ Keeps search regions centered on actual detections, not predictions
2. ✅ Shows search regions for all IN_FLIGHT balls, even when lost
3. ✅ Resets Kalman filter on re-acquisition for stable tracking
4. ✅ Provides visual feedback (RED color) when balls are lost
5. ✅ Clamps search regions to frame bounds for visibility

The fix addresses all 4 critical bugs and provides a much more intuitive and debuggable tracking visualization system. The behavior seen in frames 933-937 of the logs should now be correct, with search regions staying anchored to last detections and remaining visible throughout the tracking process.

---

**End of Documentation**