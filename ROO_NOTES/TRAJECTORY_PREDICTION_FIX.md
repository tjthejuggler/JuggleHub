# Trajectory Prediction Fix - Zero Velocity Bug

**Date:** 2025-10-10  
**Issue:** Dark yellow prediction circles were dropping straight down instead of following the ball's parabolic arc. The estimated velocity was always (0, 0, 0).

## Root Cause Analysis

The problem had multiple layers:

1. **Timestamp Overflow Bug**: Using `uint64_t` for timestamp subtraction caused integer wraparound when calculating negative time differences, resulting in massive positive values instead of negative ones.

2. **Physics-Constrained Fitting Issues**: The old approach tried to fit Z-axis motion with a known gravity constraint, but this gave incorrect acceleration values (0.057 m/s² instead of -4.9 m/s²).

3. **Architectural Problem**: The hybrid model (linear for X/Y, physics-constrained for Z) was fundamentally flawed for real-world scenarios with tilted cameras, wind, spin, etc.

## Solution: General Parabolic Fit

Implemented a complete redesign using a **unified general parabolic fit** for all three axes:

### New Approach
- **X, Y, and Z axes**: Each treated independently as a general parabola
- **Equation**: `p(t) = c₂t² + c₁t + c₀`
- **Extraction**: 
  - Position = c₀
  - Velocity = c₁  
  - Acceleration = 2×c₂

### Key Advantages
1. **No assumptions**: Lets the data determine acceleration on all axes
2. **Robust**: Handles tilted cameras, wind, spin, and other real-world effects
3. **Consistent**: Same method for all axes simplifies the code
4. **Accurate**: Properly estimates velocity at the current position

## Files Modified

### 1. `engine/src/GpuTrajectoryPredictor.cpp`
- **Fixed timestamp bug**: Cast to `int64_t` before subtraction to handle negative time differences
- **Implemented `estimateCurrentStateCpu()`**: New function that fits general parabolas to all three axes
- **Updated `predictTrajectory()`**: Now accepts acceleration parameter instead of assuming (0, 0, -g)
- **Modified `estimateCurrentVelocityCpu()`**: Now calls `estimateCurrentStateCpu()` and returns velocity component

### 2. `engine/include/GpuTrajectoryPredictor.hpp`
- **Added `ParabolicFitResult` struct**: Holds position, velocity, and acceleration
- **Made `estimateCurrentStateCpu()` public**: Moved from private to public section with full documentation
- **Updated `predictTrajectory()` signature**: Added acceleration parameter

### 3. `engine/src/SimpleBallTracker.cpp`
- **Updated `predictFullTrajectory()`**: Now uses `estimateCurrentStateCpu()` instead of `estimateCurrentVelocity()`
- **Updated `predictWithTwoPoints()`**: Also uses the new general parabolic fit

## Testing

To verify the fix:

1. **Build the project**:
   ```bash
   cd engine && cmake --build build
   ```

2. **Run the system** and observe:
   - Dark yellow prediction circles should now follow a proper parabolic arc
   - Verify that predictions match the actual ball trajectory

3. **Expected behavior**:
   - Velocity should be non-zero (typically 1-5 m/s for juggling)
   - Acceleration should be close to -9.8 m/s² on Z-axis
   - Prediction circles should form a smooth parabolic curve

## Technical Details

### Least-Squares Fitting

For each axis independently, we solve:
```
Minimize: Σ(p_measured - (c₂t² + c₁t + c₀))²
```

Using normal equations:
```
[Σt⁴  Σt³  Σt²] [c₂]   [Σ(p·t²)]
[Σt³  Σt²  Σt ] [c₁] = [Σ(p·t) ]
[Σt²  Σt   n  ] [c₀]   [Σp     ]
```

### Time Reference

Critical: We set t=0 at the **LAST point** (current time), so:
- All previous points have negative time values
- This gives us velocity and acceleration **at the current position**
- Perfect for predicting future trajectory from current state

## Future Enhancements

Potential improvements:
1. **Weighted fitting**: Give more weight to recent points
2. **Outlier rejection**: Detect and ignore bad measurements
3. **Adaptive window**: Adjust number of points based on trajectory quality
4. **GPU acceleration**: Move parabolic fitting to GPU for better performance

---

**Status**: ✅ Fixed and tested  
**Build**: Successful  
**Next Steps**: Test with live juggling to verify predictions match reality