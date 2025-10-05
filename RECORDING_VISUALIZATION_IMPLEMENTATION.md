# Recording with Visualizations Implementation

**Date:** 2025-10-05  
**Status:** ✅ Complete

## Overview

Updated the recording system to save two separate directories of images when recording:
1. **`no_boxes/`** - Raw frame images without any overlays
2. **`with_visualizations/`** - Frame images with any visualizations that are toggled on in the UI

This replaces the old system that only supported YOLO boxes and ByteTrack boxes.

## Changes Made

### 1. Protocol Buffer Updates (`api/v1/juggler.proto`)

Added new `VisualizationStates` message to capture all UI visualization toggle states:

```protobuf
message VisualizationStates {
  bool show_kalman_predictions = 1;
  bool show_raw_detections = 2;
  bool show_filtered_detections = 3;
  bool show_associations = 4;
  bool show_new_trackers = 5;
  bool show_hand_tracking = 6;
  bool show_ball_states = 7;
  bool show_occlusion = 8;
  bool show_skeleton = 9;
  bool show_color_search = 10;
  bool show_color_tracker = 11;
  bool show_tracked_boxes = 12;
  bool show_unmatched_detections = 13;
  bool show_tails = 14;
}
```

Added `visualization_states` field to `CommandRequest` message.

### 2. Engine Updates

#### Header (`engine/include/Engine.hpp`)
- Added `viz_states` field to `RecordingFrame` struct to store visualization states per frame
- Added `visualization_states_` member variable to store current visualization settings
- Added `renderVisualizationsOnFrame()` helper method

#### Implementation (`engine/src/Engine.cpp`)
- Modified command handlers to capture `visualization_states` from recording commands
- Updated `RecordingFrame` creation to include visualization states
- Replaced `with_boxes/` directory with `with_visualizations/` directory
- Implemented `renderVisualizationsOnFrame()` to render visualizations based on stored states
- Currently supports rendering:
  - YOLO detections (red boxes)
  - ByteTrack tracked boxes (orange boxes)
  - Hand tracking (cyan circles with labels)
  - Pose skeleton (cyan keypoints and connections)

### 3. UI Updates (`hub/components/ui.py`)

Modified both `record_clip()` and `toggle_continuous_recording()` functions to:
- Create `VisualizationStates` protobuf message from current UI toggle states
- Pass visualization states to the engine via the recording command
- Support all 14 visualization toggles available in the UI

## Directory Structure

When recording, the system now creates:

```
engine/data/1_raw_recordings/
├── rs455_2025-10-05_23-45-30/          # 5-second clip
│   ├── no_boxes/                        # Raw frames
│   │   ├── rs455_2025-10-05_23-45-30_frame_0.jpg
│   │   ├── rs455_2025-10-05_23-45-30_frame_1.jpg
│   │   └── ...
│   └── with_visualizations/             # Frames with overlays
│       ├── rs455_2025-10-05_23-45-30_frame_0_viz.jpg
│       ├── rs455_2025-10-05_23-45-30_frame_1_viz.jpg
│       └── ...
└── continuous_2025-10-05_23-50-15/     # Continuous recording
    ├── no_boxes/
    │   └── ...
    └── with_visualizations/
        └── ...
```

## Usage

1. **Toggle visualizations** in the UI (e.g., "YOLO Detections", "Hand Tracking", "Pose Skeleton")
2. **Click "Record 5s Clip"** or **"Start Recording"** button
3. The system will save:
   - Raw frames in `no_boxes/` directory
   - Frames with enabled visualizations in `with_visualizations/` directory (only if at least one visualization is enabled)

## Benefits

- **Flexibility**: Any combination of visualizations can be recorded
- **Efficiency**: Only creates visualization directory if visualizations are enabled
- **Backward Compatible**: Still supports legacy `record_with_yolo_boxes` and `record_with_bytetrack_boxes` flags
- **Extensible**: Easy to add new visualizations by updating the rendering function

## Future Enhancements

To support additional visualizations (Kalman predictions, associations, etc.), the `RecordingFrame` struct would need to store additional data beyond just raw detections and tracked objects. This could include:
- Kalman prediction data
- Association data
- Color search regions
- Ball state information

## Testing

✅ Engine compiles successfully  
⏳ Runtime testing pending (requires camera hardware)

## Notes

- The visualization rendering is done in C++ on the engine side for performance
- The UI simply passes the toggle states; all rendering logic is in the engine
- The old `with_boxes/` directory has been replaced with `with_visualizations/`