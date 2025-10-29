# UI Toggle Button Clarity Update

**Date:** 2025-10-11  
**Purpose:** Improve clarity and consistency of visualization toggle buttons

## Problem Statement

The UI toggle buttons had generic, numbered labels (e.g., "2. Kalman Predictions", "3. YOLO Detections") that didn't clearly indicate which exact variables they were controlling. This caused confusion when trying to understand:
- What data each toggle was showing/hiding
- How toggles related to recording log entries
- Which FrameData variables corresponded to each visualization

## Solution

Updated all toggle button labels to show the **exact variable names** from the codebase, making it immediately clear what data each toggle controls.

## Changes Made

### Toggle Button Label Updates

| Old Label | New Label | Data Source |
|-----------|-----------|-------------|
| "2. Kalman Predictions" | "kalman_predictions" | `frame_data.kalman_predictions` |
| "3. YOLO Detections" | "raw_detections" | `frame_data.raw_detections` |
| "4. Filtered Detections" | "filtered_detections" | `frame_data.filtered_detections` |
| "5. 3D Matching" | "tracker_associations" | `frame_data.tracker_associations` |
| "6. Auto-Init" | "new_trackers" | `frame_data.new_trackers` |
| "7. Hand Tracking" | "hands" | `frame_data.hands` |
| "8. Throw/Catch States" | "ball_states" | `frame_data.ball_states` |
| "9. Occlusion" | "occlusion_states" | `frame_data.occlusion_states` |
| "10. Pose Skeleton" | "keypoints" | `hand.keypoints` |
| "11. Color Search" | "color_search_regions" | `frame_data.color_search_regions` |
| "11. Color Tracking" | "color_tracked_balls" | `frame_data.color_tracked_balls` |
| "12. Final Trackers" | "color_tracked_balls (final)" | `frame_data.color_tracked_balls` |
| "13. Unmatched" | "unmatched_detections" | `frame_data.unmatched_detections` |
| "Show Trajectory" | "trajectory_points (path)" | `ball_state.trajectory_points` |
| "Show Trajectory Points" | "trajectory_points (dots)" | `ball_state.trajectory_points` |
| "Show Tails" | "tracker_history" | UI-side `self.tracker_history` |

### Tooltip Updates

All tooltips now follow the format: `"<variable_path> - <description>"`

Examples:
- `"frame_data.kalman_predictions - Predicted positions from Kalman filters"`
- `"frame_data.raw_detections - Raw YOLO detection boxes"`
- `"ball_state.trajectory_points - Predicted trajectory path lines"`

## Verification

### All Toggles Are Connected ✅

Every toggle button has a corresponding data source:
- 13 toggles map to `frame_data.*` variables
- 2 toggles map to `ball_state.trajectory_points` (different visualizations)
- 1 toggle maps to `hand.keypoints`
- 1 toggle maps to UI-side `tracker_history`

### No Disconnected Toggles ✅

All toggles have:
1. A clear data source in the protobuf definition
2. Rendering code in `update_video_feed()`
3. Proper state tracking in `VisualizationStates`

### Recording Consistency ✅

The toggle states are properly synchronized with recording:
- `record_clip()` captures all toggle states in `VisualizationStates`
- `toggle_continuous_recording()` includes visualization states
- Recording log entries will now match the exact variable names shown in the UI

## Benefits

1. **Immediate Clarity**: Users can instantly see which variable each toggle controls
2. **Code Traceability**: Easy to search codebase for the exact variable name
3. **Recording Alignment**: Recording logs now clearly match UI toggle names
4. **Developer Friendly**: New developers can quickly understand the data flow
5. **Debugging**: Easier to correlate UI state with engine output

## Example Usage

When debugging, users can now:
1. See "kalman_predictions" toggle in UI
2. Check `frame_data.kalman_predictions` in recording log
3. Search code for `kalman_predictions` to find implementation
4. Understand the complete data flow from engine → protobuf → UI

## Notes

- The "Hide Video Feed" toggle remains unchanged as it's a UI-only optimization feature
- Two toggles share the same data source (`color_tracked_balls`) but render differently:
  - `color_tracked_balls`: Shows active trackers with color circles
  - `color_tracked_balls (final)`: Shows persistent tracker visualization with white borders
- Two toggles share `trajectory_points` but render differently:
  - `trajectory_points (path)`: Draws connecting lines
  - `trajectory_points (dots)`: Draws individual points as circles