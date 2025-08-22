#!/bin/bash
set -x
rm -f hub/juggler_pb2.py
python3 -m grpc_tools.protoc --proto_path=api/v1 --python_out=hub --grpc_python_out=hub api/v1/juggler.proto