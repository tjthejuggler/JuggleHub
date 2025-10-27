# Held Circle Offset Implementation

**Date:** 2025-10-27  
**Status:** ✅ Complete

## Overview

Implemented a configurable offset for the "held circle" center in the New 3D Kalman tracking system. The held circle center is now positioned along the forearm direction from the wrist towards the hand center, rather than being fixed at the wrist position.

## Problem

Previously, the held circle center was positioned exactly at the wrist position. This was not ideal because:
- The actual center of the hand where balls are held is further along the forearm
- Detection accuracy for held balls could be improved by positioning the held circle more centrally in the hand

## Solution

Added a new setting `held_circle_offset_cm` that allows configuring the distance (in centimeters) from the wrist towards the hand center. The offset is calculated using the forearm skeleton direction (from elbow to wrist).

## Changes Made

### 1. Backend (C++)

#### [`engine/include/New3DTracker.hpp`](engine/include/New3DTracker.hpp:175)
- Added `float held_circle_offset_cm = 5.0f;` to [`New3DTrackerSettings`](engine/include/New3DTracker.hpp:172) struct
- Default value: 5cm (halfway between wrist and palm)

#### [`engine/src/New3DTracker.cpp`](engine/src/New3DTracker.cpp)

**Settings Management:**
- Added loading of `held_circle_offset_cm` in [`loadSettings()`](engine/src/New3DTracker.cpp:310) (line 324)
- Added saving of `held_circle_offset_cm` in [`saveSettings()`](engine/src/New3DTracker.cpp:436) (line 485)
- Added handling in [`updateSetting()`](engine/src/New3DTracker.cpp:2263) (line 2308)

**Held Ball Prediction:**
- Updated [`predictHeldBall()`](engine/src/New3DTracker.cpp:205) to calculate offset position:
  - Extracts elbow and wrist keypoints from pose skeleton
  - Calculates forearm direction vector (wrist - elbow)
  - Normalizes direction and applies offset distance
  - Falls back to wrist position if skeleton data unavailable

**Visualization:**
- Updated [`drawHandThresholds()`](engine/src/New3DTracker.cpp:2223) to:
  - Draw held circle at offset position (not just wrist)
  - Show wrist position as small dot for reference
  - Use same forearm direction calculation as prediction

### 2. Frontend (Python)

#### [`hub/components/ui_settings_new3d.py`](hub/components/ui_settings_new3d.py:28)
- Added slider in [`create_physics_section()`](hub/components/ui_settings_new3d.py:28) (after line 57):
  - Label: "Held Circle Offset (cm)"
  - Range: 0-15cm
  - Default: 5cm
  - Tooltip explains the feature and usage

## Technical Details

### Forearm Direction Calculation

The offset uses the COCO pose keypoint format:
- Keypoint 7: Left elbow
- Keypoint 8: Right elbow  
- Keypoint 9: Left wrist
- Keypoint 10: Right wrist

Direction vector: `forearm_dir = normalize(wrist_pos - elbow_pos)`  
Offset position: `held_center = wrist_pos + forearm_dir * (offset_cm / 100.0)`

### Fallback Behavior

If skeleton data is unavailable or invalid:
- Falls back to wrist position (0cm offset)
- Ensures backward compatibility
- No errors or crashes

## Usage

### UI Configuration

1. Navigate to Settings → New 3D Tracker → Physics & Kalman Filter
2. Adjust "Held Circle Offset (cm)" slider:
   - **0cm**: Held circle at wrist (old behavior)
   - **5cm**: Default, positions circle towards palm
   - **10cm+**: Positions circle in hand center

### Visualization

When "Show Held Radius" is enabled:
- Yellow circle shows the held detection zone at the offset position
- Small yellow dot shows the wrist position for reference
- Hand label (L/R) appears at the held circle center

### Recording

The offset is automatically applied to:
- Real-time video feed visualization
- Recording frame images
- All catch/throw detection logic

## Benefits

1. **More Accurate Held Detection**: Circle positioned where balls are actually held
2. **Configurable**: Users can adjust based on hand size and juggling style
3. **Backward Compatible**: Default 5cm works well, 0cm gives old behavior
4. **Consistent**: Same offset used for detection, visualization, and recording

## Testing Recommendations

1. **Basic Functionality**:
   - Verify held balls track correctly with default 5cm offset
   - Test with 0cm (should match old behavior)
   - Test with 10cm+ (should position in hand center)

2. **Edge Cases**:
   - Test with partial skeleton data (missing elbow)
   - Test with occluded hands
   - Verify fallback to wrist position works

3. **Visualization**:
   - Confirm yellow circle appears at offset position
   - Verify wrist dot is visible
   - Check recording images show offset correctly

4. **Settings Persistence**:
   - Change offset value and restart app
   - Verify setting is saved and loaded correctly

## Files Modified

- [`engine/include/New3DTracker.hpp`](engine/include/New3DTracker.hpp)
- [`engine/src/New3DTracker.cpp`](engine/src/New3DTracker.cpp)
- [`hub/components/ui_settings_new3d.py`](hub/components/ui_settings_new3d.py)

## Related Settings

- **Held Radius (cm)**: Defines the size of the held detection zone
- **Held Circle Offset (cm)**: Defines where the center of that zone is positioned

Both settings work together to define the held ball detection area.