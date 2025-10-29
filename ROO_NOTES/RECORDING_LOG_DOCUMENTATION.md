# Recording Log Documentation

**Last Updated:** 2025-10-06 18:07:00 UTC

## Overview

The JuggleHub system now includes a comprehensive recording log feature that captures detailed frame-by-frame tracking information during recording sessions. This log is specifically designed to help debug and analyze the Kalman filter prediction system and color tracker behavior.

## What Gets Logged

For each frame during a recording session, the system logs:

### 1. Frame Information
- **Frame number** - Sequential frame counter starting from 0
- **Timestamp** - Exact time when the frame was processed
- **Number of tracked balls** - How many balls are being tracked
- **Number of detected hands** - How many hands were detected

### 2. Hand Positions
For each detected hand:
- Hand ID (0=left, 1=right)
- 3D wrist position in meters (x, y, z)
- Visibility status
- Detection confidence

### 3. Ball Tracking Details
For each tracked ball, the log captures:

#### Current State
- **Position 3D** - Ball position in world coordinates (meters)
- **Position 2D** - Ball position in pixel coordinates
- **Bounding Box** - YOLO detection bounding box [x, y, width, height]
- **State** - HELD or IN_FLIGHT
- **Held by hand** - Which hand is holding it (-1 if not held)
- **Distance to nearest wrist** - In meters
- **Has YOLO detection** - Whether YOLO detected the ball this frame
- **Frames without YOLO** - Counter for fallback tracking
- **YOLO confidence** - Detection confidence score
- **YOLO class** - "ball" or "ball_held"
- **Color match score** - How well the detection matches the assigned color
- **Tracking reason** - Debug string explaining why this position was chosen

#### Color Predictor History (KEY FEATURE)
This is the **complete history** used by the color-based prediction system:

- **History size** - Number of frames in the history buffer
- **Has enough data** - Whether prediction can be performed (needs ≥2 frames)
- **History entries** - Complete list of all positions in the history buffer, showing:
  - Index in history (0 = oldest, N-1 = newest)
  - 3D position (x, y, z) in meters
  - Timestamp in milliseconds since epoch
- **Calculated velocity** - Average velocity computed from history (m/s)
- **Speed magnitude** - Total speed in m/s

#### Prediction Details
When enough history is available:

- **Prediction time delta** - How far ahead the prediction looks (typically 1/60s)
- **Gravity applied** - YES for in-flight balls, NO for held balls
- **Predicted position** - Where the system expects the ball to be (x, y, z)
- **Prediction radius** - Search radius in meters (typically 0.15m = 15cm)
- **Prediction error** - Distance between predicted and actual position
- **Error components** - Breakdown of error in x, y, z directions

#### Legacy Kalman Filter
- Status of the legacy Kalman filter (only used as fallback when YOLO fails)
- Whether Kalman fallback is currently active

#### Detection Evaluations
For each YOLO detection that was evaluated for this ball:
- Detection index number
- Result (SELECTED, REJECTED, or SCORED)
- Whether it passed initial filters
- Scoring breakdown:
  - Total score
  - Class score (ball vs ball_held)
  - Confidence score
  - Color match score
  - Kalman proximity score
- Distance to prediction (if applicable)

## File Location

The recording log is automatically created when you make a recording:

```
engine/data/1_raw_recordings/
└── [recording_session_name]/
    ├── recording.log          ← THE LOG FILE
    ├── no_boxes/
    │   └── [frame images]
    └── with_visualizations/   (if enabled)
        └── [frame images with overlays]
```

## Example Log Entry

```
================================================================================
FRAME 42
================================================================================
Timestamp: 2025-10-06 18:05:30
Number of tracked balls: 3
Number of detected hands: 2

--- HAND POSITIONS ---
  Hand 0 (LEFT):
    Wrist 3D: (0.1234, -0.0567, 0.8901) m
    Visible: YES
    Confidence: 0.892

--- BALL 0 (green) ---
  Current State:
    Position 3D: (0.0523, 0.1234, 0.7654) m
    Position 2D (pixel): (320.45, 240.12)
    BBox: [310.2, 230.5, 20.8, 22.1]
    State: IN_FLIGHT
    Held by hand: NONE
    Distance to nearest wrist: 0.2341 m
    Has YOLO detection: YES
    Frames without YOLO: 0
    YOLO confidence: 0.876
    YOLO class: ball
    Color match score: 0.923
    Tracking reason: YOLO detection with high confidence

  COLOR PREDICTOR HISTORY:
    History size: 5 frames
    Has enough data for prediction: YES
    History entries (oldest to newest):
      [0] Position: (0.0489, 0.1156, 0.7823) m, Timestamp: 1728234329850 ms
      [1] Position: (0.0501, 0.1189, 0.7756) m, Timestamp: 1728234329867 ms
      [2] Position: (0.0512, 0.1201, 0.7712) m, Timestamp: 1728234329883 ms
      [3] Position: (0.0518, 0.1218, 0.7689) m, Timestamp: 1728234329900 ms
      [4] Position: (0.0523, 0.1234, 0.7654) m, Timestamp: 1728234329917 ms
    Calculated velocity: (0.0204, 0.0468, -0.1016) m/s
    Speed magnitude: 0.1134 m/s

  PREDICTION DETAILS:
    Prediction time delta: 0.016667 s
    Gravity applied: YES
    Predicted position: (0.0526, 0.1242, 0.7637) m
    Prediction radius: 0.1500 m
    Prediction error: 0.0023 m
    Error components: dx=0.0003, dy=0.0008, dz=-0.0017 m

  LEGACY KALMAN FILTER:
    (Note: Kalman filter is only used as fallback when YOLO fails)
    Frames without YOLO: 0
    Kalman fallback is INACTIVE (using YOLO)

  DETECTION EVALUATIONS:
    Number of detections evaluated: 3
    Detection #1:
      Result: SELECTED
      Passed filters: YES
      Total score: 8.7234
      Class score: 3.0000
      Confidence score: 1.7520
      Color score: 0.9230
      Kalman score: 0.0000
```

## Use Cases

### 1. Debugging Prediction Issues
When you see strange prediction behavior:
- Check the **COLOR PREDICTOR HISTORY** section
- Verify that the history contains reasonable positions
- Look at the calculated velocity - does it make sense?
- Check if gravity is being applied correctly (YES for in-flight, NO for held)

### 2. Analyzing Tracking Failures
When a ball is lost or tracked incorrectly:
- Look at **Frames without YOLO** - is YOLO failing to detect?
- Check **Detection Evaluations** - why were detections rejected?
- Review **Color match score** - is the color calibration good?
- Examine **Tracking reason** - what decision was made?

### 3. Tuning Prediction Parameters
To optimize prediction accuracy:
- Compare **Predicted position** vs **Actual position**
- Calculate average **Prediction error** across frames
- Adjust prediction radius if errors are consistently outside the search area
- Tune history size if velocity calculations seem unstable

### 4. Understanding State Transitions
When throw/catch detection seems wrong:
- Track **State** changes (HELD ↔ IN_FLIGHT)
- Monitor **Distance to nearest wrist**
- Check **Held by hand** assignments
- Verify **YOLO class** matches expected state

## Tips for Analysis

1. **Use grep to filter**: 
   ```bash
   grep "FRAME" recording.log          # See all frame numbers
   grep "green" recording.log          # Track a specific ball
   grep "Prediction error" recording.log  # Find prediction errors
   ```

2. **Compare consecutive frames**: Look at how history evolves frame-by-frame

3. **Focus on problem areas**: Use frame numbers from video to find specific issues

4. **Check velocity consistency**: Sudden velocity changes indicate tracking problems

5. **Monitor history size**: Should stay at configured max (typically 5 frames)

## Configuration

The prediction system can be configured in `ball_settings.json`:

```json
{
  "tracking_settings": {
    "prediction_history_frames": 5,      // Number of frames in history
    "prediction_radius_m": 0.15          // Search radius in meters
  }
}
```

## Technical Details

- **History buffer**: Uses `std::deque<DetectionPoint>` with automatic size management
- **Velocity calculation**: Averages velocity between all consecutive history points
- **Gravity**: Applied as `y += 0.5 * g * t²` where g = 9.81 m/s²
- **Timestamps**: High-resolution `std::chrono::steady_clock` for accurate dt calculation
- **Thread safety**: Log writes are synchronized with frame buffer access

## Troubleshooting

**Q: Log file is empty or missing**
- Check that recording was saved successfully
- Verify directory permissions in `engine/data/1_raw_recordings/`

**Q: History shows only 1-2 frames**
- Ball was just detected - history builds up over time
- Check if ball is being lost and re-detected frequently

**Q: Prediction error is very large**
- Ball may be moving erratically (bouncing, spinning)
- History may contain positions from different trajectory segments
- Consider increasing prediction radius or history size

**Q: Velocity seems wrong**
- Check timestamps - are they incrementing properly?
- Verify positions are in correct coordinate system (meters)
- Look for sudden jumps in position (tracking failures)

## Related Documentation

- [`COLOR_BASED_PREDICTION_SYSTEM.md`](COLOR_BASED_PREDICTION_SYSTEM.md) - Prediction algorithm details
- [`BALL_TRACKING_USER_GUIDE.md`](BALL_TRACKING_USER_GUIDE.md) - General tracking guide
- [`DEBUG_LOGGING_GUIDE.md`](DEBUG_LOGGING_GUIDE.md) - Other debugging tools