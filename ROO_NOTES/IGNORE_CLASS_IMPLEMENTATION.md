# Ignore Class Toggle Implementation

**Date:** 2025-10-14  
**Feature:** Global "Ignore Class" toggle for ML class filtering

## Overview

Implemented a global toggle in the Ball State Detection section that allows users to disable ML class distinctions throughout the entire tracking system. When enabled, both 'ball' (class_id=0) and 'ball_held' (class_id=1) classes are treated identically.

## Problem Statement

YOLO's ML classification between 'ball' and 'ball_held' can be unreliable in certain conditions. When the model misclassifies balls, it can cause:
- Incorrect state transitions (HELD ↔ IN_FLIGHT)
- Missed detections due to class-specific confidence thresholds
- Tracking failures when class predictions are inconsistent

## Solution

Added a single toggle that disables all class-based filtering and threshold differences throughout the tracking system.

## Implementation Details

### 1. UI Changes (`hub/components/ui_settings.py`)

**Location:** Ball State Detection section (after "Min Frames Before Catch")

**Added Toggle:**
```python
self.tc_ignore_class_toggle = QPushButton("Ignore Class (Treat ball/ball_held Same)")
self.tc_ignore_class_toggle.setCheckable(True)
self.tc_ignore_class_toggle.setChecked(False)  # Default: OFF
```

**Tooltip:**
- Explains that ML class distinctions are ignored
- Both 'ball' and 'ball_held' treated identically
- No class-based filtering in detection matching
- No class-based threshold differences
- State determined purely by distance to hands
- Recommended when YOLO class predictions are unreliable

**Settings Integration:**
- Added to `get_current_settings()` for persistence
- Added to `apply_settings()` for loading
- Added to `_send_all_settings_to_engine()` for UDP transmission

### 2. Backend Structure (`engine/include/SimpleBallTracker.hpp`)

**Added to TrackingSettings struct:**
```cpp
bool ignore_class = false;  // If true, ignore ML class distinctions
```

**Location:** Line 189, after `min_frames_before_catch`

### 3. SimpleBallTracker Implementation (`engine/src/SimpleBallTracker.cpp`)

**Setting Handler (Line 493):**
```cpp
else if (key == "ignore_class") {
    tracking_settings_.ignore_class = (value == "true" || value == "1");
    return true;
}
```

**Updated Locations:**

1. **`evaluateOverrideCriteria()` (Lines 643-658):**
   - Uses ball thresholds for all classes when `ignore_class` is enabled
   - Ignores class requirement check when enabled

2. **Override Detection in `update()` (Lines 976-991):**
   - Uses ball thresholds for all classes when `ignore_class` is enabled
   - Ignores class requirement check when enabled

3. **`runBallDetection()` (Line 1617):**
   - Uses ball confidence threshold for all classes when `ignore_class` is enabled

4. **`updateHeldBall()` (Line 3522):**
   - Ignores class requirement when `ignore_class` is enabled
   - Works in conjunction with hand velocity settings

### 4. Simple2DBallTracker Implementation

**Setting Handler (`engine/src/Simple2DBallTracker.cpp`, Line 480):**
```cpp
else if (key == "ignore_class") {
    tracking_settings_.ignore_class = (value == "true" || value == "1");
    std::cout << "[Simple2DBallTracker] Ignore class "
              << (tracking_settings_.ignore_class ? "enabled" : "disabled") << std::endl;
    return true;
}
```

**Detection Threshold (Line 276):**
```cpp
// IGNORE_CLASS: When enabled, use ball threshold for all classes
float threshold = (tracking_settings_.ignore_class || class_id == 0) ? 
    ball_confidence_threshold_ : ball_held_confidence_threshold_;
```

## Behavior When Enabled

### Detection Matching
- **Before:** Different confidence thresholds for 'ball' vs 'ball_held'
- **After:** Single threshold (ball threshold) applied to all detections

### Override Detection
- **Before:** Class-specific confidence and color thresholds
- **After:** Ball thresholds used for all classes, class requirement ignored

### State Determination
- **Before:** Class influences state transitions
- **After:** State determined purely by distance to hands

### Throw Detection
- **Before:** Requires 'ball' class for standard throws
- **After:** Any class accepted if other criteria met

## Testing Recommendations

1. **Enable the toggle** in UI → Tracking Settings → Ball State Detection
2. **Test with misclassified balls:**
   - Balls incorrectly detected as 'ball_held' while in flight
   - Balls incorrectly detected as 'ball' while held
3. **Verify tracking stability:**
   - No spurious state transitions
   - Consistent tracking regardless of class prediction
4. **Compare with toggle disabled:**
   - Ensure normal behavior when disabled
   - Verify class-specific thresholds still work

## Configuration

**Default State:** Disabled (OFF)  
**Recommended Use:** Enable when YOLO class predictions are unreliable  
**Settings File:** Persisted in `hub/config/calibration_settings.json`

## Files Modified

1. `hub/components/ui_settings.py` - UI toggle and settings integration
2. `engine/include/SimpleBallTracker.hpp` - TrackingSettings struct
3. `engine/src/SimpleBallTracker.cpp` - Core tracking logic (4 locations)
4. `engine/src/Simple2DBallTracker.cpp` - 2D tracker support (2 locations)

## Backward Compatibility

- Default value is `false` (disabled), maintaining existing behavior
- Settings file automatically includes the new field
- No breaking changes to existing configurations

## Future Enhancements

Potential improvements if needed:
1. Per-ball class ignore (instead of global)
2. Automatic detection of unreliable class predictions
3. Confidence-based class filtering (ignore only low-confidence classes)
4. UI indicator showing when class predictions are inconsistent

---

**Implementation Complete:** 2025-10-14  
**Status:** Ready for testing