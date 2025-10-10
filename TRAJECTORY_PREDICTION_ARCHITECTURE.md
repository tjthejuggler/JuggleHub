# Trajectory Prediction Architecture

**Last Updated:** 2025-10-10 16:22 CEST

## Problem Statement

The original trajectory prediction system was producing linear extrapolations instead of parabolic arcs. The predicted path did not continue the current trajectory arc.

### Root Cause

The system had a **temporal inconsistency**:
- Velocity was estimated at throw time (t=0 of the polynomial fit window)
- Prediction started from the current ball position (latest trajectory point)
- This mismatch caused the prediction to use outdated velocity information

**Example:**
```
Ball thrown at t=0, currently at t=2.0s
- Old system: Used velocity from t=0 to predict from position at t=2.0
- Result: Linear extrapolation, not a continuation of the arc
```

## Solution Architecture

### Core Principle: Temporal Consistency

**All prediction components must reference the same point in time.**

The new system estimates velocity at the **current ball position** (the last trajectory point), then predicts forward from that same position using projectile motion physics.

### Physics Model

The prediction uses the kinematic equation for projectile motion:

```
p(t) = p₀ + v₀t + ½at²
```

Where:
- `p(t)` = position at future time t
- `p₀` = initial position (current ball position)
- `v₀` = initial velocity (velocity at current position)
- `a` = acceleration vector (gravity: [0, 0, -9.81] m/s²)
- `t` = time elapsed since initial position

### Component Breakdown

#### 1. Velocity Estimation

**Method Selection:**
- **2 trajectory points**: Two-point method (simple, fast)
- **3+ trajectory points**: Least-squares method (noise resistant)

**Two-Point Method:**
```cpp
v = (p₂ - p₁) / (t₂ - t₁)
```
- Uses the last two trajectory points
- Simple velocity calculation
- Good for clean data

**Least-Squares Method:**
```cpp
// For X and Y (horizontal motion - constant velocity):
v_x = Σ(t_i * x_i) / Σ(t_i²)
v_y = Σ(t_i * y_i) / Σ(t_i²)

// For Z (vertical motion - includes gravity):
// Fit: z(t) = v_z*t - ½g*t²
// Solve for v_z using least squares
```
- Fits linear model for X/Y axes
- Fits quadratic model for Z axis (accounts for gravity)
- **Critical**: Sets t=0 at the CURRENT position (last point)
- More robust to measurement noise

#### 2. Trajectory Prediction

Once velocity is estimated at the current position:

```cpp
for (int i = 0; i < num_points; i++) {
    float t = i * time_step;
    
    // Apply projectile motion equation
    float x = current_x + velocity_x * t;
    float y = current_y + velocity_y * t;
    float z = current_z + velocity_z * t - 0.5 * gravity * t * t;
    
    predicted_points.push_back({x, y, z});
}
```

This produces a proper parabolic arc that continues the current trajectory.

## Implementation Details

### Key Files Modified

1. **`engine/include/GpuTrajectoryPredictor.hpp`**
   - Added `estimateCurrentVelocity()` method
   - Deprecated `estimateInitialVelocity()` (old method)

2. **`engine/src/GpuTrajectoryPredictor.cpp`**
   - Implemented two-point velocity estimation
   - Implemented least-squares velocity estimation with t=0 at current position
   - Both methods use GPU acceleration via OpenCV UMat

3. **`engine/src/SimpleBallTracker.cpp`**
   - Updated `predictFullTrajectory()` to use new method
   - Updated `predictWithTwoPoints()` for consistency

## Advantages of New System

1. **Temporal Consistency**: Velocity and position reference the same time point
2. **Physics-Based**: Uses proper projectile motion equations
3. **Noise Resistant**: Least-squares method handles measurement noise
4. **GPU Accelerated**: Uses OpenCV UMat for performance
5. **Debuggable**: Comprehensive logging for troubleshooting
6. **Parabolic Arcs**: Produces realistic trajectory predictions

## Testing and Validation

To verify the system is working correctly:

1. Run the juggling engine with ball tracking
2. Verify that:
   - Velocity values are reasonable (typically 1-5 m/s for juggling)
   - Predicted points form a smooth parabolic arc
   - The arc continues the current trajectory direction

## Future Improvements

Potential enhancements:
- Air resistance modeling for more accurate long-range predictions
- Adaptive time step based on ball velocity
- Confidence intervals for predictions
- Multi-ball trajectory conflict detection

## References

- Kinematic equations: p(t) = p₀ + v₀t + ½at²
- Least-squares fitting: Minimizes Σ(observed - predicted)²
- Projectile motion: Constant horizontal velocity, accelerated vertical motion