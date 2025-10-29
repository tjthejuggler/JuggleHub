# Trajectory Prediction System - Visual Diagrams

## Problem Illustration

### Current (Broken) System

```
Time:        t₀         t₁         t₂         t₃         t₄         t₅
             |          |          |          |          |          |
Position:    p₀ -----> p₁ -----> p₂ -----> p₃ -----> p₄ -----> p₅
                                                                  ^
                                                                  |
                                                            CURRENT BALL
                                                            
Velocity Estimation:
  - Fit trajectory to points [p₀, p₁, p₂, p₃, p₄, p₅]
  - Set t=0 at p₀
  - Extract velocity v₀ at p₀
  
Prediction (WRONG):
  - Start from p₅ (current position)
  - Use v₀ (velocity from p₀, not p₅!)
  - Result: Linear extrapolation, not parabolic arc
  
  p₅ -----> -----> -----> (straight line, wrong!)
  
  Expected:
  p₅ 
    \
     \
      v  (parabolic arc, correct)
```

### New (Fixed) System

```
Time:        t₀         t₁         t₂         t₃         t₄         t₅
             |          |          |          |          |          |
Position:    p₀ -----> p₁ -----> p₂ -----> p₃ -----> p₄ -----> p₅
                                                                  ^
                                                                  |
                                                            CURRENT BALL
                                                            
Velocity Estimation:
  - Fit trajectory to points [p₀, p₁, p₂, p₃, p₄, p₅]
  - Set t=0 at p₅ (CURRENT TIME)
  - Extract velocity v₅ at p₅ (CURRENT VELOCITY)
  
Prediction (CORRECT):
  - Start from p₅ (current position)
  - Use v₅ (velocity at p₅)
  - Apply gravity: z(t) = z₅ + v₅ᵧ·t - ½g·t²
  
  p₅ 
    \
     \
      v  (parabolic arc, correct!)
       \
        \
         o (predicted landing)
```

## Physics Model Comparison

### Incorrect Approach (Current)

```
Step 1: Estimate velocity at throw time
  v₀ = estimate_velocity_at_t0(points)
  
Step 2: Predict from current position
  for t in [0, dt, 2dt, ...]:
    x = p_current.x + v₀.x * t        ❌ Wrong v₀!
    y = p_current.y + v₀.y * t        ❌ Wrong v₀!
    z = p_current.z + v₀.z * t - ½g·t² ❌ Wrong v₀!
    
Problem: v₀ is velocity at throw, not at current position
Result: Prediction doesn't continue the arc
```

### Correct Approach (New)

```
Step 1: Estimate velocity at current position
  v_current = estimate_velocity_at_current(points)
  
Step 2: Predict from current position
  for t in [0, dt, 2dt, ...]:
    x = p_current.x + v_current.x * t        ✓ Correct!
    y = p_current.y + v_current.y * t        ✓ Correct!
    z = p_current.z + v_current.z * t - ½g·t² ✓ Correct!
    
Result: Prediction continues the arc naturally
```

## Velocity Estimation Methods

### Method 1: Two-Point (Simple)

```
Given: Last 2 points
  p₁ = (x₁, y₁, z₁) at t₁
  p₂ = (x₂, y₂, z₂) at t₂
  
Calculate:
  Δt = t₂ - t₁
  Δp = p₂ - p₁
  v = Δp / Δt
  
Result: v is velocity at p₂ (current position)

Pros: Simple, fast, works with just 2 points
Cons: Sensitive to noise
```

### Method 2: Least-Squares (Robust)

```
Given: Last N points (N ≥ 3)
  points = [p₀, p₁, p₂, ..., pₙ]
  
Set t=0 at pₙ (current time):
  times = [t₀-tₙ, t₁-tₙ, ..., 0]  (all negative except last)
  
Fit models:
  x(t) = x₀ + vₓ·t              (linear)
  y(t) = y₀ + vᵧ·t              (linear)
  z(t) = z₀ + vᵧ·t - ½g·t²      (quadratic)
  
Extract velocity at t=0 (current):
  vₓ = slope of x(t)
  vᵧ = slope of y(t)
  vᵧ = linear coefficient of z(t)
  
Result: v = (vₓ, vᵧ, vᵧ) at current position

Pros: Noise resistant, accurate
Cons: Requires 3+ points, more computation
```

## Data Flow Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                    Ball Tracking System                      │
└─────────────────────────────────────────────────────────────┘
                              │
                              │ New ball position detected
                              ▼
┌─────────────────────────────────────────────────────────────┐
│              Add Verified Point to Trajectory                │
│  - Store position, timestamp, confidence                     │
│  - Increment verified_point_count                            │
└─────────────────────────────────────────────────────────────┘
                              │
                              │ verified_point_count >= 2?
                              ▼
┌─────────────────────────────────────────────────────────────┐
│           Estimate Current Velocity (NEW METHOD)             │
│                                                              │
│  If 2 points:                                                │
│    ├─> Use two-point method                                 │
│    └─> v = (p₂ - p₁) / (t₂ - t₁)                           │
│                                                              │
│  If 3+ points:                                               │
│    ├─> Use least-squares method                             │
│    ├─> Fit trajectory model with t=0 at last point          │
│    └─> Extract velocity at t=0 (current time)               │
└─────────────────────────────────────────────────────────────┘
                              │
                              │ current_velocity
                              ▼
┌─────────────────────────────────────────────────────────────┐
│              Predict Future Trajectory (GPU)                 │
│                                                              │
│  Input:                                                      │
│    - current_position (last verified point)                 │
│    - current_velocity (from estimation above)               │
│    - gravity = 9.81 m/s²                                    │
│                                                              │
│  For each future time t:                                     │
│    x(t) = current_pos.x + current_vel.x * t                 │
│    y(t) = current_pos.y + current_vel.y * t                 │
│    z(t) = current_pos.z + current_vel.z * t - ½g·t²         │
│                                                              │
│  Output: predicted_path = [p(t₁), p(t₂), ..., p(tₙ)]       │
└─────────────────────────────────────────────────────────────┘
                              │
                              │ predicted_path
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                  Use Prediction for Tracking                 │
│  - Search for next detection along predicted path           │
│  - Visualize predicted trajectory                           │
│  - Estimate landing point for catch detection               │
└─────────────────────────────────────────────────────────────┘
```

## Example Calculation

### Scenario: Ball thrown upward at 45°

```
Initial throw:
  Position: (0, 0, 1.5) m
  Velocity: (2, 0, 2) m/s
  
After 0.5 seconds:
  Position: (1, 0, 2.275) m
  Velocity: (2, 0, -2.905) m/s  [vz changed due to gravity!]
  
Current System (WRONG):
  - Estimates v₀ = (2, 0, 2) at throw
  - Predicts from (1, 0, 2.275) using v₀
  - Next point: (1.066, 0, 2.341)  ❌ Still going up!
  
New System (CORRECT):
  - Estimates v_current = (2, 0, -2.905) at current position
  - Predicts from (1, 0, 2.275) using v_current
  - Next point: (1.066, 0, 2.083)  ✓ Going down!
```

## Implementation Checklist

### Phase 1: New Velocity Estimation
- [ ] Add `estimateCurrentVelocity()` to GpuTrajectoryPredictor
- [ ] Implement two-point method
- [ ] Add unit tests
- [ ] Verify against known trajectories

### Phase 2: Integration
- [ ] Update `SimpleBallTracker::predictFullTrajectory()`
- [ ] Update `SimpleBallTracker::predictWithTwoPoints()`
- [ ] Add logging for comparison
- [ ] Test with recorded data

### Phase 3: Least-Squares
- [ ] Implement `estimateCurrentVelocityLeastSquares()`
- [ ] Add automatic method selection (2 points vs 3+)
- [ ] Benchmark performance
- [ ] Test noise resistance

### Phase 4: Validation
- [ ] Visual inspection of predicted arcs
- [ ] Measure prediction accuracy
- [ ] Compare with old system
- [ ] Live testing with juggling

### Phase 5: Cleanup
- [ ] Deprecate old methods
- [ ] Update documentation
- [ ] Remove debug logging
- [ ] Final performance check

## Success Metrics

| Metric | Current | Target | How to Measure |
|--------|---------|--------|----------------|
| Arc continuity | Poor (linear) | Good (parabolic) | Visual inspection |
| Landing accuracy | ±30cm | ±10cm | Measure at catch |
| Prediction smoothness | Jumpy | Smooth | Frame-to-frame delta |
| Noise resistance | Low | High | Add artificial noise |
| Computation time | <1ms | <1ms | Maintain performance |

---

**Ready for Implementation:** All design work complete. Switch to code mode to implement.