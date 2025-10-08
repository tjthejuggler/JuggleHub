#!/bin/bash
set -e

# Performance profiling script for JuggleHub Engine using perf
# This script runs the engine with perf to collect performance data

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}🔍 JuggleHub Engine Performance Profiler${NC}"

# Get script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
ENGINE_DIR="$PROJECT_ROOT/engine"
BUILD_DIR="$ENGINE_DIR/build"
ENGINE_BIN="$BUILD_DIR/juggle_engine"

# Default values
DURATION=30
OUTPUT_FILE="perf.data"
RECORD_CALLGRAPH=true
FREQUENCY=999
ENGINE_ARGS=""
REBUILD=false
SHOW_REPORT=true

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --duration)
            DURATION="$2"
            shift 2
            ;;
        --output)
            OUTPUT_FILE="$2"
            shift 2
            ;;
        --no-callgraph)
            RECORD_CALLGRAPH=false
            shift
            ;;
        --frequency)
            FREQUENCY="$2"
            shift 2
            ;;
        --rebuild)
            REBUILD=true
            shift
            ;;
        --no-report)
            SHOW_REPORT=false
            shift
            ;;
        --engine-args)
            ENGINE_ARGS="$2"
            shift 2
            ;;
        -h|--help)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --duration SECONDS      Duration to profile (default: 30)"
            echo "  --output FILE           Output file name (default: perf.data)"
            echo "  --no-callgraph          Don't record call graphs (faster but less detail)"
            echo "  --frequency HZ          Sampling frequency (default: 999)"
            echo "  --rebuild               Rebuild engine with RelWithDebInfo before profiling"
            echo "  --no-report             Don't show perf report after recording"
            echo "  --engine-args \"ARGS\"    Arguments to pass to the engine (in quotes)"
            echo "  -h, --help              Show this help message"
            echo ""
            echo "Examples:"
            echo "  $0 --duration 60 --rebuild"
            echo "  $0 --engine-args \"--verbose --device=GPU\""
            echo "  $0 --output profile_gpu.data --engine-args \"--device=GPU\""
            exit 0
            ;;
        *)
            echo -e "${RED}❌ Unknown option: $1${NC}"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

# Check if perf is installed
if ! command -v perf &> /dev/null; then
    echo -e "${RED}❌ Error: perf not found${NC}"
    echo "Please install perf tools:"
    echo "  sudo apt update"
    echo "  sudo apt install linux-tools-common linux-tools-\$(uname -r)"
    exit 1
fi

# Rebuild if requested
if [ "$REBUILD" = true ]; then
    echo -e "${YELLOW}🔨 Rebuilding engine with RelWithDebInfo...${NC}"
    cd "$BUILD_DIR"
    cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo ..
    make -j$(nproc)
    cd "$PROJECT_ROOT"
    echo -e "${GREEN}✅ Rebuild complete${NC}"
fi

# Check if engine exists
if [ ! -f "$ENGINE_BIN" ]; then
    echo -e "${RED}❌ Error: Engine binary not found at $ENGINE_BIN${NC}"
    echo "Please build the engine first:"
    echo "  cd $ENGINE_DIR/build"
    echo "  cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo .."
    echo "  make -j\$(nproc)"
    echo ""
    echo "Or use: $0 --rebuild"
    exit 1
fi

# Check if binary has debug symbols
if ! file "$ENGINE_BIN" | grep -q "not stripped"; then
    echo -e "${YELLOW}⚠️ Warning: Engine binary appears to be stripped (no debug symbols)${NC}"
    echo "For best results, rebuild with:"
    echo "  cd $ENGINE_DIR/build"
    echo "  cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo .."
    echo "  make -j\$(nproc)"
    echo ""
    read -p "Continue anyway? (y/N) " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        exit 1
    fi
fi

# Build perf command
PERF_CMD="sudo perf record"

if [ "$RECORD_CALLGRAPH" = true ]; then
    PERF_CMD="$PERF_CMD -g"
fi

PERF_CMD="$PERF_CMD -F $FREQUENCY"
PERF_CMD="$PERF_CMD -o $OUTPUT_FILE"
PERF_CMD="$PERF_CMD --"
PERF_CMD="$PERF_CMD $ENGINE_BIN"

if [ -n "$ENGINE_ARGS" ]; then
    PERF_CMD="$PERF_CMD $ENGINE_ARGS"
fi

# Show configuration
echo -e "${BLUE}📋 Profiling Configuration:${NC}"
echo -e "  Engine: $ENGINE_BIN"
echo -e "  Duration: ${DURATION}s"
echo -e "  Output: $OUTPUT_FILE"
echo -e "  Call graph: $RECORD_CALLGRAPH"
echo -e "  Frequency: ${FREQUENCY}Hz"
if [ -n "$ENGINE_ARGS" ]; then
    echo -e "  Engine args: $ENGINE_ARGS"
fi
echo ""

# Warn about sudo
echo -e "${YELLOW}⚠️ This script requires sudo to run perf${NC}"
echo ""

# Run perf record
echo -e "${GREEN}🚀 Starting profiling...${NC}"
echo -e "${YELLOW}The engine will run for ${DURATION} seconds${NC}"
echo -e "${YELLOW}Press Ctrl+C to stop early${NC}"
echo ""

# Run with timeout
if timeout ${DURATION}s $PERF_CMD; then
    echo ""
    echo -e "${GREEN}✅ Profiling completed successfully${NC}"
else
    EXIT_CODE=$?
    if [ $EXIT_CODE -eq 124 ]; then
        echo ""
        echo -e "${GREEN}✅ Profiling completed (timeout reached)${NC}"
    else
        echo ""
        echo -e "${YELLOW}⚠️ Profiling stopped (exit code: $EXIT_CODE)${NC}"
    fi
fi

# Check if perf.data was created
if [ ! -f "$OUTPUT_FILE" ]; then
    echo -e "${RED}❌ Error: Profile data file not created${NC}"
    exit 1
fi

# Show file info
echo -e "${BLUE}📊 Profile data saved to: $OUTPUT_FILE${NC}"
ls -lh "$OUTPUT_FILE"
echo ""

# Show report if requested
if [ "$SHOW_REPORT" = true ]; then
    echo -e "${BLUE}📈 Generating performance report...${NC}"
    echo -e "${YELLOW}Use arrow keys to navigate, 'q' to quit${NC}"
    echo ""
    sleep 2
    sudo perf report -i "$OUTPUT_FILE"
else
    echo -e "${BLUE}💡 To view the report later, run:${NC}"
    echo -e "  sudo perf report -i $OUTPUT_FILE"
fi

echo ""
echo -e "${GREEN}🎉 Profiling complete!${NC}"
echo ""
echo -e "${BLUE}📚 Next steps:${NC}"
echo -e "  1. Review the report to identify hotspots (functions with high overhead)"
echo -e "  2. Focus optimization efforts on the top functions"
echo -e "  3. Use 'perf annotate' for line-by-line analysis:"
echo -e "     sudo perf annotate -i $OUTPUT_FILE"
echo -e "  4. Generate a flame graph for visualization (if installed):"
echo -e "     sudo perf script -i $OUTPUT_FILE | stackcollapse-perf.pl | flamegraph.pl > flamegraph.svg"