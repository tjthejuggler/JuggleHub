#!/bin/bash

# Run script for JuggleHub Python Hub
# This script sets up the Python environment and runs the hub

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to clean up the engine process
ENGINE_PID=0
cleanup() {
    echo -e "${YELLOW}🧹 Cleaning up engine process...${NC}"
    if [ $ENGINE_PID -ne 0 ]; then
        echo "Killing engine process $ENGINE_PID"
        # Kill the entire process group to ensure child processes are terminated
        kill -TERM -$ENGINE_PID 2>/dev/null
    fi
}

# Set the trap to call cleanup on script exit
trap cleanup EXIT

echo -e "${BLUE}🚀 Starting JuggleHub Python Hub${NC}"

# Get script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
HUB_DIR="$PROJECT_ROOT/hub"
API_DIR="$PROJECT_ROOT/api/v1"

# Check if we're in the right directory
if [ ! -f "$PROJECT_ROOT/api/v1/juggler.proto" ]; then
    echo -e "${RED}❌ Error: Could not find juggler.proto. Are you in the JuggleHub root directory?${NC}"
    exit 1
fi

# Check for Python
if ! command -v python3 &> /dev/null; then
    echo -e "${RED}❌ Error: python3 not found${NC}"
    echo "Please install Python 3.8 or later"
    exit 1
fi

# Check Python version
PYTHON_VERSION=$(python3 -c "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}')")
REQUIRED_VERSION="3.8"

if [ "$(printf '%s\n' "$REQUIRED_VERSION" "$PYTHON_VERSION" | sort -V | head -n1)" != "$REQUIRED_VERSION" ]; then
    echo -e "${RED}❌ Error: Python $PYTHON_VERSION found, but $REQUIRED_VERSION or later is required${NC}"
    exit 1
fi

echo -e "${GREEN}✅ Python $PYTHON_VERSION found${NC}"

# Check for protoc
if ! command -v protoc &> /dev/null; then
    echo -e "${RED}❌ Error: protoc (Protocol Buffers compiler) not found${NC}"
    echo "Please install Protocol Buffers:"
    echo "  Ubuntu/Debian: sudo apt install protobuf-compiler"
    echo "  macOS: brew install protobuf"
    exit 1
fi

# Check if virtual environment should be used
# Auto-detect venv if it exists
VENV_DIR="$PROJECT_ROOT/venv"
if [ -d "$VENV_DIR" ] && [ -f "$VENV_DIR/bin/activate" ]; then
    USE_VENV=true
else
    USE_VENV=false
fi

# Parse command line arguments
INSTALL_DEPS=false
CREATE_VENV=false
NO_UI=false
DEBUG=false
ENGINE_LOG=false  # New flag for engine logging
TRACKING_MODE="simple"  # Default: simple (depth+color), alternative: yolo
ZMQ_ENDPOINT="tcp://localhost:5555"
CAMERA_SETTINGS=""
ENGINE_DEVICE="GPU"  # Default device for the engine
ENGINE_MODEL="yolo11n" # Default model
PASS_THROUGH_ARGS=()
 
 while [[ $# -gt 0 ]]; do
     case $1 in
         --install-deps)
             INSTALL_DEPS=true
             shift
             ;;
         --create-venv)
             CREATE_VENV=true
             USE_VENV=true
             shift
             ;;
         --use-venv)
             USE_VENV=true
             shift
             ;;
         --camera-settings)
             CAMERA_SETTINGS="$2"
             shift 2
             ;;
         --device)
             ENGINE_DEVICE="$2"
             shift 2
             ;;
         --model)
             ENGINE_MODEL="$2"
             shift 2
             ;;
         --engine-log)
             ENGINE_LOG=true
             shift
             ;;
         --yolo-tracking)
             TRACKING_MODE="yolo"
             shift
             ;;
         --simple-tracking)
             TRACKING_MODE="simple"
             shift
             ;;
         -h|--help)
             echo "Usage: $0 [OPTIONS] [-- SCRIPT_ARGS]"
             echo "Options:"
             echo "  --install-deps              Install Python dependencies"
             echo "  --create-venv               Create and use virtual environment"
             echo "  --use-venv                  Use existing virtual environment"
             echo "  --camera-settings <file>    Camera settings JSON file (e.g., default.json)"
             echo "  --device <device>           Engine inference device (CPU, GPU, NPU, AUTO) [default: GPU]"
             echo "  --model <model>             Engine model name (e.g., yolo11s) [default: yolo11n]"
             echo "  --engine-log                Enable engine logging to engine.log and engine_debug.log"
             echo "  --simple-tracking           Use depth+color ball tracking (DEFAULT - no YOLO ball model)"
             echo "  --yolo-tracking             Use YOLO ball detection model (loads all trackers)"
             echo "  -h, --help                  Show this help message"
             echo ""
             echo "Script Arguments (passed to hub/main.py):"
             echo "  All arguments after '--' or any unknown arguments will be passed to the Python script."
             echo "  Example: $0 --use-venv --device NPU --camera-settings default.json -- --watch-ips 192.168.1.101 --debug"
             exit 0
             ;;
         *)
             # Collect any other arguments to pass to the python script
             PASS_THROUGH_ARGS+=("$1")
             shift
             ;;
     esac
 done

# Create virtual environment if requested
if [ "$CREATE_VENV" = true ]; then
    if [ -d "$VENV_DIR" ]; then
        echo -e "${YELLOW}⚠️ Virtual environment already exists at $VENV_DIR${NC}"
        read -p "Remove and recreate? (y/N): " -n 1 -r
        echo
        if [[ $REPLY =~ ^[Yy]$ ]]; then
            rm -rf "$VENV_DIR"
        else
            echo -e "${BLUE}Using existing virtual environment${NC}"
        fi
    fi
    
    if [ ! -d "$VENV_DIR" ]; then
        echo -e "${YELLOW}🔧 Creating virtual environment...${NC}"
        python3 -m venv "$VENV_DIR"
        echo -e "${GREEN}✅ Virtual environment created${NC}"
    fi
fi

# Activate virtual environment if requested
if [ "$USE_VENV" = true ]; then
    if [ ! -d "$VENV_DIR" ]; then
        echo -e "${RED}❌ Error: Virtual environment not found at $VENV_DIR${NC}"
        echo "Run with --create-venv to create it first"
        exit 1
    fi
    
    echo -e "${YELLOW}🔧 Activating virtual environment...${NC}"
    source "$VENV_DIR/bin/activate"
    echo -e "${GREEN}✅ Virtual environment activated${NC}"
fi

# Install dependencies if requested
if [ "$INSTALL_DEPS" = true ]; then
    echo -e "${YELLOW}📦 Installing Python dependencies...${NC}"
    
    # Upgrade pip first
    python3 -m pip install --upgrade pip
    
    # Install requirements
    python3 -m pip install -r "$HUB_DIR/requirements.txt"
    
    echo -e "${GREEN}✅ Dependencies installed${NC}"
fi

# Check if required packages are available
echo -e "${YELLOW}🔍 Checking Python dependencies...${NC}"

MISSING_DEPS=()

# Check core dependencies
python3 -c "import google.protobuf" 2>/dev/null || MISSING_DEPS+=("protobuf")
python3 -c "import zmq" 2>/dev/null || MISSING_DEPS+=("pyzmq")
python3 -c "import numpy" 2>/dev/null || MISSING_DEPS+=("numpy")

# Check optional dependencies
if [ "$NO_UI" != true ]; then
    python3 -c "import PyQt6" 2>/dev/null || echo -e "${YELLOW}⚠️ PyQt6 not found - will use console UI${NC}"
fi

if [ ${#MISSING_DEPS[@]} -gt 0 ]; then
    echo -e "${RED}❌ Missing required dependencies: ${MISSING_DEPS[*]}${NC}"
    echo "Run with --install-deps to install them, or install manually:"
    echo "  pip install ${MISSING_DEPS[*]}"
    exit 1
fi

echo -e "${GREEN}✅ All required dependencies found${NC}"

# Generate Python Protocol Buffer files if they don't exist or are outdated
# NOTE: This must happen AFTER venv activation to use venv's grpcio-tools
PROTO_FILE="$API_DIR/juggler.proto"
PY_PROTO_FILE="$HUB_DIR/juggler_pb2.py"

if [ ! -f "$PY_PROTO_FILE" ] || [ "$PROTO_FILE" -nt "$PY_PROTO_FILE" ]; then
    echo -e "${YELLOW}🔄 Generating Python Protocol Buffer files...${NC}"
    
    # Generate the file directly into the hub directory
    # Use grpc_tools.protoc to generate both message and gRPC stub files
    python3 -m grpc_tools.protoc -I="$API_DIR" --python_out="$HUB_DIR" --grpc_python_out="$HUB_DIR" "$API_DIR/juggler.proto"
    
    # Ensure the hub directory is treated as a package
    touch "$HUB_DIR/__init__.py"
    
    if [ -f "$HUB_DIR/juggler_pb2.py" ] && [ -f "$HUB_DIR/juggler_pb2_grpc.py" ]; then
        echo -e "${GREEN}✅ Protocol Buffer and gRPC files generated${NC}"
    else
        echo -e "${RED}❌ Error: Failed to generate Protocol Buffer files${NC}"
        exit 1
    fi
else
    echo -e "${GREEN}✅ Protocol Buffer files are up to date${NC}"
fi

# --- C++ Engine Management ---
ENGINE_EXECUTABLE="$PROJECT_ROOT/engine/build/juggle_engine"

# Check if engine executable exists
if [ ! -f "$ENGINE_EXECUTABLE" ]; then
    echo -e "${RED}❌ Error: C++ engine executable not found at $ENGINE_EXECUTABLE${NC}"
    echo "Please build the engine first by running: ./scripts/build_engine.sh"
    exit 1
fi

# Check if engine binary is older than source files
echo -e "${YELLOW}🔍 Verifying engine build is up to date...${NC}"
ENGINE_SRC_DIR="$PROJECT_ROOT/engine/src"
ENGINE_INCLUDE_DIR="$PROJECT_ROOT/engine/include"

# Find the newest source file
NEWEST_SRC=$(find "$ENGINE_SRC_DIR" "$ENGINE_INCLUDE_DIR" -type f \( -name "*.cpp" -o -name "*.hpp" -o -name "*.h" \) -printf '%T@ %p\n' 2>/dev/null | sort -rn | head -1 | cut -d' ' -f2-)

if [ -n "$NEWEST_SRC" ]; then
    # Compare timestamps
    if [ "$NEWEST_SRC" -nt "$ENGINE_EXECUTABLE" ]; then
        echo -e "${YELLOW}⚠️ Warning: Engine source files are newer than the binary!${NC}"
        echo -e "${YELLOW}   Newest source: $NEWEST_SRC${NC}"
        echo -e "${YELLOW}   Binary: $ENGINE_EXECUTABLE${NC}"
        echo -e "${YELLOW}   The engine should be rebuilt to include recent changes.${NC}"
        echo ""
        read -p "Rebuild engine now? (Y/n): " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Nn]$ ]]; then
            echo -e "${BLUE}🔨 Rebuilding engine...${NC}"
            cd "$PROJECT_ROOT/engine"
            rm -rf build
            cmake -B build -S .
            cmake --build build --target juggle_engine -j$(nproc)
            cd "$PROJECT_ROOT"
            echo -e "${GREEN}✅ Engine rebuilt successfully${NC}"
        else
            echo -e "${YELLOW}⚠️ Continuing with outdated binary - this may cause issues!${NC}"
        fi
    else
        echo -e "${GREEN}✅ Engine binary is up to date${NC}"
    fi
fi

# Start the C++ engine in the background
echo -e "${BLUE}🧠 Starting C++ engine with device: $ENGINE_DEVICE${NC}"
ENGINE_ARGS=("--verbose" "--device=$ENGINE_DEVICE" "--model=$ENGINE_MODEL" "--pose-model=yolo11n-pose")

# Set ball tracking mode
if [ "$TRACKING_MODE" = "yolo" ]; then
    echo -e "${BLUE}🎯 Ball tracking mode: YOLO (loading ball detection model)${NC}"
    ENGINE_ARGS+=("--yolo-tracking")
else
    echo -e "${BLUE}⚡ Ball tracking mode: Simple (depth + LED color, no YOLO ball model)${NC}"
    ENGINE_ARGS+=("--simple-tracking")
fi

# Add debug-log flag if engine logging is enabled
if [ "$ENGINE_LOG" = true ]; then
    ENGINE_ARGS+=("--debug-log")
fi

# Determine which camera settings to use
if [ -n "$CAMERA_SETTINGS" ]; then
    CAMERA_SETTINGS_PATH="$PROJECT_ROOT/camera_settings/$CAMERA_SETTINGS"
    if [ -f "$CAMERA_SETTINGS_PATH" ]; then
        echo -e "${BLUE}📷 Using camera settings: $CAMERA_SETTINGS${NC}"
        ENGINE_ARGS+=("--camera-settings=$CAMERA_SETTINGS_PATH")
    else
        echo -e "${RED}❌ Error: Camera settings file not found: $CAMERA_SETTINGS_PATH${NC}"
        exit 1
    fi
else
    # Default to default.json if no camera settings specified
    DEFAULT_SETTINGS_PATH="$PROJECT_ROOT/camera_settings/default.json"
    if [ -f "$DEFAULT_SETTINGS_PATH" ]; then
        echo -e "${BLUE}📷 Using default camera settings: default.json${NC}"
        ENGINE_ARGS+=("--camera-settings=$DEFAULT_SETTINGS_PATH")
    else
        echo -e "${RED}❌ Error: Default camera settings file not found: $DEFAULT_SETTINGS_PATH${NC}"
        exit 1
    fi
fi

echo "Engine command: $ENGINE_EXECUTABLE ${ENGINE_ARGS[@]}"

# Clear log files before starting engine to ensure fresh logs each time
if [ "$ENGINE_LOG" = true ]; then
    echo -e "${YELLOW}🧹 Clearing previous log files for fresh start...${NC}"
    rm -f "$PROJECT_ROOT/engine.log" "$PROJECT_ROOT/engine_debug.log" 2>/dev/null
    touch "$PROJECT_ROOT/engine.log" "$PROJECT_ROOT/engine_debug.log"
    echo -e "${GREEN}✅ Log files cleared and ready for new session${NC}"
fi

# Clean up old log files if logging is disabled
if [ "$ENGINE_LOG" != true ]; then
    rm -f "$PROJECT_ROOT/engine.log" "$PROJECT_ROOT/engine_debug.log" 2>/dev/null
fi

# No longer need to change directories. Execute from project root.
set -m

# Check if gamemode is available
GAMEMODE_CMD=""
if command -v gamemoderun &> /dev/null; then
    GAMEMODE_CMD="gamemoderun"
    echo -e "${GREEN}🎮 GameMode detected - will use for better FPS performance${NC}"
else
    echo -e "${YELLOW}⚠️ GameMode not found - install with 'sudo apt install gamemode' for better FPS${NC}"
fi

# Redirect engine output based on --engine-log flag
if [ "$ENGINE_LOG" = true ]; then
    echo -e "${YELLOW}📝 Engine logging enabled - output will be written to engine.log and engine_debug.log${NC}"
    $GAMEMODE_CMD "$ENGINE_EXECUTABLE" "${ENGINE_ARGS[@]}" > "$PROJECT_ROOT/engine.log" 2>&1 &
else
    # Discard engine output by default
    $GAMEMODE_CMD "$ENGINE_EXECUTABLE" "${ENGINE_ARGS[@]}" > /dev/null 2>&1 &
fi
ENGINE_PID=$!
set +m

echo -e "${GREEN}✅ C++ engine started with PID $ENGINE_PID${NC}"
# Give the engine a moment to start up the ZMQ server
sleep 2
# --- End C++ Engine Management ---

# Build hub arguments
HUB_ARGS=("${PASS_THROUGH_ARGS[@]}")

# Change to hub directory
cd "$HUB_DIR"

# Run the hub
echo -e "${BLUE}🎯 Starting JuggleHub...${NC}"
echo -e "${BLUE}Arguments passed to hub: ${HUB_ARGS[*]}${NC}"

# Add the API directory to Python path

# Run the hub
# Loop to allow for restarts
while true; do
    python3 main.py "${HUB_ARGS[@]}"
    EXIT_CODE=$?
    if [ $EXIT_CODE -ne 10 ]; then
        break # Exit the loop if the exit code is not 10 (our restart code)
    fi
    echo -e "${YELLOW}🔄 Restarting hub as requested...${NC}"
    # The cleanup function will run, killing the old engine.
    # The loop will then restart it.
done

echo -e "${GREEN}✅ JuggleHub hub stopped${NC}"