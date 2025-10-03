#!/bin/bash
# Run engine briefly and capture debug output
timeout 5s ./engine/build/bin/juggle_engine 2>&1 | grep -A 50 "DETECTION DEBUG\|3D Matching" | tail -n 100 > engine_debug_sample.txt
echo "Debug output saved to engine_debug_sample.txt"
cat engine_debug_sample.txt