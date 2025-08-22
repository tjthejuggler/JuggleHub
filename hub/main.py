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
from typing import Optional

# Add the current directory to Python path for imports
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from components.zmq_listener import ZMQListener
from components.ui import JuggleHubUI
from components.database_logger import DatabaseLogger

class JuggleHub:
    """Main JuggleHub application class."""
    
    def __init__(self, config: dict):
        self.config = config
        self.running = False
        
        # Initialize components
        self.zmq_listener: Optional[ZMQListener] = None
        self.ui: Optional[JuggleHubUI] = None
        self.database_logger: Optional[DatabaseLogger] = None
        
        # Set up signal handlers for graceful shutdown
        signal.signal(signal.SIGINT, self._signal_handler)
        signal.signal(signal.SIGTERM, self._signal_handler)
    
    def initialize(self) -> bool:
        """Initialize all components."""
        try:
            print("🚀 Initializing JuggleHub...")
            
            # Initialize ZMQ listener
            zmq_endpoint = self.config.get('zmq_endpoint', 'tcp://localhost:5555')
            self.zmq_listener = ZMQListener(zmq_endpoint)
            
            # Initialize database logger if enabled
            if self.config.get('enable_logging', True):
                db_path = self.config.get('database_path', 'juggling_data.db')
                self.database_logger = DatabaseLogger(db_path)
                
                # Connect logger to ZMQ listener
                self.zmq_listener.add_frame_callback(self.database_logger.log_frame_data)
            
            # Initialize UI if enabled
            if self.config.get('enable_ui', True):
                self.ui = JuggleHubUI(self.config)
                
                # Connect UI to ZMQ listener
                self.zmq_listener.add_frame_callback(self.ui.update_frame_data)
            
            # Initialize ZMQ listener (this starts the background thread)
            if not self.zmq_listener.initialize():
                print("❌ Failed to initialize ZMQ listener")
                return False
            
            print("✅ JuggleHub initialized successfully")
            return True
            
        except Exception as e:
            print(f"❌ Error initializing JuggleHub: {e}")
            return False
    
    def run(self):
        """Run the main application loop."""
        if not self.initialize():
            return
        
        self.running = True
        print("🎯 JuggleHub is running...")
        
        try:
            if self.ui:
                # Run with UI (blocking)
                self.ui.run()
            else:
                # Run headless
                print("Running in headless mode. Press Ctrl+C to stop.")
                while self.running:
                    time.sleep(0.1)
                    
        except KeyboardInterrupt:
            print("\n🛑 Received interrupt signal")
        except Exception as e:
            print(f"❌ Error in main loop: {e}")
        finally:
            self.cleanup()
    
    def cleanup(self):
        """Clean up all components."""
        print("🧹 Cleaning up JuggleHub...")
        
        self.running = False
        
        if self.ui:
            self.ui.cleanup()
        
        if self.database_logger:
            self.database_logger.cleanup()
        
        if self.zmq_listener:
            self.zmq_listener.cleanup()
        
        print("✅ JuggleHub cleanup completed")
    
    def _signal_handler(self, signum, frame):
        """Handle system signals for graceful shutdown."""
        print(f"\n🛑 Received signal {signum}")
        self.running = False


def parse_arguments():
    """Parse command line arguments."""
    parser = argparse.ArgumentParser(description='JuggleHub - Juggling Analysis Hub')
    
    parser.add_argument('--zmq-endpoint', type=str, default='tcp://localhost:5555',
                       help='ZeroMQ endpoint to connect to engine (default: tcp://localhost:5555)')
    
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