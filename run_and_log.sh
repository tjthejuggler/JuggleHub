#!/bin/bash
/home/twain/Projects/JuggleHub/engine/build/bin/juggle_engine &
ENGINE_PID=$!
sleep 2
PYTHONPATH=$(pwd)/hub python3 scripts/load_module.py UdpBallColorModule --ip 10.54.136.205
PYTHONPATH=$(pwd)/hub python3 scripts/load_module.py PositionToRgbModule
wait $ENGINE_PID