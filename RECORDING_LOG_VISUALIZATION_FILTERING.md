# Recording Log Visualization-Based Filtering

**Date:** 2025-10-11  
**Status:** ✅ Implemented

## Overview

Modified the `recording.log` output to be conditional based on which visualization toggles are selected in the UI. This ensures that the log file only contains information relevant to the visualizations being recorded, making it cleaner and more focused.

## Changes Made

### 1. RecordingLogger.hpp
- Added `juggler.pb.h` include for protobuf visualization states
- Modified `logFrame()` signature to accept `VisualizationStates` parameter
- Implemented conditional logging based on visualization toggles

### 2. Engine.cpp
- Updated both `saveRecording()` and `stopContinuousRecording()` calls to pass `visualization_states_` to `logFrame()`

## Logging Behavior

### Always Logged (Core Information)
These items are logged regardless of visualization settings:
- Frame number
- Timestamp
- Number of tracked balls and hands
- Hand positions (wrist 3D coordinates, visibility, confidence)
- Ball basic state:
  - Position 3D and 2D (pixel coordinates)
  - Bounding box
  - State (HELD or IN_FLIGHT)
  - Which hand is holding the ball (if held)

### Conditionally Logged

#### Color Tracker Details (`show_color_tracker`)
When enabled, logs:
- Has YOLO detection
- YOLO confidence
- YOLO class (ball vs ball_held)
- Color match score
- Tracking reason

#### Trajectory Information (`show_trajectory`)
When enabled, logs:
- Verified points count
- Trajectory confidence
- Search radius
- All trajectory points with positions and verification status
- Initial velocity and speed magnitude

#### Prediction Details (`show_trajectory_predictions`)
When enabled, logs:
- Predicted path points count
- Gravity value
- Prediction validity
- Next predicted position
- Search radius
- Prediction error (if YOLO detection available)

## Benefits

1. **Cleaner Logs**: Only relevant information is logged based on what's being visualized
2. **Easier Debugging**: Focus on specific aspects without wading through unrelated data
3. **Consistency**: Log content matches what's shown in recorded images
4. **Flexibility**: Users can control log verbosity through UI toggles

## Example Usage

If a user only enables `show_color_tracker` and `show_hand_tracking`:
- Log will show basic ball/hand positions (always)
- Log will show color tracker details (conditional - enabled)
- Log will NOT show trajectory points (conditional - disabled)
- Log will NOT show prediction details (conditional - disabled)

## Technical Notes

- Default parameter value ensures backward compatibility if called without visualization states
- All "ALWAYS" logged items provide essential context for any recording
- Conditional sections align with corresponding visualization overlays in images
- Frame events (throws/catches) are always logged as they're fundamental to juggling analysis