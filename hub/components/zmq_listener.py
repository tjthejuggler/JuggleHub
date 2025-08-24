"""
ZMQ Listener Component

Handles receiving Protocol Buffer messages from the C++ engine via ZeroMQ.
"""

import zmq
import threading
import time
from typing import List, Callable, Optional
import sys
import os

# Add the api directory to the path for protobuf imports
api_path = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(__file__))), 'api', 'v1')
sys.path.insert(0, api_path)

try:
    import juggler_pb2
except ImportError:
    print("❌ Error: Protocol Buffer files not found. Please run 'make generate-proto' first.")
    sys.exit(1)


class ZMQListener:
    """ZeroMQ listener for receiving frame data from the C++ engine."""
    
    def __init__(self, endpoint: str = "tcp://localhost:5555"):
        self.endpoint = endpoint
        self.context: Optional[zmq.Context] = None
        self.socket: Optional[zmq.Socket] = None
        self.running = False
        self.thread: Optional[threading.Thread] = None
        
        # Callbacks for frame data
        self.frame_callbacks: List[Callable[[juggler_pb2.FrameData], None]] = []
        
        # Statistics
        self.frames_received = 0
        self.bytes_received = 0
        self.last_frame_time = 0
        self.fps = 0.0
        
        # Error handling
        self.consecutive_errors = 0
        self.max_consecutive_errors = 10
    
    def add_frame_callback(self, callback: Callable[[juggler_pb2.FrameData], None]):
        """Add a callback function to be called when frame data is received."""
        self.frame_callbacks.append(callback)
    
    def remove_frame_callback(self, callback: Callable[[juggler_pb2.FrameData], None]):
        """Remove a callback function."""
        if callback in self.frame_callbacks:
            self.frame_callbacks.remove(callback)
    
    def initialize(self) -> bool:
        """Initialize ZeroMQ connection."""
        try:
            print(f"🔌 Connecting to engine at {self.endpoint}")
            
            # Create ZMQ context and socket
            self.context = zmq.Context()
            self.socket = self.context.socket(zmq.SUB)
            
            # Subscribe to all messages (empty filter)
            self.socket.setsockopt(zmq.SUBSCRIBE, b"")
            
            # Set socket options for better performance
            self.socket.setsockopt(zmq.RCVHWM, 10)  # High water mark
            self.socket.setsockopt(zmq.RCVTIMEO, 1000)  # 1 second timeout
            
            # Connect to the engine
            self.socket.connect(self.endpoint)
            
            # Start the listening thread
            self.running = True
            self.thread = threading.Thread(target=self._listen_loop, daemon=True)
            self.thread.start()
            
            print("✅ ZMQ listener initialized successfully")
            return True
            
        except Exception as e:
            print(f"❌ Error initializing ZMQ listener: {e}")
            self.cleanup()
            return False
    
    def _listen_loop(self):
        """Main listening loop running in a separate thread."""
        print("🎧 ZMQ listener thread started")
        
        frame_times = []
        fps_update_interval = 30  # Update FPS every 30 frames
        
        while self.running:
            try:
                # Receive message with timeout
                message = self.socket.recv(zmq.NOBLOCK)
                
                if message:
                    # Update statistics
                    current_time = time.time()
                    self.frames_received += 1
                    self.bytes_received += len(message)
                    
                    # Calculate FPS
                    frame_times.append(current_time)
                    if len(frame_times) > fps_update_interval:
                        frame_times.pop(0)
                        if len(frame_times) > 1:
                            time_span = frame_times[-1] - frame_times[0]
                            self.fps = (len(frame_times) - 1) / time_span if time_span > 0 else 0
                    
                    self.last_frame_time = current_time
                    
                    # Parse Protocol Buffer message
                    try:
                        frame_data = juggler_pb2.FrameData()
                        frame_data.ParseFromString(message)
                        
                        # Call all registered callbacks
                        for callback in self.frame_callbacks:
                            try:
                                callback(frame_data)
                            except Exception as e:
                                print(f"⚠️ Error in frame callback: {e}")
                        
                        # Reset error counter on successful processing
                        self.consecutive_errors = 0
                        
                    except Exception as e:
                        print(f"⚠️ Error parsing Protocol Buffer message: {e}")
                        self.consecutive_errors += 1
                
            except zmq.Again:
                # Timeout - this is normal, just continue
                continue
            except zmq.ZMQError as e:
                if e.errno == zmq.ETERM:
                    # Context was terminated
                    break
                else:
                    print(f"⚠️ ZMQ error: {e}")
                    self.consecutive_errors += 1
            except Exception as e:
                print(f"⚠️ Unexpected error in listen loop: {e}")
                self.consecutive_errors += 1
            
            # Check for too many consecutive errors
            if self.consecutive_errors >= self.max_consecutive_errors:
                print(f"❌ Too many consecutive errors ({self.consecutive_errors}). Stopping listener.")
                break
            
            # Brief pause on error to prevent tight error loops
            if self.consecutive_errors > 0:
                time.sleep(0.1)
        
        print("🛑 ZMQ listener thread stopped")
    
    def get_statistics(self) -> dict:
        """Get current statistics."""
        current_time = time.time()
        time_since_last_frame = current_time - self.last_frame_time if self.last_frame_time > 0 else 0
        
        return {
            'frames_received': self.frames_received,
            'bytes_received': self.bytes_received,
            'fps': self.fps,
            'last_frame_time': self.last_frame_time,
            'time_since_last_frame': time_since_last_frame,
            'consecutive_errors': self.consecutive_errors,
            'is_running': self.running,
            'endpoint': self.endpoint
        }
    
    def is_receiving_data(self, timeout_seconds: float = 5.0) -> bool:
        """Check if we're actively receiving data from the engine."""
        if self.last_frame_time == 0:
            return False
        
        current_time = time.time()
        time_since_last_frame = current_time - self.last_frame_time
        return time_since_last_frame < timeout_seconds
    
    def cleanup(self):
        """Clean up ZMQ resources."""
        print("🧹 Cleaning up ZMQ listener...")
        
        self.running = False
        
        # Wait for thread to finish
        if self.thread and self.thread.is_alive():
            self.thread.join(timeout=2.0)
            if self.thread.is_alive():
                print("⚠️ ZMQ listener thread did not stop gracefully")
        
        # Close socket and context
        if self.socket:
            self.socket.close()
            self.socket = None
        
        if self.context:
            self.context.term()
            self.context = None
        
        print("✅ ZMQ listener cleanup completed")
    
    def __del__(self):
        """Destructor to ensure cleanup."""
        self.cleanup()


# Example usage and testing
if __name__ == "__main__":
    def test_callback(frame_data):
        """Test callback function."""
        print(f"📦 Received frame {frame_data.frame_number} with {len(frame_data.balls)} balls")
        for ball in frame_data.balls:
            print(f"  🏀 Track ID {ball.track_id}: ({ball.position_3d.x:.3f}, {ball.position_3d.y:.3f}, {ball.position_3d.z:.3f})")
    
    # Create and test listener
    listener = ZMQListener()
    listener.add_frame_callback(test_callback)
    
    if listener.initialize():
        try:
            print("🎯 Test listener running. Press Ctrl+C to stop.")
            while True:
                time.sleep(1)
                stats = listener.get_statistics()
                print(f"📊 Stats: {stats['frames_received']} frames, {stats['fps']:.1f} FPS, "
                      f"{stats['bytes_received']} bytes, {stats['consecutive_errors']} errors")
        except KeyboardInterrupt:
            print("\n🛑 Test stopped by user")
        finally:
            listener.cleanup()
    else:
        print("❌ Failed to initialize test listener")