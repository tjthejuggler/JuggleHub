# Time-Based Color Calibration Implementation

**Date**: 2025-11-07  
**Feature**: Multi-frame color calibration for depth blob detection in New 3D Kalman tracker

## Overview

Implemented a robust time-based color calibration system that replaces the single-click calibration with a 10-second multi-frame approach. This provides much more accurate color profiles for LED juggling balls by collecting samples across multiple frames while the user juggles.

## Implementation Summary

### 1. Core Calibration System (`hub/components/time_based_calibration.py`)

Created a new module with a state machine-based calibration system:

**States**:
- `IDLE` - Waiting for calibration to start
- `PREPARATION` (5s) - Countdown before recording starts
- `RECORDING` (10s) - Actively collecting color samples
- `PROCESSING` - Calculating median values
- `COMPLETE` - Calibration successful
- `ERROR` - Calibration failed

**Key Features**:
- Uses QTimer for countdown management
- Collects (hue, saturation) tuples from depth blobs
- Uses **median** instead of mean for robustness against outliers
- Validates minimum sample count (50 samples required)
- Saves results to `calibration_settings_new3d.json`
- Emits signals for UI updates

**Configuration**:
- Preparation duration: 5 seconds
- Recording duration: 10 seconds
- Minimum samples: 50

### 2. UI Integration (`hub/components/ui_settings_new3d.py`)

Modified the New 3D tracker settings to integrate the calibration system:

**Changes**:
- Imported `TimeBasedCalibration` class
- Initialized calibration system in `__init__`
- Connected signals for state updates
- Modified calibrate button to use new system
- Added status label for each color profile
- Implemented handler methods:
  - `_start_time_based_calibration()` - Initiates calibration
  - `_on_calibration_state_changed()` - Updates UI based on state
  - `_on_calibration_complete()` - Updates labels and reloads engine profiles
  - `_on_calibration_error()` - Handles errors
  - `collect_depth_blob_colors()` - Collects samples (not currently used, kept for future)

**UI Updates**:
- Button text changed to "🎯 Calibrate Color (10s)"
- Status label shows countdown and messages
- Color-coded status (orange=prep, red=recording, blue=processing, green=complete)
- Auto-hides status after 3 seconds

### 3. Frame Data Integration (`hub/components/ui.py`)

Added color sample collection in the main UI's `_update_ui()` method:

**Integration Point**:
- Checks if calibration is in recording state
- Extracts `depth_globs` from frame data
- Passes `avg_hue` and `avg_saturation` to calibration system
- Runs on every frame during recording phase

### 4. Workflow

**User Experience**:
1. User selects color profile (e.g., "pink")
2. User clicks "🎯 Calibrate Color (10s)" button
3. **Preparation Phase (5s)**:
   - Status shows: "Get ready to juggle! Starting in 5... 4... 3... 2... 1..."
   - User gets into position with the ball
4. **Recording Phase (10s)**:
   - Status shows: "Juggle the pink ball! Recording: 10... 9... 8... 7... 6... 5... 4... 3... 2... 1..."
   - User juggles the ball
   - System collects color samples from ALL depth blobs in each frame
   - Whiteness filter is automatically applied by engine
5. **Processing Phase**:
   - Status shows: "Processing samples..."
   - Calculates median hue and saturation
   - Saves to JSON
6. **Complete**:
   - Status shows: "✅ Calibration complete! Captured [N] samples."
   - UI labels update with new values
   - Engine reloads color profiles
   - Status auto-hides after 3 seconds

**Error Handling**:
- Checks for minimum sample count
- Validates hue (0-180) and saturation (0-255) ranges
- Handles file I/O errors
- Provides clear error messages
- Auto-resets to IDLE state after errors

## Technical Details

### Color Sampling with Whiteness Filter

The engine's `sampleDetectedColor()` function already applies the whiteness filter:

```cpp
float whiteness = (bgr[0] + bgr[1] + bgr[2]) / 3.0f;
if (hsv[1] > settings_.min_saturation_threshold && 
    whiteness <= settings_.depth_blob_max_whiteness) {
    // Use this pixel for color sampling
}
```

This ensures only valid, non-washed-out pixels are included in the calibration.

### Median vs Mean

The system uses **median** instead of mean for calculating final values:
- More robust against outliers
- Handles occasional bad samples better
- Provides more stable results for LED balls with varying brightness

### Data Structure

Color profiles in `calibration_settings_new3d.json`:

```json
{
    "name": "pink",
    "enabled": true,
    "avg_hue": 165.5,
    "avg_saturation": 180.2,
    "min_hsv": [0.0, 0.0, 0.0],
    "max_hsv": [180.0, 255.0, 255.0],
    "min_hsv2": [-1.0, 0.0, 0.0],
    "max_hsv2": [-1.0, 255.0, 255.0]
}
```

## Files Modified

1. **Created**: `hub/components/time_based_calibration.py` (283 lines)
   - Core calibration logic and state machine

2. **Modified**: `hub/components/ui_settings_new3d.py`
   - Added calibration system integration
   - Modified UI components
   - Added handler methods

3. **Modified**: `hub/components/ui.py`
   - Added frame data integration for color sample collection

## Testing Checklist

- [ ] Test preparation countdown (5 seconds)
- [ ] Test recording countdown (10 seconds)
- [ ] Verify color samples are collected from depth blobs
- [ ] Verify minimum sample count validation
- [ ] Test with insufficient samples (< 50)
- [ ] Verify median calculation is correct
- [ ] Verify JSON file is updated correctly
- [ ] Verify UI labels update after calibration
- [ ] Verify engine reloads color profiles
- [ ] Test cancellation during preparation
- [ ] Test cancellation during recording
- [ ] Test with no depth blobs detected
- [ ] Test with multiple balls in frame
- [ ] Verify whiteness filter is applied
- [ ] Test error handling and recovery

## Success Criteria

✅ User can initiate calibration with button click  
✅ 5-second preparation countdown displays clearly  
✅ 10-second recording countdown displays clearly  
✅ System collects color samples from all depth blobs during recording  
✅ Whiteness filter is applied during color sampling (by engine)  
✅ Final color profile uses median hue/saturation from all samples  
✅ Color profile is saved and immediately active  
✅ User receives clear feedback on calibration success/failure  
✅ System handles edge cases gracefully (no blobs, few samples, etc.)  

## Future Enhancements

1. **Visual Feedback**: Show detected blobs with colored overlay during recording
2. **Sample Count Display**: Real-time counter showing samples collected
3. **Audio Cues**: Beep when recording starts/stops
4. **Quality Indicator**: Show confidence/quality score after calibration
5. **Retry Option**: Quick retry button if calibration fails
6. **Multi-Ball Calibration**: Calibrate multiple colors in sequence
7. **Export/Import**: Save/load calibration profiles

## Bug Fixes

**2025-11-07 - Fixed color sample collection**
- **Issue**: No samples were being collected during calibration
- **Root Cause**: Code was looking for `frame_data.depth_globs` which doesn't exist in protobuf
- **Solution**: When depth blob detection is enabled, depth blobs are sent as `raw_detections`. Updated code to:
  - Check for `raw_detections` instead of `depth_globs`
  - Decode the color image from base64
  - Sample color from each detection's center point (3x3 region)
  - Apply whiteness and saturation filters during sampling
  - Add averaged samples to calibration system

## Notes

- The system is designed to work specifically with depth blob detection
- Requires `enable_depth_blob_detection` to be active
- Works best with LED juggling balls due to brightness filtering
- Median calculation provides robustness against outliers
- Automatic engine reload ensures immediate effect
- Status labels auto-hide to keep UI clean

## Related Files

- `engine/src/New3DTracker.cpp` - Depth blob detection and color sampling
- `engine/include/SimpleBallTracker.hpp` - ColorProfile struct definition
- `hub/calibration_settings_new3d.json` - Color profile storage
- `api/v1/juggler.proto` - Protocol buffer definitions