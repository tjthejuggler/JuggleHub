#!/usr/bin/env python3
import socket
import struct
import time

def send_blue_to_ball(ball_ip):
    """Send blue color command to a specific ball"""
    port = 41412
    
    # Create socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    
    try:
        # First send brightness command (like the C++ code does)
        brightness_header = struct.pack("!bIBH", 66, 0, 0, 0)
        brightness_data = struct.pack("!BB", 0x10, 7)  # brightness command, max brightness
        brightness_packet = brightness_header + brightness_data
        
        print(f"Sending brightness command to {ball_ip}:{port}")
        sock.sendto(brightness_packet, (ball_ip, port))
        
        # Small delay between commands
        time.sleep(0.1)
        
        # Then send blue color command
        color_header = struct.pack("!bIBH", 66, 0, 0, 0)
        color_data = struct.pack("!BBBB", 0x0a, 0, 0, 255)  # Blue: R=0, G=0, B=255
        color_packet = color_header + color_data
        
        print(f"Sending blue color command to {ball_ip}:{port}")
        sock.sendto(color_packet, (ball_ip, port))
        
        print(f"Successfully sent blue color command to ball at {ball_ip}")
        
    except Exception as e:
        print(f"Error sending command to {ball_ip}: {e}")
    finally:
        sock.close()

if __name__ == "__main__":
    # Send blue color to the specific ball
    ball_ip = "10.54.136.205"
    send_blue_to_ball(ball_ip)