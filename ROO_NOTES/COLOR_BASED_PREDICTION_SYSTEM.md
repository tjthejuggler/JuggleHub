# Color-Based Prediction System

**Date:** 2025-10-06  
**Status:** ✅ Complete  
**Purpose:** Simplified prediction system based purely on color detection history

---

## Overview

The color-based prediction system replaces the complex Kalman filter prediction with a simpler, more intuitive approach that uses only the recent history of color detections to predict where a ball will be in the next few frames.

## Key Differences from Previous System

### Old System (Kalman Filter)
- Used internal Kalman state (position + velocity estimates)
- Prediction was based on filtered state, not raw detections
- Complex covariance matrix calculations
- Could drift from actual color detections

### New System (Color-Based)
- Uses **only** recent color detection positions
- Calculates velocity from actual detection history
- Simple, configurable parameters
- Always grounded in real color tracker data

## Architecture

### Components

1. **ColorBasedPredictor** ([`engine/include/ColorBasedPredictor.hpp`](engine/include/ColorBasedPredictor.hpp:1))
   - Lightweight prediction class
   - Maintains deque of recent detections with timestamps
   - Calculates velocity from detection history
   - Applies physics (gravity) for in-flight balls

2. **SimpleBall Integration** ([`engine/include/SimpleBallTracker.hpp`](engine/include/SimpleBallTracker.hpp:76))
   - Each ball has a `color_predictor` member
   - Updated every time a color detection is made
   - Independent from Kalman filter (which remains for fallback tracking)

3. **Configurable Settings** ([`engine/include/SimpleBallTracker.hpp`](engine/include/SimpleBallTracker.hpp:117-119))
   - `prediction_history_frames`: Number of frames to use (default: 5)
   - `prediction_radius_m`: Circle radius in meters (default: 0.15m = 15cm)
   - `prediction_time_s`: How far ahead to predict (default: 0.2s = 200ms)

## How It Works

### 1. Detection Recording
Every time a ball is detected via YOLO + color matching:
```cpp
ball.color_predictor.addDetection(ball.position);
```

This stores the 3D position and timestamp in a history buffer.

### 2. Velocity Calculation
When prediction is needed, the system:
1. Takes the last N detections (configurable, default 5)
2. Calculates velocity between consecutive detections
3. Averages the velocities to get a smooth estimate

### 3. Position Prediction
```cpp
predicted_position = current_position + velocity * time
```

### 4. Gravity Application (In-Air Mode)
For balls in flight:
```cpp
predicted_y += 0.5 * gravity * time²
```

For held balls, no gravity is applied (simpler linear prediction).

### 5. Visualization
The prediction circle shows:
- **Center**: Where the ball will likely be in 0.2 seconds
- **Radius**: Search region (~15cm, configurable)
- **Color**: 
  - Cyan for in-flight (with gravity)
  - Red for held (no gravity)
- **Label**: "P{id}({history_size})" - shows ball ID and number of detections used

## Configuration

### Via UDP Settings Module

You can adjust prediction parameters at runtime:

```python
# In hub or via UDP
send_setting("prediction_history_frames", "7")  # Use last 7 frames
send_setting("prediction_radius_m", "0.20")     # 20cm search radius
send_setting("prediction_time_s", "0.3")        # Predict 300ms ahead
```

### Default Values

```cpp
prediction_history_frames = 5      // Last 5 detections
prediction_radius_m = 0.15f        // 15cm radius
prediction_time_s = 0.2f           // 200ms ahead
```

## Use Cases

### 1. Predictive Search Region
The prediction circle shows where to prioritize searching for the ball in the next few frames. This is especially useful when:
- Ball is moving fast
- Occlusions are likely
- Multiple balls are close together

### 2. Debugging Tracking
- See if predictions match actual ball motion
- Identify when velocity estimates are wrong
- Tune history frame count for smoother/faster response

### 3. Understanding Ball Dynamics
- Visualize gravity effects on in-flight balls
- See difference between held (linear) and thrown (parabolic) motion
- Validate physics model

## Implementation Details

### File Structure

```
engine/include/ColorBasedPredictor.hpp  - Prediction class
engine/include/SimpleBallTracker.hpp    - Integration with balls
engine/src/SimpleBallTracker.cpp        - Detection recording
engine/src/Engine.cpp                   - Visualization rendering
```

### Key Code Locations

**Adding detections** ([`SimpleBallTracker.cpp:693`](engine/src/SimpleBallTracker.cpp:693)):
```cpp
ball.color_predictor.addDetection(ball.position);
```

**Getting predictions** ([`Engine.cpp:268`](engine/src/Engine.cpp:268)):
```cpp
cv::Point3f pred_pos = ball.color_predictor.getPredictedPosition(!ball.is_held);
```

**Rendering** ([`Engine.cpp:1100-1150`](engine/src/Engine.cpp:1100)):
- Projects 3D prediction to 2D
- Scales radius based on depth
- Draws semi-transparent circle

### Prediction Algorithm

```cpp
// 1. Calculate average velocity from history
for (i = 1; i < history.size(); i++) {
    dt = history[i].timestamp - history[i-1].timestamp
    velocity += (history[i].position - history[i-1].position) / dt
}
velocity /= (history.size() - 1)

// 2. Extrapolate position
predicted = current_position + velocity * prediction_time

// 3. Apply gravity if in air
if (is_in_air) {
    predicted.y += 0.5 * 9.81 * prediction_time²
}
```

## Advantages

1. **Simplicity**: Easy to understand and debug
2. **Transparency**: Directly based on what the color tracker sees
3. **Configurability**: All parameters are adjustable at runtime
4. **Dual Modes**: Separate physics for in-air vs held balls
5. **No Drift**: Always grounded in recent detections

## Limitations

1. **Requires History**: Needs at least 2 detections to work
2. **Lag Sensitivity**: Very sensitive to detection timing
3. **No Smoothing**: More jittery than Kalman filter
4. **Simple Physics**: Assumes constant gravity, no air resistance

## Future Enhancements

Potential improvements:
- Weighted velocity averaging (recent frames count more)
- Outlier rejection for bad detections
- Adaptive prediction time based on velocity
- Air resistance model for more accurate trajectories
- Separate settings per ball color

---

## Testing

To test the system:

1. **Enable visualization**: Toggle "Show Kalman Predictions" in hub UI
2. **Throw a ball**: Watch the prediction circle lead the ball's motion
3. **Hold a ball**: See the circle follow hand motion (no gravity)
4. **Adjust settings**: Try different history frame counts and radii
5. **Record video**: Save with visualizations to analyze offline

## Troubleshooting

**Prediction circle not showing:**
- Check if ball has at least 2 recent detections
- Verify `hasEnoughData()` returns true
- Ensure visualization toggle is enabled

**Prediction is wrong:**
- Increase `prediction_history_frames` for smoother estimates
- Decrease `prediction_time_s` for shorter-term predictions
- Check if ball state (held/in-air) is correct

**Circle too small/large:**
- Adjust `prediction_radius_m` setting
- Remember radius scales with depth (farther = larger pixels)

---

**Implementation completed:** 2025-10-06  
**Compiled successfully:** ✅  
**Ready for testing:** ✅