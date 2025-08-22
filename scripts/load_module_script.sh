#!/bin/bash

# Load module script for JuggleHub Python Hub
# This script sets up the Python environment and runs the load_module.py script

set -e  # Exit on any error

# Get script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
HUB_DIR="$PROJECT_ROOT/hub"
API_DIR="$PROJECT_ROOT/api/v1"
VENV_DIR="$HUB_DIR/.venv" # Assuming venv is inside hub directory

# Change to hub directory
cd "$HUB_DIR"

# Activate virtual environment
if [ -d "$VENV_DIR" ]; then
    source "$VENV_DIR/bin/activate"
else
    echo "Error: Virtual environment not found at $VENV_DIR"
    exit 1
fi

# Add the API directory to Python path
export PYTHONPATH="$API_DIR:$PYTHONPATH"

# Run the load_module.py script with the module name
python3 "$SCRIPT_DIR/load_module.py" "PositionToRgbModule"