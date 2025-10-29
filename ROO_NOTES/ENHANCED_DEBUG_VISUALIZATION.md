# Enhanced Debug Visualization System

**Date:** 2025-10-06  
**Purpose:** Organized tracking information display for debugging color tracker flickering issues

## Overview

The visualization system has been enhanced to provide clear, organized tracking information in an upper-right info panel, with numbered YOLO detections and color-coded ball tracking data.

## Visualization Features

### 1. **Numbered YOLO Detections**
- Each YOLO detection box is numbered with `#1`, `#2`, etc.
- Numbers appear in white text on the detection box itself
- Corresponding info appears in the tracking panel

### 2. **Info Panel (Upper Right Corner)**
- Semi-transparent black background for readability
- Title: "TRACKING INFO"
- Line-by-line information display

### 3. **YOLO Detection Info** (White Text)
Format: `#N YOLO: class conf=X.XX`
- `#N` - Detection number (matches box number)
- `class` - Either "ball" (class_id=0) or "ball_held" (class_id=1)
- `conf` - YOLO confidence score

Example:
```
#1 YOLO: ball conf=0.92
#2 YOLO: ball_held conf=0.85
```

### 4. **Color Tracker Info** (Ball Color)
Format: `color: STATE [HAND] z=X.XXm | tracking_reason`
- `color` - Ball color name (pink, green, etc.)
- `STATE` - Either "FLIGHT" or "HELD"
- `[HAND]` - Hand association: [L] for left, [R] for right (only when HELD)
- `z` - Distance from camera in meters
- `tracking_reason` - Why this tracking decision was made

Examples:
```
pink: FLIGHT z=1.23m | YOLO: cls=0 conf=0.92 col=0.85
green: HELD [R] z=0.45m | Kalman+Near[R] d=0.08m
```

### 5. **Tracking Reasons**

The system now shows exactly why each tracking decision was made:

- **`YOLO: cls=N conf=X.XX col=X.XX`** - Using YOLO detection
  - `cls` - Class ID (0=ball, 1=ball_held)
  - `conf` - YOLO confidence
  - `col` - Color match score

- **`Kalman pred`** - Using Kalman prediction for free flight

- **`Kalman+Near[H] d=X.XXm`** - Kalman prediction near a hand
  - `H` - Hand ID (L or R)
  - `d` - Distance to hand

- **`Traj→[H] d=X.XXm`** - Trajectory leads to hand
  - `H` - Hand ID (L or R)
  - `d` - Predicted distance to hand

- **`Traj→Flight`** - Trajectory indicates free flight

### 6. **Ball Visualization**
- Filled circles for balls in flight
- Hollow circles for held balls
- Single letter label on ball (e.g., "p" for pink, "g" for green)
- Color matches the ball's actual color from color profiles

## How to Use

1. **Enable Visualizations:**
   - In the UI, enable "Show Color Tracker" visualization
   - Optionally enable "Show Raw Detections" for YOLO boxes

2. **Record Frames:**
   - Use "Record" or "Start Continuous Recording"
   - Frames will be saved in `with_visualizations` folder

3. **Analyze Tracking:**
   - Open recorded frames
   - Check YOLO detection numbers on boxes
   - Read corresponding info in upper-right panel
   - For color trackers, note the tracking reason
   - Verify decisions make sense given ball state

## Debugging Workflow

### For Flickering Issues:

1. **Identify Problem Frames:**
   - Note frame numbers where flickering occurs
   - Look at frames 171-175 in your case

2. **Check YOLO Detections:**
   - Are there multiple detections?
   - What are their class IDs and confidences?
   - Which detection should be chosen?

3. **Check Color Tracker Decisions:**
   - What tracking_reason is shown?
   - Does it match expected behavior?
   - Is the ball in FLIGHT or HELD state?

4. **Verify Priority Logic:**
   - Free-flight balls (cls=0) should get 3x weight
   - High confidence YOLO should dominate
   - Trajectory validation should prevent impossible jumps

### Example Analysis:

**Frame 171:**
```
#1 YOLO: ball conf=0.92
#2 YOLO: ball_held conf=0.45
pink: FLIGHT z=1.20m | YOLO: cls=0 conf=0.92 col=0.85
```
✅ Correct - Using high-confidence free-flight detection

**Frame 172 (Problem):**
```
#1 YOLO: ball conf=0.88
#2 YOLO: ball_held conf=0.50
pink: HELD [R] z=0.35m | Traj→[R] d=0.12m
```
❌ Issue - Should use YOLO detection #1, not snap to hand

## Files Modified

1. **[`engine/include/SimpleBallTracker.hpp:88`](engine/include/SimpleBallTracker.hpp:88)** - Added `tracking_reason` field
2. **[`engine/src/SimpleBallTracker.cpp`](engine/src/SimpleBallTracker.cpp)** - Populated tracking reasons
3. **[`engine/src/Engine.cpp:908-1101`](engine/src/Engine.cpp:908)** - Enhanced visualization rendering

## Configuration

The info panel can be adjusted by modifying these values in [`Engine.cpp:1066-1070`](engine/src/Engine.cpp:1066):

```cpp
int panel_x = result.cols - 550;  // Right side position
int panel_y = 30;                 // Top margin
int line_height = 25;             // Space between lines
int panel_width = 540;            // Panel width
```

## Benefits

1. **Clear Organization** - All tracking info in one place
2. **Easy Correlation** - Numbered boxes match panel entries
3. **Color Coding** - Ball colors match their tracking info
4. **Detailed Reasons** - Know exactly why each decision was made
5. **Debugging Efficiency** - Quickly identify tracking logic issues

## Next Steps

If flickering persists after reviewing the visualization:

1. Adjust priority weights in [`SimpleBallTracker.cpp:277-315`](engine/src/SimpleBallTracker.cpp:277)
2. Tune trajectory validation thresholds
3. Consider adding projectile motion prediction for free-flight tracking
4. Review Kalman filter parameters