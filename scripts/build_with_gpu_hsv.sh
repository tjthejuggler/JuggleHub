#!/bin/bash

# Build script for JuggleHub Engine with GPU-accelerated HSV conversion
# This script builds the engine with GPU acceleration enabled

set -e  # Exit on error

echo "=========================================="
echo "Building JuggleHub Engine with GPU HSV"
echo "=========================================="
echo ""

# Check if we're in the right directory
if [ ! -d "engine" ]; then
    echo "Error: Must run from project root directory"
    exit 1
fi

# Check OpenCV OpenCL support
echo "Checking OpenCV OpenCL support..."
if python3 -c "import cv2; exit(0 if cv2.ocl.haveOpenCL() else 1)" 2>/dev/null; then
    echo "✓ OpenCV has OpenCL support"
else
    echo "⚠ WARNING: OpenCV does not have OpenCL support"
    echo "  GPU acceleration will fall back to CPU"
    echo "  To enable OpenCL, rebuild OpenCV with -DWITH_OPENCL=ON"
fi
echo ""

# Check for OpenCL runtime
echo "Checking OpenCL runtime..."
if command -v clinfo &> /dev/null; then
    echo "✓ clinfo found"
    OPENCL_DEVICES=$(clinfo -l 2>/dev/null | grep -c "Device" || echo "0")
    echo "  OpenCL devices detected: $OPENCL_DEVICES"
else
    echo "⚠ WARNING: clinfo not found"
    echo "  Install with: sudo apt install clinfo"
fi
echo ""

# Create build directory
cd engine
mkdir -p build
cd build

# Configure with CMake
echo "Configuring with CMake..."
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build
echo ""
echo "Building..."
make -j$(nproc)

echo ""
echo "=========================================="
echo "Build complete!"
echo "=========================================="
echo ""
echo "To run the engine:"
echo "  cd ../.."
echo "  ./scripts/run_hub.sh"
echo ""
echo "Look for these messages in the output:"
echo "  [GpuHsvConverter] GPU acceleration ENABLED"
echo "  [GpuHsvConverter] Device: Intel(R) Arc(TM) Graphics"
echo ""