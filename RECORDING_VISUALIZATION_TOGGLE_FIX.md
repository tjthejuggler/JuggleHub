# Recording Visualization Toggle Fix

**Date:** 2025-10-25  
**Status:** ✅ Fixed

## Overview

Fixed two visualization issues in the recording feature where visualizations were not respecting their toggle states in the UI.

## Issues Fixed

### Issue 1: Hand Threshold Circles Always Showing

**Problem:**
- Hand threshold circles (held range visualization) were being drawn even when the toggle was OFF
- The circles show throw/catch distance thresholds around each hand
- Located at line 2145-2147 in [`Engine.cpp`](engine/src/Engine.cpp:2145)

**Root Cause:**
- The `drawHandThresholds()` function was called unconditionally without checking any visualization toggle state

**Solution:**
- Added conditional check for `viz.show_ball_states()` before drawing hand thresholds
- This makes sense because hand thresholds are related to ball state transitions (held/in-flight)

**Code Change:**
```cpp
// Before:
if (tracker_) {
    tracker_->drawHandThresholds(temp_result, rec_frame.tracked_hands_simple, camera_intrinsics_);
}

// After:
if (viz.show_ball_states() && tracker_) {
    tracker_->drawHandThresholds(temp_result, rec_frame.tracked_hands_simple, camera_intrinsics_);
}
```

### Issue 2: YOLO Color Calibration Squares Not Showing

**Problem:**
- YOLO detection color squares (8x8 colored squares at detection corners) were not visible even when toggle was ON
- These squares show which color profile each YOLO detection matches
- Located at lines 1519-1615 in [`Engine.cpp`](engine/src/Engine.cpp:1519)

**Root Cause:**
- The color squares were being drawn EARLY in the rendering pipeline (after raw detections)
- They were being covered up by other visualizations drawn later (hand tracking, trajectories, etc.)
- There was even a comment saying "MOVED: YOLO Color Calibration Squares moved to end of function" but the code was still at the beginning!

**Solution:**
- Moved the entire YOLO color calibration drawing code to the END of the `renderVisualizationsOnFrame()` function
- Now draws on the final `result` image (which includes the text panel) instead of `temp_result`
- This ensures the colored squares appear on top of all other visualizations

**Code Change:**
- Removed lines 1517-1615 (color square drawing code)
- Added the same code at the end of the function (before the final return statement)
- Changed drawing target from `temp_result` to `result` to draw on the final composite image

## Technical Details

### Visualization Toggle: `show_yolo_color_calibration`

From [`juggler.proto`](api/v1/juggler.proto:351):
```protobuf
bool show_yolo_color_calibration = 17;  // Show 8x8 colored squares for YOLO color calibration
```

### Visualization Toggle: `show_ball_states`

From [`juggler.proto`](api/v1/juggler.proto:341):
```protobuf
bool show_ball_states = 7;
```

## Files Modified

- [`engine/src/Engine.cpp`](engine/src/Engine.cpp) - Fixed both visualization issues

## Testing

The fixes ensure that:
1. ✅ Hand threshold circles only appear when "Ball States" toggle is enabled
2. ✅ YOLO color calibration squares appear on top of all other visualizations when their toggle is enabled
3. ✅ Both visualizations respect their toggle states correctly

## Benefits

1. **Correct Toggle Behavior**: Visualizations now properly respect their UI toggle states
2. **Better Visibility**: Color squares are now visible on top of other visualizations
3. **Cleaner Recordings**: Users can control exactly what appears in their recordings
4. **Consistent UX**: Toggle behavior matches user expectations

## Related Documentation

- [`RECORDING_VISUALIZATION_IMPLEMENTATION.md`](RECORDING_VISUALIZATION_IMPLEMENTATION.md) - Original recording visualization feature
- [`RECORDING_LOG_VISUALIZATION_FILTERING.md`](RECORDING_LOG_VISUALIZATION_FILTERING.md) - Log filtering based on toggles