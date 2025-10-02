"""
App API

High-level API for apps to interact with the JuggleHub engine.
"""

import zmq
import juggler_pb2
from typing import Callable, Optional
import threading
import time


class AppAPI:
    """High-level API for apps to interact with the engine."""
    
    def __init__(self, zmq_context: zmq.Context):
        """
        Initialize the AppAPI.
        
        Args:
            zmq_context: ZMQ context for creating sockets
        """
        self.context = zmq_context
        
        # Subscriber for receiving frame data
        self.subscriber = self.context.socket(zmq.SUB)
        self.subscriber.connect("tcp://localhost:5555")
        self.subscriber.setsockopt(zmq.SUBSCRIBE, b"")
        self.subscriber.setsockopt(zmq.RCVTIMEO, 100)  # 100ms timeout
        
        # Commander for sending commands
        self.commander = self.context.socket(zmq.REQ)
        self.commander.connect("tcp://localhost:5565")  # Correct command port
        self.commander.setsockopt(zmq.RCVTIMEO, 5000)  # 5 second timeout
        
        self._data_callback: Optional[Callable] = None
        self._receiver_thread: Optional[threading.Thread] = None
        self._running = False
    
    def subscribe_to_data(self, callback: Callable):
        """
        Subscribe to frame data from engine.
        
        Args:
            callback: Function to call with each FrameData message
        """
        self._data_callback = callback
        self._running = True
        
        # Start background thread to receive data
        self._receiver_thread = threading.Thread(
            target=self._receive_loop,
            daemon=True,
            name="AppAPI-Receiver"
        )
        self._receiver_thread.start()
        print("📡 Subscribed to engine data stream")
    
    def unsubscribe_from_data(self):
        """Unsubscribe from frame data."""
        self._running = False
        self._data_callback = None
        
        if self._receiver_thread:
            self._receiver_thread.join(timeout=1.0)
            self._receiver_thread = None
        
        print("📡 Unsubscribed from engine data stream")
    
    def _receive_loop(self):
        """Background thread to receive frame data."""
        print("🔄 AppAPI receiver thread started")
        
        while self._running and self._data_callback:
            try:
                # Try to receive a message (with timeout)
                message = self.subscriber.recv()
                
                # Parse the message
                frame_data = juggler_pb2.FrameData()
                frame_data.ParseFromString(message)
                
                # Call the callback
                if self._data_callback:
                    self._data_callback(frame_data)
                    
            except zmq.Again:
                # Timeout - no message received, continue
                continue
            except Exception as e:
                if self._running:  # Only log if we're still supposed to be running
                    print(f"⚠️ Error in AppAPI receiver: {e}")
                time.sleep(0.1)  # Brief pause before retrying
        
        print("🔄 AppAPI receiver thread stopped")
    
    def enable_feature(self, feature_name: str) -> bool:
        """
        Enable an engine feature.
        
        Args:
            feature_name: Name of the feature to enable
            
        Returns:
            bool: True if successful, False otherwise
        """
        command = juggler_pb2.CommandRequest()
        command.type = juggler_pb2.CommandRequest.CommandType.ENABLE_FEATURE
        command.feature_name = feature_name
        
        try:
            self.commander.send(command.SerializeToString())
            response_bytes = self.commander.recv()
            response = juggler_pb2.CommandResponse()
            response.ParseFromString(response_bytes)
            return response.success
        except Exception as e:
            print(f"❌ Error enabling feature '{feature_name}': {e}")
            return False
    
    def disable_feature(self, feature_name: str) -> bool:
        """
        Disable an engine feature.
        
        Args:
            feature_name: Name of the feature to disable
            
        Returns:
            bool: True if successful, False otherwise
        """
        command = juggler_pb2.CommandRequest()
        command.type = juggler_pb2.CommandRequest.CommandType.DISABLE_FEATURE
        command.feature_name = feature_name
        
        try:
            self.commander.send(command.SerializeToString())
            response_bytes = self.commander.recv()
            response = juggler_pb2.CommandResponse()
            response.ParseFromString(response_bytes)
            return response.success
        except Exception as e:
            print(f"❌ Error disabling feature '{feature_name}': {e}")
            return False
    
    def send_command(self, command: juggler_pb2.CommandRequest) -> juggler_pb2.CommandResponse:
        """
        Send a custom command to the engine.
        
        Args:
            command: CommandRequest message to send
            
        Returns:
            CommandResponse: Response from the engine
        """
        try:
            self.commander.send(command.SerializeToString())
            response_bytes = self.commander.recv()
            response = juggler_pb2.CommandResponse()
            response.ParseFromString(response_bytes)
            return response
        except Exception as e:
            print(f"❌ Error sending command: {e}")
            # Return a failure response
            response = juggler_pb2.CommandResponse()
            response.success = False
            response.message = str(e)
            return response
    
    def cleanup(self):
        """Clean up resources."""
        self.unsubscribe_from_data()
        
        # Close sockets
        try:
            self.subscriber.close()
            self.commander.close()
        except:
            pass