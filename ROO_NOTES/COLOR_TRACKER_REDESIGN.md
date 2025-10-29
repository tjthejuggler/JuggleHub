# Color Tracker Redesign

**Date:** 2025-09-30  
**Last Updated:** 2025-10-01 - Fixed multi-ball activation bug  
**Status:** Implemented and Built Successfully

## Overview

The color tracking system has been fundamentally reworked to be simpler, more direct, and better integrated with ByteTrack and skeleton tracking. The new system removes the laggy Kalman filter from color tracking and focuses on immediate, responsive tracking.

## Key Changes

### 1. New ColorTracker Class

Created a new [`ColorTracker`](engine/include/ColorTracker.hpp) class that replaces the complex color tracking logic in [`BallTracker`](engine/include/BallTracker.hpp).

**Design Principles:**
- **No Kalman filtering** - Direct position updates for immediate response
- **ByteTrack integration** - Associates color profiles with ByteTrack detections
- **Wrist association** - Tracks balls near wrists using skeleton tracking data
- **Persistent color profiles** - Loads and saves color calibrations from past runs

### 2. Integration Architecture

```
┌─────────────────┐
│   DNNTracker    │
│  (Main System)  │
└────────┬────────┘
         │
         ├──► ByteTrack (YOLO detections)
         │
         ├──► Skeleton Tracking (Wrist positions)
         │
         └──► ColorTracker (New!)
              │
              ├─ Uses ByteTrack detections
              ├─ Uses wrist positions
              └─ Outputs color-tracked balls
```

### 3. Tracking Logic

The new ColorTracker follows this simple, step-by-step process:

#### Step 1: Activation
- Inactive trackers look for ByteTrack detections
- If a detection matches a known color profile, activate that tracker
- Associate the tracker with that color

#### Step 2: Wrist Association
- Check if any active ball is near a wrist (< 15cm in 3D space)
- If yes, associate the ball with that wrist
- Search for the largest color blob around the wrist position
- Update ball position even if ByteTrack loses it

#### Step 3: ByteTrack Fallback
- If not associated with a wrist, look for ByteTrack detections
- Find detections that match the ball's color profile
- Choose the closest detection to last known position
- Update ball position

#### Step 4: Simple Color Tracking
- If no ByteTrack detection found, do simple color search
- Look for largest color blob around last known position
- Search within 100-pixel radius
- Update if found

#### Step 5: Loss Management
- If ball not found for 30 frames (~1 second), deactivate tracker
- Reset wrist association if ball moves away from wrist

## Bug Fixes

### Multi-Ball Activation Fix (2025-10-01)

**Issue:** Only one color tracker was activating at a time, even when multiple balls were present.

**Root Cause:** In the activation logic (Step 1), the nested loops had premature `break` statements that prevented multiple balls from being activated simultaneously. After finding the first match, the code would exit both the color profile loop AND the ByteTrack objects loop, preventing other balls from being checked.

**Solution:** 
1. Added a `std::set<int>` to track which ByteTrack detections have been assigned to avoid double-assignment
2. Added logic to check if a color is already in use by another active ball
3. Restructured the break statements so that:
   - After matching a color profile, we break out of the color profiles loop
   - After activating a ball, we break out of the ByteTrack objects loop
   - But the outer loop continues to check remaining inactive balls

**Changes Made:**
- Modified [`ColorTracker::update()`](engine/src/ColorTracker.cpp:42-95) activation logic
- Added `#include <set>` to ColorTracker.cpp
- Now properly activates multiple balls simultaneously when they match different color profiles


## Implementation Details

### Files Created

1. **[`engine/include/ColorTracker.hpp`](engine/include/ColorTracker.hpp)**
   - Header file defining the ColorTracker class
   - Defines ColorProfile and ColorTrackedBall structures
   - Forward declarations to avoid circular dependencies

2. **[`engine/src/ColorTracker.cpp`](engine/src/ColorTracker.cpp)**
   - Implementation of all tracking logic
   - Color blob detection using OpenCV
   - Settings management (load/save color profiles)
   - Color calibration support

### Files Modified

1. **[`engine/include/DNNTracker.hpp`](engine/include/DNNTracker.hpp)**
   - Added ColorTracker member variable
   - Added getter for color-tracked balls
   - Includes ColorTracker header

2. **[`engine/src/DNNTracker.cpp`](engine/src/DNNTracker.cpp)**
   - Initialize ColorTracker in constructor
   - Call ColorTracker update after pose estimation
   - Pass ByteTrack objects and tracked hands to ColorTracker

3. **[`engine/CMakeLists.txt`](engine/CMakeLists.txt)**
   - Added ColorTracker.cpp to build sources

## Key Features

### 1. Color Profile Management

Color profiles are stored in `ball_settings.json` and include:
- HSV color ranges (min/max for H, S, V)
- Support for wrap-around colors (like red/pink)
- Persistent across runs

**Default Profiles:**
```json
{
  "pink": {"min_hsv": [150, 150, 90], "max_hsv": [170, 255, 255]},
  "orange": {"min_hsv": [5, 150, 120], "max_hsv": [15, 255, 255]},
  "green": {"min_hsv": [45, 120, 70], "max_hsv": [75, 255, 255]},
  "yellow": {"min_hsv": [25, 120, 100], "max_hsv": [35, 255, 255]}
}
```

### 2. Wrist Association

When a ball gets within 15cm of a wrist:
- Ball is associated with that wrist (left or right)
- Color tracking searches around wrist position
- Continues tracking even if ByteTrack loses the ball
- Association breaks when ball moves > 22.5cm away (hysteresis)

### 3. No Smoothing

Unlike the old system:
- **No Kalman filter** - positions update immediately
- **No interpolation** - shows actual detected position
- **No lag** - responsive to rapid movements
- **Simple and predictable** - easier to debug

## Parameters

Key parameters in [`ColorTracker.hpp`](engine/include/ColorTracker.hpp):

```cpp
static constexpr int NUM_BALLS = 3;                          // Track up to 3 balls
static constexpr float WRIST_ASSOCIATION_DISTANCE = 0.15f;   // 15cm for catch detection
static constexpr int WRIST_SEARCH_RADIUS = 100;              // pixels around wrist
static constexpr int MAX_FRAMES_LOST = 30;                   // ~1 second at 30fps
static constexpr float MIN_DEPTH = 0.2f;                     // 20cm minimum depth
static constexpr float MAX_DEPTH = 3.0f;                     // 3m maximum depth
static constexpr double MIN_BLOB_AREA = 50.0;                // minimum blob size in pixels
```

## API

### ColorTrackedBall Structure

```cpp
struct ColorTrackedBall {
    int logical_id;              // Persistent ID (0, 1, 2)
    std::string color_name;      // Associated color profile
    cv::Point2f pixel_pos;       // Current 2D position
    cv::Point3f world_pos;       // Current 3D position
    bool is_active;              // Whether tracking
    int associated_wrist_id;     // -1 or 0=left, 1=right
    int frames_since_seen;       // Frames without detection
};
```

### Main Update Function

```cpp
std::vector<ColorTrackedBall> update(
    const cv::Mat& color_frame,
    const cv::Mat& depth_frame,
    const rs2_intrinsics& intrinsics,
    const std::vector<TrackedObject>& bytetrack_objects,
    const std::vector<TrackedHand>& tracked_hands
);
```

## Advantages Over Old System

1. **Simpler Logic**
   - No complex Kalman filter state management
   - Straightforward if-then decision tree
   - Easier to understand and debug

2. **Better Integration**
   - Uses ByteTrack detections directly
   - Leverages skeleton tracking for wrist positions
   - Combines multiple data sources intelligently

3. **More Responsive**
   - No smoothing lag
   - Immediate position updates
   - Better for fast juggling movements

4. **Persistent Color Profiles**
   - Remembers calibrations across runs
   - Automatically associates colors with detections
   - Reduces need for re-calibration

5. **Wrist-Aware Tracking**
   - Continues tracking when ball is held
   - Handles catches and throws naturally
   - Uses 3D distance for accurate association

## Testing

The system has been successfully built and compiled. To test:

1. **Build the engine:**
   ```bash
   cd engine/build
   cmake ..
   make -j$(nproc)
   ```

2. **Run the system:**
   ```bash
   ./scripts/run_hub.sh
   ```

3. **Verify color tracking:**
   - Balls should be tracked by color
   - When caught, tracking should continue around wrist
   - When thrown, tracking should resume with ByteTrack
   - Color profiles should persist across restarts

## Future Improvements

Potential enhancements:

1. **Dynamic color learning** - Automatically adjust color ranges based on lighting
2. **Multi-ball disambiguation** - Better handling when multiple balls have similar colors
3. **Confidence scoring** - Add confidence values to color matches
4. **Adaptive search radius** - Adjust search area based on ball velocity
5. **Color profile UI** - Visual interface for color calibration

## Troubleshooting

If color tracking isn't working:

1. **Check color profiles** - Verify `ball_settings.json` has correct HSV ranges
2. **Lighting conditions** - Color tracking is sensitive to lighting
3. **ByteTrack integration** - Ensure ByteTrack is detecting balls
4. **Wrist tracking** - Verify skeleton tracking is working
5. **Depth data** - Check that depth values are valid (0.2m - 3.0m)

## Technical Notes

- Uses OpenCV's `inRange()` for color segmentation
- Morphological operations (open/close) clean up noise
- Contour detection finds color blobs
- Distance-based matching associates detections
- Forward declarations avoid circular dependencies with DNNTracker

## UI Integration

The color tracker toggle in the UI now controls the new simplified color tracking system:

**Location:** [`hub/components/ui.py`](hub/components/ui.py:645-649)

**Visualization:** Lines 895-930 display color-tracked balls with:
- Color names and logical IDs
- Wrist association (dashed outline when near wrist)
- Depth information
- Distinct colors for each ball (Orange, Yellow, Green)

**Data Flow:**
1. Engine sends `color_tracked_balls` in FrameData protobuf
2. Hub receives and displays them when toggle is enabled
3. No Kalman filtering - immediate position updates
4. Shows wrist association status visually

## Conclusion

The new ColorTracker provides a simpler, more direct approach to color-based ball tracking. By removing the Kalman filter and focusing on immediate updates, it's more responsive and easier to understand. The integration with ByteTrack and skeleton tracking makes it more robust and capable of handling complex juggling scenarios like catches and throws.

**The color tracker toggle now controls this new, improved system** - no more laggy, drifting tracking!

---

**Last Updated:** 2025-10-01
**Build Status:** ✅ Successfully compiled
**Integration Status:** ✅ Fully integrated with DNNTracker and UI
**UI Connection:** ✅ Color tracker toggle connected to new system