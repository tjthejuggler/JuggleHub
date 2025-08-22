#!/usr/bin/env python3
"""
Direct UDP test script to send color commands to the ball
This bypasses the entire JuggleHub system to test basic connectivity
"""

import socket
import time
import sys

def send_udp_packet(ip, port, packet_data):
    """Send a UDP packet to the specified IP and port"""
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.settimeout(2.0)  # 2 second timeout
        
        print(f"Sending UDP packet to {ip}:{port}")
        print(f"Packet data: {[hex(b) for b in packet_data]}")
        print(f"Packet bytes: {packet_data}")
        
        sock.sendto(packet_data, (ip, port))
        print("✅ Packet sent successfully")
        
        sock.close()
        return True
        
    except socket.timeout:
        print("❌ Socket timeout - no response from ball")
        return False
    except socket.gaierror as e:
        print(f"❌ DNS/Address error: {e}")
        return False
    except Exception as e:
        print(f"❌ Error sending packet: {e}")
        return False

def test_ball_color(ip, port=41412):
    """Test different color packet formats based on the C++ code"""
    
    print(f"🎯 Testing ball at {ip}:{port}")
    print("=" * 50)
    
    # Format from UdpBallColorModule.cpp:
    # Header: [66, 0, 0, 0, 0, 0]
    # Color data: [0x0a, R, G, B]
    
    test_cases = [
        {
            "name": "Red (255, 0, 0) - Current C++ format",
            "packet": bytes([66, 0, 0, 0, 0, 0, 0x0a, 255, 0, 0])
        },
        {
            "name": "Green (0, 255, 0) - Current C++ format", 
            "packet": bytes([66, 0, 0, 0, 0, 0, 0x0a, 0, 255, 0])
        },
        {
            "name": "Blue (0, 0, 255) - Current C++ format",
            "packet": bytes([66, 0, 0, 0, 0, 0, 0x0a, 0, 0, 255])
        },
        {
            "name": "Red - Alternative format (no header)",
            "packet": bytes([0x0a, 255, 0, 0])
        },
        {
            "name": "Red - Simple RGB format",
            "packet": bytes([255, 0, 0])
        },
        {
            "name": "Red - With different header",
            "packet": bytes([0x42, 0x00, 255, 0, 0])
        }
    ]
    
    for i, test in enumerate(test_cases, 1):
        print(f"\n{i}. Testing: {test['name']}")
        success = send_udp_packet(ip, port, test['packet'])
        
        if success:
            print("   Waiting 2 seconds to observe ball color change...")
            time.sleep(2)
            
            response = input("   Did the ball change color? (y/n/q to quit): ").lower().strip()
            if response == 'y':
                print(f"🎉 SUCCESS! Working packet format: {test['name']}")
                print(f"   Packet: {[hex(b) for b in test['packet']]}")
                return test['packet']
            elif response == 'q':
                print("   Test stopped by user")
                return None
        
        print("   Moving to next test...")
    
    print("\n❌ None of the packet formats worked")
    return None

def main():
    if len(sys.argv) != 2:
        print("Usage: python3 test_ball_udp.py <ball_ip>")
        print("Example: python3 test_ball_udp.py 10.54.136.205")
        sys.exit(1)
    
    ball_ip = sys.argv[1]
    
    # First test connectivity
    print(f"🔍 Testing connectivity to {ball_ip}...")
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.settimeout(1.0)
        # Send a simple ping-like packet
        sock.sendto(b"ping", (ball_ip, 41412))
        print("✅ Basic UDP connectivity works")
        sock.close()
    except Exception as e:
        print(f"⚠️  Basic connectivity test failed: {e}")
        print("   Continuing with color tests anyway...")
    
    # Test color commands
    working_packet = test_ball_color(ball_ip)
    
    if working_packet:
        print(f"\n🎉 Found working packet format!")
        print(f"Packet bytes: {list(working_packet)}")
        print(f"Packet hex: {[hex(b) for b in working_packet]}")
    else:
        print(f"\n❌ No working packet format found")
        print("Possible issues:")
        print("- Ball is not powered on")
        print("- Ball is not connected to network")
        print("- Ball uses different port (not 41412)")
        print("- Ball uses different packet format")
        print("- Network firewall blocking UDP")

if __name__ == "__main__":
    main()