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

# Generate Python Protocol Buffer files if they don't exist or are outdated
PROTO_FILE="$API_DIR/juggler.proto"
PY_PROTO_FILE="$API_DIR/juggler_pb2.py"

if [ ! -f "$PY_PROTO_FILE" ] || [ "$PROTO_FILE" -nt "$PY_PROTO_FILE" ]; then
    echo -e "${YELLOW}🔄 Generating Python Protocol Buffer files...${NC}"
    
    cd "$API_DIR"
    protoc --python_out=. juggler.proto
    
    if [ -f "juggler_pb2.py" ]; then
        echo -e "${GREEN}✅ Protocol Buffer files generated${NC}"
    else
        echo -e "${RED}❌ Error: Failed to generate Protocol Buffer files${NC}"
        exit 1
    fi
    
    cd "$PROJECT_ROOT"
else
    echo -e "${GREEN}✅ Protocol Buffer files are up to date${NC}"
fi

# Check if virtual environment should be used
USE_VENV=false
VENV_DIR="$PROJECT_ROOT/venv"

# Parse command line arguments
INSTALL_DEPS=false
CREATE_VENV=false
NO_UI=false
DEBUG=false
ZMQ_ENDPOINT="tcp://localhost:5555"
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
        -h|--help)
            echo "Usage: $0 [OPTIONS] [-- SCRIPT_ARGS]"
            echo "Options:"
            echo "  --install-deps    Install Python dependencies"
            echo "  --create-venv     Create and use virtual environment"
            echo "  --use-venv        Use existing virtual environment"
            echo "  -h, --help        Show this help message"
            echo ""
            echo "Script Arguments (passed to hub/main.py):"
            echo "  All arguments after '--' or any unknown arguments will be passed to the Python script."
            echo "  Example: $0 -- --watch-ips 192.168.1.101 --debug"
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

# Build hub arguments
HUB_ARGS=("${PASS_THROUGH_ARGS[@]}")

# Change to hub directory
cd "$HUB_DIR"

# Run the hub
echo -e "${BLUE}🎯 Starting JuggleHub...${NC}"
echo -e "${BLUE}Arguments passed to hub: ${HUB_ARGS[*]}${NC}"

# Add the API directory to Python path
export PYTHONPATH="$API_DIR:$PYTHONPATH"

# Run the hub
python3 main.py "${HUB_ARGS[@]}"

echo -e "${GREEN}✅ JuggleHub hub stopped${NC}"