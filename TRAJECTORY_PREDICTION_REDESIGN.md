# Trajectory Prediction System Redesign

**Date:** 2025-10-10  
**Status:** Architecture Phase  
**Goal:** Fix trajectory prediction to properly model parabolic arcs using projectile motion physics

---

## Problem Analysis

### Current Issues

1. **Incorrect Prediction Starting Point**: The system uses `initial_position` from throw time, but then predicts from the **current** position with the **initial** velocity. This creates a mismatch.

2. **Velocity Estimation Confusion**: The code estimates velocity at t=0 of the fit window, but then uses this velocity to predict from the **current** ball position, not from t=0.

3. **Linear Extrapolation**: Line 2048 in [`SimpleBallTracker.cpp`](engine/src/SimpleBallTracker.cpp:2048) shows:
   ```cpp
   std::vector<cv::Point3f> predicted_path = gpu_trajectory_predictor_->predictTrajectory(
       current_position,  // Start from current position, not initial throw position
   ```
   This is predicting from the current position with a velocity that was calculated for a different point in time.

4. **Physics Model Mismatch**: The prediction equation in [`GpuTrajectoryPredictor.cpp`](engine/src/GpuTrajectoryPredictor.cpp:145-147) is correct:
   ```cpp
   float x = initial_pos.x + initial_vel.x * t;
   float y = initial_pos.y + initial_vel.y * t;
   float z = initial_pos.z + initial_vel.z * t - 0.5f * params.gravity * t * t;
   ```
   But it's being called with inconsistent parameters.

### Root Cause

The fundamental issue is **temporal inconsistency**: 
- We estimate velocity at one point in time (t=0 of fit window)
- We predict from a different point in time (current ball position)
- We don't account for how velocity changes due to gravity between these two times

---

## Solution Architecture

### Core Principle: Consistent Physics Model

The key insight from the provided physics explanation is:

> **p(t) = p₀ + v₀t + ½at²**

Where:
- `p₀` = initial position (at time t=0)
- `v₀` = initial velocity (at time t=0)
- `a` = acceleration (gravity: [0, 0, -g])
- `t` = time elapsed since t=0

**Critical:** All three components (p₀, v₀, t) must reference the **same time origin**.

### New Approach: Current State Prediction

Instead of tracking "initial" conditions from throw time, we should:

1. **Estimate Current Velocity**: Calculate the ball's velocity **right now** from recent trajectory points
2. **Predict from Current State**: Use current position + current velocity to predict future path
3. **Apply Gravity**: Account for gravitational acceleration in the prediction

This matches the physics explanation perfectly:
- `p₀` = current ball position (last verified point)
- `v₀` = current ball velocity (estimated from last 2-3 points)
- `t` = time into the future (0, dt, 2dt, 3dt, ...)

---

## Detailed Design

### 1. Velocity Estimation Algorithm

#### Simple Two-Point Method (for 2 points)

```
Given: p₁ at t₁, p₂ at t₂
Calculate: Δt = t₂ - t₁
Calculate: Δp = p₂ - p₁
Velocity: v = Δp / Δt
```

**Implementation:**
```cpp
cv::Point3f estimateCurrentVelocity(const std::vector<TrajectoryPoint>& points) {
    if (points.size() < 2) return cv::Point3f(0, 0, 0);
    
    const TrajectoryPoint& p1 = points[points.size() - 2];
    const TrajectoryPoint& p2 = points[points.size() - 1];
    
    float dt = (p2.timestamp - p1.timestamp) / 1000000.0f;  // µs to seconds
    if (dt < 0.001f) return cv::Point3f(0, 0, 0);
    
    cv::Point3f velocity = (p2.position - p1.position) / dt;
    return velocity;
}
```

#### Least-Squares Method (for 3+ points)

For better noise resistance, fit a parabolic trajectory to recent points:

**For X and Y (horizontal motion - constant velocity):**
```
x(t) = x₀ + vₓ·t
y(t) = y₀ + vᵧ·t
```
Use linear regression to find vₓ and vᵧ.

**For Z (vertical motion - constant acceleration):**
```
z(t) = z₀ + vᵧ·t - ½g·t²
```
Use quadratic regression to find vᵧ.

**Key Change:** Instead of estimating velocity at t=0 of the fit window, we:
1. Fit the trajectory model to recent points
2. Evaluate the velocity **at the time of the last point** (current time)
3. Use: `v(t) = v₀ + a·t` where `a = [0, 0, -g]`

```cpp
cv::Point3f estimateCurrentVelocityLeastSquares(
    const std::vector<TrajectoryPoint>& points,
    float gravity
) {
    // Use last 5-10 points for fitting
    int n = std::min(10, (int)points.size());
    int start_idx = points.size() - n;
    
    // Set t=0 at the LAST point (current time)
    uint64_t t_current = points.back().timestamp;
    
    std::vector<double> times;
    std::vector<double> x_vals, y_vals, z_vals;
    
    for (int i = start_idx; i < points.size(); i++) {
        // Negative time - we're looking backwards from current
        double t = (points[i].timestamp - t_current) / 1000000.0;
        times.push_back(t);
        x_vals.push_back(points[i].position.x);
        y_vals.push_back(points[i].position.y);
        z_vals.push_back(points[i].position.z);
    }
    
    // Fit linear models for x and y
    // ... (standard least squares)
    
    // Fit parabolic model for z
    // ... (quadratic least squares)
    
    // Extract velocity at t=0 (current time)
    // For x,y: v = slope
    // For z: v = b coefficient (linear term)
    
    return cv::Point3f(vx, vy, vz);
}
```

### 2. Trajectory Prediction Algorithm

Once we have current velocity, prediction is straightforward:

```cpp
std::vector<cv::Point3f> predictTrajectory(
    const cv::Point3f& current_pos,      // p₀ = current position
    const cv::Point3f& current_vel,      // v₀ = current velocity
    const TrajectoryPredictionParams& params
) {
    std::vector<cv::Point3f> trajectory;
    
    for (int i = 0; i < params.max_points; i++) {
        float t = i * params.time_step;  // Time into future
        
        // Projectile motion equations
        float x = current_pos.x + current_vel.x * t;
        float y = current_pos.y + current_vel.y * t;
        float z = current_pos.z + current_vel.z * t - 0.5f * params.gravity * t * t;
        
        trajectory.push_back(cv::Point3f(x, y, z));
    }
    
    return trajectory;
}
```

**Key Points:**
- `current_pos` is the last verified ball position
- `current_vel` is the velocity **at that position** (not at throw time)
- `t` starts at 0 and increases (predicting forward in time)
- Gravity only affects Z axis (assuming Z is up)

### 3. Integration with Existing System

#### Changes to `SimpleBallTracker::predictFullTrajectory()`

**Current (WRONG):**
```cpp
// Line 2030-2050 in SimpleBallTracker.cpp
cv::Point3f refined_velocity = gpu_trajectory_predictor_->estimateInitialVelocity(
    ball.trajectory.points,
    tracking_settings_.traj_gravity
);

ball.trajectory.initial_velocity = refined_velocity;

cv::Point3f current_position = ball.trajectory.points.back().position;

std::vector<cv::Point3f> predicted_path = gpu_trajectory_predictor_->predictTrajectory(
    current_position,  // ❌ Current position
    refined_velocity,  // ❌ But velocity from t=0 of fit window!
    params
);
```

**New (CORRECT):**
```cpp
// Estimate velocity AT the current position
cv::Point3f current_velocity = gpu_trajectory_predictor_->estimateCurrentVelocity(
    ball.trajectory.points,
    tracking_settings_.traj_gravity
);

// Store for reference
ball.trajectory.current_velocity = current_velocity;

// Get current position
cv::Point3f current_position = ball.trajectory.points.back().position;

// Predict from current state
std::vector<cv::Point3f> predicted_path = gpu_trajectory_predictor_->predictTrajectory(
    current_position,   // ✓ Current position
    current_velocity,   // ✓ Current velocity (at current position)
    params
);
```

#### Changes to `GpuTrajectoryPredictor`

**Add new method:**
```cpp
/**
 * Estimate the ball's CURRENT velocity from recent trajectory points
 * 
 * This calculates the velocity at the time of the LAST verified point,
 * accounting for gravitational acceleration.
 * 
 * Algorithm:
 * 1. Use last 2-10 points (depending on availability)
 * 2. Fit trajectory model with t=0 at the LAST point
 * 3. Extract velocity at t=0 (current time)
 * 
 * @param points Recent trajectory points (minimum 2 required)
 * @param gravity Gravitational acceleration (m/s²)
 * @return Current velocity vector at the last point
 */
cv::Point3f estimateCurrentVelocity(
    const std::vector<TrajectoryPoint>& points,
    float gravity = 9.81f
);
```

**Deprecate old method:**
```cpp
/**
 * DEPRECATED: Use estimateCurrentVelocity instead
 * 
 * This method estimates velocity at t=0 of the fit window,
 * which is NOT the current velocity. This causes prediction errors.
 */
[[deprecated("Use estimateCurrentVelocity instead")]]
cv::Point3f estimateInitialVelocity(...);
```

---

## Implementation Strategy

### Phase 1: Add New Velocity Estimation (Minimal Risk)

1. Add `estimateCurrentVelocity()` method to [`GpuTrajectoryPredictor`](engine/include/GpuTrajectoryPredictor.hpp)
2. Implement simple two-point version first
3. Add unit tests to verify correctness
4. Keep existing `estimateInitialVelocity()` for comparison

### Phase 2: Update Prediction Call Sites

1. Update [`SimpleBallTracker::predictFullTrajectory()`](engine/src/SimpleBallTracker.cpp:2017) to use new method
2. Update [`SimpleBallTracker::predictWithTwoPoints()`](engine/src/SimpleBallTracker.cpp:1983) to use consistent approach
3. Add logging to compare old vs new predictions

### Phase 3: Implement Least-Squares Version

1. Add `estimateCurrentVelocityLeastSquares()` for better noise resistance
2. Use when 3+ points available
3. Fall back to two-point method for 2 points

### Phase 4: Remove Deprecated Code

1. Remove `estimateInitialVelocity()` after verification
2. Remove `initial_velocity` field from `BallTrajectory` (replace with `current_velocity`)
3. Update documentation

---

## Expected Results

### Before (Current System)

```
Trajectory points: [p₁, p₂, p₃, p₄, p₅]
Estimate velocity at p₁: v₁
Predict from p₅ using v₁: ❌ WRONG
Result: Linear extrapolation, not parabolic arc
```

### After (New System)

```
Trajectory points: [p₁, p₂, p₃, p₄, p₅]
Estimate velocity at p₅: v₅ (accounting for gravity since p₁)
Predict from p₅ using v₅: ✓ CORRECT
Result: Proper parabolic arc continuing from current trajectory
```

---

## Validation Plan

### Test Cases

1. **Straight Throw Test**
   - Throw ball horizontally
   - Verify prediction shows downward parabola
   - Check that arc continues smoothly from current position

2. **High Arc Test**
   - Throw ball upward at 45° angle
   - Verify prediction shows symmetric parabola
   - Check apex prediction accuracy

3. **Catch Prediction Test**
   - Throw ball to partner
   - Verify predicted landing point matches actual catch location
   - Measure prediction error over time

4. **Noise Resistance Test**
   - Add artificial noise to trajectory points
   - Verify least-squares method smooths out noise
   - Compare with two-point method

### Success Criteria

- [ ] Predicted path forms smooth parabolic arc
- [ ] Arc continues naturally from current ball position
- [ ] No sudden jumps or discontinuities in prediction
- [ ] Landing point prediction within 10cm of actual
- [ ] Prediction updates smoothly as ball moves

---

## Code Changes Summary

### Files to Modify

1. **[`engine/include/GpuTrajectoryPredictor.hpp`](engine/include/GpuTrajectoryPredictor.hpp)**
   - Add `estimateCurrentVelocity()` declaration
   - Add `estimateCurrentVelocityLeastSquares()` declaration
   - Deprecate `estimateInitialVelocity()`

2. **[`engine/src/GpuTrajectoryPredictor.cpp`](engine/src/GpuTrajectoryPredictor.cpp)**
   - Implement `estimateCurrentVelocity()` (two-point method)
   - Implement `estimateCurrentVelocityLeastSquares()` (least-squares method)
   - Update `estimateInitialConditionsCpu()` to use new approach

3. **[`engine/src/SimpleBallTracker.cpp`](engine/src/SimpleBallTracker.cpp)**
   - Update `predictFullTrajectory()` (line 2017)
   - Update `predictWithTwoPoints()` (line 1983)
   - Update `updateInFlightBall()` to use new predictions

4. **[`engine/include/SimpleBallTracker.hpp`](engine/include/SimpleBallTracker.hpp)** (if needed)
   - Update `BallTrajectory` struct to include `current_velocity` field

### Estimated Lines of Code

- New code: ~150 lines
- Modified code: ~50 lines
- Removed code: ~0 lines (keep deprecated for now)
- Total impact: ~200 lines

---

## Risk Assessment

### Low Risk
- Adding new methods alongside existing ones
- Can test new approach without breaking current system
- Easy to revert if issues arise

### Medium Risk
- Changing prediction call sites requires careful testing
- Need to verify no regressions in tracking accuracy

### Mitigation
- Implement in phases with testing at each step
- Keep old code available for comparison
- Add comprehensive logging for debugging
- Test with recorded data before live testing

---

## Timeline Estimate

- **Phase 1** (New methods): 2-3 hours
- **Phase 2** (Integration): 2-3 hours  
- **Phase 3** (Least-squares): 3-4 hours
- **Phase 4** (Cleanup): 1-2 hours
- **Testing**: 4-6 hours

**Total: 12-18 hours** of development + testing

---

## References

- Physics explanation provided by user (projectile motion)
- Current implementation: [`GpuTrajectoryPredictor.cpp`](engine/src/GpuTrajectoryPredictor.cpp)
- Current usage: [`SimpleBallTracker.cpp`](engine/src/SimpleBallTracker.cpp)

---

**Next Steps:** Review this architecture, then switch to code mode for implementation.