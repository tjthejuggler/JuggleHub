# Color Calibration System Upgrade

**Date:** 2025-10-07  
**Status:** Implemented in Engine, UI Update Pending

## Overview

Upgraded the color matching system from range-based HSV matching to euclidean distance matching in hue-saturation space. This provides more robust and accurate ball identification, especially when multiple balls of similar colors are present.

## Changes Made

### 1. Engine Changes (`engine/`)

#### Modified Files:
- `engine/include/SimpleBallTracker.hpp`
- `engine/src/SimpleBallTracker.cpp`

#### Key Changes:

##### A. ColorProfile Structure (SimpleBallTracker.hpp)
Added new fields to store calibrated average hue and saturation:
```cpp
struct ColorProfile {
    std::string name;
    bool enabled;
    
    // NEW: Average hue and saturation from calibration
    float avg_hue;        // Average hue value (0-180)
    float avg_saturation; // Average saturation value (0-255)
    
    // LEGACY: Keep old min/max ranges for backward compatibility
    cv::Scalar min_hsv;
    cv::Scalar max_hsv;
    // ...
};
```

##### B. Calibration Method (calibrateColor())
**Old Behavior:**
- Sampled entire bounding box
- Calculated average HSV
- Set min/max ranges with ±20 hue tolerance

**New Behavior:**
- Finds detection containing click point
- Samples **5x5 pixel square from the exact center** of bounding box
- Calculates average hue and saturation from center samples
- Stores `avg_hue` and `avg_saturation` in profile
- Also updates legacy min/max ranges for backward compatibility

##### C. Color Matching Method (matchColor())
**Old Behavior:**
- Sampled 7x7 region around detection center
- Checked if each pixel falls within HSV ranges
- Returned percentage of matching pixels

**New Behavior:**
- If profile has calibrated `avg_hue` and `avg_saturation`:
  - Samples 5x5 region around detection center
  - Calculates average hue and saturation
  - Computes **euclidean distance** in normalized hue-saturation space
  - Handles hue wrap-around (circular nature of hue)
  - Returns similarity score using exponential decay: `exp(-distance * 10)`
  - **CRITICAL**: No threshold rejection - always assigns to closest match
- Falls back to legacy range-based matching if not calibrated (uses MIN_COLOR_MATCH_SCORE threshold)

**Euclidean Distance Formula:**
```cpp
// Normalize to 0-1 range
hue_diff = (avg_hue / 180.0) - (profile.avg_hue / 180.0)
sat_diff = (avg_sat / 255.0) - (profile.avg_saturation / 255.0)

// Handle hue wrap-around
if (hue_diff > 0.5) hue_diff -= 1.0
if (hue_diff < -0.5) hue_diff += 1.0

// Calculate distance
euclidean_dist = sqrt(hue_diff² + sat_diff²)

// Convert to similarity score
similarity = exp(-euclidean_dist * 10.0)
```

##### D. Settings Persistence
- `loadSettings()`: Loads `avg_hue` and `avg_saturation` from JSON if available
- `saveSettings()`: Saves `avg_hue` and `avg_saturation` to JSON when calibrated
- Maintains backward compatibility with old format

**JSON Format:**
```json
{
  "green": {
    "enabled": true,
    "avg_hue": 60.5,
    "avg_saturation": 180.3,
    "min_hsv": [40, 50, 50],
    "max_hsv": [80, 255, 255]
  }
}
```

### 2. Hub UI Changes (Pending)

#### Files to Update:
- `hub/components/ui_settings.py` - Ball Profiles section

#### Required Changes:
1. Replace min/max hue sliders with read-only displays showing:
   - Average Hue: `{avg_hue}` (0-180)
   - Average Saturation: `{avg_saturation}` (0-255)

2. Add "Calibrate" button for each ball color that:
   - Instructs user to click on a ball in the video feed
   - Sends calibration command to engine
   - Updates display with new values

3. Keep the "Track {Color}" toggle buttons

## Benefits

1. **More Accurate Matching**: Euclidean distance in hue-saturation space provides better discrimination between similar colors

2. **Robust to Lighting**: By sampling from the center of the ball (5x5 pixels), we avoid edge artifacts and get the true ball color

3. **Better Multi-Ball Tracking**: When multiple balls are detected, the system can now match them more accurately by finding the closest color match in hue-saturation space

4. **Backward Compatible**: System falls back to legacy range-based matching if profiles aren't calibrated with new method

## Usage

### Calibrating Colors:
1. Start the engine and hub
2. Ensure balls are visible in the camera feed
3. Click on a ball in the video feed
4. System samples 5x5 pixels from center of clicked bounding box
5. Calculates and stores average hue and saturation
6. Ball is now calibrated for euclidean distance matching

### How Matching Works:
1. For each detected ball, system samples 5x5 pixels from its center
2. Calculates average hue and saturation
3. Computes euclidean distance to each calibrated color profile
4. Assigns ball to closest matching color
5. Uses greedy matching to ensure each detection is assigned to only one ball

## Testing Recommendations

1. **Single Ball Test**: Calibrate one color, verify it tracks correctly
2. **Multi-Ball Test**: Calibrate 3 different colors, verify correct assignment
3. **Similar Colors Test**: Test with balls of similar hues (e.g., orange and red)
4. **Lighting Variation**: Test under different lighting conditions
5. **Backward Compatibility**: Test with old ball_settings.json files

## Future Enhancements

1. **Value (Brightness) Component**: Currently only uses hue and saturation. Could add value for better discrimination in low-light conditions.

2. **Adaptive Thresholds**: Could adjust the exponential decay scale factor based on detection confidence.

3. **Multi-Sample Calibration**: Average multiple calibration samples for more robust color profiles.

4. **Visual Feedback**: Show euclidean distance values in debug overlay.

## Notes

- The 5x5 center sampling avoids edge effects and specular highlights
- Hue wrap-around handling is critical for red/pink colors near 0°/180°
- Exponential decay with scale factor 10 means:
  - Distance 0.0 → similarity 1.0 (perfect match)
  - Distance 0.1 → similarity 0.37
  - Distance 0.2 → similarity 0.14
  - Distance 0.3 → similarity 0.05

---

**Implementation Status:**
- ✅ Engine implementation complete
- ⏳ Hub UI update pending
- ⏳ Testing pending