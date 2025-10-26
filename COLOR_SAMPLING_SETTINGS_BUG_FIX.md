# Color Sampling Settings Bug Fix

**Date:** 2025-10-26  
**Issue:** Min Saturation Threshold setting not persisting or loading from JSON file

## Problem Description

The user reported that the color sampling settings (`color_sample_radius` and `min_saturation_threshold`) were not affecting the YOLO color square visualizations, even when set to extreme values like 255.

### Root Cause

The `min_saturation_threshold` setting was **missing from the JSON load/save functions** in [`New3DTracker.cpp`](engine/src/New3DTracker.cpp). This caused:

1. **Default value always used**: The setting defaulted to 50 (from header file) on every engine start
2. **UDP updates not persisted**: When changed via UI, the setting updated in memory but was never saved to JSON
3. **Settings reload reverted changes**: Any restart or settings reload would revert to the default value of 50

### Evidence

- **Line 191** in [`New3DTracker.hpp`](engine/include/New3DTracker.hpp): Default value is `50`
- **Lines 364-368** in [`New3DTracker.cpp`](engine/src/New3DTracker.cpp): `loadSettings()` loaded `color_sample_radius` but NOT `min_saturation_threshold`
- **Lines 454-458** in [`New3DTracker.cpp`](engine/src/New3DTracker.cpp): `saveSettings()` saved `color_sample_radius` but NOT `min_saturation_threshold`
- **Line 2269-2270** in [`New3DTracker.cpp`](engine/src/New3DTracker.cpp): UDP handler WAS present and working

## Solution

Added the missing load and save code for `min_saturation_threshold`:

### 1. Added to `loadSettings()` (after line 366)

```cpp
if (j.contains("min_saturation_threshold")) {
    settings_.min_saturation_threshold = j["min_saturation_threshold"];
}
```

### 2. Added to `saveSettings()` (after line 456)

```cpp
j["min_saturation_threshold"] = settings_.min_saturation_threshold;
```

## Files Modified

- [`engine/src/New3DTracker.cpp`](engine/src/New3DTracker.cpp:367-369) - Added load code
- [`engine/src/New3DTracker.cpp`](engine/src/New3DTracker.cpp:457) - Added save code

## Testing Instructions

1. **Restart the engine** to load the fix
2. Set **Min Saturation Threshold = 255** in UI
3. **Expected result**: YOLO color squares should turn **BLACK** (no pixels pass the saturation filter)
4. Set **Min Saturation Threshold = 0** in UI
5. **Expected result**: YOLO color squares should show **actual ball colors** (all pixels pass)
6. **Restart the engine** and verify the setting persists

## Technical Details

### How the Setting Works

The `min_saturation_threshold` (0-255) filters out low-saturation pixels when sampling ball colors:

```cpp
// Line 1444 in New3DTracker.cpp
if (hsv[1] > settings_.min_saturation_threshold) {
    bgr_samples.push_back(color_roi.at<cv::Vec3b>(y, x));
}
```

- **Low values (0-50)**: Include most pixels, even grays/whites
- **High values (200-255)**: Only include highly saturated (colorful) pixels
- **Value 255**: Excludes ALL pixels → returns black (0,0,0)

### Why This Matters

This setting is crucial for robust color detection because:
- **Filters out lighting variations**: Gray/white pixels vary most with lighting changes
- **Improves color stability**: Only uses saturated pixels for more consistent color matching
- **Debugging tool**: Setting to 255 immediately shows if the setting is connected (should see black squares)

## Related Code

- [`sampleDetectedColor()`](engine/src/New3DTracker.cpp:1406-1474) - Uses the threshold for color sampling
- [`matchColor()`](engine/src/New3DTracker.cpp:1290-1404) - Uses the threshold for color matching
- [`updateSetting()`](engine/src/New3DTracker.cpp:2269-2270) - UDP handler (was already working)

## Status

✅ **FIXED** - Setting now properly loads from and saves to JSON file  
✅ **COMPILED** - Engine rebuilt successfully  
⏳ **PENDING** - User testing required to verify fix works as expected