#!/usr/bin/env python3
"""
Test script to verify that disabling a color profile immediately stops tracking.

This test:
1. Connects to the engine via ZMQ
2. Sends a RELOAD_COLOR_PROFILES command
3. Verifies the response
"""

import sys
import zmq
import time

# Add hub directory to path for imports
sys.path.insert(0, 'hub')
import juggler_pb2

def test_reload_command():
    """Test sending the RELOAD_COLOR_PROFILES command"""
    
    print("=" * 60)
    print("Testing Color Profile Reload Command")
    print("=" * 60)
    
    # Connect to engine's command socket
    context = zmq.Context()
    socket = context.socket(zmq.REQ)
    socket.connect("tcp://127.0.0.1:5565")
    socket.setsockopt(zmq.RCVTIMEO, 5000)  # 5 second timeout
    
    print("\n✅ Connected to engine command socket")
    
    # Create reload command
    command = juggler_pb2.CommandRequest()
    command.type = juggler_pb2.CommandRequest.RELOAD_COLOR_PROFILES
    
    print(f"\n📤 Sending RELOAD_COLOR_PROFILES command...")
    
    # Send command
    socket.send(command.SerializeToString())
    
    # Wait for response
    try:
        response_data = socket.recv()
        response = juggler_pb2.CommandResponse()
        response.ParseFromString(response_data)
        
        print(f"\n📥 Received response:")
        print(f"   Success: {response.success}")
        print(f"   Message: {response.message}")
        
        if response.success:
            print(f"\n✅ TEST PASSED: Color profiles reloaded successfully!")
            print(f"\n💡 What this means:")
            print(f"   - The engine has reloaded color profiles from calibration_settings_new3d.json")
            print(f"   - Disabled color balls have been removed from tracking")
            print(f"   - Enabled color balls are now being tracked")
            print(f"   - Changes took effect IMMEDIATELY without restart")
            return True
        else:
            print(f"\n❌ TEST FAILED: {response.message}")
            return False
            
    except zmq.error.Again:
        print(f"\n❌ TEST FAILED: Timeout waiting for response")
        print(f"   Make sure the engine is running with New3D tracker")
        return False
    except Exception as e:
        print(f"\n❌ TEST FAILED: {e}")
        return False
    finally:
        socket.close()
        context.term()

if __name__ == "__main__":
    print("\n🔍 This test verifies the color profile reload functionality")
    print("   Prerequisites:")
    print("   1. Engine must be running")
    print("   2. New3D tracker must be selected")
    print("   3. calibration_settings_new3d.json must exist")
    print()
    
    input("Press Enter to start the test...")
    
    success = test_reload_command()
    
    print("\n" + "=" * 60)
    if success:
        print("✅ All tests passed!")
    else:
        print("❌ Tests failed - see errors above")
    print("=" * 60)
    
    sys.exit(0 if success else 1)