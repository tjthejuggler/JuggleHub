#!/bin/bash

# Setup NPU Environment for JuggleHub Engine
# This script configures the environment to enable NPU support in OpenVINO

echo "Setting up NPU environment for JuggleHub..."

# Add Intel NPU driver libraries to LD_LIBRARY_PATH
export LD_LIBRARY_PATH="/snap/intel-npu-driver/10/usr/lib/x86_64-linux-gnu:$LD_LIBRARY_PATH"

# Source OpenVINO environment
source /opt/intel/openvino_2025.2.0/setupvars.sh

echo "NPU environment setup complete!"
echo "Available devices should now include NPU"

# Verify NPU is available
python3 -c "
import openvino as ov
core = ov.Core()
devices = core.get_available_devices()
print('Available devices:', devices)
if 'NPU' in devices:
    print('✅ NPU is available and ready to use!')
else:
    print('❌ NPU not detected. Check your setup.')
"