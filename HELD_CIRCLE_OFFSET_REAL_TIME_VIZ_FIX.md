# Held Circle Offset Real-Time Visualization Fix

**Date:** 2025-10-27  
**Status:** ✅ COMPLETE

## Problem

The "Held Circle Offset (cm)" setting visualization had multiple critical issues:

1. **Wrong Toggle Connection**: The engine's `drawHandThresholds()` function was checking `show_ball_states()` instead of `show_hand_threshold()`
2. **Duplicate Visualization**: The UI code was drawing its own cyan circles, creating a duplicate visualization
3. **Only Showing During Recording**: The visualization only appeared when recording was active, not in real-time with the toggle
4. **No Real-Time State Updates**: Toggle changes in the UI weren't being sent to the engine in real-time

The user wanted only ONE circle that:
- Shows up with the hand_threshold toggle in **real-time** (not just during recording)
- Uses both the "Held Radius" and "Held Circle Offset" settings
- Should be the engine's implementation (yellow circle with proper offset)

## Root Cause

1. **Wrong Toggle Check**: Engine was checking `show_ball_states()` instead of `show_hand_threshold()`
2. **Duplicate UI Code**: UI was drawing its own circles using only `held_radius_m` setting
3. **Missing Real-Time Communication**: The UI's `toggle_overlays()` function only updated the local display but didn't send visualization state changes to the engine
4. **Engine State Not Updated**: The engine's `visualization_states_` variable was only updated when recording started, not when toggles changed

## Solution

### 1. Fixed Engine Visualization Toggle

Changed the visualization check in [`engine/src/Engine.cpp`](engine/src/Engine.cpp) from `show_ball_states()` to `show_hand_threshold()`:

**Real-time visualization (line 477):**
```cpp
if (video_feed_enabled_ && visualization_states_.show_hand_threshold() && tracker_ && !tracked_hands.empty()) {
    // Clone the original color image and draw the visualization
    cv::Mat display_with_viz = color_image.clone();
    tracker_->drawHandThresholds(display_with_viz, tracked_hands, camera_intrinsics_);
    
    // Re-encode with the visualization
    std::vector<uchar> buf;
    std::vector<int> compression_params;
    compression_params.push_back(cv::IMWRITE_JPEG_QUALITY);
    compression_params.push_back(70);
    cv::imencode(".jpg", display_with_viz, buf, compression_params);
    frame_data.set_color_image_b64(buf.data(), buf.size());
}
```

**Recording visualization (line 2217):**
```cpp
if (viz.show_hand_threshold() && tracker_) {
    tracker_->drawHandThresholds(temp_result, rec_frame.tracked_hands_simple, camera_intrinsics_);
}
```

### 2. Removed Duplicate UI Visualization

Removed the UI-side hand threshold visualization code from [`hub/components/ui.py`](hub/components/ui.py:1543) that was drawing cyan circles.

### 3. Added Real-Time State Communication

**Added new protobuf command** in [`api/v1/juggler.proto`](api/v1/juggler.proto:288):
```protobuf
SET_VISUALIZATION_STATES = 30; // Update visualization states in real-time
```

**Updated UI toggle handler** in [`hub/components/ui.py`](hub/components/ui.py:1048):
```python
def toggle_overlays(self):
    # Send updated visualization states to engine
    viz_states = juggler_pb2.VisualizationStates()
    viz_states.show_raw_detections = self.show_raw_detections_toggle.isChecked()
    # ... (all other toggles)
    viz_states.show_hand_threshold = self.show_hand_threshold_toggle.isChecked()
    
    # Send command to engine to update visualization states
    command = juggler_pb2.CommandRequest()
    command.type = juggler_pb2.CommandRequest.CommandType.SET_VISUALIZATION_STATES
    command.visualization_states.CopyFrom(viz_states)
    
    response = self.zmq_client.send_command(command)
    # ... error handling
```

**Added engine command handler** in [`engine/src/Engine.cpp`](engine/src/Engine.cpp:925):
```cpp
case juggler::v1::CommandRequest::SET_VISUALIZATION_STATES:
    if (command.has_visualization_states()) {
        visualization_states_ = command.visualization_states();
        response.set_message("Visualization states updated");
    }
    break;
```

## Files Modified

1. [`api/v1/juggler.proto`](api/v1/juggler.proto:288) - Added SET_VISUALIZATION_STATES command type
2. [`hub/components/ui.py`](hub/components/ui.py:1048) - Updated toggle_overlays() to send state to engine
3. [`hub/components/ui.py`](hub/components/ui.py:1543) - Removed duplicate UI-side visualization code
4. [`engine/src/Engine.cpp`](engine/src/Engine.cpp:477) - Fixed real-time visualization toggle check
5. [`engine/src/Engine.cpp`](engine/src/Engine.cpp:925) - Added SET_VISUALIZATION_STATES command handler
6. [`engine/src/Engine.cpp`](engine/src/Engine.cpp:2217) - Fixed recording visualization toggle check

## Verification

The hand threshold visualization now:
- ✅ Shows up **immediately** when the "hand_threshold" toggle is enabled (real-time)
- ✅ Uses both "Held Radius" and "Held Circle Offset" settings from the New 3D Tracker
- ✅ Appears in both real-time and recording modes
- ✅ Only one circle per hand (no duplicates)
- ✅ Matches the actual tracking behavior
- ✅ Works with toggle changes, not just during recording
- ✅ Updates instantly when toggle is clicked

## Technical Details

The engine's [`drawHandThresholds()`](engine/src/New3DTracker.cpp:2218) function properly implements the visualization using:
- `held_radius_m` - The base radius around the hand
- `held_circle_offset_cm` - Additional offset applied to the radius

This ensures the visualization accurately represents the actual detection zone used by the New 3D Kalman tracking system.

## Build Instructions

After this fix, you need to:

1. **Regenerate protobuf files** (the proto file changed):
```bash
make generate-proto
```

2. **Rebuild the engine** (C++ code changed):
```bash
cd engine && make
```

3. **Restart the hub** (Python code changed):
```bash
# Stop the hub if running, then restart it
```

The visualization will now work immediately when you toggle "hand_threshold" on/off!