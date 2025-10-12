#!/bin/bash
set -x
source venv/bin/activate # Activate the virtual environment
rm -f hub/juggler_pb2.py
python3 -m grpc_tools.protoc --proto_path=api/v1 --python_out=hub --grpc_python_out=hub api/v1/juggler.proto
protoc --proto_path=api/v1 --cpp_out=engine/include api/v1/juggler.proto