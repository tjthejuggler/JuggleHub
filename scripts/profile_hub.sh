#!/bin/bash
# Profile the Python hub to find performance bottlenecks
# This script uses py-spy to record performance data

set -e

echo "=========================================="
echo "JuggleHub Python Hub Profiler"
echo "=========================================="
echo ""

# Check if py-spy is installed
if ! command -v py-spy &> /dev/null; then
    echo "py-spy is not installed. Installing..."
    pip install py-spy
    echo ""
fi

# Check if hub exists
if [ ! -d "hub" ]; then
    echo "ERROR: hub directory not found"
    exit 1
fi

echo "=========================================="
echo "Starting Performance Recording"
echo "=========================================="
echo "This will record performance data while the hub runs."
echo "Let it run for 30-60 seconds to capture representative data."
echo "Press Ctrl+C to stop recording."
echo ""
echo "Starting in 3 seconds..."
sleep 3

# Clean up old profile data
rm -f hub_profile.svg hub_profile.txt hub_profile.speedscope.json

echo "Recording performance data..."
echo ""

# Record with py-spy (creates flamegraph)
py-spy record \
    --rate 100 \
    --format speedscope \
    --output hub_profile.speedscope.json \
    --duration 60 \
    -- python -m hub.main &

PY_SPY_PID=$!

# Also create SVG flamegraph
py-spy record \
    --rate 100 \
    --format flamegraph \
    --output hub_profile.svg \
    --duration 60 \
    -- python -m hub.main &

wait $PY_SPY_PID 2>/dev/null || true

echo ""
echo "=========================================="
echo "Profile Complete!"
echo "=========================================="
echo ""
echo "Results saved to:"
echo "  - hub_profile.svg (flamegraph - open in browser)"
echo "  - hub_profile.speedscope.json (interactive - upload to speedscope.app)"
echo ""
echo "To view the flamegraph:"
echo "  firefox hub_profile.svg"
echo "  # or"
echo "  google-chrome hub_profile.svg"
echo ""
echo "For interactive analysis:"
echo "  1. Go to https://www.speedscope.app/"
echo "  2. Upload hub_profile.speedscope.json"
echo ""