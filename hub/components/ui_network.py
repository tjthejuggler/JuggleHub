"""
Network communication components for JuggleHub UI.
"""

import socket
from typing import Any


class UdpClient:
    """UDP client for sending settings to the engine."""
    
    def __init__(self, host="127.0.0.1", port=12346):
        self.host = host
        self.port = port
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    def send_setting(self, key: str, value: Any):
        """Send a setting key-value pair via UDP."""
        message = f"{key}={value}"
        self.sock.sendto(message.encode('utf-8'), (self.host, self.port))
        print(f"Sent UDP setting: {message}")