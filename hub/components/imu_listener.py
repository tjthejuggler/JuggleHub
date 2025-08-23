import asyncio
import websockets
import json
import threading
import time
from typing import Dict, Optional, List

import juggler_pb2

class IMUListener:
    """
    Connects to and streams IMU data from smartwatches over WebSockets.
    Based on the high-performance implementation from the JugVid2 project.
    """
    def __init__(self, watch_ips: List[str], port: int = 8081):
        self.watch_ips = watch_ips
        self.port = port
        self.running = False
        self._data_lock = threading.Lock()
        self._latest_data: Dict[str, juggler_pb2.IMUData] = {}
        # This state will hold the latest sensor readings for each watch.
        self._watch_states: Dict[str, dict] = {}
        self._thread: Optional[threading.Thread] = None

    def start(self):
        """Starts the listener thread."""
        if self.running:
            print("IMUListener is already running.")
            return

        print(f"🚀 Starting high-performance IMU listener for IPs: {self.watch_ips}...")
        self.running = True
        self._thread = threading.Thread(target=self._run_event_loop, daemon=True)
        self._thread.start()
        print("✅ IMU listener started successfully.")

    def stop(self):
        """Stops the listener thread gracefully."""
        if not self.running:
            return

        print("🧹 Stopping IMU listener...")
        self.running = False
        if self._thread and self._thread.is_alive():
            self._thread.join(timeout=2.0)
        print("✅ IMU listener stopped.")

    def _run_event_loop(self):
        """Runs the asyncio event loop in a dedicated thread."""
        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)
        try:
            tasks = [self._stream_from_watch(ip) for ip in self.watch_ips]
            loop.run_until_complete(asyncio.gather(*tasks))
        except Exception as e:
            print(f"❌ Error in IMU listener event loop: {e}")
        finally:
            loop.close()

    async def _stream_from_watch(self, ip: str):
        """Manages the WebSocket connection and data stream for a single watch."""
        uri = f"ws://{ip}:{self.port}/imu"
        while self.running:
            try:
                async with websockets.connect(uri, ping_interval=None, ping_timeout=None) as websocket:
                    print(f"✅ Connected to watch at {uri}")
                    await websocket.send(json.dumps({"command": "start"}))
                    print(f"▶️ Sent 'start' command to {ip}")
                    
                    while self.running:
                        message = await websocket.recv()
                        self._process_message(message, ip)
                
                # This part is now outside the 'with' block, so it's reached on graceful exit
                if 'websocket' in locals() and websocket.open:
                     await websocket.send(json.dumps({"command": "stop"}))
                     print(f"⏹️ Sent 'stop' command to {ip}")

            except (websockets.exceptions.ConnectionClosed, ConnectionRefusedError, OSError) as e:
                if self.running:
                    print(f"⚠️  Connection to {ip} lost, retrying in 2s... ({e})")
                    await asyncio.sleep(2)
            except Exception as e:
                 if self.running:
                    print(f"❌ Unexpected error with watch {ip}: {e}")
                    await asyncio.sleep(2)

    def _process_message(self, message: str, ip: str):
        """
        Robustly parses a JSON message and updates the IMU data.
        This new logic maintains the latest state for each sensor and combines them.
        """
        try:
            raw_data = json.loads(message)
            watch_name = raw_data.get('watch_id', ip)
            data_type = raw_data.get('type')

            if not all([watch_name, data_type]):
                return

            # Get or create the state for this watch
            state = self._watch_states.get(watch_name, {})
            state['watch_ip'] = ip

            # Update the state with the new data
            if data_type == 'accel':
                state['accel'] = (raw_data['x'], raw_data['y'], raw_data['z'])
                state['timestamp_us'] = raw_data['timestamp_ns'] // 1000
            elif data_type == 'gyro':
                state['gyro'] = (raw_data['x'], raw_data['y'], raw_data['z'])
                state['timestamp_us'] = raw_data['timestamp_ns'] // 1000
            
            self._watch_states[watch_name] = state

            # If we have a complete record (at least one of each sensor type), update the latest data
            if 'accel' in state and 'gyro' in state:
                imu_data = juggler_pb2.IMUData()
                imu_data.timestamp_us = state['timestamp_us']
                imu_data.watch_name = watch_name
                imu_data.watch_ip = state['watch_ip']
                imu_data.acceleration.x, imu_data.acceleration.y, imu_data.acceleration.z = state['accel']
                imu_data.gyroscope.x, imu_data.gyroscope.y, imu_data.gyroscope.z = state['gyro']
                
                with self._data_lock:
                    self._latest_data[watch_name] = imu_data

        except (json.JSONDecodeError, KeyError):
            # Ignore malformed or unexpected JSON
            pass
        except Exception as e:
            print(f"❌ Error processing IMU message: {e}")

    def get_latest_data(self) -> Dict[str, juggler_pb2.IMUData]:
        """
        Returns a copy of the latest IMU data from all connected watches.
        """
        with self._data_lock:
            return self._latest_data.copy()
