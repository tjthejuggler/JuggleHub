#!/bin/bash

# Script to build librealsense2 with GLSL extensions for GPU-accelerated alignment
# This enables the rs2::gl::align class for offloading depth-to-color alignment to GPU

set -e  # Exit on error

echo "=========================================="
echo "RealSense SDK GPU Acceleration Builder"
echo "=========================================="
echo ""
echo "This script will:"
echo "  1. Install required dependencies (OpenGL, GLFW, Mesa)"
echo "  2. Clone librealsense repository"
echo "  3. Build with GLSL extensions enabled"
echo "  4. Install to system directories"
echo ""
echo "Estimated time: 15-30 minutes"
echo "=========================================="
echo ""

# Check if running as root
if [ "$EUID" -eq 0 ]; then 
    echo "❌ ERROR: Do not run this script as root/sudo"
    echo "   The script will prompt for sudo when needed"
    exit 1
fi

# Step 1: Install dependencies
echo "📦 Step 1/4: Installing dependencies..."
echo ""
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    git \
    libglfw3-dev \
    libgl1-mesa-dev \
    libglu1-mesa-dev \
    intel-gpu-tools

echo ""
echo "✅ Dependencies installed"
echo ""

# Step 2: Clone repository
echo "📥 Step 2/4: Cloning librealsense repository..."
echo ""

BUILD_DIR="/tmp/librealsense_gpu_build"

# Remove old build directory if it exists
if [ -d "$BUILD_DIR" ]; then
    echo "   Removing old build directory..."
    rm -rf "$BUILD_DIR"
fi

# Clone fresh copy
git clone https://github.com/IntelRealSense/librealsense.git "$BUILD_DIR"
cd "$BUILD_DIR"

echo ""
echo "✅ Repository cloned to $BUILD_DIR"
echo ""

# Step 3: Build with GLSL extensions
echo "🔨 Step 3/4: Building librealsense with GLSL extensions..."
echo ""
echo "   This will take 10-20 minutes depending on your CPU..."
echo ""

mkdir -p build
cd build

# Configure with GLSL extensions enabled
cmake ../ \
    -DBUILD_GLSL_EXTENSIONS=ON \
    -DBUILD_EXAMPLES=ON \
    -DCMAKE_BUILD_TYPE=Release

# Build using all available CPU cores
make -j$(nproc)

echo ""
echo "✅ Build complete"
echo ""

# Step 4: Install
echo "📦 Step 4/4: Installing to system directories..."
echo ""
sudo make install

# Update library cache
sudo ldconfig

echo ""
echo "✅ Installation complete"
echo ""

# Verify installation
echo "🔍 Verifying GPU library installation..."
echo ""

if ldconfig -p | grep -q "librealsense2-gl"; then
    echo "✅ GPU library found:"
    ldconfig -p | grep librealsense2-gl
    echo ""
    echo "=========================================="
    echo "✨ SUCCESS!"
    echo "=========================================="
    echo ""
    echo "RealSense SDK with GPU acceleration is now installed."
    echo ""
    echo "Next steps:"
    echo "  1. Rebuild JuggleHub engine:"
    echo "     cd /home/twain/Projects/JuggleHub"
    echo "     ./scripts/build_engine.sh"
    echo ""
    echo "  2. Verify GPU acceleration is working:"
    echo "     sudo intel_gpu_top"
    echo "     (Run engine in another terminal and watch Render/3D usage)"
    echo ""
    echo "Expected performance improvement:"
    echo "  - CPU usage: 50-70% reduction in alignment overhead"
    echo "  - GPU usage: 20-60% on Render/3D engine"
    echo "  - Frame rate: More stable 60 FPS"
    echo ""
    echo "See GPU_ACCELERATION_IMPLEMENTATION.md for details."
    echo "=========================================="
else
    echo "❌ WARNING: GPU library not found in ldconfig"
    echo "   Installation may have failed"
    echo "   Check for errors above"
    exit 1
fi

# Cleanup option
echo ""
read -p "Remove build directory ($BUILD_DIR)? [y/N] " -n 1 -r
echo ""
if [[ $REPLY =~ ^[Yy]$ ]]; then
    rm -rf "$BUILD_DIR"
    echo "✅ Build directory removed"
fi

echo ""
echo "Done!"