# YOLO Detection Settings and Tracker Persistence Fixes

**Date:** 2025-10-09  
**Status:** Implemented, Ready for Testing

## Overview

This document describes the implementation of class-specific YOLO confidence thresholds, dual visualization for raw vs filtered detections, and critical fixes to prevent tracker disappearance.

## Problems Addressed

### 1. Single Confidence Threshold
**Problem:** A single confidence threshold was applied to both 'ball' (in-air) and 'ball_held' classes, making it impossible to tune detection sensitivity independently.

**Impact:** 
- In-air balls might need lower thresholds (more permissive) due to motion blur
- Held balls might need higher thresholds (more strict) due to hand occlusion
- One-size-fits-all approach led to either missing detections or false positives

### 2. Settings Not Affecting Detection
**Problem:** UI settings were not properly filtering YOLO detections during postprocessing.

**Impact:** Changing confidence thresholds in the UI had no effect on which detections were used for tracking.

### 3. No Visualization of Raw Detections
**Problem:** No way to see what YOLO was detecting before filtering, making it impossible to debug detection issues.

**Impact:** Users couldn't tell if problems were due to YOLO not detecting balls or filters being too strict.

### 4. Tracker Disappearing Unexpectedly
**Problem:** Trackers would vanish after `MAX_FRAMES_WITHOUT_YOLO` (30 frames) even when ball was still visible on-screen.

**Impact:** 
- Tracking would stop during brief occlusions
- Balls would disappear mid-juggle
- No way to recover tracking without manual intervention

### 5. Poor Fallback Tracking Priority
**Problem:** Fallback tracking didn't properly prioritize color blobs near wrist, ML-detected ball_held, or wrist snapping.

**Impact:**
- Tracker would fail to find balls held in hands
- Color tracking wasn't being used effectively
- Wrist snapping was used too early instead of as last resort

## Solutions Implemented

### 1. Class-Specific Confidence Thresholds

#### UI Changes ([`hub/components/ui_settings.py`](hub/components/ui_settings.py:224-302))
- Replaced single "Confidence Threshold" slider with two class-specific sliders:
  - **"'Ball' Confidence"** - for in-air ball detections (class_id=0)
  - **"'Ball Held' Confidence"** - for held ball detections (class_id=1)
- Added **"Show Raw YOLO Detections"** toggle button
- Settings persist across sessions via `get_current_settings()` and `apply_settings()`
- Real-time updates sent to engine via UDP

#### Engine Changes ([`engine/include/SimpleBallTracker.hpp`](engine/include/SimpleBallTracker.hpp:249-256))
- Added separate threshold members:
  ```cpp
  float ball_confidence_threshold_ = 0.25f;      // class_id=0
  float ball_held_confidence_threshold_ = 0.25f; // class_id=1
  bool show_raw_yolo_detections_ = false;
  ```

#### Detection Filtering ([`engine/src/SimpleBallTracker.cpp`](engine/src/SimpleBallTracker.cpp:2188-2224))
- Apply class-specific thresholds during YOLO postprocessing:
  ```cpp
  float threshold = (class_id == 0) ? ball_confidence_threshold_ : ball_held_confidence_threshold_;
  if (confidence > threshold) {
      // Add detection
  }
  ```
- NMS threshold uses minimum of both class thresholds

### 2. Dual Visualization System

#### Visualization Rendering ([`engine/src/Engine.cpp`](engine/src/Engine.cpp:944-1000))
Implemented two-tier visualization:

**Raw Detections (when toggle enabled):**
- Color: Darker red (0, 0, 139)
- Line thickness: 3px
- Box size: Enlarged by +5px on all sides
- Label: "R#N" (R for Raw)

**Filtered Detections (always shown):**
- Color: Bright red (0, 0, 255)
- Line thickness: 2px
- Box size: Standard
- Label: "#N"

**Info Panel:**
- Shows count of both "RAW" and "FILTERED" detections
- Helps users understand filtering effectiveness

### 3. Tracker Persistence Fixes

#### Removed Time-Based Disappearance ([`engine/src/SimpleBallTracker.cpp`](engine/src/SimpleBallTracker.cpp:1765-1900))

**Before:**
```cpp
if (has_prediction && ball.frames_without_yolo < MAX_FRAMES_WITHOUT_YOLO) {
    // Use Kalman prediction
}
```

**After:**
```cpp
if (has_prediction) {
    // Check if on-screen
    cv::Point2f pred_pixel = project_3d_to_2d(kalman_pred, intrinsics);
    bool is_on_screen = (pred_pixel.x >= 0 && pred_pixel.x < color_frame.cols &&
                        pred_pixel.y >= 0 && pred_pixel.y < color_frame.rows);
    
    if (!is_on_screen) {
        // ONLY reason to stop tracking
        ball.position = cv::Point3f(0, 0, 0);
        ball.tracking_reason = "OFF-SCREEN";
        continue;
    }
    // Use Kalman prediction
}
```

**Key Changes:**
- Removed `frames_without_yolo < MAX_FRAMES_WITHOUT_YOLO` checks
- Tracker persists indefinitely as long as ball is on-screen
- Only disappears when ball goes off-screen (pixel coordinates outside frame bounds)

### 4. Improved Fallback Tracking Priority

#### New Priority Order ([`engine/src/SimpleBallTracker.cpp`](engine/src/SimpleBallTracker.cpp:1783-1856))

When YOLO detection is lost and Kalman prediction is near a hand:

**Priority 1: Color Blob Near Wrist**
- Search radius increased to 120 pixels (from 80)
- Validates position is on-screen before using
- Updates Kalman filter with color detection
- Reason: `"Color@Hand[L/R] d=X.XXm"`

**Priority 2: ML-Detected ball_held Near Hand**
- Searches for YOLO ball_held detections within 25cm of hand
- Uses closest ML detection if found
- Updates Kalman filter with ML position
- Reason: `"ML_held@Hand[L/R] d=X.XXm"`

**Priority 3: Wrist Snap (Last Resort)**
- Only used if hand is on-screen
- Does NOT update Kalman (prevents corruption)
- Only updates color predictor for history continuity
- Reason: `"Snap→Hand[L/R] d=X.XXm"`

**Benefits:**
- Color tracking is prioritized (most accurate for held balls)
- ML detections are used when available
- Wrist snapping is truly last resort
- Each fallback validates on-screen position

## Technical Details

### UDP Settings Communication

Settings are transmitted from Python UI to C++ engine via UDP:
```python
# hub/components/ui_settings.py
def _send_all_settings_to_engine(self):
    settings = {
        'ball_confidence_threshold': self.ball_confidence_slider.value() / 100.0,
        'ball_held_confidence_threshold': self.ball_held_confidence_slider.value() / 100.0,
        'show_raw_yolo_detections': self.show_raw_yolo_toggle.isChecked(),
        # ... other settings
    }
```

### Detection Flow

1. **YOLO Inference** → Raw detections with confidence scores
2. **Class-Specific Filtering** → Apply `ball_confidence_threshold_` or `ball_held_confidence_threshold_`
3. **NMS** → Remove overlapping boxes using `min(ball_threshold, ball_held_threshold)`
4. **Euclidean Matching** → Assign detections to ball trackers by color
5. **Fallback Tracking** → For unmatched balls, use priority system
6. **Visualization** → Render both raw (if enabled) and filtered detections

### Off-Screen Detection

Ball is considered off-screen when projected pixel coordinates are outside frame bounds:
```cpp
cv::Point2f pixel = project_3d_to_2d(ball.position, intrinsics);
bool is_on_screen = (pixel.x >= 0 && pixel.x < frame.cols &&
                     pixel.y >= 0 && pixel.y < frame.rows);
```

## Files Modified

### UI Layer
- [`hub/components/ui_settings.py`](hub/components/ui_settings.py) - Added class-specific sliders and raw detection toggle

### Engine Layer
- [`engine/include/SimpleBallTracker.hpp`](engine/include/SimpleBallTracker.hpp) - Added threshold members
- [`engine/src/SimpleBallTracker.cpp`](engine/src/SimpleBallTracker.cpp) - Implemented filtering and fallback fixes
- [`engine/src/Engine.cpp`](engine/src/Engine.cpp) - Implemented dual visualization

## Testing Checklist

- [ ] Compile engine with updated code
- [ ] Start hub UI and verify new sliders appear
- [ ] Adjust 'Ball' confidence threshold and observe in-air detection changes
- [ ] Adjust 'Ball Held' confidence threshold and observe held ball detection changes
- [ ] Enable "Show Raw YOLO Detections" and verify darker red boxes appear
- [ ] Verify filtered detections (bright red) are subset of raw detections
- [ ] Test tracker persistence during brief occlusions
- [ ] Verify tracker only disappears when ball goes off-screen
- [ ] Test fallback tracking with ball held in hand
- [ ] Verify color blob detection near wrist works
- [ ] Verify settings persist after UI restart
- [ ] Test with actual juggling footage

## Expected Behavior

### Normal Operation
1. YOLO detects balls → Trackers follow YOLO detections
2. Ball briefly occluded → Tracker uses Kalman prediction
3. Ball held in hand → Tracker finds color blob near wrist OR snaps to wrist
4. Ball thrown → Tracker follows Kalman prediction until YOLO reacquires

### Edge Cases
1. Ball goes off-screen → Tracker disappears (only valid reason)
2. Long occlusion → Tracker persists using Kalman until ball reappears or goes off-screen
3. Hand occludes ball → Tracker searches for color blob, then ML ball_held, then snaps to wrist
4. Fast juggling → Temporal consistency bonus prevents identity swaps

## Known Limitations

1. **Wrist snapping accuracy**: Wrist position is not exact ball position, may cause slight offset
2. **Color blob false positives**: May detect wrong colored objects if similar to ball
3. **Off-screen detection**: Relies on projection, may have edge cases with extreme angles
4. **Kalman drift**: Long periods without YOLO updates may cause prediction drift

## Future Improvements

1. Add UI slider for color blob search radius
2. Implement confidence decay for long-term Kalman predictions
3. Add visual indicator when tracker is using fallback vs YOLO
4. Implement automatic threshold tuning based on detection statistics
5. Add per-ball confidence threshold overrides

## Debugging

Enable debug logging to see fallback tracking decisions:
```cpp
// In SimpleBallTracker.cpp, debug logs show:
// - "[FALLBACK] Ball X lost YOLO detection"
// - "-> Found color blob near hand at (x, y, z)"
// - "-> Found ML ball_held near hand"
// - "-> Snapped to wrist (last resort)"
// - "-> Ball went OFF-SCREEN - STOPPING TRACKER"
```

## Conclusion

These changes provide:
1. **Fine-grained control** over detection sensitivity per class
2. **Visual debugging** of detection filtering
3. **Robust tracking** that persists through occlusions
4. **Intelligent fallback** that prioritizes accuracy over convenience

The tracker now only disappears when the ball actually leaves the frame, not due to arbitrary time limits.

---
*Last Updated: 2025-10-09 14:48 UTC*