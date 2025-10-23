# New 3D Tracker FPS Optimization Fix

**Date:** 2025-10-23
**Issue:** FPS does not improve when YOLO models are disabled in New 3D Kalman tracking system

## Problem Description

When using the Simple 3D Tracking system, disabling the YOLO pose and ball models increases FPS from ~40 to ~60. However, in the New 3D Kalman tracking system, disabling both YOLO models does not change the FPS - it stays around 30 regardless of whether the models are enabled or disabled.

**Additional Issue:** The pose model toggle in the UI was not properly connected to the engine's `enable_pose_estimation` setting.

## Root Cause

The New 3D Tracker was running YOLO ball detection and pose estimation **unconditionally** on every frame, regardless of whether these models were enabled or disabled in the UI. The code had no checks to skip YOLO inference when models were disabled.

**Key Issues:**
1. Frame preprocessing always ran (line 1710 in `New3DTracker.cpp`)
2. Ball detection always ran every frame (lines 1712-1714)
3. Pose estimation always ran every other frame (lines 1722-1723)
4. No enable/disable flags existed in `New3DTrackerSettings`

## Solution Implemented

### 1. Added Enable/Disable Flags to Settings

**File:** [`engine/include/New3DTracker.hpp`](engine/include/New3DTracker.hpp:192-196)

Added two new boolean flags to `New3DTrackerSettings`:
```cpp
// === YOLO INTEGRATION ===
bool enable_ball_detection = true;              // Enable/disable YOLO ball detection
bool enable_pose_estimation = true;             // Enable/disable YOLO pose estimation
float ball_confidence_threshold = 0.25f;        // Min confidence for 'ball' class
float ball_held_confidence_threshold = 0.25f;   // Min confidence for 'ball_held' class
bool ignore_class = false;                      // Treat ball/ball_held same
```

### 2. Added Conditional Checks in Update Loop

**File:** [`engine/src/New3DTracker.cpp`](engine/src/New3DTracker.cpp:1706-1745)

Modified the `updateNew3D()` method to:
- Only preprocess frames when at least one YOLO model is enabled
- Only run ball detection when `enable_ball_detection` is true
- Only run pose estimation when `enable_pose_estimation` is true
- Use empty detection/hand lists when models are disabled

```cpp
// Preprocess frame only if at least one YOLO model is enabled
float scale_x = 1.0f, scale_y = 1.0f;
cv::Mat preprocessed;

if (settings_.enable_ball_detection || settings_.enable_pose_estimation) {
    preprocessed = preprocess(color_frame, scale_x, scale_y);
}

// Run YOLO ball detection (conditionally)
std::vector<Detection> detections;
if (settings_.enable_ball_detection) {
    detections = runBallDetection(
        preprocessed, scale_x, scale_y, color_frame, depth_frame, intrinsics);
    logDebug("--- BALL DETECTION: ENABLED (", detections.size(), " detections) ---");
} else {
    logDebug("--- BALL DETECTION: DISABLED ---");
}

// Run YOLO pose estimation (conditionally)
std::vector<SimpleHand> current_hands;
if (settings_.enable_pose_estimation) {
    // ... run pose estimation on odd frames
} else {
    // Pose estimation disabled - use empty hands list
    current_hands.clear();
    logDebug("--- POSE DETECTION: DISABLED ---");
}
```

### 3. Added Settings Persistence

**File:** [`engine/src/New3DTracker.cpp`](engine/src/New3DTracker.cpp:346-352)

Added loading of new settings from JSON:
```cpp
if (j.contains("enable_ball_detection")) {
    settings_.enable_ball_detection = j["enable_ball_detection"];
}
if (j.contains("enable_pose_estimation")) {
    settings_.enable_pose_estimation = j["enable_pose_estimation"];
}
```

**File:** [`engine/src/New3DTracker.cpp`](engine/src/New3DTracker.cpp:436-440)

Added saving of new settings to JSON:
```cpp
j["enable_ball_detection"] = settings_.enable_ball_detection;
j["enable_pose_estimation"] = settings_.enable_pose_estimation;
```

### 4. Added UI Control Support

**File:** [`engine/src/New3DTracker.cpp`](engine/src/New3DTracker.cpp:2074-2080)

Added support for updating settings via UI:
```cpp
} else if (key == "enable_ball_detection") {
    settings_.enable_ball_detection = (value == "true" || value == "1");
} else if (key == "enable_pose_estimation") {
    settings_.enable_pose_estimation = (value == "true" || value == "1");
```

## Expected Results

With this fix:
- When both YOLO models are **enabled**: FPS ~40 (as before)
- When both YOLO models are **disabled**: FPS ~60 (matching Simple 3D Tracker behavior)
- The tracker will continue to function using only Kalman predictions when models are disabled
- Settings persist across sessions via JSON file

## Testing

To test the fix:
1. Enable both YOLO models and verify FPS is around 40
2. Disable both YOLO models and verify FPS increases to around 60
3. Verify the tracker still functions (using predictions only) when models are disabled
4. Verify settings persist after restarting the application

## Files Modified

1. [`engine/include/New3DTracker.hpp`](engine/include/New3DTracker.hpp) - Added enable flags to settings structure
2. [`engine/src/New3DTracker.cpp`](engine/src/New3DTracker.cpp) - Added conditional checks and settings support
3. [`hub/components/ui_settings.py`](hub/components/ui_settings.py) - Fixed pose model toggle to send UDP setting instead of ZMQ command

## UI Connection Fix

The pose model toggle was using a ZMQ command instead of sending the UDP setting. This has been fixed:

**File:** [`hub/components/ui_settings.py`](hub/components/ui_settings.py:889-895)

Changed from:
```python
def toggle_pose_model(self):
    """Toggle pose model"""
    is_enabled = self.pose_model_toggle.isChecked()
    command = juggler_pb2.CommandRequest(
        type=juggler_pb2.CommandRequest.CommandType.SET_POSE_MODEL_ENABLED,
        pose_model_enabled=is_enabled
    )
    # ... ZMQ command handling
```

To:
```python
def toggle_pose_model(self):
    """Toggle pose model"""
    is_enabled = self.pose_model_toggle.isChecked()
    self.pose_model_toggle.setText("Enable Pose Model" if is_enabled else "Pose Model DISABLED")
    self.udp_client.send_setting('enable_pose_estimation', 1 if is_enabled else 0)
    print(f"✅ Pose model {'enabled' if is_enabled else 'disabled'}")
    if not self._loading_settings:
        self.save_settings()
```

This ensures the pose toggle properly sends the `enable_pose_estimation` setting to the engine via UDP, matching the behavior of the ball detection toggle.

## Notes

- The tracker will still function when YOLO models are disabled, relying entirely on Kalman filter predictions
- This is useful for testing prediction accuracy and for scenarios where detection is not needed
- The preprocessing step is also skipped when both models are disabled, providing additional performance improvement
- Both the ball detection and pose estimation toggles now properly control their respective YOLO models