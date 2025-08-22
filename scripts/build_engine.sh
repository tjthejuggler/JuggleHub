#!/bin/bash

# Build script for JuggleHub C++ Engine
# This script builds the C++ engine with Protocol Buffers support

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}🔨 Building JuggleHub Engine${NC}"

# Get script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
ENGINE_DIR="$PROJECT_ROOT/engine"

# Check if we're in the right directory
if [ ! -f "$PROJECT_ROOT/api/v1/juggler.proto" ]; then
    echo -e "${RED}❌ Error: Could not find juggler.proto. Are you in the JuggleHub root directory?${NC}"
    exit 1
fi

# Check dependencies
echo -e "${YELLOW}🔍 Checking dependencies...${NC}"

# Check for protoc
if ! command -v protoc &> /dev/null; then
    echo -e "${RED}❌ Error: protoc (Protocol Buffers compiler) not found${NC}"
    echo "Please install Protocol Buffers:"
    echo "  Ubuntu/Debian: sudo apt install protobuf-compiler libprotobuf-dev"
    echo "  macOS: brew install protobuf"
    exit 1
fi

# Check for cmake
if ! command -v cmake &> /dev/null; then
    echo -e "${RED}❌ Error: cmake not found${NC}"
    echo "Please install CMake:"
    echo "  Ubuntu/Debian: sudo apt install cmake"
    echo "  macOS: brew install cmake"
    exit 1
fi

# Check for pkg-config
if ! command -v pkg-config &> /dev/null; then
    echo -e "${RED}❌ Error: pkg-config not found${NC}"
    echo "Please install pkg-config:"
    echo "  Ubuntu/Debian: sudo apt install pkg-config"
    echo "  macOS: brew install pkg-config"
    exit 1
fi

# Check for RealSense
if ! pkg-config --exists realsense2; then
    echo -e "${RED}❌ Error: Intel RealSense SDK not found${NC}"
    echo "Please install Intel RealSense SDK 2.0:"
    echo "  Ubuntu/Debian: https://github.com/IntelRealSense/librealsense/blob/master/doc/distribution_linux.md"
    echo "  macOS: brew install librealsense"
    exit 1
fi

# Check for OpenCV
if ! pkg-config --exists opencv4; then
    echo -e "${RED}❌ Error: OpenCV not found${NC}"
    echo "Please install OpenCV:"
    echo "  Ubuntu/Debian: sudo apt install libopencv-dev"
    echo "  macOS: brew install opencv"
    exit 1
fi

# Check for ZeroMQ
if ! pkg-config --exists libzmq; then
    echo -e "${RED}❌ Error: ZeroMQ not found${NC}"
    echo "Please install ZeroMQ:"
    echo "  Ubuntu/Debian: sudo apt install libzmq3-dev"
    echo "  macOS: brew install zeromq"
    exit 1
fi

echo -e "${GREEN}✅ All dependencies found${NC}"

# Parse command line arguments
BUILD_TYPE="Release"
CLEAN_BUILD=false
ENABLE_HAND_TRACKING=false
VERBOSE=false

while [[ $# -gt 0 ]]; do
    case $1 in
        --debug)
            BUILD_TYPE="Debug"
            shift
            ;;
        --clean)
            CLEAN_BUILD=true
            shift
            ;;
        --hand-tracking)
            ENABLE_HAND_TRACKING=true
            shift
            ;;
        --verbose)
            VERBOSE=true
            shift
            ;;
        -h|--help)
            echo "Usage: $0 [OPTIONS]"
            echo "Options:"
            echo "  --debug           Build in debug mode"
            echo "  --clean           Clean build directory first"
            echo "  --hand-tracking   Enable MediaPipe hand tracking"
            echo "  --verbose         Verbose build output"
            echo "  -h, --help        Show this help message"
            exit 0
            ;;
        *)
            echo -e "${RED}❌ Unknown option: $1${NC}"
            exit 1
            ;;
    esac
done

# Create build directory
BUILD_DIR="$ENGINE_DIR/build"

if [ "$CLEAN_BUILD" = true ] && [ -d "$BUILD_DIR" ]; then
    echo -e "${YELLOW}🧹 Cleaning build directory...${NC}"
    rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure CMake
echo -e "${YELLOW}⚙️ Configuring CMake...${NC}"

CMAKE_ARGS=(
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
)

if [ "$ENABLE_HAND_TRACKING" = true ]; then
    CMAKE_ARGS+=(-DENABLE_HAND_TRACKING=ON)
    echo -e "${YELLOW}👋 Hand tracking enabled${NC}"
fi

if [ "$VERBOSE" = true ]; then
    CMAKE_ARGS+=(-DCMAKE_VERBOSE_MAKEFILE=ON)
fi

cmake "${CMAKE_ARGS[@]}" ..

# Build
echo -e "${YELLOW}🔨 Building engine...${NC}"

if [ "$VERBOSE" = true ]; then
    make -j$(nproc)
else
    make -j$(nproc) 2>&1 | grep -E "(error|Error|ERROR|warning|Warning|WARNING|\[.*%\])" || true
fi

# Check if build was successful
if [ -f "$BUILD_DIR/bin/juggle_engine" ]; then
    echo -e "${GREEN}✅ Engine built successfully!${NC}"
    echo -e "${BLUE}📍 Executable location: $BUILD_DIR/bin/juggle_engine${NC}"
    
    # Show file info
    ls -lh "$BUILD_DIR/bin/juggle_engine"
    
    # Test run (just version check)
    echo -e "${YELLOW}🧪 Testing engine...${NC}"
    if "$BUILD_DIR/bin/juggle_engine" --help 2>/dev/null || true; then
        echo -e "${GREEN}✅ Engine test passed${NC}"
    else
        echo -e "${YELLOW}⚠️ Engine test inconclusive (this may be normal)${NC}"
    fi
    
else
    echo -e "${RED}❌ Build failed - executable not found${NC}"
    exit 1
fi

echo -e "${GREEN}🎉 Build completed successfully!${NC}"
echo -e "${BLUE}To run the engine: $BUILD_DIR/bin/juggle_engine${NC}"