# Engine Debug Logging Guide

**Date:** 2025-10-21  
**Purpose:** Comprehensive debug logging for New3DTracker to diagnose ball tracking issues

## Overview

Added extensive debug logging to the New3DTracker to help diagnose why the pink ball incorrectly snapped to the left hand when it should have stayed with the right hand (as seen in recording frames 198-203).

## How to Enable Debug Logging

Use the `--engine-log` flag when running the hub:

```bash
./scripts/run_hub.sh --use-venv --device GPU --engine-log
```

This will:
1. Start the engine with the `--debug-log` flag
2. Write all debug output to `engine_debug.log` in the project root
3. Write standard output to `engine.log`

## What Gets Logged

### 1. Frame Header
- **Frame number** (incremental counter)
- **Recording frame number** (if recording is active, -1 otherwise)

Example:
```
================================================================================
FRAME 198 (Recording Frame: 198)
================================================================================
```

### 2. YOLO Detections
For each detection from YOLO:
- Detection index
- Bounding box coordinates `[x, y, width, height]`
- Center 2D position (calculated from bbox)
- World position in 3D `(x, y, z)` meters
- Confidence score
- Class (ball or ball_held)

Example:
```
--- YOLO DETECTIONS ---
Number of detections: 1
  Detection 0:
    BBox: [164.0, 213.0, 36.0, 29.0]
    Center 2D: (182.0, 227.5)
    World Pos: (-0.5056, -0.0471, 1.3560) m
    Confidence: 0.85
    Class: ball
```

### 3. Hand Positions
For each detected hand:
- Hand ID (0=LEFT, 1=RIGHT)
- Wrist 3D position in meters
- Visibility status
- Confidence score

Example:
```
--- HAND POSITIONS ---
Number of hands: 2
  Hand 0 (LEFT):
    Wrist 3D: (-0.0036, 0.3775, 1.5000) m
    Visible: YES
    Confidence: 0.958
  Hand 1 (RIGHT):
    Wrist 3D: (-0.5678, 0.3417, 1.5390) m
    Visible: YES
    Confidence: 0.984
```

### 4. Prediction Step
For each ball:
- Ball ID and color name
- Predicted position from Kalman filter

Example:
```
--- STEP 1: PREDICTION ---
Ball 0 (pink) predicted at: (-0.5312, 0.0342, 1.3770) m
```

### 5. Association Step
Shows the matching process between balls and detections:
- Number of matched pairs
- For each match:
  - Ball ID and color
  - Detection world position
  - Total distance/cost
  - Detailed cost breakdown (distance + color penalty)
  - Color matching information

Example:
```
--- STEP 2: ASSOCIATION ---
  associateDetections: 1 balls, 1 detections
  Starting greedy association...
  Iteration 1: Matched Ball 0 (pink) to Detection at (-0.5312, 0.0342, 1.3770) | dist=0.025m (colors match: pink) = total_cost=0.025m
Matched pairs: 1
  Ball 0 (pink) <-> Detection at (-0.5312, 0.0342, 1.3770) | Distance: 0.025m
Unmatched balls: 0
Unmatched detections: 0
```

### 6. State Update (Matched Balls)
For balls in **IN_FLIGHT** state:
- Detection position
- Distance to each hand
- Catch threshold comparison
- Whether catch was detected

Example:
```
--- STEP 3: UPDATE MATCHED BALLS ---
    handleInFlightStateUpdate for Ball 0 (pink)
      Detection at: (-0.5699, 0.2836, 1.3930) m
      Checking catch distances (threshold: 0.12m):
        Hand 0 (LEFT): distance=0.6107m
        No catch (distance 0.6107m >= threshold 0.12m)
        Hand 1 (RIGHT): distance=0.1467m
        >>> CATCH DETECTED! Ball 0 caught by Hand 1
```

For balls in **HELD** state:
- Currently held by which hand
- Detection position
- Distance to holding hand
- Throw detection logic (distance exceeded, velocity exceeded, hand velocity check)
- Whether throw was detected

Example:
```
    handleHeldStateUpdate for Ball 0 (pink)
      Currently held by Hand 1
      Detection at: (-0.5699, 0.2836, 1.3930) m
      Distance to holding hand: 0.05m (threshold: 0.12m)
      Throw detection: distance_exceeded=false, velocity_exceeded=false, hand_velocity_check=true
      No throw detected - ball remains HELD
```

### 7. Final Ball States
After all processing:
- Ball ID and color
- State (HELD or IN_FLIGHT)
- Final position
- Associated hand ID
- Frames since seen
- Tracking reason

Example:
```
--- FINAL BALL STATES ---
Ball 0 (pink):
  State: HELD
  Position: (0.0124, 0.4004, 1.5580) m
  Held by hand: 0
  Frames since seen: 0
  Tracking reason: Catch detected
```

### 8. Events
Any throw/catch events that occurred:

Example:
```
--- EVENTS ---
CATCH event for ball 0 and hand 1
```

## Key Information for Debugging

### The Incorrect Snap Issue

Looking at the logs from frames 198-203, we need to track:

1. **YOLO Detection Positions**: Where did YOLO actually detect the ball?
2. **Color Matching**: Did the detection match the pink color profile?
3. **Association Decision**: Which ball was matched to which detection and why?
4. **State Transitions**: When and why did the ball transition from IN_FLIGHT to HELD?
5. **Hand Assignment**: Why was it assigned to the left hand instead of the right hand?

### Critical Questions to Answer

1. Was there a YOLO detection near the left hand at frame 203?
2. Did the color matching incorrectly identify a detection as pink?
3. Did the association algorithm choose the wrong detection due to distance or color penalties?
4. Did the catch detection logic incorrectly trigger for the left hand?
5. Was the ball position somehow set to the left hand's wrist position without a proper detection?

## Output Location

Debug logs are written to:
- **stdout** (console output)
- Can be redirected to a file: `./engine/build/jugglehub_engine 2>&1 | tee engine_debug.log`

## Performance Note

Debug logging adds overhead. Only enable it when actively debugging issues. Disable for production use by not setting `JUGGLEHUB_DEBUG=1`.

## Related Files

- [`engine/src/New3DTracker.cpp`](engine/src/New3DTracker.cpp) - Main tracking implementation with debug logging
- [`engine/include/DebugLog.hpp`](engine/include/DebugLog.hpp) - Debug logging utility
- [`engine/include/New3DTracker.hpp`](engine/include/New3DTracker.hpp) - Tracker header with frame counter

## Next Steps

1. Run the engine with `JUGGLEHUB_DEBUG=1`
2. Reproduce the issue (pink ball snapping to left hand)
3. Examine the debug logs for frames 198-203
4. Look for:
   - Unexpected YOLO detections
   - Color matching errors
   - Association mismatches
   - Incorrect state transitions
   - Hand assignment logic errors