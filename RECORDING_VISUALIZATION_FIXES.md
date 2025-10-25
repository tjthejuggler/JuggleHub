# Recording Visualization Fixes

**Date:** 2025-10-25  
**Issue:** Two visualization problems in recording playback system

## Problems Identified

### Issue 1: Hand Threshold Circles Showing When Toggle is Off
**Symptom:** Hand threshold circles (held range visualization) were appearing in recorded visualization images even when the `show_ball_states` toggle was disabled.

**Root Cause:** The code in [`renderVisualizationsOnFrame()`](engine/src/Engine.cpp:2139) was calling `drawHandThresholds()` without checking if the visualization toggle was enabled.

**Fix:** Added conditional check before drawing hand thresholds:
```cpp
// Only draw if ball_states visualization is enabled
if (viz.show_ball_states() && tracker_) {
    tracker_->drawHandThresholds(temp_result, rec_frame.tracked_hands_simple, camera_intrinsics_);
}
```

### Issue 2: YOLO Detection Color Squares Not Showing When Toggle is On
**Symptom:** When the `yolo_color_calibration` toggle was enabled, the 8x8 colored squares that should appear at the upper-left corner of each YOLO detection were not being rendered in the recorded visualization images.

**Root Cause:** The code was loading color profiles from `hub/config/color_profiles.json`, but the JSON structure has a `"profiles"` array wrapper:
```json
{
  "profiles": [
    {"name": "pink", "rgb": [233, 30, 99], "enabled": true, ...},
    ...
  ]
}
```

However, the code was iterating directly over the top-level JSON object instead of accessing the `"profiles"` array first:
```cpp
// WRONG - iterates over object keys, not profiles
for (const auto& profile : color_profiles_json) {
```

This caused the color profile loading to fail silently, resulting in an empty `color_profiles` vector and no color squares being drawn.

**Fix:** Updated all 5 locations in [`Engine.cpp`](engine/src/Engine.cpp) where color profiles are loaded to properly access the `"profiles"` array:

1. **Line 1409-1428** - Raw detections color distance calculation
2. **Line 1524-1549** - YOLO color calibration squares (main fix)
3. **Line 1615-1636** - Filtered detections color distance calculation
4. **Line 1762-1774** - Color tracker visualization color map
5. **Line 1896-1908** - Final tracker visualization color map

Changed pattern:
```cpp
// CORRECT - access the "profiles" array first
if (color_profiles_json.contains("profiles") && color_profiles_json["profiles"].is_array()) {
    for (const auto& profile : color_profiles_json["profiles"]) {
        // ... process profile
    }
}
```

## Files Modified

- [`engine/src/Engine.cpp`](engine/src/Engine.cpp) - Fixed hand threshold conditional and color profile JSON parsing (5 locations)

## Testing

After compilation, the fixes should result in:
1. Hand threshold circles only appearing when `show_ball_states` toggle is enabled
2. YOLO detection color squares (8x8 pixels) appearing at the upper-left corner of each detection when `yolo_color_calibration` toggle is enabled

## Technical Details

The YOLO color calibration visualization works by:
1. Loading enabled color profiles from `hub/config/color_profiles.json`
2. For each YOLO detection, sampling the color at the detection center
3. Converting BGR to HSV and calculating Euclidean distance to each color profile
4. Finding the closest matching color profile
5. Drawing an 8x8 solid square in that color at the upper-left corner of the detection bounding box
6. Adding a 1px black border around the square for visibility

The hand threshold visualization shows:
- Orange circles for throw distance threshold
- Green circles for catch distance threshold
- Only drawn when `show_ball_states` toggle is enabled