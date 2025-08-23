#!/usr/bin/env python3
"""
IMU Simulator (WebSocket Server) for JuggleHub

This script simulates the behavior of a smartwatch by running a WebSocket server
that broadcasts mock IMU data. It's designed to be used for testing the
IMUListener component in the main JuggleHub application.
"""

import asyncio
import websockets
import json
import time
import random
import argparse
import threading
from typing import Set

# A set to keep track of all connected clients
CONNECTED_CLIENTS: Set[websockets.WebSocketServerProtocol] = set()

async def broadcast_imu_data(watch_id: str, rate_hz: int):
    """Periodically generates and broadcasts IMU data to all connected clients."""
    print(f"🚀 Starting data broadcast for '{watch_id}' at {rate_hz} Hz...")
    while True:
        try:
            timestamp_ns = time.time_ns()
            
            # Create separate messages for accel and gyro, like the real watch
            accel_data = {
                "watch_id": watch_id,
                "type": "accel",
                "timestamp_ns": timestamp_ns,
                "x": 9.8 + random.uniform(-0.5, 0.5),
                "y": random.uniform(-0.5, 0.5),
                "z": random.uniform(-0.5, 0.5)
            }
            
            gyro_data = {
                "watch_id": watch_id,
                "type": "gyro",
                "timestamp_ns": timestamp_ns,
                "x": random.uniform(-1.0, 1.0),
                "y": random.uniform(-1.0, 1.0),
                "z": random.uniform(-1.0, 1.0)
            }
            
            # Broadcast to all connected clients
            if CONNECTED_CLIENTS:
                await asyncio.wait([client.send(json.dumps(accel_data)) for client in CONNECTED_CLIENTS])
                await asyncio.wait([client.send(json.dumps(gyro_data)) for client in CONNECTED_CLIENTS])
            
            await asyncio.sleep(1.0 / rate_hz)
            
        except websockets.exceptions.ConnectionClosed:
            # A client disconnected, which is handled by the handler
            pass
        except Exception as e:
            print(f"❌ Error in broadcast loop: {e}")
            await asyncio.sleep(1) # Avoid spamming errors

async def connection_handler(websocket: websockets.WebSocketServerProtocol, path: str):
    """Handles incoming WebSocket connections."""
    print(f"✅ Client connected from {websocket.remote_address}")
    CONNECTED_CLIENTS.add(websocket)
    try:
        # Keep the connection open and listen for any messages from the client
        # (though we don't expect any for this simulator)
        async for message in websocket:
            print(f"Received message from client (unexpected): {message}")
    except websockets.exceptions.ConnectionClosed:
        print(f"Client disconnected from {websocket.remote_address}")
    finally:
        CONNECTED_CLIENTS.remove(websocket)

async def start_server(host: str, port: int, watch_id: str, rate_hz: int):
    """Starts the WebSocket server and the data broadcast task."""
    print(f"🔗 Starting WebSocket IMU simulator server for '{watch_id}'...")
    print(f"   Listening on ws://{host}:{port}/imu")
    
    server = await websockets.serve(connection_handler, host, port)
    
    # Run the data broadcast task concurrently
    broadcast_task = asyncio.create_task(broadcast_imu_data(watch_id, rate_hz))
    
    await server.wait_closed()
    broadcast_task.cancel()
    
def main():
    """Main entry point."""
    parser = argparse.ArgumentParser(description="IMU Simulator (WebSocket Server) for JuggleHub")
    parser.add_argument('--host', type=str, default='0.0.0.0',
                        help="The host to bind the server to (default: 0.0.0.0)")
    parser.add_argument('--port', type=int, default=8081,
                        help="The WebSocket port to listen on (default: 8081)")
    parser.add_argument('--watch-id', type=str, default='sim_watch_left',
                        help="The identifier for the simulated watch (e.g., left_watch)")
    parser.add_argument('--rate', type=int, default=100,
                        help="The rate to send data in Hz (default: 100)")
    
    args = parser.parse_args()
    
    try:
        asyncio.run(start_server(
            host=args.host,
            port=args.port,
            watch_id=args.watch_id,
            rate_hz=args.rate
        ))
    except KeyboardInterrupt:
        print("\n🛑 Server stopped by user.")

if __name__ == '__main__':
    main()