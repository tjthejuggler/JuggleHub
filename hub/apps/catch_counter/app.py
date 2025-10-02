"""
Catch Counter App

A simple app that counts catches detected by the JuggleHub engine.
"""

from PyQt6.QtWidgets import QMainWindow, QWidget, QVBoxLayout, QLabel, QPushButton
from PyQt6.QtCore import Qt, pyqtSignal, QObject
from PyQt6.QtGui import QFont
from apps.base import BaseApp


class CatchSignal(QObject):
    """Signal emitter for thread-safe UI updates."""
    catch_detected = pyqtSignal()


class CatchCounterApp(BaseApp):
    """Simple catch counter application."""
    
    def get_metadata(self) -> dict:
        """Return app metadata."""
        return {
            "id": "catch_counter",
            "name": "Catch Counter",
            "version": "1.0.0"
        }
    
    def initialize(self):
        """Initialize the app."""
        self.catch_count = 0
        self.signal_emitter = CatchSignal()
        self.signal_emitter.catch_detected.connect(self._increment_counter)
        
        # Enable throw/catch detection feature
        self.api.enable_feature("throw_catch_detection")
    
    def create_window(self) -> QMainWindow:
        """Create and return the app window."""
        window = QMainWindow()
        window.setWindowTitle("Catch Counter")
        window.setGeometry(100, 100, 400, 300)
        
        # Central widget
        central = QWidget()
        window.setCentralWidget(central)
        layout = QVBoxLayout(central)
        layout.setSpacing(20)
        layout.setContentsMargins(40, 40, 40, 40)
        
        # Title
        title = QLabel("🎯 Catch Counter")
        title.setFont(QFont("Arial", 24, QFont.Weight.Bold))
        title.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(title)
        
        # Counter display
        self.counter_label = QLabel("0")
        self.counter_label.setFont(QFont("Arial", 72, QFont.Weight.Bold))
        self.counter_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.counter_label.setStyleSheet("color: #4CAF50;")
        layout.addWidget(self.counter_label, 1)
        
        # Catches label
        catches_label = QLabel("catches")
        catches_label.setFont(QFont("Arial", 16))
        catches_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        catches_label.setStyleSheet("color: #aaaaaa;")
        layout.addWidget(catches_label)
        
        # Restart button
        restart_btn = QPushButton("Restart")
        restart_btn.setFixedHeight(50)
        restart_btn.setFont(QFont("Arial", 14, QFont.Weight.Bold))
        restart_btn.clicked.connect(self._reset_counter)
        restart_btn.setStyleSheet("""
            QPushButton {
                background-color: #555555;
                color: white;
                border: none;
                padding: 10px;
                border-radius: 5px;
            }
            QPushButton:hover {
                background-color: #666666;
            }
            QPushButton:pressed {
                background-color: #444444;
            }
        """)
        layout.addWidget(restart_btn)
        
        # Apply dark theme
        window.setStyleSheet("""
            QMainWindow {
                background-color: #2b2b2b;
            }
            QWidget {
                background-color: #2b2b2b;
                color: #ffffff;
            }
        """)
        
        return window
    
    def on_frame_data(self, frame_data):
        """
        Process incoming frame data from the engine.
        
        Args:
            frame_data: FrameData protobuf message
        """
        # Check for throw/catch events
        if hasattr(frame_data, 'throw_catch_events'):
            for event in frame_data.throw_catch_events:
                # Count catch events
                if event.event_type == event.CATCH:
                    self.signal_emitter.catch_detected.emit()
    
    def _increment_counter(self):
        """Increment the catch counter (thread-safe)."""
        self.catch_count += 1
        self.counter_label.setText(str(self.catch_count))
    
    def _reset_counter(self):
        """Reset the catch counter."""
        self.catch_count = 0
        self.counter_label.setText("0")
    
    def cleanup(self):
        """Clean up resources."""
        # Disable throw/catch detection feature
        self.api.disable_feature("throw_catch_detection")