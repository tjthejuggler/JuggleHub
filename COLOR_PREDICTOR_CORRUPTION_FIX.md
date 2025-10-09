# Color Predictor Velocity Corruption - Complete Fix

**Date:** 2025-10-09  
**Issue:** Snapped positions entering color predictor, creating unrealistic velocities (4.6-28.9 m/s)

## Root Cause Analysis

The color predictor tracks position history to calculate velocities for Kalman predictions. When a ball "snaps" to a hand position (teleportation), this creates a large position jump that gets interpreted as extremely high velocity.

### Evidence from Logs
- Frame 120: Pink ball velocity 4.6 m/s (position jumped 0.42m in 29ms)
- Frame 123: Yellow ball velocity 5.9 m/s (position jumped 1.6m in 99ms)  
- Frame 129: Yellow ball velocity 28.9 m/s (completely unrealistic)

### Corruption Sources Identified

1. **Direct Snapping** (FIXED in previous commits)
   - Lines 2014, 2138, 2170, 2291, 2352, 2684: Catch inference and hand snapping
   - All `addDetection()` calls after snapping were already removed

2. **Snap→Color Blob Transition** (FIXED in this commit)
   - **Lines 2637-2641**: When held ball finds color blob near hand
   - **Problem**: Ball snaps to wrist in frame N (no addDetection), then finds color blob in frame N+1
   - **Result**: Large jump from wrist→color blob creates velocity spike
   - **Solution**: Added stricter MAX_POSITION_JUMP validation (0.15m vs 0.5m)

## Complete Fix Implementation

### 1. All Snap Locations - No Color Predictor Updates
These locations set `ball.tracking_reason = "Held_Snap@Wrist"` or similar:
- Line 2672: Main held ball snap to wrist - `addDetection()` REMOVED ✓
- Line 2338: Snap during catch inference - `addDetection()` REMOVED ✓  
- Line 2160: Proximity-based catch - `addDetection()` REMOVED ✓
- Line 2002: Swipe-through catch (visible) - `addDetection()` REMOVED ✓
- Line 2128: Swipe-through catch (vanished) - `addDetection()` REMOVED ✓

### 2. Held Ball Color Blob Detection - Strict Validation
**Lines 2630-2652** (NEW FIX):
```cpp
// CRITICAL: Validate before adding to color predictor
// EXTRA STRICT for held balls to prevent snap→color blob jumps
if (ball.color_predictor.getHistorySize() > 0) {
    auto history = ball.color_predictor.getHistory();
    cv::Point3f last_pos = history.back().position;
    float distance = cv::norm(color_pos - last_pos);
    // STRICTER threshold for held balls (0.15m vs 0.5m)
    // This prevents snap→color blob velocity corruption
    const float MAX_POSITION_JUMP = 0.15f;
    
    if (distance < MAX_POSITION_JUMP) {
        ball.color_predictor.addDetection(color_pos);
    } else {
        // Log rejection for debugging
    }
}
```

### 3. Valid Color Predictor Updates (Unchanged)
These locations correctly add real ball motion:
- Line 1629: YOLO detection (euclidean match) ✓
- Line 1796: YOLO detection (override) ✓
- Line 2248: Color blob near hand (validated) ✓
- Line 2428: Kalman glob detection (validated) ✓
- Line 2498: Kalman prediction (validated, < 0.5m jump) ✓
- Line 2509: First position (no history to validate) ✓

## Expected Results

After this fix, velocities should be realistic:
- **Normal juggling**: < 5 m/s
- **Fast throws**: 5-8 m/s  
- **Maximum**: Never exceed 8 m/s (clamped by Kalman)

### Validation Thresholds
- **In-air balls**: 0.5m max position jump per frame
- **Held balls**: 0.15m max position jump per frame (stricter)
- **Kalman velocity**: 8.0 m/s maximum (clamped before prediction)

## Testing Recommendations

1. **Monitor velocity logs**: Check that velocities stay < 5 m/s for normal juggling
2. **Watch for rejection logs**: `[HELD_COLOR_JUMP_REJECT]` indicates snap→color blob transitions being caught
3. **Verify tracking quality**: Ensure balls still track smoothly despite stricter validation

## Technical Details

### Why 0.15m for Held Balls?
- Held balls should move slowly with the hand (< 2 m/s typical hand motion)
- At 30 FPS: 2 m/s = 0.067m per frame
- 0.15m threshold = ~4.5 m/s, allowing for fast hand movements
- Prevents snap→color blob jumps (typically 0.3-1.6m) from corrupting velocity

### Color Predictor Velocity Calculation
The `ColorBasedPredictor` calculates velocity from position history:
```cpp
velocity = (current_pos - prev_pos) / time_delta
```
If `current_pos` is from a snap and `prev_pos` is from real motion, the velocity becomes unrealistic.

## Related Files
- `engine/src/SimpleBallTracker.cpp`: Main tracking logic
- `engine/include/SimpleBallTracker.hpp`: Tracker interface
- `ColorBasedPredictor.hpp`: Velocity calculation (not modified)

## Commit History
1. Initial fix: Removed `addDetection()` from catch inference and snap locations
2. This fix: Added strict validation for held ball color blob detection