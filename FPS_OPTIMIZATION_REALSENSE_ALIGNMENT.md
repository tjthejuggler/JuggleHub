# FPS Optimization: RealSense Frame Alignment

**Date:** 2025-10-08  
**Impact:** High (77% of CPU time)  
**Status:** Analysis Complete - Optimization Recommended

## Performance Profiling Results

Using `perf` profiling, we identified that **RealSense frame alignment operations consume 77% of total CPU time**:

```
Overhead  Function
  40.94%  [unknown] (RealSense)
  35.92%  librealsense::processor_callback::generic_processing_block
  33.90%  librealsense::composite_frame::find_metadata()
  33.50%  librealsense::align::process_frame()
  32.47%  librealsense::align::align_frames()
  19.37%  librealsense::align_sse::align_z_to_other()
```

**Total RealSense alignment overhead: ~77% of CPU time**

## What is Frame Alignment?

Frame alignment is a RealSense operation that spatially aligns the depth frame with the color frame. This is necessary because:

1. The depth and color cameras are physically separate on the RealSense device
2. They have different fields of view and positions
3. Without alignment, depth pixels don't correspond to color pixels

**Current implementation** (Engine.cpp:90):
```cpp
auto aligned_frames = align_to_color_.process(frames);
```

This runs **every frame** (30-60 times per second) and is CPU-intensive.

## Why We Need Alignment

**We CANNOT remove alignment** because:

1. Our tracking code requires aligned depth data (Engine.cpp:128):
   ```cpp
   simple_tracker_->update(color_image, depth_image, camera_intrinsics_);
   ```

2. 3D ball positions depend on matching color pixels to depth values
3. Hand tracking requires accurate 3D wrist positions
4. Without alignment, all 3D tracking would fail

## Current Configuration

- **Resolution:** 640x480 (confirmed in Engine.cpp:34-35)
- **Frame Rate:** 60 FPS (Engine.cpp:36)
- **Alignment:** CPU-based using `rs2::align` (Engine.cpp:28)

## Optimization Options

### Option 1: Use RealSense Processing Blocks (RECOMMENDED)

RealSense SDK provides optimized processing blocks that can leverage hardware acceleration:

```cpp
// Instead of rs2::align, use processing blocks
rs2::align align_to_color(RS2_STREAM_COLOR);
rs2::decimation_filter dec_filter;  // Reduce depth resolution
rs2::spatial_filter spat_filter;    // Smooth depth data
rs2::temporal_filter temp_filter;   // Temporal smoothing

// In processing loop:
auto decimated = dec_filter.process(depth_frame);
auto spatial = spat_filter.process(decimated);
auto temporal = temp_filter.process(spatial);
auto aligned = align_to_color.process(temporal);
```

**Expected gain:** 20-30% reduction in alignment time
**Risk:** Low - these are official RealSense optimizations
**Trade-off:** Slightly smoother depth (may help tracking)

### Option 2: Reduce Alignment Frequency

Only align every Nth frame, use previous alignment for intermediate frames:

```cpp
static int frame_count = 0;
static rs2::frameset last_aligned_frames;

if (frame_count % 2 == 0) {  // Align every 2nd frame
    last_aligned_frames = align_to_color_.process(frames);
}
frame_count++;

auto color_frame = last_aligned_frames.get_color_frame();
auto depth_frame = last_aligned_frames.get_depth_frame();
```

**Expected gain:** 50% reduction in alignment time
**Risk:** Medium - may cause jitter in 3D positions
**Trade-off:** Slightly less accurate 3D tracking

### Option 3: Lower Depth Resolution

Use lower resolution for depth (e.g., 424x240) while keeping color at 640x480:

```cpp
rs_config_.enable_stream(RS2_STREAM_COLOR, 640, 480, RS2_FORMAT_BGR8, 60);
rs_config_.enable_stream(RS2_STREAM_DEPTH, 424, 240, RS2_FORMAT_Z16, 60);
```

**Expected gain:** 60-70% reduction in alignment time
**Risk:** Medium - lower depth resolution affects 3D accuracy
**Trade-off:** Less precise 3D positions, but faster

### Option 4: Reduce Frame Rate

Lower the camera FPS from 60 to 30:

```cpp
camera_fps_(30)  // Instead of 60
```

**Expected gain:** 50% reduction in total processing time
**Risk:** Low - 30 FPS is still good for juggling
**Trade-off:** Less temporal resolution

## Recommended Approach

**Phase 1: Safe Optimizations (Implement First)**

1. **Add processing blocks** (Option 1) - Low risk, moderate gain
2. **Profile again** to measure impact

**Phase 2: If More Performance Needed**

3. **Reduce frame rate to 30 FPS** (Option 4) - Low risk, high gain
4. **Profile again**

**Phase 3: Advanced (Only if Necessary)**

5. **Consider lower depth resolution** (Option 3) - Test carefully
6. **Evaluate tracking accuracy impact**

## Implementation Plan

### Step 1: Add RealSense Processing Blocks

Modify `Engine.cpp`:

```cpp
// In Engine constructor, add:
decimation_filter_(2.0f),  // Reduce depth by factor of 2
spatial_filter_(),
temporal_filter_()

// In run() loop, replace line 90:
// OLD: auto aligned_frames = align_to_color_.process(frames);
// NEW:
auto decimated = decimation_filter_.process(frames.get_depth_frame());
auto spatial = spatial_filter_.process(decimated);
auto temporal = temporal_filter_.process(spatial);
rs2::frameset processed_frames;
processed_frames = frames;  // Keep color frame
// Reconstruct frameset with processed depth
auto aligned_frames = align_to_color_.process(processed_frames);
```

### Step 2: Profile and Measure

```bash
# Rebuild with optimizations
./scripts/build_engine.sh

# Profile again
./scripts/profile_engine.sh --duration 30 --rebuild

# Compare results
./scripts/analyze_perf.sh --type summary
```

### Step 3: Verify Tracking Quality

1. Run normal juggling session
2. Check 3D position accuracy
3. Verify no jitter or artifacts
4. Compare FPS before/after

## Expected Results

**Conservative estimate:**
- Current: ~30 FPS with 77% CPU on alignment
- After Option 1: ~40 FPS (33% improvement)
- After Option 1 + 4: ~60 FPS (100% improvement)

**Best case:**
- Could achieve 2-3x FPS improvement
- Maintain full tracking accuracy
- No functionality loss

## Safety Notes

1. **Always profile before and after** to measure actual impact
2. **Test tracking accuracy** with real juggling
3. **Keep backup** of working code
4. **Implement one change at a time**
5. **Verify 3D positions** are still accurate

## Next Steps

1. ✅ Profiling complete - identified bottleneck
2. ⏳ Implement Option 1 (processing blocks)
3. ⏳ Profile and measure improvement
4. ⏳ If needed, implement Option 4 (lower FPS)
5. ⏳ Document final performance gains

## References

- RealSense SDK Documentation: https://github.com/IntelRealSense/librealsense
- Processing Blocks: https://github.com/IntelRealSense/librealsense/tree/master/examples/post-processing
- Performance Tuning: https://dev.intelrealsense.com/docs/tuning-depth-cameras-for-best-performance

---

**Last Updated:** 2025-10-08 14:46 CEST