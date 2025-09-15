#!/usr/bin/env python3
"""
JuggleHub - Web UI Component
"""

import threading
import socket
from flask import Flask, jsonify

class WebUI:
    """A Flask-based web UI for JuggleHub."""

    def __init__(self, screen_controller, port: int = 5000):
        self.port = port
        self.screen_controller = screen_controller
        self._app = Flask(__name__)
        self._server_thread: threading.Thread = None
        self._is_running = False
        self._setup_routes()

    def _setup_routes(self):
        """Sets up the routes for the Flask application."""
        @self._app.route('/')
        def index():
            return """
                <h1>JuggleHub Control Panel</h1>
                <h3>Top Screen (eDP-1)</h3>
                <button onclick="fetch('/api/v1/screens/top/enable', { method: 'POST' })">Enable</button>
                <button onclick="fetch('/api/v1/screens/top/disable', { method: 'POST' })">Disable</button>
                <h3>Bottom Screen (eDP-2)</h3>
                <button onclick="fetch('/api/v1/screens/bottom/enable', { method: 'POST' })">Enable</button>
                <button onclick="fetch('/api/v1/screens/bottom/disable', { method: 'POST' })">Disable</button>
            """

        @self._app.route('/api/v1/status')
        def get_status():
            status_data = {
                "engine_fps": 60,  # Placeholder
                "pattern_name": "3-Ball Cascade",  # Placeholder
                "is_recording": False  # Placeholder
            }
            return jsonify(status_data)

        # --- Top Screen Endpoints ---
        @self._app.route('/api/v1/screens/top/enable', methods=['POST'])
        def enable_top_screen():
            self.screen_controller.enable_top_screen()
            return jsonify({"status": "enable_top_sent"})

        @self._app.route('/api/v1/screens/top/disable', methods=['POST'])
        def disable_top_screen():
            self.screen_controller.disable_top_screen()
            return jsonify({"status": "disable_top_sent"})

        # --- Bottom Screen Endpoints ---
        @self._app.route('/api/v1/screens/bottom/enable', methods=['POST'])
        def enable_bottom_screen():
            self.screen_controller.enable_bottom_screen()
            return jsonify({"status": "enable_bottom_sent"})

        @self._app.route('/api/v1/screens/bottom/disable', methods=['POST'])
        def disable_bottom_screen():
            self.screen_controller.disable_bottom_screen()
            return jsonify({"status": "disable_bottom_sent"})

    def start(self):
        """Starts the Flask server in a separate thread."""
        if self._is_running:
            print("Web UI is already running.")
            return

        self._is_running = True
        self._server_thread = threading.Thread(
            target=lambda: self._app.run(host='0.0.0.0', port=self.port, debug=False),
            daemon=True
        )
        self._server_thread.start()
        print(f"Web UI started at http://{self.get_ip_address()}:{self.port}")

    def stop(self):
        """Stops the Flask server."""
        if not self._is_running:
            print("Web UI is not running.")
            return

        # Flask doesn't have a clean stop function, so we rely on the daemon thread exiting
        self._is_running = False
        print("Web UI stopped.")

    @property
    def is_running(self) -> bool:
        """Returns True if the server is running."""
        return self._is_running

    @staticmethod
    def get_ip_address() -> str:
        """Gets the local IP address of the machine."""
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
                # Doesn't have to be reachable
                s.connect(("8.8.8.8", 80))
                return s.getsockname()[0]
        except Exception:
            return "127.0.0.1"