#!/bin/bash

# Run JuggleHub Engine with NPU Support
# This script sets up the NPU environment and runs the engine

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}🚀 Starting JuggleHub Engine with NPU Support${NC}"

# Get script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
ENGINE_DIR="$PROJECT_ROOT/engine"
BUILD_DIR="$ENGINE_DIR/build"

# Check if engine is built
if [ ! -f "$BUILD_DIR/juggle_engine" ]; then
    echo -e "${RED}❌ Engine not found. Building first...${NC}"
    "$SCRIPT_DIR/build_engine.sh"
fi

# Setup NPU environment
echo -e "${YELLOW}🔧 Setting up NPU environment...${NC}"

# Add Intel NPU driver libraries to LD_LIBRARY_PATH
export LD_LIBRARY_PATH="/snap/intel-npu-driver/10/usr/lib/x86_64-linux-gnu:$LD_LIBRARY_PATH"

# Source OpenVINO environment
source /opt/intel/openvino_2025.2.0/setupvars.sh

# Verify NPU is available
echo -e "${YELLOW}🔍 Verifying NPU availability...${NC}"
python3 -c "
import openvino as ov
core = ov.Core()
devices = core.get_available_devices()
if 'NPU' in devices:
    print('✅ NPU is available and ready to use!')
else:
    print('❌ NPU not detected. Will fall back to CPU/GPU.')
    exit(1)
" || {
    echo -e "${YELLOW}⚠️ NPU not available, but continuing with CPU/GPU support${NC}"
}

# Run the engine with all arguments passed through
echo -e "${GREEN}🎯 Starting JuggleHub Engine...${NC}"
echo -e "${BLUE}💡 To use NPU, add: --device=NPU --use-dnn-tracker${NC}"
echo -e "${BLUE}💡 Example: $0 --device=NPU --use-dnn-tracker --verbose${NC}"
echo ""

cd "$PROJECT_ROOT"
exec "$BUILD_DIR/juggle_engine" "$@"