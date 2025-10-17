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
import logging
from typing import Optional, List


from components.zmq_client import ZMQClient
from components.ui import JuggleHubUI
from components.database_logger import DatabaseLogger
from components.imu_listener import IMUListener
from components.web_ui import WebUI
from components.screen_controller import ScreenController
import juggler_pb2
from components.juggling_system_manager import JugglingSystemManager
from apps.manager import AppManager
import zmq

class JuggleHub:
    """Main JuggleHub application class."""
    
    def __init__(self, config: dict):
        self.config = config
        self.running = False
        self.restart_requested = False
        
        # Initialize components
        self.zmq_client: Optional[ZMQClient] = None
        self.ui: Optional[JuggleHubUI] = None
        self.database_logger: Optional[DatabaseLogger] = None
        self.imu_listener: Optional[IMUListener] = None
        self.web_ui: Optional[WebUI] = None
        self.screen_controller: Optional[ScreenController] = None
        self.juggling_system_manager: Optional[JugglingSystemManager] = None
        self.app_manager: Optional[AppManager] = None
        self.zmq_context: Optional[zmq.Context] = None
        
        self._data_thread: Optional[threading.Thread] = None
        
        # Set up signal handlers for graceful shutdown
        signal.signal(signal.SIGINT, self._signal_handler)
        signal.signal(signal.SIGTERM, self._signal_handler)
    
    def initialize(self) -> bool:
        """Initialize all components."""
        try:
            # Initialize ZMQ context for apps
            self.zmq_context = zmq.Context()
            
            # Initialize ZMQ client
            self.zmq_client = ZMQClient()
            
            # Initialize App Manager
            self.app_manager = AppManager(self.zmq_context)

            # Initialize UI
            if self.config['enable_ui']:
                self.ui = JuggleHubUI(self.config, self.zmq_client, self)

            # Initialize ScreenController
            self.screen_controller = ScreenController()

            # Initialize WebUI
            self.web_ui = WebUI(self.screen_controller)

            # Initialize DatabaseLogger
            if self.config['enable_logging']:
                self.database_logger = DatabaseLogger(self.config['database_path'])


            # Initialize IMU listener
            if self.config.get('watch_ips'):
                self.imu_listener = IMUListener(
                    watch_ips=self.config['watch_ips'],
                    port=self.config.get('imu_port', 8081)
                )
                self.imu_listener.start()
            
            self.juggling_system_manager = JugglingSystemManager(self.config)
            
            return True
            
        except Exception as e:
            print(f"❌ Error during initialization: {e}")
            import traceback
            traceback.print_exc()
            return False

    def _data_processing_loop(self):
        """The main loop for processing data from all sources."""
        while self.running:
            try:
                # 1. Receive ball tracking data from the C++ engine
                frame_data = self.zmq_client.receive_frame_data()
                if frame_data:
                    image_size = len(frame_data.color_image_b64)
                else:
                    pass

                # 2. Get the latest IMU data from the listener
                if self.imu_listener:
                    imu_datas = self.imu_listener.get_latest_data()
                else:
                    imu_datas = {}

                # If no ball data, create an empty FrameData to carry the IMU data
                if frame_data is None and imu_datas:
                    frame_data = juggler_pb2.FrameData()
                    frame_data.timestamp_us = int(time.time() * 1_000_000)
                
                # If we have any data, process it
                if frame_data:
                    # Augment with the latest IMU data
                    del frame_data.imu_data[:]
                    frame_data.imu_data.extend(list(imu_datas.values()))
                    
                    # Process the frame data through the JugglingSystemManager
                    if self.juggling_system_manager:
                        try:
                            # Get the latest frame image from UI
                            frame_image = None
                            if self.ui:
                                frame_image = self.ui.get_latest_frame()
                            
                            managed_balls = self.juggling_system_manager.process_frame(frame_data, frame_image)
                            # TODO: Use managed_balls to enhance the display
                        except Exception as e:
                            pass
                    
                    # Pass the frame_data (with original balls from engine) to UI
                    if self.ui:
                        self.ui.update_frame_data(frame_data)
                    
                    if self.database_logger:
                        self.database_logger.log_frame_data(frame_data)

                # Prevent busy-waiting
                time.sleep(0.001)
            except Exception as e:
                time.sleep(1) # Avoid spamming errors if in a tight loop
    
    def run(self):
        """Run the main application."""
        if not self.initialize():
            return
        
        self.running = True
        # Start the data processing loop in a background thread
        self._data_thread = threading.Thread(target=self._data_processing_loop, daemon=True)
        self._data_thread.start()

        # Run the UI in the main thread (this will block until the UI is closed)
        if self.ui:
            self.ui.run()
        else:
            # If no UI, just wait for a signal to shut down
            try:
                while self.running:
                    time.sleep(1)
            except KeyboardInterrupt:
                pass

        # After the UI is closed or headless mode is interrupted, cleanup
        self.cleanup()

    def cleanup(self):
        """Clean up all components."""
        self.running = False
        
        # Close all running apps
        if self.app_manager:
            self.app_manager.close_all_apps()
        
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
        
        if self.juggling_system_manager:
            self.juggling_system_manager.shutdown()
        
        # Cleanup ZMQ context
        if self.zmq_context:
            self.zmq_context.term()
        
    
    def _signal_handler(self, signum, frame):
        """Handle system signals for graceful shutdown."""
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
        pass
    
    # Create and run JuggleHub
    hub = JuggleHub(config)
    
    if config['profile']:
        import cProfile
        import pstats
        
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

        # Exit with a special code if a restart was requested
        if hub.restart_requested:
            sys.exit(10)


if __name__ == '__main__':
    main()