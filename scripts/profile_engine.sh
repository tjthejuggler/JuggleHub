#!/bin/bash
# Profile the C++ engine to find performance bottlenecks
# This script uses perf to record performance data and generate reports

set -e

echo "=========================================="
echo "JuggleHub Engine Performance Profiler"
echo "=========================================="
echo ""

# Check if perf is installed
if ! command -v perf &> /dev/null; then
    echo "ERROR: 'perf' is not installed"
    echo "Install with: sudo apt-get install linux-tools-common linux-tools-generic"
    exit 1
fi

# Check if engine binary exists
ENGINE_BIN="./engine/build/bin/juggle_engine"
if [ ! -f "$ENGINE_BIN" ]; then
    echo "ERROR: Engine binary not found at $ENGINE_BIN"
    echo "Build it first with: ./scripts/build_engine.sh"
    exit 1
fi

# Clean up old profile data
echo "Cleaning up old profile data..."
rm -f perf.data perf.data.old engine_profile.txt

echo ""
echo "=========================================="
echo "Starting Performance Recording"
echo "=========================================="
echo "This will record performance data while the engine runs."
echo "Let it run for 30-60 seconds to capture representative data."
echo "Press Ctrl+C to stop recording."
echo ""
echo "Starting in 3 seconds..."
sleep 3

# Record performance data with call graph
echo "Recording performance data..."
sudo perf record -g -F 99 --call-graph dwarf -o perf.data $ENGINE_BIN &
PERF_PID=$!

# Wait for user to stop or timeout after 60 seconds
echo ""
echo "Recording... Press Ctrl+C to stop (or will auto-stop after 60s)"
sleep 60 || true

# Stop perf if still running
if ps -p $PERF_PID > /dev/null 2>&1; then
    echo "Stopping recording..."
    sudo kill -INT $PERF_PID
    sleep 2
fi

# Check if perf.data was created
if [ ! -f "perf.data" ]; then
    echo "ERROR: No performance data was recorded"
    exit 1
fi

echo ""
echo "=========================================="
echo "Generating Performance Report"
echo "=========================================="

# Generate text report
echo "Creating text report: engine_profile.txt"
sudo perf report --stdio -i perf.data > engine_profile.txt 2>/dev/null || true

# Show top 20 functions
echo ""
echo "Top 20 functions by CPU time:"
echo "----------------------------------------"
sudo perf report --stdio -i perf.data --sort comm,dso,symbol --percent-limit 1 2>/dev/null | head -40

echo ""
echo "=========================================="
echo "Profile Complete!"
echo "=========================================="
echo ""
echo "Results saved to:"
echo "  - perf.data (raw data)"
echo "  - engine_profile.txt (text report)"
echo ""
echo "To view interactive report:"
echo "  sudo perf report -i perf.data"
echo ""
echo "To generate flamegraph (if installed):"
echo "  sudo perf script -i perf.data | flamegraph.pl > engine_flamegraph.svg"
echo ""