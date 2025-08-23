#!/usr/bin/env python3
"""
JuggleHub - Main Hub Application

This is the main entry point for the JuggleHub Python application that receives
data from the C++ engine via Protocol Buffers and ZeroMQ.
"""

import os
import sys
import time
import argparse
import signal
import threading
from typing import Optional, List

# Add the current directory to Python path for imports
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from components.zmq_client import ZMQClient
from components.ui import JuggleHubUI
from components.database_logger import DatabaseLogger
from components.imu_listener import IMUListener
import juggler_pb2

class JuggleHub:
    """Main JuggleHub application class."""
    
    def __init__(self, config: dict):
        self.config = config
        self.running = False
        
        # Initialize components
        self.zmq_client: Optional[ZMQClient] = None
        self.ui: Optional[JuggleHubUI] = None
        self.database_logger: Optional[DatabaseLogger] = None
        self.imu_listener: Optional[IMUListener] = None
        
        self._data_thread: Optional[threading.Thread] = None
        
        # Set up signal handlers for graceful shutdown
        signal.signal(signal.SIGINT, self._signal_handler)
        signal.signal(signal.SIGTERM, self._signal_handler)
    
    def initialize(self) -> bool:
        """Initialize all components."""
        try:
            print("🚀 Initializing JuggleHub...")
            
            # Initialize ZMQ client
            self.zmq_client = ZMQClient()

            # Initialize UI
            if self.config['enable_ui']:
                self.ui = JuggleHubUI(self.config)

            # Initialize DatabaseLogger
            if self.config['enable_logging']:
                self.database_logger = DatabaseLogger(self.config['database_path'])
                print(f"📊 Database initialized: {self.config['database_path']}")


            # Initialize IMU listener
            if self.config.get('watch_ips'):
                self.imu_listener = IMUListener(
                    watch_ips=self.config['watch_ips'],
                    port=self.config.get('imu_port', 8081)
                )
                self.imu_listener.start()
            
            print("✅ JuggleHub initialized successfully")
            return True
            
        except Exception as e:
            print(f"❌ Error initializing JuggleHub: {e}")
            return False

    def _data_processing_loop(self):
        """The main loop for processing data from all sources."""
        while self.running:
            try:
                # 1. Receive ball tracking data from the C++ engine
                frame_data = self.zmq_client.receive_frame_data()

                # 2. Get the latest IMU data from the listener
                if self.imu_listener:
                    imu_datas = self.imu_listener.get_latest_data()
                else:
                    imu_datas = {}

                # If no ball data, create an empty FrameData to carry the IMU data
                if not frame_data and imu_datas:
                    frame_data = juggler_pb2.FrameData()
                    frame_data.timestamp_us = int(time.time() * 1_000_000)
                
                # If we have any data, process it
                if frame_data:
                    # Augment with the latest IMU data
                    del frame_data.imu_data[:]
                    frame_data.imu_data.extend(list(imu_datas.values()))
                    
                    # Pass the combined frame_data to other components
                    if self.ui:
                        self.ui.update_frame_data(frame_data)
                    
                    if self.database_logger:
                        self.database_logger.log_frame_data(frame_data)

                # Prevent busy-waiting
                time.sleep(0.001)
            except Exception as e:
                print(f"❌ Error in data processing loop: {e}")
                time.sleep(1) # Avoid spamming errors if in a tight loop

    def run(self):
        """Run the main application."""
        if not self.initialize():
            return
        
        self.running = True
        print("🎯 JuggleHub is running...")
        
        # Start the data processing loop in a background thread
        self._data_thread = threading.Thread(target=self._data_processing_loop, daemon=True)
        self._data_thread.start()

        # Run the UI in the main thread (this will block until the UI is closed)
        if self.ui:
            self.ui.run()
        else:
            # If no UI, just wait for a signal to shut down
            print("Running in headless mode. Press Ctrl+C to stop.")
            try:
                while self.running:
                    time.sleep(1)
            except KeyboardInterrupt:
                print("\n🛑 Headless mode stopped by user")

        # After the UI is closed or headless mode is interrupted, cleanup
        self.cleanup()

    def cleanup(self):
        """Clean up all components."""
        print("🧹 Cleaning up JuggleHub...")
        
        self.running = False
        
        # Stop data-generating components first
        if self.imu_listener:
            self.imu_listener.stop()

        # Join the data processing thread
        if self._data_thread and self._data_thread.is_alive():
            self._data_thread.join(timeout=2.0)
        
        # Cleanup other components
        if self.database_logger:
            self.database_logger.cleanup()
        
        # UI cleanup is handled by its own exit
        if self.ui:
            self.ui.cleanup()
        
        print("✅ JuggleHub cleanup completed")
    
    def _signal_handler(self, signum, frame):
        """Handle system signals for graceful shutdown."""
        print(f"\n🛑 Received signal {signum}")
        self.running = False
        # For PyQt, we need to properly exit the app from a signal
        if self.ui and self.ui.app:
            self.ui.app.quit()


def parse_arguments():
    """Parse command line arguments."""
    parser = argparse.ArgumentParser(description='JuggleHub - Juggling Analysis Hub')
    
    parser.add_argument('--zmq-endpoint', type=str, default='tcp://localhost:5555',
                        help='ZeroMQ endpoint to connect to engine (default: tcp://localhost:5555)')

    parser.add_argument('--watch-ips', nargs='+',
                        help='Space-separated IP addresses of smartwatches for IMU streaming')
    
    parser.add_argument('--imu-port', type=int, default=8081,
                       help='WebSocket port for IMU data on watches (default: 8081)')
    
    parser.add_argument('--no-ui', action='store_true',
                      help='Run in headless mode without UI')
    
    parser.add_argument('--no-logging', action='store_true',
                       help='Disable database logging')
    
    parser.add_argument('--database-path', type=str, default='juggling_data.db',
                       help='Path to SQLite database file (default: juggling_data.db)')
    
    parser.add_argument('--config-dir', type=str,
                       help='Directory for configuration files')
    
    parser.add_argument('--debug', action='store_true',
                       help='Enable debug mode with verbose output')
    
    parser.add_argument('--profile', action='store_true',
                       help='Enable performance profiling')
    
    return parser.parse_args()


def main():
    """Main entry point."""
    args = parse_arguments()
    
    # Create configuration dictionary
    config = {
        'zmq_endpoint': args.zmq_endpoint,
        'watch_ips': args.watch_ips,
        'imu_port': args.imu_port,
        'enable_ui': not args.no_ui,
        'enable_logging': not args.no_logging,
        'database_path': args.database_path,
        'config_dir': args.config_dir or os.path.join(os.path.dirname(__file__), 'config'),
        'debug': args.debug,
        'profile': args.profile
    }
    
    # Ensure config directory exists
    os.makedirs(config['config_dir'], exist_ok=True)
    
    if config['debug']:
        print("🐛 Debug mode enabled")
        print(f"Configuration: {config}")
    
    # Create and run JuggleHub
    hub = JuggleHub(config)
    
    if config['profile']:
        import cProfile
        import pstats
        
        print("📊 Performance profiling enabled")
        profiler = cProfile.Profile()
        profiler.enable()
        
        try:
            hub.run()
        finally:
            profiler.disable()
            stats = pstats.Stats(profiler)
            stats.sort_stats('cumulative')
            stats.print_stats(20)  # Print top 20 functions
    else:
        hub.run()


if __name__ == '__main__':
    main()