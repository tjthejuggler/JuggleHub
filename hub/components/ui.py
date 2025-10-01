"""
UI Component

Provides a simple UI for visualizing juggling data from the engine.
"""

import sys
import time
import threading
from typing import Optional, Dict, Any
import os
import base64
import socket
import qrcode
import io
import cv2
import numpy as np
import json
from datetime import datetime
from .color_profile_manager import ColorProfileManager, ColorProfileDialog


try:
    import juggler_pb2
except ImportError:
    print("❌ Error: Protocol Buffer files not found. Please run 'make generate-proto' first.")
    sys.exit(1)

# Try to import PyQt6, fall back to console UI if not available
try:
    from PyQt6.QtWidgets import (QApplication, QMainWindow, QWidget, QVBoxLayout,
                                 QHBoxLayout, QLabel, QTextEdit, QPushButton,
                                 QGroupBox, QGridLayout, QProgressBar, QGraphicsView,
                                 QGraphicsScene, QGraphicsPixmapItem, QSlider, QLineEdit,
                                 QComboBox, QMessageBox, QDialog, QVBoxLayout as QVBoxLayout_Dialog,
                                 QMenuBar, QFileDialog)
    from PyQt6.QtCore import QTimer, pyqtSignal, QObject, Qt
    from PyQt6.QtGui import QFont, QPalette, QColor, QPixmap, QImage, QPen, QPainter, QKeySequence, QBrush, QAction
    PYQT_AVAILABLE = True
except ImportError:
    print("⚠️ PyQt6 not available. Using console UI.")
    PYQT_AVAILABLE = False


class UdpClient:
    def __init__(self, host="127.0.0.1", port=12346):
        self.host = host
        self.port = port
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    def send_setting(self, key: str, value: Any):
        message = f"{key}={value}"
        self.sock.sendto(message.encode('utf-8'), (self.host, self.port))
        print(f"Sent UDP setting: {message}")

class ConsoleUI:
    """Simple console-based UI for systems without PyQt6."""
    
    def __init__(self, config: dict):
        self.config = config
        self.running = False
        self.last_frame_data: Optional[juggler_pb2.FrameData] = None
        self.frame_count = 0
        self.start_time = time.time()
        
    def update_frame_data(self, frame_data: juggler_pb2.FrameData):
        """Update with new frame data."""
        self.last_frame_data = frame_data
        self.frame_count += 1
        
        # Print periodic updates
        if self.frame_count % 30 == 0:  # Every 30 frames (~1 second at 30 FPS)
            elapsed = time.time() - self.start_time
            fps = self.frame_count / elapsed if elapsed > 0 else 0
            
            print(f"\n📊 Frame {frame_data.frame_number} | FPS: {fps:.1f} | Balls: {len(frame_data.balls)}")
            
            for i, ball in enumerate(frame_data.balls):
                print(f"  🏀 ID {ball.id}: "
                      f"3D({ball.position.x:.3f}, {ball.position.y:.3f}, {ball.position.z:.3f})")
            
            if frame_data.hands:
                print(f"  👋 Hands: {len(frame_data.hands)}")
                for hand in frame_data.hands:
                    print(f"    {hand.side}: 2D({hand.position_2d.x:.0f}, {hand.position_2d.y:.0f})")
            
            if frame_data.imu_data:
                print(f"  📱 IMU: {len(frame_data.imu_data)} sensors")
    
    def run(self):
        """Run the console UI."""
        self.running = True
        print("🖥️ Console UI started. Press Ctrl+C to stop.")
        
        try:
            while self.running:
                time.sleep(0.1)
        except KeyboardInterrupt:
            print("\n🛑 Console UI stopped by user")
        finally:
            self.cleanup()
    
    def cleanup(self):
        """Clean up console UI."""
        self.running = False
        print("✅ Console UI cleanup completed")


if PYQT_AVAILABLE:
    class FrameDataSignal(QObject):
        """Signal emitter for thread-safe UI updates."""
        frame_received = pyqtSignal(object)

    class CalibrationSettingsWidget(QWidget):
        def __init__(self, udp_client: UdpClient, zmq_client: 'ZMQClient', hub_instance=None, parent=None):
            super().__init__(parent)
            self.udp_client = udp_client
            self.zmq_client = zmq_client
            self.hub_instance = hub_instance
            self.settings_file = os.path.join("hub", "config", "calibration_settings.json")
            self.calibration_saves_dir = os.path.join("hub", "calibration_saves")
            self._loading_settings = True  # Flag to prevent auto-save during initialization and load
            # Ensure calibration_saves directory exists
            os.makedirs(self.calibration_saves_dir, exist_ok=True)
            self.init_ui()
            # Load settings after UI is initialized
            self.load_settings()
            # Now allow auto-save
            self._loading_settings = False

        def init_ui(self):
            layout = QVBoxLayout(self)
            
            # Initialize resolution-FPS mapping first
            self.resolution_fps_map = {
                "1280 x 800": [60, 30, 15, 6],
                "1280 x 720": [60, 30, 15, 6],
                "960 x 540": [60, 30, 15, 6],
                "848 x 480": [60, 30, 15, 6],
                "640 x 480": [60, 30, 15, 6],
                "640 x 360": [60, 30, 15, 6],
                "424 x 240": [60, 30, 15, 6],
                "320 x 240": [60, 30, 15, 6]
            }
            
            # -- Camera Settings --
            camera_group = QGroupBox("📷 Camera Settings")
            camera_layout = QGridLayout(camera_group)
            
            # Camera settings dropdown
            camera_layout.addWidget(QLabel("Settings Profile:"), 0, 0)
            self.camera_settings_combo = QComboBox()
            self.populate_camera_settings()
            camera_layout.addWidget(self.camera_settings_combo, 0, 1)
            
            # Resolution dropdown
            camera_layout.addWidget(QLabel("Resolution:"), 1, 0)
            self.resolution_combo = QComboBox()
            self.populate_resolution_options()
            self.resolution_combo.currentTextChanged.connect(self.on_resolution_changed)
            camera_layout.addWidget(self.resolution_combo, 1, 1)
            
            # FPS dropdown
            camera_layout.addWidget(QLabel("Frame Rate (FPS):"), 2, 0)
            self.fps_combo = QComboBox()
            self.populate_fps_options()
            camera_layout.addWidget(self.fps_combo, 2, 1)
            
            # Camera control buttons
            camera_control_layout = QHBoxLayout()
            
            # Stop camera button
            self.stop_camera_button = QPushButton("Stop Camera")
            self.stop_camera_button.clicked.connect(self.stop_camera_feed)
            self.stop_camera_button.setStyleSheet("""
                QPushButton {
                    background-color: #f44336;
                    color: white;
                    border: none;
                    padding: 8px;
                    border-radius: 4px;
                    font-weight: bold;
                }
                QPushButton:hover {
                    background-color: #da190b;
                }
                QPushButton:pressed {
                    background-color: #b71c1c;
                }
                QPushButton:disabled {
                    background-color: #666666;
                }
            """)
            camera_control_layout.addWidget(self.stop_camera_button)
            
            # Start camera button
            self.start_camera_button = QPushButton("Start Camera")
            self.start_camera_button.clicked.connect(self.start_camera_feed)
            self.start_camera_button.setStyleSheet("""
                QPushButton {
                    background-color: #4CAF50;
                    color: white;
                    border: none;
                    padding: 8px;
                    border-radius: 4px;
                    font-weight: bold;
                }
                QPushButton:hover {
                    background-color: #45a049;
                }
                QPushButton:pressed {
                    background-color: #2e7d32;
                }
                QPushButton:disabled {
                    background-color: #666666;
                }
            """)
            self.start_camera_button.setEnabled(True)
            camera_control_layout.addWidget(self.start_camera_button)
            
            camera_layout.addLayout(camera_control_layout, 3, 0, 1, 2)
            
            # Camera status indicator
            self.camera_status_label = QLabel("● Camera Stopped")
            self.camera_status_label.setStyleSheet("color: #f44336; font-weight: bold;")
            camera_layout.addWidget(self.camera_status_label, 4, 0, 1, 2)

            # IR Projector status
            self.ir_status_label = QLabel("🔆 IR Projector: Unknown")
            camera_layout.addWidget(self.ir_status_label, 5, 0, 1, 2)
            
            layout.addWidget(camera_group)
            
            # -- DNN Tracker Settings --
            dnn_group = QGroupBox("YOLO Tracker Settings")
            dnn_layout = QGridLayout(dnn_group)

            self.confidence_slider, self.confidence_value_label = self._create_slider_widget(
                parent_layout=dnn_layout,
                row=0,
                label_text="Confidence Threshold",
                tooltip_text="Minimum confidence for an object to be detected by YOLO.\n"
                             "Range: 0.00 to 1.00. Default: 0.25.\n"
                             "Lower values detect more objects but increase false positives.",
                range_min=0,
                range_max=100,
                initial_value=25,
                update_func=lambda v: self.update_setting('confidence_threshold', v / 100.0),
                is_float=True
            )

            self.nms_slider, self.nms_value_label = self._create_slider_widget(
                parent_layout=dnn_layout,
                row=1,
                label_text="NMS Threshold",
                tooltip_text="Non-Maximum Suppression threshold for merging overlapping boxes.\n"
                             "Range: 0.00 to 1.00. Default: 0.50.\n"
                             "Higher values allow more overlap.",
                range_min=0,
                range_max=100,
                initial_value=50,
                update_func=lambda v: self.update_setting('nms_threshold', v / 100.0),
                is_float=True
            )
            
            layout.addWidget(dnn_group)

            # -- ByteTrack Settings --
            bytetrack_group = QGroupBox("ByteTrack Settings")
            bytetrack_layout = QGridLayout(bytetrack_group)

            self.track_buffer_slider, self.track_buffer_value_label = self._create_slider_widget(
                parent_layout=bytetrack_layout,
                row=0,
                label_text="Track Buffer (Frames)",
                tooltip_text="How long (in frames) ByteTrack remembers a lost object.\n"
                             "Range: 1 to 600. Default: 300.\n"
                             "Higher values keep tracks alive longer during occlusions.",
                range_min=1,
                range_max=600,
                initial_value=300,
                update_func=lambda v: self.update_setting('track_buffer', v)
            )

            self.track_thresh_slider, self.track_thresh_value_label = self._create_slider_widget(
                parent_layout=bytetrack_layout,
                row=1,
                label_text="Track Threshold",
                tooltip_text="Confidence score needed to start a new track.\n"
                             "Range: 0.00 to 1.00. Default: 0.10.\n"
                             "Lower values allow tracking of less confident detections.",
                range_min=0,
                range_max=100,
                initial_value=10,
                update_func=lambda v: self.update_setting('track_thresh', v / 100.0),
                is_float=True
            )

            self.high_thresh_slider, self.high_thresh_value_label = self._create_slider_widget(
                parent_layout=bytetrack_layout,
                row=2,
                label_text="High Confidence Threshold",
                tooltip_text="Confidence score for a detection to be considered 'high confidence'.\n"
                             "Range: 0.00 to 1.00. Default: 0.40.\n"
                             "Lower values treat more detections as high confidence.",
                range_min=0,
                range_max=100,
                initial_value=40,
                update_func=lambda v: self.update_setting('high_thresh', v / 100.0),
                is_float=True
            )

            self.match_thresh_slider, self.match_thresh_value_label = self._create_slider_widget(
                parent_layout=bytetrack_layout,
                row=3,
                label_text="Match Threshold (IoU)",
                tooltip_text="The minimum Intersection over Union (IoU) to match a detection to a track.\n"
                             "Range: 0.00 to 1.00. Default: 0.50.\n"
                             "Lower values make it easier to maintain tracks with fast-moving objects.",
                range_min=0,
                range_max=100,
                initial_value=50,
                update_func=lambda v: self.update_setting('match_thresh', v / 100.0),
                is_float=True
            )

            layout.addWidget(bytetrack_group)

            # -- Pose Model Settings --
            pose_group = QGroupBox("Pose Model Settings")
            pose_layout = QGridLayout(pose_group)

            self.pose_model_toggle = QPushButton("Enable Pose Model")
            self.pose_model_toggle.setCheckable(True)
            self.pose_model_toggle.setChecked(True)
            self.pose_model_toggle.clicked.connect(self.toggle_pose_model)
            pose_layout.addWidget(self.pose_model_toggle, 0, 0, 1, 2)

            layout.addWidget(pose_group)
            layout.addStretch()

        def toggle_pose_model(self):
            is_enabled = self.pose_model_toggle.isChecked()
            command = juggler_pb2.CommandRequest(
                type=juggler_pb2.CommandRequest.CommandType.SET_POSE_MODEL_ENABLED,
                pose_model_enabled=is_enabled
            )
            try:
                response = self.zmq_client.send_command(command)
                if response.success:
                    print(f"✅ Pose model {'enabled' if is_enabled else 'disabled'}")
                    # Auto-save settings after pose model toggle
                    self.save_settings()
                else:
                    print(f"❌ Failed to toggle pose model: {response.message}")
            except Exception as e:
                print(f"❌ Error toggling pose model: {e}")

        def _create_slider_widget(self, parent_layout, row, label_text, tooltip_text,
                                  range_min, range_max, initial_value,
                                  update_func, is_float=False):
            """Helper function to create a labeled slider with a value display."""
            label = QLabel(label_text)
            label.setToolTip(tooltip_text)
            parent_layout.addWidget(label, row, 0)
            
            slider = QSlider(Qt.Orientation.Horizontal)
            slider.setRange(range_min, range_max)
            slider.setValue(initial_value)
            parent_layout.addWidget(slider, row, 1)

            value_label = QLabel()
            value_label.setMinimumWidth(40) # Ensure consistent width
            parent_layout.addWidget(value_label, row, 2)
            
            def on_value_changed(value):
                if is_float:
                    display_value = f"{value / 100.0:.2f}"
                    update_func(value)
                else:
                    display_value = str(value)
                    update_func(value)
                value_label.setText(display_value)

            slider.valueChanged.connect(on_value_changed)
            
            # Set initial value display
            on_value_changed(initial_value)
            
            return slider, value_label

        def update_ir_status(self, is_active: bool):
            """Update the IR projector status label."""
            if is_active:
                self.ir_status_label.setText("🔆 IR Projector: ON")
                self.ir_status_label.setStyleSheet("color: #4CAF50; font-weight: bold;")
            else:
                self.ir_status_label.setText("🔆 IR Projector: OFF")
                self.ir_status_label.setStyleSheet("color: #f44336; font-weight: bold;")

        def populate_camera_settings(self):
            """Populate the camera settings dropdown with available JSON files."""
            self.camera_settings_combo.clear()
            
            # Look for camera settings files in the camera_settings directory
            camera_settings_dir = os.path.join("..", "camera_settings")
            if os.path.exists(camera_settings_dir):
                for filename in os.listdir(camera_settings_dir):
                    if filename.endswith('.json'):
                        # Remove .json extension for display name
                        display_name = filename[:-5].replace('_', ' ').title()
                        # Use filename without extension as the data value
                        profile_name = filename[:-5]
                        self.camera_settings_combo.addItem(display_name, profile_name)
            
            # If no files found, add default options
            if self.camera_settings_combo.count() == 0:
                self.camera_settings_combo.addItem("Default", "default")
                self.camera_settings_combo.addItem("No Blur", "no_blur")
            
            # Set default to "default" profile
            default_index = self.camera_settings_combo.findData("default")
            if default_index >= 0:
                self.camera_settings_combo.setCurrentIndex(default_index)

        def populate_resolution_options(self):
            """Populate the resolution dropdown with D455 supported resolutions."""
            self.resolution_combo.clear()
            for resolution in self.resolution_fps_map.keys():
                self.resolution_combo.addItem(resolution)
            
            # Set default to 640x480
            default_index = self.resolution_combo.findText("640 x 480")
            if default_index >= 0:
                self.resolution_combo.setCurrentIndex(default_index)

        def populate_fps_options(self):
            """Populate the FPS dropdown based on selected resolution."""
            current_resolution = self.resolution_combo.currentText()
            if current_resolution in self.resolution_fps_map:
                fps_options = self.resolution_fps_map[current_resolution]
                
                self.fps_combo.clear()
                for fps in fps_options:
                    self.fps_combo.addItem(f"{fps} FPS", fps)
                
                # Set default to 60 FPS if available, otherwise first option
                default_index = self.fps_combo.findText("60 FPS")
                if default_index >= 0:
                    self.fps_combo.setCurrentIndex(default_index)
                elif self.fps_combo.count() > 0:
                    self.fps_combo.setCurrentIndex(0)

        def on_resolution_changed(self):
            """Handle resolution change to update FPS options."""
            self.populate_fps_options()

        def stop_camera_feed(self):
            """Stop the camera feed."""
            try:
                command = juggler_pb2.CommandRequest()
                command.type = juggler_pb2.CommandRequest.CommandType.CAMERA_STOP
                
                response = self.zmq_client.send_command(command)
                if response.success:
                    self.stop_camera_button.setEnabled(False)
                    self.start_camera_button.setEnabled(True)
                    self.camera_status_label.setText("● Camera Stopped")
                    self.camera_status_label.setStyleSheet("color: #f44336; font-weight: bold;")
                    self.update_ir_status(False)
                    print(f"✅ Camera stopped: {response.message}")
                else:
                    QMessageBox.critical(self, "Error", f"Failed to stop camera: {response.message}")
                    print(f"❌ Failed to stop camera: {response.message}")
            except Exception as e:
                QMessageBox.critical(self, "Error", f"Error stopping camera: {str(e)}")
                print(f"❌ Error stopping camera: {e}")

        def start_camera_feed(self):
            """Start the camera feed with selected settings."""
            try:
                selected_profile = self.camera_settings_combo.currentData()
                settings_name = self.camera_settings_combo.currentText()
                
                # Get selected resolution and FPS
                selected_resolution = self.resolution_combo.currentText()
                selected_fps = self.fps_combo.currentData()
                
                # Parse resolution (e.g., "640 x 480" -> width=640, height=480)
                width, height = map(int, selected_resolution.split(' x '))
                
                # Construct the full path to the camera settings file
                settings_file_path = f"camera_settings/{selected_profile}.json"
                
                command = juggler_pb2.CommandRequest()
                command.type = juggler_pb2.CommandRequest.CommandType.CAMERA_START
                command.camera_settings_file = settings_file_path
                
                # Add resolution and FPS parameters
                command.camera_width = width
                command.camera_height = height
                command.camera_fps = selected_fps
                
                response = self.zmq_client.send_command(command)
                if response.success:
                    self.stop_camera_button.setEnabled(True)
                    self.start_camera_button.setEnabled(False)
                    self.camera_status_label.setText(f"● Camera Running ({selected_resolution} @ {selected_fps} FPS)")
                    self.camera_status_label.setStyleSheet("color: #4CAF50; font-weight: bold;")
                    print(f"✅ Camera started with {settings_name} at {selected_resolution} @ {selected_fps} FPS: {response.message}")
                    # Auto-save settings after camera starts successfully
                    self.save_settings()
                else:
                    QMessageBox.critical(self, "Error", f"Failed to start camera: {response.message}")
                    print(f"❌ Failed to start camera: {response.message}")
            except Exception as e:
                QMessageBox.critical(self, "Error", f"Error starting camera: {str(e)}")
                print(f"❌ Error starting camera: {e}")

        def update_setting(self, key: str, value: Any):
            self.udp_client.send_setting(key, value)
            # Auto-save settings whenever they change (but not during initial load)
            if not self._loading_settings:
                self.save_settings()

        def get_current_settings(self) -> dict:
            """Get current calibration settings as a dictionary."""
            # Check if ALL UI elements exist before accessing them
            required_attrs = [
                'confidence_slider', 'nms_slider', 'track_buffer_slider',
                'track_thresh_slider', 'high_thresh_slider', 'match_thresh_slider',
                'pose_model_toggle', 'camera_settings_combo', 'resolution_combo', 'fps_combo'
            ]
            
            for attr in required_attrs:
                if not hasattr(self, attr):
                    return {}
            
            return {
                'camera_settings_profile': self.camera_settings_combo.currentData(),
                'resolution': self.resolution_combo.currentText(),
                'fps': self.fps_combo.currentData(),
                'confidence_threshold': self.confidence_slider.value() / 100.0,
                'nms_threshold': self.nms_slider.value() / 100.0,
                'track_buffer': self.track_buffer_slider.value(),
                'track_thresh': self.track_thresh_slider.value() / 100.0,
                'high_thresh': self.high_thresh_slider.value() / 100.0,
                'match_thresh': self.match_thresh_slider.value() / 100.0,
                'pose_model_enabled': self.pose_model_toggle.isChecked()
            }

        def apply_settings(self, settings: dict):
            """Apply settings from a dictionary to the UI controls."""
            # Camera settings
            if 'camera_settings_profile' in settings:
                index = self.camera_settings_combo.findData(settings['camera_settings_profile'])
                if index >= 0:
                    self.camera_settings_combo.setCurrentIndex(index)
            
            if 'resolution' in settings:
                index = self.resolution_combo.findText(settings['resolution'])
                if index >= 0:
                    self.resolution_combo.setCurrentIndex(index)
            
            if 'fps' in settings:
                index = self.fps_combo.findData(settings['fps'])
                if index >= 0:
                    self.fps_combo.setCurrentIndex(index)
            
            # DNN Tracker settings
            if 'confidence_threshold' in settings:
                self.confidence_slider.setValue(int(settings['confidence_threshold'] * 100))
            
            if 'nms_threshold' in settings:
                self.nms_slider.setValue(int(settings['nms_threshold'] * 100))
            
            # ByteTrack settings
            if 'track_buffer' in settings:
                self.track_buffer_slider.setValue(settings['track_buffer'])
            
            if 'track_thresh' in settings:
                self.track_thresh_slider.setValue(int(settings['track_thresh'] * 100))
            
            if 'high_thresh' in settings:
                self.high_thresh_slider.setValue(int(settings['high_thresh'] * 100))
            
            if 'match_thresh' in settings:
                self.match_thresh_slider.setValue(int(settings['match_thresh'] * 100))
            
            # Pose model
            if 'pose_model_enabled' in settings:
                self.pose_model_toggle.setChecked(settings['pose_model_enabled'])

        def save_settings(self, filepath: str = None):
            """Save current calibration settings to a JSON file."""
            if filepath is None:
                filepath = self.settings_file
            
            settings = self.get_current_settings()
            settings['saved_at'] = datetime.now().isoformat()
            
            # Ensure directory exists
            os.makedirs(os.path.dirname(filepath), exist_ok=True)
            
            try:
                with open(filepath, 'w') as f:
                    json.dump(settings, f, indent=2)
                print(f"✅ Settings saved to {filepath}")
                return True
            except Exception as e:
                print(f"❌ Error saving settings: {e}")
                return False

        def load_settings(self, filepath: str = None):
            """Load calibration settings from a JSON file."""
            if filepath is None:
                filepath = self.settings_file
            
            if not os.path.exists(filepath):
                print(f"ℹ️ No saved settings found at {filepath}")
                return False
            
            try:
                # Set flag to prevent auto-save during load
                self._loading_settings = True
                
                with open(filepath, 'r') as f:
                    settings = json.load(f)
                
                self.apply_settings(settings)
                print(f"✅ Settings loaded from {filepath}")
                if 'saved_at' in settings:
                    print(f"   Saved at: {settings['saved_at']}")
                return True
            except Exception as e:
                print(f"❌ Error loading settings: {e}")
                return False
            finally:
                # Always reset the flag
                self._loading_settings = False

    class JuggleHubMainWindow(QMainWindow):
        """Main window for JuggleHub UI."""
        
        def __init__(self, config: dict, zmq_client: 'ZMQClient', hub_instance=None):
            super().__init__()
            self.config = config
            self.zmq_client = zmq_client
            self.hub_instance = hub_instance
            self.frame_count = 0
            self.start_time = time.time()
            self.last_frame_data: Optional[juggler_pb2.FrameData] = None
            self.calibration_mode = False
            self.udp_client = UdpClient()
            self.is_continuous_recording = False
            self.tracker_history = {} # For drawing tails
            self.calibrating_id = -1 # ID of the ball we are currently calibrating
            self.color_profile_manager = ColorProfileManager()
            
            # Signal for thread-safe updates
            self.signal_emitter = FrameDataSignal()
            self.signal_emitter.frame_received.connect(self._update_ui)
            
            self.init_ui()
            
            # Timer for periodic UI updates
            self.update_timer = QTimer()
            self.update_timer.timeout.connect(self._periodic_update)
            self.update_timer.start(100)  # Update every 100ms
        
        def init_ui(self):
            """Initialize the user interface."""
            self.setWindowTitle("JuggleHub - Juggling Analysis")
            self.setGeometry(100, 100, 1000, 700)
            
            # Create menu bar
            self.create_menu_bar()
            
            # Central widget
            central_widget = QWidget()
            self.setCentralWidget(central_widget)
            
            # Main layout
            main_layout = QVBoxLayout(central_widget)
            
            # Status bar
            status_layout = QHBoxLayout()
            self.status_label = QLabel("🔄 Waiting for data...")
            self.fps_label = QLabel("FPS: 0.0")
            self.frame_count_label = QLabel("Frames: 0")
            
            status_layout.addWidget(self.status_label)
            status_layout.addStretch()
            status_layout.addWidget(self.fps_label)
            status_layout.addWidget(self.frame_count_label)
            
            main_layout.addLayout(status_layout)
            
            # Content area
            content_layout = QHBoxLayout()
            
            # Left panel - Ball tracking
            ball_group = QGroupBox("🏀 Ball Tracking")
            ball_layout = QVBoxLayout(ball_group)
            
            self.ball_count_label = QLabel("Balls detected: 0")
            ball_layout.addWidget(self.ball_count_label)
            
            self.ball_list = QTextEdit()
            self.ball_list.setMaximumHeight(200)
            self.ball_list.setReadOnly(True)
            ball_layout.addWidget(self.ball_list)
            
            # Calibration button
            self.calibration_button = QPushButton("Enter Calibration Mode")
            self.calibration_button.clicked.connect(self.toggle_calibration_mode)
            ball_layout.addWidget(self.calibration_button)

            # Record button
            self.record_button = QPushButton("Record 5s Clip")
            self.record_button.clicked.connect(self.record_clip)
            ball_layout.addWidget(self.record_button)

            # Continuous recording button
            self.continuous_record_button = QPushButton("Start Recording")
            self.continuous_record_button.clicked.connect(self.toggle_continuous_recording)
            self.continuous_record_button.setCheckable(True)
            self.continuous_record_button.setStyleSheet("""
                QPushButton {
                    background-color: #4CAF50;
                    color: white;
                    border: none;
                    padding: 8px;
                    border-radius: 4px;
                    font-weight: bold;
                }
                QPushButton:checked {
                    background-color: #f44336;
                }
                QPushButton:hover {
                    background-color: #45a049;
                }
                QPushButton:checked:hover {
                    background-color: #da190b;
                }
            """)
            ball_layout.addWidget(self.continuous_record_button)
            
            # Recording status indicator
            self.recording_status = QLabel("● Not Recording")
            self.recording_status.setStyleSheet("color: #666666; font-weight: bold;")
            ball_layout.addWidget(self.recording_status)

            content_layout.addWidget(ball_group)

            # Center panel - Video Feed
            self.video_group = QGroupBox("📹 Camera Feed")
            self.video_layout = QVBoxLayout(self.video_group)
            self.video_scene = QGraphicsScene()
            self.video_view = QGraphicsView(self.video_scene)
            self.video_pixmap_item = QGraphicsPixmapItem()
            self.video_scene.addItem(self.video_pixmap_item)
            self.video_view.mousePressEvent = self.video_view_clicked
            self.video_layout.addWidget(self.video_view)

            # --- Color Profile Controls ---
            color_profile_layout = QVBoxLayout()
            
            # Top row: dropdown and button
            profile_control_layout = QHBoxLayout()
            profile_control_layout.addWidget(QLabel("Color Profile:"))
            
            self.color_profile_combo = QComboBox()
            self.populate_color_profiles()
            profile_control_layout.addWidget(self.color_profile_combo)
            
            self.set_color_profile_button = QPushButton("Set Color Profile")
            self.set_color_profile_button.setCheckable(True)
            self.set_color_profile_button.clicked.connect(self.start_color_profile_calibration)
            self.set_color_profile_button.setStyleSheet("""
                QPushButton {
                    background-color: #555;
                    color: white;
                    border: 1px solid #777;
                    padding: 5px;
                    border-radius: 3px;
                }
                QPushButton:checked {
                    background-color: #007ACC;
                    border-color: #005A9E;
                }
            """)
            profile_control_layout.addWidget(self.set_color_profile_button)
            
            color_profile_layout.addLayout(profile_control_layout)
            
            # Status label
            self.color_profile_status_label = QLabel("Select a color profile and click 'Set Color Profile', then click on a ball in the video.")
            self.color_profile_status_label.setWordWrap(True)
            color_profile_layout.addWidget(self.color_profile_status_label)
            
            self.video_layout.addLayout(color_profile_layout)

            # --- NEW: Visualization Toggles ---
            toggles_layout = QHBoxLayout()
            self.show_raw_detections_toggle = QPushButton("YOLO Detections")
            self.show_raw_detections_toggle.setCheckable(True)
            self.show_raw_detections_toggle.setChecked(False)
            self.show_raw_detections_toggle.clicked.connect(self.toggle_overlays)
            toggles_layout.addWidget(self.show_raw_detections_toggle)

            self.show_tracked_boxes_toggle = QPushButton("ByteTrack Boxes")
            self.show_tracked_boxes_toggle.setCheckable(True)
            self.show_tracked_boxes_toggle.setChecked(False)
            self.show_tracked_boxes_toggle.clicked.connect(self.toggle_overlays)
            toggles_layout.addWidget(self.show_tracked_boxes_toggle)

            self.show_color_tracker_toggle = QPushButton("Color Tracking")
            self.show_color_tracker_toggle.setCheckable(True)
            self.show_color_tracker_toggle.setChecked(True) # Default to on
            self.show_color_tracker_toggle.clicked.connect(self.toggle_overlays)
            toggles_layout.addWidget(self.show_color_tracker_toggle)

            self.show_tails_toggle = QPushButton("Show Tails")
            self.show_tails_toggle.setCheckable(True)
            self.show_tails_toggle.setChecked(False)
            self.show_tails_toggle.clicked.connect(self.toggle_overlays)
            toggles_layout.addWidget(self.show_tails_toggle)
            
            self.show_skeleton_toggle = QPushButton("Show Skeleton")
            self.show_skeleton_toggle.setCheckable(True)
            self.show_skeleton_toggle.setChecked(False)
            self.show_skeleton_toggle.clicked.connect(self.toggle_overlays)
            toggles_layout.addWidget(self.show_skeleton_toggle)
            
            self.video_layout.addLayout(toggles_layout)

            # --- NEW: Tail Length Slider ---
            tail_layout = QHBoxLayout()
            tail_layout.addWidget(QLabel("Tail Length:"))
            self.tail_length_slider = QSlider(Qt.Orientation.Horizontal)
            self.tail_length_slider.setRange(10, 200) # 10 to 200 frames
            self.tail_length_slider.setValue(50)
            self.tail_length_slider.valueChanged.connect(self.update_tail_length)
            tail_layout.addWidget(self.tail_length_slider)
            self.tail_length_label = QLabel("50 frames")
            tail_layout.addWidget(self.tail_length_label)
            self.video_layout.addLayout(tail_layout)
            
            content_layout.addWidget(self.video_group, 2)
            
            # Calibration settings panel
            self.settings_widget = CalibrationSettingsWidget(self.udp_client, self.zmq_client, self.hub_instance)
            self.settings_widget.setVisible(False)
            content_layout.addWidget(self.settings_widget, 1)
            
            # Right panel - System info
            system_group = QGroupBox("⚙️ System Status")
            system_layout = QVBoxLayout(system_group)
            
            self.camera_status = QLabel("📷 Camera: Unknown")
            self.engine_status = QLabel("🔧 Engine: Unknown")
            self.mode_status = QLabel("🎯 Mode: Unknown")
            
            system_layout.addWidget(self.camera_status)
            system_layout.addWidget(self.engine_status)
            system_layout.addWidget(self.mode_status)
            
            # Hand tracking
            self.hand_status = QLabel("👋 Hands: 0")
            system_layout.addWidget(self.hand_status)
            
            # IMU status
            self.imu_status = QLabel("📱 IMU: 0 sensors")

            # Web UI controls
            web_ui_layout = QHBoxLayout()
            self.web_ui_button = QPushButton("Start Web UI")
            self.web_ui_button.clicked.connect(self.toggle_web_ui)
            web_ui_layout.addWidget(self.web_ui_button)

            # Screen control buttons
            screen_control_layout = QHBoxLayout()
            self.disable_top_button = QPushButton("Disable Top Screen")
            self.disable_top_button.clicked.connect(self.disable_top_screen)
            screen_control_layout.addWidget(self.disable_top_button)

            self.disable_bottom_button = QPushButton("Disable Bottom Screen")
            self.disable_bottom_button.clicked.connect(self.disable_bottom_screen)
            screen_control_layout.addWidget(self.disable_bottom_button)
            
            system_layout.addLayout(web_ui_layout)
            system_layout.addLayout(screen_control_layout)
            system_layout.addWidget(self.imu_status)

            self.imu_list = QTextEdit()
            self.imu_list.setMaximumHeight(200)
            self.imu_list.setReadOnly(True)
            system_layout.addWidget(self.imu_list)
            
            system_layout.addStretch()
            
            content_layout.addWidget(system_group)
            
            main_layout.addLayout(content_layout)
            
            # Bottom panel - Log
            log_group = QGroupBox("📝 Activity Log")
            log_layout = QVBoxLayout(log_group)
            
            self.log_text = QTextEdit()
            self.log_text.setMaximumHeight(150)
            self.log_text.setReadOnly(True)
            log_layout.addWidget(self.log_text)
            
            main_layout.addWidget(log_group)
            
            self.apply_dark_theme()
            self.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
        
        def create_menu_bar(self):
            """Create the menu bar with File and Help menus."""
            menubar = self.menuBar()
            
            # File menu
            file_menu = menubar.addMenu("&File")
            
            # Save Settings action
            save_action = QAction("&Save Settings", self)
            save_action.setShortcut("Ctrl+S")
            save_action.triggered.connect(self.save_settings_dialog)
            file_menu.addAction(save_action)
            
            # Load Settings action
            load_action = QAction("&Load Settings", self)
            load_action.setShortcut("Ctrl+O")
            load_action.triggered.connect(self.load_settings_dialog)
            file_menu.addAction(load_action)
            
            file_menu.addSeparator()
            
            # Manage Color Profiles action
            color_profiles_action = QAction("Manage &Color Profiles", self)
            color_profiles_action.setShortcut("Ctrl+P")
        
        def populate_color_profiles(self):
            """Populate the color profile dropdown from the manager."""
            self.color_profile_combo.clear()
            profile_names = self.color_profile_manager.get_profile_names()
            self.color_profile_combo.addItems(profile_names)
        
        def open_color_profile_manager(self):
            """Open the color profile manager dialog."""
            dialog = ColorProfileDialog(self)
            if dialog.exec() == QDialog.DialogCode.Accepted:
                # Reload the color profile manager
                self.color_profile_manager.load_profiles()
                # Refresh the dropdown
                current_selection = self.color_profile_combo.currentText()
                self.populate_color_profiles()
                # Try to restore previous selection
                index = self.color_profile_combo.findText(current_selection)
                if index >= 0:
                    self.color_profile_combo.setCurrentIndex(index)
                self.log_message("✅ Color profiles updated")
                # Refresh the video feed to show updated colors
                if self.last_frame_data:
                    self.update_video_feed(self.last_frame_data)
            color_profiles_action.triggered.connect(self.open_color_profile_manager)
            file_menu.addAction(color_profiles_action)
            
            # Help menu
            help_menu = menubar.addMenu("&Help")
            
            # About action
            about_action = QAction("&About", self)
            about_action.triggered.connect(self.show_about_dialog)
            help_menu.addAction(about_action)
        
        def save_settings_dialog(self):
            """Show file dialog to save calibration settings."""
            if not self.calibration_mode:
                QMessageBox.information(self, "Info", "Please enter Calibration Mode first to save settings.")
                return
            
            filepath, _ = QFileDialog.getSaveFileName(
                self,
                "Save Calibration Settings",
                os.path.join(self.settings_widget.calibration_saves_dir, "calibration_settings.json"),
                "JSON Files (*.json);;All Files (*)"
            )
            
            if filepath:
                if self.settings_widget.save_settings(filepath):
                    QMessageBox.information(self, "Success", f"Settings saved to:\n{filepath}")
                    self.log_message(f"✅ Settings saved to {filepath}")
                else:
                    QMessageBox.critical(self, "Error", "Failed to save settings. Check console for details.")
        
        def load_settings_dialog(self):
            """Show file dialog to load calibration settings."""
            if not self.calibration_mode:
                QMessageBox.information(self, "Info", "Please enter Calibration Mode first to load settings.")
                return
            
            filepath, _ = QFileDialog.getOpenFileName(
                self,
                "Load Calibration Settings",
                self.settings_widget.calibration_saves_dir,
                "JSON Files (*.json);;All Files (*)"
            )
            
            if filepath:
                if self.settings_widget.load_settings(filepath):
                    QMessageBox.information(self, "Success", f"Settings loaded from:\n{filepath}")
                    self.log_message(f"✅ Settings loaded from {filepath}")
                else:
                    QMessageBox.critical(self, "Error", "Failed to load settings. Check console for details.")
        
        def show_about_dialog(self):
            """Show the About dialog."""
            about_text = """
            <h2>JuggleHub</h2>
            <p><b>Version:</b> 1.0.0</p>
            <p><b>Description:</b> A comprehensive juggling analysis system that tracks balls in real-time using computer vision and deep learning.</p>
            <br>
            <p><b>Features:</b></p>
            <ul>
                <li>Real-time ball tracking with YOLO object detection</li>
                <li>ByteTrack multi-object tracking</li>
                <li>Color-based ball identification</li>
                <li>3D position estimation</li>
                <li>Camera calibration tools</li>
                <li>Video recording capabilities</li>
            </ul>
            <br>
            <p><b>Technologies:</b></p>
            <ul>
                <li>Python & PyQt6</li>
                <li>C++ & OpenVINO</li>
                <li>Intel RealSense D455</li>
                <li>ZeroMQ & Protocol Buffers</li>
            </ul>
            <br>
            <p>© 2025 JuggleHub Project</p>
            """
            
            QMessageBox.about(self, "About JuggleHub", about_text)
        
        def apply_dark_theme(self):
            """Apply a dark theme to the UI."""
            self.setStyleSheet("""
                QMainWindow, QDialog { background-color: #2b2b2b; color: #ffffff; }
                QGroupBox {
                    font-weight: bold; border: 2px solid #555555;
                    border-radius: 5px; margin-top: 1ex; padding-top: 10px;
                }
                QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }
                QLabel { color: #ffffff; }
                QTextEdit { background-color: #1e1e1e; border: 1px solid #555555; color: #ffffff; }
                QPushButton {
                    background-color: #555; color: white; border: 1px solid #777;
                    padding: 5px; border-radius: 3px;
                }
                QPushButton:hover { background-color: #666; }
                QPushButton:pressed { background-color: #444; }
                QPushButton:checked { background-color: #007ACC; border-color: #005A9E; }
            """)
        
        def update_frame_data(self, frame_data: juggler_pb2.FrameData):
            self.signal_emitter.frame_received.emit(frame_data)
        
        def _update_ui(self, frame_data: juggler_pb2.FrameData):
            self.last_frame_data = frame_data
            self.frame_count += 1
            self.log_message(f"UI received frame {frame_data.frame_number} with {len(frame_data.balls)} balls.")
            
            ball_count = len(frame_data.balls)
            self.ball_count_label.setText(f"Balls detected: {ball_count}")
            ball_text = ""
            # Define a mapping from enum to string for display
            status_map = {
                juggler_pb2.Ball.TRACKED: "Tracked",
                juggler_pb2.Ball.PREDICTED: "Predicted",
                juggler_pb2.Ball.OCCLUDED: "Occluded",
            }

            for ball in frame_data.balls:
                status_str = status_map.get(ball.status, "Unknown")
                ball_text += f"Ball {ball.logical_id} ({status_str}): 3D({ball.position.x:.3f}, {ball.position.y:.3f}, {ball.position.z:.3f})\n"
                
                # Update tracker history using logical_id
                if ball.logical_id not in self.tracker_history:
                    self.tracker_history[ball.logical_id] = []
                
                # Only add to history if the point is valid
                if ball.projected_pos_2d.x > 0 and ball.projected_pos_2d.y > 0:
                    self.tracker_history[ball.logical_id].append((ball.projected_pos_2d.x, ball.projected_pos_2d.y))
                
                # Prune history to tail length
                max_len = self.tail_length_slider.value()
                while len(self.tracker_history[ball.logical_id]) > max_len:
                    self.tracker_history[ball.logical_id].pop(0)

            self.ball_list.setPlainText(ball_text)

            # Always try to update the video feed if the widget is visible
            if self.video_group.isVisible() and frame_data.color_image_b64:
                self.update_video_feed(frame_data)
            elif self.video_group.isVisible():
                self.log_message(f"UI: Video feed is visible but frame {frame_data.frame_number} has no image data.")
            
            if self.settings_widget:
                is_camera_running = "Running" in self.settings_widget.camera_status_label.text()
                self.settings_widget.update_ir_status(frame_data.ir_projector_active and is_camera_running)

            if frame_data.HasField('status'):
                status = frame_data.status
                self.camera_status.setText(f"📷 Camera: {'Connected' if status.camera_connected else 'Disconnected'}")
                self.engine_status.setText(f"🔧 Engine: {'Running' if status.engine_running else 'Stopped'}")
                self.mode_status.setText(f"🎯 Mode: {status.mode}")
                if status.error_message: self.log_message(f"❌ Error: {status.error_message}")
            
            self.hand_status.setText(f"👋 Hands: {len(frame_data.hands)}")
            self.imu_status.setText(f"📱 IMU: {len(frame_data.imu_data)} sensors")
            imu_text = ""
            for imu in frame_data.imu_data:
                imu_text += f"Watch: {imu.watch_name} ({imu.watch_ip})\n"
                imu_text += f"  Accel: ({imu.acceleration.x:.2f}, {imu.acceleration.y:.2f}, {imu.acceleration.z:.2f})\n"
                imu_text += f"  Gyro:  ({imu.gyroscope.x:.2f}, {imu.gyroscope.y:.2f}, {imu.gyroscope.z:.2f})\n"
            self.imu_list.setPlainText(imu_text)
            self.status_label.setText(f"✅ Receiving data - Frame {frame_data.frame_number}")
        
        def _periodic_update(self):
            elapsed = time.time() - self.start_time
            fps = self.frame_count / elapsed if elapsed > 0 else 0
            self.fps_label.setText(f"FPS: {fps:.1f}")
            self.frame_count_label.setText(f"Frames: {self.frame_count}")
            if self.last_frame_data:
                time_since_last = (time.time() * 1000000 - self.last_frame_data.timestamp_us) / 1000000
                if time_since_last > 2.0: self.status_label.setText("⚠️ No data received recently")
        
        def log_message(self, message: str):
            timestamp = time.strftime("%H:%M:%S")
            self.log_text.append(f"[{timestamp}] {message}")
            if self.log_text.document().blockCount() > 100:
                cursor = self.log_text.textCursor()
                cursor.movePosition(cursor.MoveOperation.Start)
                cursor.select(cursor.SelectionType.BlockUnderCursor)
                cursor.removeSelectedText()
        
        def toggle_calibration_mode(self):
            self.calibration_mode = not self.calibration_mode
            self.video_group.setVisible(self.calibration_mode)
            self.settings_widget.setVisible(self.calibration_mode)
            self.calibration_button.setText("Exit Calibration Mode" if self.calibration_mode else "Enter Calibration Mode")
            
            # Auto-load settings when entering calibration mode
            if self.calibration_mode:
                self.settings_widget.load_settings()
                self.log_message("💾 Auto-loaded calibration settings")

        def toggle_overlays(self):
            if self.last_frame_data: self.update_video_feed(self.last_frame_data)

        def update_video_feed(self, frame_data: juggler_pb2.FrameData):
            self.log_message(f"UI: update_video_feed called for frame {frame_data.frame_number}.")
            self.log_message(f"UI: Frame has {len(frame_data.hands)} hands, {len(frame_data.balls)} balls")
            if not frame_data.color_image_b64:
                self.log_message(f"UI ERROR: Frame {frame_data.frame_number} has no color_image_b64 data.")
                return

            image = QImage()
            load_success = image.loadFromData(frame_data.color_image_b64, "JPEG")

            if not load_success:
                self.log_message(f"UI ERROR: QImage.loadFromData failed for frame {frame_data.frame_number}. Image data size: {len(frame_data.color_image_b64)} bytes.")
                # Optionally, save the bad frame for debugging
                # with open(f"bad_frame_{frame_data.frame_number}.jpg", "wb") as f:
                #     f.write(frame_data.color_image_b64)
                return
            
            self.log_message(f"UI: Frame {frame_data.frame_number} loaded into QImage successfully. Size: {image.width()}x{image.height()}.")

            pixmap = QPixmap.fromImage(image)
            painter = QPainter(pixmap)
            
            # --- Draw YOLO Detections ---
            if self.show_raw_detections_toggle.isChecked():
                painter.setPen(QPen(QColor(255, 0, 0, 100), 2)) # Semi-transparent red
                for det in frame_data.raw_detections:
                    painter.drawRect(int(det.x), int(det.y), int(det.width), int(det.height))

            # --- Draw ByteTrack Boxes ---
            if self.show_tracked_boxes_toggle.isChecked():
                painter.setPen(QPen(QColor(255, 165, 0, 150), 2, Qt.PenStyle.DashLine)) # Orange dash
                for obj in frame_data.balls:
                    if obj.status == juggler_pb2.Ball.TRACKED:
                        bbox = obj.bounding_box_2d
                        painter.drawRect(int(bbox.x), int(bbox.y), int(bbox.width), int(bbox.height))

            # --- Draw Color Trackers (NEW SIMPLIFIED SYSTEM) ---
            if self.show_color_tracker_toggle.isChecked():
                # Get color map from profile manager
                color_name_map = self.color_profile_manager.get_color_map()
                
                for color_ball in frame_data.color_tracked_balls:
                    if not color_ball.is_active:
                        continue
                    
                    center_x, center_y = int(color_ball.pixel_pos.x), int(color_ball.pixel_pos.y)
                    # Use the actual color name from the tracker, fallback to white if unknown
                    color = color_name_map.get(color_ball.color_name.lower(), QColor(255, 255, 255))
                    radius = 12
                    
                    # Render based on wrist association
                    if color_ball.associated_wrist_id >= 0:
                        # Ball is near a wrist - draw with dashed outline
                        painter.setBrush(Qt.BrushStyle.NoBrush)
                        pen = QPen(color, 3)
                        pen.setStyle(Qt.PenStyle.DashLine)
                        painter.setPen(pen)
                    else:
                        # Ball is being tracked normally - solid fill
                        painter.setBrush(QBrush(color))
                        # Use thicker black border for white balls to make them visible
                        if color_ball.color_name.lower() == 'white':
                            painter.setPen(QPen(QColor(0, 0, 0), 3))
                        else:
                            painter.setPen(QPen(QColor(0, 0, 0, 100), 1))
                    
                    painter.drawEllipse(center_x - radius, center_y - radius, radius * 2, radius * 2)
                    
                    # Draw label with background for better visibility
                    painter.setFont(QFont("Arial", 10, QFont.Weight.Bold))
                    label = f"{color_ball.color_name} ({color_ball.logical_id})"
                    pos_label = f"({color_ball.world_pos.z:.2f}m)"
                    
                    # Use black text with white outline for white balls, white text with black outline for others
                    if color_ball.color_name.lower() == 'white':
                        # Draw text shadow for white balls
                        painter.setPen(QPen(QColor(255, 255, 255)))
                        painter.drawText(center_x + 14, center_y - 1, label)
                        painter.drawText(center_x + 16, center_y + 1, label)
                        painter.drawText(center_x + 14, center_y + 14, pos_label)
                        painter.drawText(center_x + 16, center_y + 16, pos_label)
                        # Draw actual text
                        painter.setPen(QPen(QColor(0, 0, 0)))
                        painter.drawText(center_x + 15, center_y, label)
                        painter.drawText(center_x + 15, center_y + 15, pos_label)
                    else:
                        painter.setPen(QPen(QColor(255, 255, 255)))
                        painter.drawText(center_x + 15, center_y, label)
                        painter.drawText(center_x + 15, center_y + 15, pos_label)
                    
                    # Show wrist association if present
                    if color_ball.associated_wrist_id >= 0:
                        wrist_label = "L" if color_ball.associated_wrist_id == 0 else "R"
                        if color_ball.color_name.lower() == 'white':
                            painter.setPen(QPen(QColor(0, 0, 0)))
                        else:
                            painter.setPen(QPen(QColor(255, 255, 255)))
                        painter.drawText(center_x + 15, center_y + 30, f"[{wrist_label}]")

            # --- Draw Tracker Tails ---
            if self.show_tails_toggle.isChecked():
                for ball_id, history in self.tracker_history.items():
                    if len(history) > 1:
                        # Find the corresponding ball to get its color
                        ball_for_tail = next((b for b in frame_data.balls if b.logical_id == ball_id), None)
                        if ball_for_tail:
                            color = self.get_average_color(image, ball_for_tail.bounding_box_2d)
                            pen = QPen(color, 2)
                            
                            # Draw lines between consecutive points in the history
                            for i in range(len(history) - 1):
                                p1 = history[i]
                                p2 = history[i+1]
                                if p1[0] > 0 and p1[1] > 0 and p2[0] > 0 and p2[1] > 0: # Check for valid points
                                    painter.setPen(pen)
                                    painter.drawLine(int(p1[0]), int(p1[1]), int(p2[0]), int(p2[1]))

            # --- Draw Skeleton and Hand Trackers ---
            if self.show_skeleton_toggle.isChecked():
                self.log_message(f"UI: Drawing skeleton for {len(frame_data.hands)} hands")
                
                # Draw hand wrist markers
                painter.setPen(QPen(QColor(3, 169, 244), 4)) # Bright blue, thick line
                painter.setBrush(Qt.BrushStyle.NoBrush)
                for hand in frame_data.hands:
                    # Draw a large circle for high visibility
                    center_x, center_y = int(hand.position_2d.x), int(hand.position_2d.y)
                    radius = 20 # Large radius for visibility
                    painter.drawEllipse(center_x - radius, center_y - radius, radius * 2, radius * 2)
                    
                    # Draw hand side label
                    painter.setPen(QPen(QColor(255, 255, 255)))
                    painter.setFont(QFont("Arial", 12, QFont.Weight.Bold))
                    side_label = "L" if hand.side == "left" else "R"
                    painter.drawText(center_x - 5, center_y + 5, side_label)
                    
                    # Reset pen for keypoints
                    painter.setPen(QPen(QColor(3, 169, 244), 4))
                
                # Draw all body keypoints
                painter.setPen(QPen(QColor(0, 255, 0, 150), 2)) # Green for skeleton
                for hand in frame_data.hands:
                    self.log_message(f"UI: Hand has {len(hand.keypoints)} keypoints")
                    for i, kp in enumerate(hand.keypoints):
                        if kp.confidence > 0.5:
                            self.log_message(f"UI: Drawing keypoint {i} at ({kp.pos_2d.x:.1f}, {kp.pos_2d.y:.1f})")
                            painter.drawEllipse(int(kp.pos_2d.x) - 3, int(kp.pos_2d.y) - 3, 6, 6)
                        else:
                            self.log_message(f"UI: Skipping keypoint {i} (confidence {kp.confidence:.2f} < 0.5)")

            painter.end()
            self.video_pixmap_item.setPixmap(pixmap)
            self.video_view.fitInView(self.video_pixmap_item, Qt.AspectRatioMode.KeepAspectRatio)

        def update_tail_length(self, value):
            self.tail_length_label.setText(f"{value} frames")
            # The actual tail length will be used during rendering.
            if self.last_frame_data: self.update_video_feed(self.last_frame_data)

        def get_average_color(self, image: QImage, bbox: juggler_pb2.BoundingBox2D) -> QColor:
            """Calculates the average color within a bounding box."""
            x, y, w, h = int(bbox.x), int(bbox.y), int(bbox.width), int(bbox.height)
            
            # Clamp the bounding box to the image dimensions.
            x = max(0, x)
            y = max(0, y)
            w = min(w, image.width() - x)
            h = min(h, image.height() - y)

            if w <= 0 or h <= 0:
                return QColor(255, 255, 255) # Return white if the box is invalid.

            total_r, total_g, total_b = 0, 0, 0
            pixel_count = 0
            
            for i in range(x, x + w):
                for j in range(y, y + h):
                    pixel_color = image.pixelColor(i, j)
                    total_r += pixel_color.red()
                    total_g += pixel_color.green()
                    total_b += pixel_color.blue()
                    pixel_count += 1
            
            if pixel_count == 0:
                return QColor(255, 255, 255) # Return white if no pixels were processed.

            return QColor(total_r // pixel_count, total_g // pixel_count, total_b // pixel_count)

        def record_clip(self):
            self.log_message("Sending record command to engine...")
            
            command = juggler_pb2.CommandRequest(
                type=juggler_pb2.CommandRequest.CommandType.RECORD_START,
                record_with_yolo_boxes=self.show_raw_detections_toggle.isChecked(),
                record_with_bytetrack_boxes=self.show_tracked_boxes_toggle.isChecked()
            )
            
            try:
                response = self.zmq_client.send_command(command)
                self.log_message(f"✅ Record command acknowledged: {response.message}" if response.success else f"❌ Record command failed: {response.message}")
            except Exception as e:
                self.log_message(f"❌ Error sending record command: {e}")

        def toggle_continuous_recording(self):
            is_starting = not self.is_continuous_recording
            self.log_message(f"{'Starting' if is_starting else 'Stopping'} continuous recording...")
            command_type = juggler_pb2.CommandRequest.CommandType.RECORD_CONTINUOUS_START if is_starting else juggler_pb2.CommandRequest.CommandType.RECORD_CONTINUOUS_STOP
            
            command = juggler_pb2.CommandRequest(
                type=command_type,
                record_with_yolo_boxes=self.show_raw_detections_toggle.isChecked(),
                record_with_bytetrack_boxes=self.show_tracked_boxes_toggle.isChecked()
            )
            
            try:
                response = self.zmq_client.send_command(command)
                if response.success:
                    self.is_continuous_recording = is_starting
                    self.continuous_record_button.setText("Stop Recording" if is_starting else "Start Recording")
                    self.continuous_record_button.setChecked(is_starting)
                    self.recording_status.setText(f"● {'Recording' if is_starting else 'Not Recording'}")
                    self.recording_status.setStyleSheet(f"color: {'#f44336' if is_starting else '#666666'}; font-weight: bold;")
                    self.log_message(f"✅ Continuous recording {'started' if is_starting else 'stopped'}: {response.message}")
                else:
                    self.log_message(f"❌ Failed to {'start' if is_starting else 'stop'} continuous recording: {response.message}")
            except Exception as e:
                self.log_message(f"❌ Error {'starting' if is_starting else 'stopping'} continuous recording: {e}")
        
        def keyPressEvent(self, event):
            if event.key() == Qt.Key.Key_R:
                self.log_message("🎹 'R' key pressed - triggering recording")
                self.record_clip()
            else:
                super().keyPressEvent(event)
        
        def get_latest_frame(self):
            """Returns the latest video frame as a numpy array."""
            if self.last_frame_data and self.last_frame_data.color_image_b64:
                # Use np.frombuffer which is more direct for bytes
                nparr = np.frombuffer(self.last_frame_data.color_image_b64, np.uint8)
                # Decode the image
                img_np = cv2.imdecode(nparr, cv2.IMREAD_COLOR)
                return img_np
            return None

        def toggle_web_ui(self):
            if not self.hub_instance.web_ui.is_running:
                self.hub_instance.web_ui.start()
                self.web_ui_button.setText("Stop Web UI")
                self.show_qr_code()
            else:
                self.hub_instance.web_ui.stop()
                self.web_ui_button.setText("Start Web UI")

        def show_qr_code(self):
            dialog = QDialog(self)
            dialog.setWindowTitle("Scan QR Code")
            layout = QVBoxLayout_Dialog(dialog)
            qr_label = QLabel()
            url = f"http://{self.hub_instance.web_ui.get_ip_address()}:{self.hub_instance.web_ui.port}"
            qr = qrcode.make(url)
            buffer = io.BytesIO()
            qr.save(buffer, "PNG")
            pixmap = QPixmap()
            pixmap.loadFromData(buffer.getvalue())
            qr_label.setPixmap(pixmap)
            layout.addWidget(qr_label)
            dialog.exec()
        
        def disable_top_screen(self): self.hub_instance.screen_controller.disable_top_screen()
        def disable_bottom_screen(self): self.hub_instance.screen_controller.disable_bottom_screen()

        def start_color_profile_calibration(self):
            """Start color profile calibration mode."""
            if self.set_color_profile_button.isChecked():
                selected_color = self.color_profile_combo.currentText()
                self.color_profile_status_label.setText(f"Click on a {selected_color} ball in the video feed...")
                self.log_message(f"Waiting for user to click on a {selected_color} ball to set color profile.")
            else:
                self.color_profile_status_label.setText("Select a color profile and click 'Set Color Profile', then click on a ball in the video.")
                self.log_message("Color profile calibration cancelled.")

        def video_view_clicked(self, event):
            if self.set_color_profile_button.isChecked():
                scene_pos = self.video_view.mapToScene(event.pos())
                pixmap_item = self.video_pixmap_item
                
                # Check if the click is within the pixmap bounds
                if pixmap_item.pixmap() and pixmap_item.sceneBoundingRect().contains(scene_pos):
                    # Transform scene coordinates to pixmap (image) coordinates
                    img_pos = pixmap_item.mapFromScene(scene_pos)
                    
                    color_name = self.color_profile_combo.currentText()
                    self.log_message(f"Clicked at pixel ({img_pos.x():.1f}, {img_pos.y():.1f}) to set '{color_name}' color profile")
                    
                    # Get the current frame image
                    frame_image = self.get_latest_frame()
                    if frame_image is not None:
                        # Extract color at the clicked position
                        x, y = int(img_pos.x()), int(img_pos.y())
                        # Ensure coordinates are within bounds
                        x = max(0, min(x, frame_image.shape[1] - 1))
                        y = max(0, min(y, frame_image.shape[0] - 1))
                        
                        # Sample a small region around the click for better color accuracy
                        sample_size = 10
                        x1 = max(0, x - sample_size)
                        y1 = max(0, y - sample_size)
                        x2 = min(frame_image.shape[1], x + sample_size)
                        y2 = min(frame_image.shape[0], y + sample_size)
                        
                        roi = frame_image[y1:y2, x1:x2]
                        hsv_roi = cv2.cvtColor(roi, cv2.COLOR_BGR2HSV)
                        avg_hsv = np.mean(hsv_roi, axis=(0, 1))
                        
                        self.log_message(f"Sampled HSV color: H={avg_hsv[0]:.1f}, S={avg_hsv[1]:.1f}, V={avg_hsv[2]:.1f}")
                        
                        # Send calibration command to engine via ZMQ
                        command = juggler_pb2.CommandRequest()
                        command.type = juggler_pb2.CommandRequest.CommandType.CALIBRATE_COLOR
                        command.color_name = color_name
                        command.click_x = x
                        command.click_y = y
                        
                        try:
                            response = self.zmq_client.send_command(command)
                            if response.success:
                                self.log_message(f"✅ Color profile '{color_name}' updated successfully")
                                self.color_profile_status_label.setText(f"✅ '{color_name}' profile set! Click 'Set Color Profile' again to calibrate another color.")
                            else:
                                self.log_message(f"❌ Failed to update color profile: {response.message}")
                                self.color_profile_status_label.setText(f"❌ Failed to set '{color_name}' profile")
                        except Exception as e:
                            self.log_message(f"❌ Error sending calibration command: {e}")
                            self.color_profile_status_label.setText(f"❌ Error: {e}")
                    else:
                        self.log_message(f"❌ Error: No frame image available for color sampling")
                        self.color_profile_status_label.setText("❌ No frame available")
                    
                    # Uncheck the button after calibration
                    self.set_color_profile_button.setChecked(False)
                    self.color_profile_status_label.setText("Select a color profile and click 'Set Color Profile', then click on a ball in the video.")
            # Call original event handler
            QGraphicsView.mousePressEvent(self.video_view, event)

class JuggleHubUI:
    def __init__(self, config: dict, zmq_client: Optional['ZMQClient'] = None, hub_instance=None):
        self.config = config
        self.zmq_client = zmq_client
        self.hub_instance = hub_instance
        if PYQT_AVAILABLE and config.get('enable_ui', True):
            self.app = QApplication.instance() or QApplication(sys.argv)
            self.main_window = JuggleHubMainWindow(config, self.zmq_client, self.hub_instance)
            self.ui_type = "pyqt6"
        else:
            self.console_ui = ConsoleUI(config)
            self.ui_type = "console"

    def get_latest_frame(self):
        """Returns the latest video frame as a numpy array."""
        if self.ui_type == "pyqt6" and hasattr(self.main_window, 'get_latest_frame'):
            return self.main_window.get_latest_frame()
        return None
    
    def update_frame_data(self, frame_data: juggler_pb2.FrameData):
        getattr(self.main_window if self.ui_type == "pyqt6" else self.console_ui, 'update_frame_data')(frame_data)
    
    def run(self):
        if self.ui_type == "pyqt6":
            print("🖥️ Starting PyQt6 UI...")
            self.main_window.show()
            self.app.exec()
        else:
            self.console_ui.run()
    
    def cleanup(self):
        print("🧹 Cleaning up UI...")
        if self.ui_type == "pyqt6":
            # Auto-save settings before closing
            if self.main_window and hasattr(self.main_window, 'settings_widget'):
                self.main_window.settings_widget.save_settings()
                print("💾 Auto-saved calibration settings on app close")
            if self.main_window: self.main_window.close()
            if self.app: self.app.quit()
        else:
            self.console_ui.cleanup()
        print("✅ UI cleanup completed")

if __name__ == "__main__":
    def test_ui():
        config = {'enable_ui': True, 'debug': True}
        ui = JuggleHubUI(config)
        def generate_test_data():
            time.sleep(2)
            for i in range(100):
                frame_data = juggler_pb2.FrameData(timestamp_us=int(time.time() * 1000000), frame_number=i+1)
                ball = frame_data.balls.add(id=i, position={'x': 0.1 * (i % 10 - 5), 'y': 0.1 * ((i // 10) % 10 - 5), 'z': 0.8 + 0.1 * (i % 5)})
                ui.update_frame_data(frame_data)
                time.sleep(0.033)
        threading.Thread(target=generate_test_data, daemon=True).start()
        ui.run()
    
    print("🧪 Testing JuggleHub UI...")
    test_ui()