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

from components.zmq_client import ZMQClient
from components.ui import JuggleHubUI
from components.database_logger import DatabaseLogger
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
        
        # Set up signal handlers for graceful shutdown
        signal.signal(signal.SIGINT, self._signal_handler)
        signal.signal(signal.SIGTERM, self._signal_handler)
    
    def initialize(self) -> bool:
        """Initialize all components."""
        try:
            print("🚀 Initializing JuggleHub...")
            
            # Initialize ZMQ client
            self.zmq_client = ZMQClient()

            # The rest of the initialization logic will be simplified for now.
            # We'll add back the UI and database logger later.
            
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
            print("Running in interactive mode. Type 'load <module_name>', 'unload', 'color', or 'quit'.")
            while self.running:
                command_line = input("> ")
                parts = command_line.split()
                if not parts:
                    continue

                command = parts[0]
                args = parts[1:]

                if command == "load":
                    if len(args) == 1:
                        module_name = args[0]
                        load_request = juggler_pb2.CommandRequest()
                        load_request.type = juggler_pb2.CommandRequest.LOAD_MODULE
                        load_request.module_name = module_name
                        load_response = self.zmq_client.send_command(load_request)
                        print(f"Response: {load_response.message}")

                        if load_response.success and module_name == "PositionToRgbModule":
                            ball_id = input("Enter target LED ball ID (e.g., 205): ")
                            if ball_id:
                                config_request = juggler_pb2.CommandRequest()
                                config_request.type = juggler_pb2.CommandRequest.CONFIGURE_MODULE
                                config_request.module_name = module_name
                                config_request.module_args["target_ball_id"] = ball_id
                                config_response = self.zmq_client.send_command(config_request)
                                print(f"Configuration Response: {config_response.message}")
                            else:
                                print("No ball ID provided, using default (201).")
                    else:
                        print("Invalid load command. Usage: load <module_name>")
                elif command == "unload":
                    unload_request = juggler_pb2.CommandRequest()
                    unload_request.type = juggler_pb2.CommandRequest.UNLOAD_MODULE
                    # We don't need module_name for unload, as the engine unloads the active module.
                    unload_response = self.zmq_client.send_command(unload_request)
                    print(f"Response: {unload_response.message}")
                elif command.startswith("color"):
                    parts = command.split()
                    if len(parts) == 5:
                        ball_id = parts[1]
                        r = int(parts[2])
                        g = int(parts[3])
                        b = int(parts[4])
                        
                        color_command = juggler_pb2.ColorCommand()
                        color_command.ball_id = ball_id
                        color_command.color.r = r
                        color_command.color.g = g
                        color_command.color.b = b
                        
                        request = juggler_pb2.CommandRequest()
                        request.type = juggler_pb2.CommandRequest.SEND_COLOR_COMMAND
                        request.color_command.CopyFrom(color_command)
                        
                        response = self.zmq_client.send_command(request)
                        print(f"Response: {response.message}")
                    else:
                        print("Invalid color command. Usage: color <ball_id> <r> <g> <b>")
                elif command == "quit":
                    self.running = False
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
        
        # No cleanup needed for the new ZMQClient
        
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