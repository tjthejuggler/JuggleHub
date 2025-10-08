#!/bin/bash

# Test script to verify GPU-accelerated alignment is working
# This script runs the engine and monitors both CPU and GPU usage

echo "=== GPU Alignment Verification Test ==="
echo ""
echo "This script will:"
echo "1. Start monitoring GPU usage in background"
echo "2. Run the engine for 30 seconds"
echo "3. Show CPU and GPU usage statistics"
echo ""
echo "Expected results if GPU acceleration is working:"
echo "  - CPU usage by juggle_engine: ~15-25% (down from ~50%)"
echo "  - GPU Render/3D usage: 20-60%"
echo "  - Console message: 'OpenGL context is active on processing thread'"
echo ""
echo "Press Ctrl+C to stop early"
echo ""

# Check if intel_gpu_top is available
if ! command -v intel_gpu_top &> /dev/null; then
    echo "WARNING: intel_gpu_top not found. Install with: sudo apt install intel-gpu-tools"
    echo "Continuing without GPU monitoring..."
    GPU_MONITOR=false
else
    GPU_MONITOR=true
fi

# Start GPU monitoring in background if available
if [ "$GPU_MONITOR" = true ]; then
    echo "Starting GPU monitor..."
    sudo intel_gpu_top -o gpu_usage.log &
    GPU_PID=$!
    sleep 2
fi

# Start the engine
echo "Starting engine..."
cd /home/twain/Projects/JuggleHub
./scripts/run_hub.sh --use-venv --device GPU &
ENGINE_PID=$!

# Monitor for 30 seconds
echo ""
echo "Monitoring for 30 seconds..."
echo "Watch for the GPU verification message in the engine output above"
echo ""

for i in {1..30}; do
    echo -n "."
    sleep 1
done
echo ""

# Stop the engine
echo "Stopping engine..."
kill $ENGINE_PID 2>/dev/null
wait $ENGINE_PID 2>/dev/null

# Stop GPU monitor
if [ "$GPU_MONITOR" = true ]; then
    echo "Stopping GPU monitor..."
    sudo kill $GPU_PID 2>/dev/null
    wait $GPU_PID 2>/dev/null
    
    if [ -f gpu_usage.log ]; then
        echo ""
        echo "=== GPU Usage Summary ==="
        echo "Check gpu_usage.log for detailed GPU usage"
        echo "Look for 'Render/3D' line - should show 20-60% if GPU acceleration is working"
        tail -20 gpu_usage.log
    fi
fi

echo ""
echo "=== Test Complete ==="
echo ""
echo "To verify GPU acceleration is working:"
echo "1. Check engine console output for: 'OpenGL context is active on processing thread'"
echo "2. Run 'htop' and check juggle_engine CPU usage (should be ~15-25%)"
echo "3. Run 'sudo intel_gpu_top' and check Render/3D usage (should be 20-60%)"
echo ""
echo "If CPU is still at 50%, GPU acceleration is NOT working"