# Ball Out-of-Frame Lockup Fix

**Date:** 2025-10-11  
**Issue:** App locks up when a ball is thrown out of the camera view  
**Status:** ✅ Fixed

## Problem Analysis

When a ball was thrown out of the camera view, the tracking system would enter an infinite loop causing the app to freeze. The root causes were:

1. **Unbounded trajectory growth**: The system continuously added unverified trajectory points without any limit when the ball couldn't be detected
2. **Expensive prediction loop**: The `predictFullTrajectory()` method ran ballistic motion calculations on an increasingly large trajectory dataset
3. **No escape mechanism**: The fallback system only triggered if the ball was within `max_tracker_distance_per_frame` of a hand, leaving balls far out of frame stuck in an invalid state

## Solution Implemented

### 1. Added Tracking Counters to SimpleBall Struct

Added two new fields to track when a ball is lost:
- `frames_without_verified_detection`: Counts frames since last real detection
- `unverified_trajectory_points`: Counts unverified points added to trajectory

**File:** [`engine/include/SimpleBallTracker.hpp`](engine/include/SimpleBallTracker.hpp:107-109)

```cpp
// Lockup prevention (NEW: track frames without verified detection)
int frames_without_verified_detection;  // Counter for frames without real detection
int unverified_trajectory_points;       // Counter for unverified points added
```

### 2. Added Lockup Prevention Logic

Added two safety checks at the start of `updateInFlightBall()`:

**File:** [`engine/src/SimpleBallTracker.cpp`](engine/src/SimpleBallTracker.cpp:1495-1575)

#### Check 1: Frame Timeout (90 frames / ~3 seconds)
If a ball hasn't had a verified detection for 90 frames:
- Force catch to nearest visible hand
- If no hands available, reset trajectory and keep last position

#### Check 2: Unverified Point Limit (30 points / ~1 second)
If trajectory has accumulated more than 30 unverified points:
- Force catch to nearest hand within 3x `max_tracker_distance_per_frame`
- If no suitable hand, reset trajectory

### 3. Counter Management

Counters are properly managed throughout the tracking lifecycle:

- **Reset to 0** when:
  - Ball gets a verified detection (line 1873)
  - Ball is caught (`initiateCatch`, line 2207)
  - Ball is thrown (`initiateThrow`, line 2154)

- **Incremented** when:
  - Ball doesn't have a verified detection (line 1895)

## Testing Recommendations

1. **Basic test**: Throw a ball completely out of frame and verify:
   - App doesn't freeze
   - Ball is caught to nearest hand after ~3 seconds
   - Tracking resumes normally when ball returns

2. **Edge cases**:
   - Ball thrown out with no hands visible
   - Ball thrown very far from hands
   - Multiple balls thrown out simultaneously

3. **Performance**: Verify no performance degradation during normal juggling

## Technical Details

### Constants Used
```cpp
const int MAX_FRAMES_WITHOUT_DETECTION = 90;  // ~3 seconds at 30fps
const int MAX_UNVERIFIED_POINTS = 30;          // ~1 second at 30fps
```

### Fallback Distance Multiplier
When forcing a catch due to unverified point limit, the system uses `3.0f * max_tracker_distance_per_frame` to allow catching balls that are farther away than normal tracking would permit.

## Files Modified

1. [`engine/include/SimpleBallTracker.hpp`](engine/include/SimpleBallTracker.hpp) - Added tracking counters to SimpleBall struct
2. [`engine/src/SimpleBallTracker.cpp`](engine/src/SimpleBallTracker.cpp) - Added lockup prevention logic and counter management

## Build Status

✅ Compiles successfully with no errors (only pre-existing warnings)

## Related Issues

This fix prevents the infinite loop scenario described in the trajectory prediction system when balls leave the camera frame, ensuring the app remains responsive even when tracking is lost.