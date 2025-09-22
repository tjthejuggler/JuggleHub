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
                                 QComboBox, QMessageBox, QDialog, QVBoxLayout as QVBoxLayout_Dialog)
    from PyQt6.QtCore import QTimer, pyqtSignal, QObject, Qt
    from PyQt6.QtGui import QFont, QPalette, QColor, QPixmap, QImage, QPen, QPainter, QKeySequence, QBrush
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
            self.init_ui()

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
                             "Range: 1 to 300. Default: 150.\n"
                             "Increase this for objects that get occluded for longer periods.",
                range_min=1,
                range_max=300,
                initial_value=150,
                update_func=lambda v: self.update_setting('track_buffer', v)
            )

            self.track_thresh_slider, self.track_thresh_value_label = self._create_slider_widget(
                parent_layout=bytetrack_layout,
                row=1,
                label_text="Track Threshold",
                tooltip_text="Confidence score needed to start a new track.\n"
                             "Range: 0.00 to 1.00. Default: 0.25.\n"
                             "This is the threshold for low-confidence detections.",
                range_min=0,
                range_max=100,
                initial_value=25,
                update_func=lambda v: self.update_setting('track_thresh', v / 100.0),
                is_float=True
            )

            self.high_thresh_slider, self.high_thresh_value_label = self._create_slider_widget(
                parent_layout=bytetrack_layout,
                row=2,
                label_text="High Confidence Threshold",
                tooltip_text="Confidence score for a detection to be considered 'high confidence'.\n"
                             "Range: 0.00 to 1.00. Default: 0.50.\n"
                             "These detections are matched first.",
                range_min=0,
                range_max=100,
                initial_value=50,
                update_func=lambda v: self.update_setting('high_thresh', v / 100.0),
                is_float=True
            )

            self.match_thresh_slider, self.match_thresh_value_label = self._create_slider_widget(
                parent_layout=bytetrack_layout,
                row=3,
                label_text="Match Threshold (IoU)",
                tooltip_text="The minimum Intersection over Union (IoU) to match a detection to a track.\n"
                             "Range: 0.00 to 1.00. Default: 0.70.\n"
                             "Lower values make it easier to maintain a track ID, even with jittery detections.",
                range_min=0,
                range_max=100,
                initial_value=70,
                update_func=lambda v: self.update_setting('match_thresh', v / 100.0),
                is_float=True
            )

            layout.addWidget(bytetrack_group)
            layout.addStretch()

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
                
                # Set default to 30 FPS if available, otherwise first option
                default_index = self.fps_combo.findText("30 FPS")
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
                else:
                    QMessageBox.critical(self, "Error", f"Failed to start camera: {response.message}")
                    print(f"❌ Failed to start camera: {response.message}")
            except Exception as e:
                QMessageBox.critical(self, "Error", f"Error starting camera: {str(e)}")
                print(f"❌ Error starting camera: {e}")

        def update_setting(self, key: str, value: Any):
            self.udp_client.send_setting(key, value)

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
            self.video_layout.addWidget(self.video_view)

            # --- NEW: Visualization Toggles ---
            toggles_layout = QHBoxLayout()
            self.show_raw_detections_toggle = QPushButton("Raw Detections")
            self.show_raw_detections_toggle.setCheckable(True)
            self.show_raw_detections_toggle.setChecked(False)
            self.show_raw_detections_toggle.clicked.connect(self.toggle_overlays)
            toggles_layout.addWidget(self.show_raw_detections_toggle)

            self.show_tracked_boxes_toggle = QPushButton("Tracked Boxes")
            self.show_tracked_boxes_toggle.setCheckable(True)
            self.show_tracked_boxes_toggle.setChecked(False)
            self.show_tracked_boxes_toggle.clicked.connect(self.toggle_overlays)
            toggles_layout.addWidget(self.show_tracked_boxes_toggle)

            self.show_3d_trackers_toggle = QPushButton("3D Trackers")
            self.show_3d_trackers_toggle.setCheckable(True)
            self.show_3d_trackers_toggle.setChecked(True) # Default to on
            self.show_3d_trackers_toggle.clicked.connect(self.toggle_overlays)
            toggles_layout.addWidget(self.show_3d_trackers_toggle)
            
            self.video_layout.addLayout(toggles_layout)
            
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
            for ball in frame_data.balls:
                class_name = f" ({ball.class_name})" if ball.class_name else ""
                ball_text += f"ID {ball.id}{class_name}: 3D({ball.position.x:.3f}, {ball.position.y:.3f}, {ball.position.z:.3f})\n"
            self.ball_list.setPlainText(ball_text)

            if self.calibration_mode and frame_data.color_image_b64:
                self.update_video_feed(frame_data)
            
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

        def toggle_overlays(self):
            if self.last_frame_data: self.update_video_feed(self.last_frame_data)

        def update_video_feed(self, frame_data: juggler_pb2.FrameData):
            image = QImage()
            image.loadFromData(frame_data.color_image_b64, "JPEG")
            pixmap = QPixmap.fromImage(image)
            painter = QPainter(pixmap)
            
            # --- Draw Raw Detections ---
            if self.show_raw_detections_toggle.isChecked():
                painter.setPen(QPen(QColor(255, 0, 0, 100), 2)) # Semi-transparent red
                for det in frame_data.raw_detections:
                    painter.drawRect(int(det.x), int(det.y), int(det.width), int(det.height))

            # --- Draw Tracked Boxes (from ByteTrack) ---
            if self.show_tracked_boxes_toggle.isChecked():
                painter.setPen(QPen(QColor(255, 165, 0, 150), 2, Qt.PenStyle.DashLine)) # Orange dash
                for obj in frame_data.balls:
                    bbox = obj.bounding_box_2d
                    painter.drawRect(int(bbox.x), int(bbox.y), int(bbox.width), int(bbox.height))

            # --- Draw 3D Trackers (Kalman Filtered) ---
            if self.show_3d_trackers_toggle.isChecked():
                colors = [QColor(255, 87, 34), QColor(255, 193, 7), QColor(139, 195, 74),
                          QColor(0, 188, 212), QColor(3, 169, 244), QColor(63, 81, 181)]
                for obj in frame_data.balls:
                    bbox = obj.bounding_box_2d
                    center_x, center_y = int(bbox.x + bbox.width / 2), int(bbox.y + bbox.height / 2)
                    color = colors[obj.id % len(colors)]
                    
                    painter.setBrush(QBrush(color))
                    painter.setPen(Qt.PenStyle.NoPen)
                    painter.drawEllipse(center_x - 5, center_y - 5, 10, 10)
                    
                    painter.setPen(QPen(QColor(255, 255, 255)))
                    painter.setFont(QFont("Arial", 10, QFont.Weight.Bold))
                    label = f"ID: {obj.id}"
                    pos_label = f"({obj.position.x:.2f}, {obj.position.y:.2f}, {obj.position.z:.2f})m"
                    painter.drawText(center_x + 10, center_y, label)
                    painter.drawText(center_x + 10, center_y + 15, pos_label)

            painter.end()
            self.video_pixmap_item.setPixmap(pixmap)
            self.video_view.fitInView(self.video_pixmap_item, Qt.AspectRatioMode.KeepAspectRatio)

        def record_clip(self):
            self.log_message("Sending record command to engine...")
            command = juggler_pb2.CommandRequest(type=juggler_pb2.CommandRequest.CommandType.RECORD_START)
            try:
                response = self.zmq_client.send_command(command)
                self.log_message(f"✅ Record command acknowledged: {response.message}" if response.success else f"❌ Record command failed: {response.message}")
            except Exception as e:
                self.log_message(f"❌ Error sending record command: {e}")

        def toggle_continuous_recording(self):
            is_starting = not self.is_continuous_recording
            self.log_message(f"{'Starting' if is_starting else 'Stopping'} continuous recording...")
            command_type = juggler_pb2.CommandRequest.CommandType.RECORD_CONTINUOUS_START if is_starting else juggler_pb2.CommandRequest.CommandType.RECORD_CONTINUOUS_STOP
            command = juggler_pb2.CommandRequest(type=command_type)
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