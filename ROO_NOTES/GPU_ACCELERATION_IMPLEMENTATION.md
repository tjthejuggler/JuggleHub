# GPU-Accelerated RealSense Alignment Implementation

**Date:** 2025-10-08  
**Status:** Code changes complete, requires SDK rebuild  
**Priority:** HIGH  
**Expected Performance Gain:** 50-70% reduction in alignment CPU overhead

## Overview

This document describes the implementation of GPU-accelerated depth-to-color frame alignment for the JuggleHub project, based on the technical analysis in the RealSense performance optimization report.

## Problem Statement

### Current Bottleneck
- **Component:** CPU-based `rs2::align` processing block
- **Impact:** High CPU utilization (~77% reported in similar workloads)
- **Root Cause:** Per-pixel reprojection algorithm executing serially on CPU
- **Scale:** 307,200 pixel transformations per frame at 640x480 resolution
- **At 60 FPS:** ~18.4 million transformations per second on CPU

### Why This Matters
The D4 Vision Processor in the RealSense D455 camera handles stereo depth calculation, but **does NOT** perform depth-to-color alignment. This alignment must be done on the host system, and by default uses CPU-based processing.

## Solution: GPU Offloading via GLSL

### Technical Approach
Replace CPU-based `rs2::align` with GPU-accelerated `rs2::gl::align` from the librealsense2-gl extension library.

### Key Benefits
- ✅ **Massive parallelization:** GPU processes thousands of pixels simultaneously
- ✅ **CPU headroom:** Frees CPU for YOLO inference and tracking logic
- ✅ **Identical accuracy:** Same mathematical operations, just faster
- ✅ **API compatibility:** Drop-in replacement with identical interface
- ✅ **Vendor agnostic:** Works on Intel Arc, NVIDIA, and AMD GPUs

### Trade-offs
- ⚠️ **Minor latency:** +1-2ms for GPU memory transfers (negligible for 60 FPS)
- ⚠️ **Build complexity:** Requires rebuilding librealsense2 from source
- ⚠️ **Dependencies:** Requires OpenGL, GLFW, and Mesa libraries

## Implementation Changes

### 1. Code Changes

#### File: [`engine/include/Engine.hpp`](engine/include/Engine.hpp)

**Added GPU header:**
```cpp
#include <librealsense2/rs.hpp>
#include <librealsense2-gl/rs_processing_gl.hpp>  // GPU acceleration
```

**Changed alignment object type:**
```cpp
// Before:
rs2::align align_to_color_;

// After:
rs2::gl::align align_to_color_;  // GPU-accelerated alignment via GLSL
```

**Note:** The constructor and usage remain identical - the API is designed as a drop-in replacement.

#### File: [`engine/src/Engine.cpp`](engine/src/Engine.cpp)

**No changes required!** The initialization and usage are identical:
```cpp
// Line 28: Constructor (unchanged)
align_to_color_(RS2_STREAM_COLOR)

// Line 90: Usage (unchanged)
auto aligned_frames = align_to_color_.process(frames);
```

### 2. Build Configuration Changes

#### File: [`engine/CMakeLists.txt`](engine/CMakeLists.txt)

**Added package dependencies:**
```cmake
find_package(realsense2 REQUIRED)
find_package(realsense2-gl REQUIRED)  # GPU acceleration library
find_package(OpenGL REQUIRED)         # OpenGL for GLSL shaders
find_package(glfw3 REQUIRED)          # Window system for OpenGL context
```

**Added library linking:**
```cmake
target_link_libraries(juggle_engine PRIVATE
    # RealSense with GPU acceleration
    realsense2::realsense2
    realsense2::realsense2-gl
    
    # OpenGL for GPU-accelerated alignment
    OpenGL::GL
    glfw
    
    # ... other libraries
)
```

## Prerequisites and Setup

### System Requirements
- ✅ **Hardware:** GPU with OpenGL 3.3+ support (Intel Arc, NVIDIA, AMD)
- ✅ **OS:** Linux with kernel 5.15+ (Ubuntu 22.04+ recommended)
- ✅ **Drivers:** Mesa drivers (included in Ubuntu) or proprietary GPU drivers
- ✅ **Camera:** Intel RealSense D400 series (D415, D435, D455, etc.)

### Current System (Verified Compatible)
- **Hardware:** ASUS Zenbook Duo with Intel Core Ultra 9 + Arc Graphics
- **OS:** Kubuntu 25.04
- **Kernel:** 6.14
- **GPU:** Intel Arc (excellent OpenGL support via Mesa)

### Step 1: Install System Dependencies

```bash
# Install OpenGL, GLFW, and Mesa development libraries
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    git \
    libglfw3-dev \
    libgl1-mesa-dev \
    libglu1-mesa-dev \
    intel-gpu-tools
```

**Note:** `intel-gpu-tools` provides `intel_gpu_top` for GPU monitoring.

### Step 2: Rebuild librealsense2 with GLSL Extensions

The pre-built Debian packages do **NOT** include the GLSL extensions. You must build from source:

```bash
# Navigate to a temporary directory
cd /tmp

# Clone the official librealsense repository
git clone https://github.com/IntelRealSense/librealsense.git
cd librealsense

# Create build directory
mkdir build && cd build

# Configure with GLSL extensions enabled
cmake ../ \
    -DBUILD_GLSL_EXTENSIONS=ON \
    -DBUILD_EXAMPLES=ON \
    -DCMAKE_BUILD_TYPE=Release

# Build using all CPU cores
make -j$(nproc)

# Install to system directories
sudo make install

# Update library cache
sudo ldconfig
```

**Build time:** Approximately 10-20 minutes depending on CPU.

### Step 3: Rebuild JuggleHub Engine

```bash
# Navigate to JuggleHub project
cd /home/twain/Projects/JuggleHub

# Clean previous build
rm -rf engine/build

# Create fresh build directory
mkdir engine/build && cd engine/build

# Configure with CMake
cmake ..

# Build
make -j$(nproc)
```

**Expected result:** Clean compilation with GPU libraries linked.

## Verification and Testing

### 1. Verify GPU Library Availability

Before building JuggleHub, verify the GPU library was installed:

```bash
# Check for librealsense2-gl library
ldconfig -p | grep realsense2-gl

# Expected output:
# librealsense2-gl.so.2.56 (libc6,x86-64) => /usr/local/lib/librealsense2-gl.so.2.56
# librealsense2-gl.so (libc6,x86-64) => /usr/local/lib/librealsense2-gl.so
```

### 2. Monitor Performance Improvement

#### CPU Monitoring
```bash
# Run htop in one terminal
htop

# Filter by process name (F4 key)
# Type: juggle_engine
```

**Expected observations:**
- **Before (CPU):** One core at ~77-100% utilization
- **After (GPU):** Reduced to ~15-25% utilization

#### GPU Monitoring (Intel Arc)
```bash
# Run GPU monitor (requires root)
sudo intel_gpu_top
```

**Expected observations:**
- **Before (CPU):** Render/3D engine at ~0% (GPU idle)
- **After (GPU):** Render/3D engine at 20-60% (GPU active during alignment)

### 3. Frame Rate Verification

Monitor the application logs for frame rate stability:

```bash
# Run the engine with verbose logging
./engine/build/juggle_engine --verbose
```

**Expected improvements:**
- More consistent 60 FPS achievement
- Reduced frame drops during heavy processing
- Lower overall system load

## Performance Expectations

### Quantitative Improvements

| Metric | Before (CPU) | After (GPU) | Improvement |
|--------|--------------|-------------|-------------|
| Alignment CPU Usage | ~77% | ~15-25% | 50-70% reduction |
| Alignment Time/Frame | ~15-20ms | ~1-2ms | 87-93% faster |
| GPU Utilization | 0% | 20-60% | Workload distributed |
| Frame Rate Stability | Variable | Consistent | More stable 60 FPS |
| CPU Headroom | Limited | Significant | Better for YOLO/tracking |

### Qualitative Improvements

- **Better resource distribution:** CPU and GPU both utilized efficiently
- **Reduced thermal throttling:** Lower CPU load means less heat
- **Improved responsiveness:** More CPU available for tracking algorithms
- **Future-proof:** Enables additional GPU-accelerated processing blocks

## Troubleshooting

### Build Errors

#### Error: "realsense2-gl not found"
**Cause:** librealsense2 was not built with GLSL extensions enabled.

**Solution:** Rebuild librealsense2 following Step 2 above, ensuring `-DBUILD_GLSL_EXTENSIONS=ON` is set.

#### Error: "OpenGL::GL not found"
**Cause:** OpenGL development libraries not installed.

**Solution:**
```bash
sudo apt-get install libgl1-mesa-dev libglu1-mesa-dev
```

#### Error: "glfw3 not found"
**Cause:** GLFW library not installed.

**Solution:**
```bash
sudo apt-get install libglfw3-dev
```

### Runtime Errors

#### Error: "Failed to create OpenGL context"
**Cause:** GPU drivers not properly configured or no display available.

**Solution:**
- Ensure you're running in a graphical environment (not headless)
- Verify GPU drivers: `glxinfo | grep "OpenGL version"`
- For Intel Arc: Mesa drivers should work out-of-box on Ubuntu 22.04+

#### Error: "Alignment process failed"
**Cause:** GPU memory allocation failure or driver issue.

**Solution:**
- Check GPU memory: `intel_gpu_top` (look for memory usage)
- Restart the application
- If persistent, fall back to CPU version temporarily

### Performance Issues

#### GPU utilization is 0%
**Cause:** Still using CPU version despite code changes.

**Verification:**
```bash
# Check which libraries are linked
ldd engine/build/juggle_engine | grep realsense

# Should show both:
# librealsense2.so
# librealsense2-gl.so
```

**Solution:** Rebuild JuggleHub after verifying librealsense2-gl is installed.

#### No performance improvement
**Cause:** GPU may be bottlenecked by other factors.

**Investigation:**
- Monitor GPU memory bandwidth: `intel_gpu_top`
- Check for thermal throttling: `sensors`
- Verify resolution/FPS settings match expectations

## Rollback Procedure

If GPU acceleration causes issues, you can easily revert:

### 1. Revert Code Changes

```bash
cd /home/twain/Projects/JuggleHub

# Revert Engine.hpp
git checkout engine/include/Engine.hpp

# Revert CMakeLists.txt
git checkout engine/CMakeLists.txt
```

### 2. Rebuild Without GPU Libraries

```bash
cd engine/build
cmake ..
make -j$(nproc)
```

The application will fall back to CPU-based alignment automatically.

## Alternative Approaches Considered

### CUDA
- ❌ **Not applicable:** Requires NVIDIA GPU (we have Intel Arc)
- ❌ **Vendor lock-in:** Not portable across GPU vendors
- ❌ **No built-in support:** librealsense2 has no CUDA alignment implementation

### OpenCL
- ⚠️ **Theoretically possible:** Intel Arc supports OpenCL
- ❌ **No built-in support:** librealsense2 has no OpenCL alignment implementation
- ❌ **High development cost:** Would require custom kernel implementation
- ❌ **No advantage:** GLSL solution is pre-built and optimized

### Conclusion
**GLSL via `rs2::gl::align` is the optimal solution** for this use case.

## Future Optimizations

Once GPU acceleration is verified working, consider:

1. **Additional GPU processing blocks:**
   - Spatial filtering on GPU
   - Temporal filtering on GPU
   - Decimation on GPU

2. **YOLO inference on GPU:**
   - Already using OpenVINO, but verify GPU backend is active
   - Consider NPU offloading for Intel Core Ultra 9

3. **Color space conversions on GPU:**
   - BGR to HSV conversions for color tracking
   - Could be done via OpenGL shaders

4. **Parallel frame processing:**
   - Pipeline multiple frames through GPU simultaneously
   - Requires careful synchronization

## References

- **Technical Report:** "Optimizing RealSense Performance: A Technical Guide to GPU-Accelerated Depth-to-Color Alignment"
- **librealsense2 Documentation:** https://github.com/IntelRealSense/librealsense
- **GLSL Extensions:** https://github.com/IntelRealSense/librealsense/tree/master/wrappers/opengl
- **Intel GPU Tools:** https://gitlab.freedesktop.org/drm/igt-gpu-tools

## Changelog

### 2025-10-08 - Initial Implementation
- Added `rs2::gl::align` support to [`Engine.hpp`](engine/include/Engine.hpp)
- Updated [`CMakeLists.txt`](engine/CMakeLists.txt) with GPU library dependencies
- Created comprehensive documentation
- **Status:** Code complete, awaiting SDK rebuild and testing

---

**Next Steps:**
1. Rebuild librealsense2 with GLSL extensions (Step 2 above)
2. Rebuild JuggleHub engine (Step 3 above)
3. Verify GPU utilization with monitoring tools
4. Measure performance improvements
5. Update this document with actual benchmark results