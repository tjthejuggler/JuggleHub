#!/bin/bash

# Test script to run engine with debug logging and monitor GPU_debug.log

echo "=== Starting Engine Debug Test ==="
echo "This script will:"
echo "1. Clear GPU_debug.log"
echo "2. Start the engine"
echo "3. Monitor GPU_debug.log in real-time"
echo "4. Stop after 30 seconds or on Ctrl+C"
echo ""

# Clear the debug log
echo "Clearing GPU_debug.log..."
> GPU_debug.log

# Start the engine in the background
echo "Starting engine..."
./engine/build/juggle_engine --use-dnn-tracker &
ENGINE_PID=$!

echo "Engine started with PID: $ENGINE_PID"
echo "Monitoring GPU_debug.log (Ctrl+C to stop)..."
echo "================================"

# Monitor the log file
tail -f GPU_debug.log &
TAIL_PID=$!

# Wait for 30 seconds or until user interrupts
sleep 30 &
SLEEP_PID=$!

# Trap Ctrl+C to clean up
trap "echo ''; echo 'Stopping...'; kill $ENGINE_PID 2>/dev/null; kill $TAIL_PID 2>/dev/null; kill $SLEEP_PID 2>/dev/null; exit 0" INT

# Wait for sleep to finish
wait $SLEEP_PID

# Stop monitoring and engine
echo ""
echo "30 seconds elapsed, stopping engine..."
kill $TAIL_PID 2>/dev/null
kill $ENGINE_PID 2>/dev/null

# Wait a moment for engine to stop
sleep 2

echo ""
echo "=== Final GPU_debug.log content ==="
cat GPU_debug.log
echo ""
echo "=== Test Complete ==="