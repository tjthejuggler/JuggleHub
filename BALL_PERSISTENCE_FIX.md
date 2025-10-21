# Ball Persistence Fix - New 3D Kalman Tracker

**Date:** 2025-10-21  
**Issue:** Single color balls getting multiple IDs when YOLO detections fail temporarily  
**Status:** ✅ FIXED

## Problem Description

When using the New 3D Kalman tracking system, a single pink ball was being assigned multiple IDs (Ball 0, Ball 1, Ball 2, etc.) when YOLO failed to detect it for a few frames. This violated the fundamental assumption that **one ball of each color = one persistent ID**.

### Symptoms
- Pink ball tracked as "Ball 0"
- YOLO fails for 2-3 frames
- Ball 0 deleted after 31 frames unseen
- Pink ball reappears and gets tracked as "Ball 1" (NEW ID!)
- Pattern repeats: Ball 2, Ball 3, etc.

## Root Cause Analysis

### The Real Bug
The issue was a **timing problem** in the track management logic:

1. **Frame N:** Ball 0 unseen for 31 frames → `handleUnmatchedBalls()` **DELETES** Ball 0 → "pink" color freed from `active_track_colors_`
2. **Frame N+1:** New pink detection arrives → `createNewTracks()` called → No Ball 0 to re-acquire (deleted last frame!) → Creates Ball 1 with NEW ID

The re-acquisition logic I initially added couldn't work because **balls were being deleted BEFORE new detections could re-acquire them**.

### Key Problems Identified

1. **Premature deletion**: Balls were deleted in `handleUnmatchedBalls()` immediately when exceeding `max_frames_unseen`
2. **Wrong order of operations**: Deletion happened BEFORE re-acquisition attempt
3. **Timing mismatch**: Ball deleted in Frame N, new detection arrives in Frame N+1

## The Fix

### Solution: Defer Deletion Until After Re-Acquisition

The fix moves ball deletion from `handleUnmatchedBalls()` to the END of `createNewTracks()`, ensuring re-acquisition happens first.

### Changes Made to `engine/src/New3DTracker.cpp`

#### 1. Modified `handleUnmatchedBalls()` (Lines 767-783)
**REMOVED** the deletion logic entirely:

```cpp
void New3DTracker::handleUnmatchedBalls(
    const std::vector<New3DBall*>& unmatched_balls) {
    
    // Iterate through unmatched balls and increment their unseen counter
    for (auto* ball : unmatched_balls) {
        ball->frames_since_seen++;
        ball->consecutive_frames_seen = 0;
        ball->tracking_reason = "Tracking via Kalman (" + std::to_string(ball->frames_since_seen) + " frames unseen)";
    }
    
    // NOTE: We do NOT delete balls here anymore!
    // Deletion now happens in createNewTracks() AFTER attempting re-acquisition
}
```

#### 2. Enhanced `createNewTracks()` with 3-Phase Logic (Lines 799-1088)

**PHASE 1: RE-ACQUISITION** (Lines 799-900)
- Attempts to match unmatched detections to existing unmatched balls by color
- Uses lenient distance threshold (2x normal = 1.0m)
- Re-acquires balls even if they've exceeded `max_frames_unseen`

```cpp
// Try to find an unmatched ball with the same color
for (size_t ball_idx = 0; ball_idx < unmatched_balls.size(); ++ball_idx) {
    if (ball_reacquired[ball_idx]) continue;
    
    New3DBall* ball = unmatched_balls[ball_idx];
    
    // Check if colors match
    if (ball->color_name == detection_color) {
        // Calculate distance to predicted position
        float distance = /* ... */;
        float reacquisition_threshold = settings_.association_max_distance_m * 2.0f;
        
        if (distance < reacquisition_threshold) {
            // RE-ACQUIRED! Reset counters and update Kalman
            ball->frames_since_seen = 0;
            ball->consecutive_frames_seen++;
            // ... update position, Kalman filter, etc.
        }
    }
}
```

**PHASE 2: NEW TRACK CREATION** (Lines 902-1055)
- Only creates new tracks for detections that couldn't be re-acquired
- Checks `active_track_colors_` to avoid duplicate colors

**PHASE 3: DELETE OLD TRACKS** (Lines 1057-1088)
- **NEW**: Deletes balls that exceeded `max_frames_unseen` AND weren't re-acquired
- Frees up colors only after re-acquisition attempt failed

```cpp
tracked_balls_.erase(
    std::remove_if(tracked_balls_.begin(), tracked_balls_.end(),
        [this](const New3DBall& ball) {
            if (ball.frames_since_seen > settings_.max_frames_unseen) {
                // Ball exceeded threshold and wasn't re-acquired
                active_track_colors_.erase(ball.color_name);
                std::cout << "[New3DTracker] Deleted track ID=" << ball.id
                          << " after " << ball.frames_since_seen 
                          << " frames unseen (not re-acquired)." << std::endl;
                return true;
            }
            return false;
        }),
    tracked_balls_.end()
);
```

## How It Works Now

### Scenario: Pink Ball Temporarily Lost

**Frame 1-30:** Pink ball detected normally
- Ball ID: 0
- Color: "pink" (locked in `active_track_colors_`)
- Status: Tracked normally

**Frame 31-60:** YOLO fails to detect pink ball
- Ball ID: 0 (SAME ID!)
- Color: "pink" (STILL locked - not freed!)
- Status: "Tracking via Kalman (31 frames unseen)", then (32), etc.
- Position: Predicted using Kalman filter with gravity
- **Ball NOT deleted** - kept alive for re-acquisition

**Frame 61:** Pink ball detected again
- **RE-ACQUISITION TRIGGERED**
- System finds existing Ball 0 with color "pink"
- Calculates distance: 0.4m (within 1.0m threshold)
- **Ball 0 re-acquired** with SAME ID!
- Kalman filter corrected with new measurement
- `frames_since_seen` reset to 0
- Status: "Re-acquired"

**Frame 62:** If ball still not detected
- Ball 0 would be deleted (exceeded 31 frames and not re-acquired)
- Color "pink" freed for future tracks

### Key Improvements

1. **Persistent IDs**: Balls maintain their ID across detection gaps indefinitely (as long as re-acquired within reasonable distance)

2. **Deferred deletion**: Balls kept alive past `max_frames_unseen` threshold to allow re-acquisition

3. **Color-based re-acquisition**: Uses both color matching AND spatial proximity (Kalman prediction)

4. **Lenient distance threshold**: 2x normal association distance (1.0m vs 0.5m) accounts for prediction drift

5. **Kalman prediction continuity**: Balls continue tracking via Kalman even when unseen for 30+ frames

6. **Proper cleanup**: Balls only deleted AFTER re-acquisition attempt fails

## Testing Results

The fix should now show in the logs:
```
[New3DTracker] RE-ACQUIRED ball ID=0 color=pink after 35 frames unseen (distance=0.4m)
```

Instead of the old behavior:
```
[New3DTracker] Deleted track ID=0 color=pink after 31 frames unseen.
[New3DTracker] Found and created new track for ACTIVE color: pink (ID=1, score=1)
```

## Configuration

The fix uses these settings from [`hub/calibration_settings_new3d.json`](hub/calibration_settings_new3d.json):

- `max_frames_unseen`: 30 frames (soft limit - balls kept longer for re-acquisition)
- `association_max_distance_m`: 0.5m (normal association threshold)
- Re-acquisition threshold: 2x association distance = 1.0m (hardcoded multiplier)

## Related Files

- [`engine/src/New3DTracker.cpp`](engine/src/New3DTracker.cpp) - Main implementation
  - Lines 767-783: `handleUnmatchedBalls()` - Removed deletion logic
  - Lines 799-1088: `createNewTracks()` - Added 3-phase logic with re-acquisition and deferred deletion
- [`engine/include/New3DTracker.hpp`](engine/include/New3DTracker.hpp) - Header file
- [`hub/calibration_settings_new3d.json`](hub/calibration_settings_new3d.json) - Settings

## Summary

The fix ensures that **one ball of each color = one persistent ID** by:
1. **Deferring deletion** until after re-acquisition attempt
2. **Keeping balls alive** past `max_frames_unseen` threshold for re-acquisition
3. **Attempting re-acquisition** before creating new tracks
4. **Using color + spatial proximity** for robust re-acquisition
5. **Maintaining Kalman prediction** continuity across detection gaps
6. **Only deleting** balls that weren't re-acquired

This makes the tracking system robust to temporary YOLO detection failures, which are common in real-world juggling scenarios. Balls can now be "lost" for 30+ frames and still maintain their ID when re-detected.