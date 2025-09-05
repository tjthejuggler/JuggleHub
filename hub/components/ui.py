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
                                 QGraphicsScene, QGraphicsPixmapItem, QSlider, QLineEdit)
    from PyQt6.QtCore import QTimer, pyqtSignal, QObject, Qt
    from PyQt6.QtGui import QFont, QPalette, QColor, QPixmap, QImage, QPen, QPainter, QKeySequence
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
        def __init__(self, udp_client: UdpClient, parent=None):
            super().__init__(parent)
            self.udp_client = udp_client
            self.init_ui()

        def init_ui(self):
            layout = QVBoxLayout(self)
            
            # -- DNN Tracker Settings --
            dnn_group = QGroupBox("DNN Tracker Settings")
            dnn_layout = QGridLayout(dnn_group)
            
            # Confidence Threshold
            dnn_layout.addWidget(QLabel("Confidence Threshold:"), 0, 0)
            self.confidence_slider = QSlider(Qt.Orientation.Horizontal)
            self.confidence_slider.setRange(0, 100)
            self.confidence_slider.setValue(25)
            self.confidence_slider.valueChanged.connect(
                lambda v: self.update_setting('confidence_threshold', v / 100.0))
            dnn_layout.addWidget(self.confidence_slider, 0, 1)

            # NMS Threshold
            dnn_layout.addWidget(QLabel("NMS Threshold:"), 1, 0)
            self.nms_slider = QSlider(Qt.Orientation.Horizontal)
            self.nms_slider.setRange(0, 100)
            self.nms_slider.setValue(50)
            self.nms_slider.valueChanged.connect(
                lambda v: self.update_setting('nms_threshold', v / 100.0))
            dnn_layout.addWidget(self.nms_slider, 1, 1)

            layout.addWidget(dnn_group)
            layout.addStretch()

        def update_setting(self, key: str, value: Any):
            self.udp_client.send_setting(key, value)

    class JuggleHubMainWindow(QMainWindow):
        """Main window for JuggleHub UI."""
        
        def __init__(self, config: dict, zmq_client: 'ZMQClient'):
            super().__init__()
            self.config = config
            self.zmq_client = zmq_client
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

            # Add checkbox for raw detections
            self.show_raw_detections_checkbox = QPushButton("Show Raw Detections", self)
            self.show_raw_detections_checkbox.setCheckable(True)
            self.show_raw_detections_checkbox.setChecked(False)
            self.show_raw_detections_checkbox.clicked.connect(self.toggle_raw_detections)
            self.video_layout.addWidget(self.show_raw_detections_checkbox)
            
            content_layout.addWidget(self.video_group, 2) # Give it more stretch factor
            
            # Calibration settings panel
            self.settings_widget = CalibrationSettingsWidget(self.udp_client)
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
            
            # Apply dark theme
            self.apply_dark_theme()
            
            # Enable keyboard focus for the main window
            self.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
        
        def apply_dark_theme(self):
            """Apply a dark theme to the UI."""
            self.setStyleSheet("""
                QMainWindow {
                    background-color: #2b2b2b;
                    color: #ffffff;
                }
                QGroupBox {
                    font-weight: bold;
                    border: 2px solid #555555;
                    border-radius: 5px;
                    margin-top: 1ex;
                    padding-top: 10px;
                }
                QGroupBox::title {
                    subcontrol-origin: margin;
                    left: 10px;
                    padding: 0 5px 0 5px;
                }
                QLabel {
                    color: #ffffff;
                }
                QTextEdit {
                    background-color: #1e1e1e;
                    border: 1px solid #555555;
                    color: #ffffff;
                }
            """)
        
        def update_frame_data(self, frame_data: juggler_pb2.FrameData):
            """Update with new frame data (called from worker thread)."""
            self.signal_emitter.frame_received.emit(frame_data)
        
        def _update_ui(self, frame_data: juggler_pb2.FrameData):
            """Update UI with new frame data (called from main thread)."""
            self.last_frame_data = frame_data
            self.frame_count += 1

            self.log_message(f"UI received frame {frame_data.frame_number} with {len(frame_data.balls)} balls and "
                             f"{len(frame_data.raw_detections)} raw detections.")
            
            # Update ball information
            ball_count = len(frame_data.balls)
            self.ball_count_label.setText(f"Balls detected: {ball_count}")
            
            ball_text = ""
            if not frame_data.balls and frame_data.raw_detections:
                ball_text = "Raw detections present, but no tracked balls.\n"
                
            for ball in frame_data.balls:
                ball_text += f"ID {ball.id}: "
                ball_text += f"3D({ball.position.x:.3f}, {ball.position.y:.3f}, {ball.position.z:.3f})\n"
            
            self.ball_list.setPlainText(ball_text)

            # Update video feed if in calibration mode
            if self.calibration_mode and frame_data.color_image_b64:
                self.update_video_feed(frame_data)
            
            # Update system status
            if frame_data.HasField('status'):
                status = frame_data.status
                self.camera_status.setText(f"📷 Camera: {'Connected' if status.camera_connected else 'Disconnected'}")
                self.engine_status.setText(f"🔧 Engine: {'Running' if status.engine_running else 'Stopped'}")
                self.mode_status.setText(f"🎯 Mode: {status.mode}")
                
                if status.error_message:
                    self.log_message(f"❌ Error: {status.error_message}")
            
            # Update hand status
            hand_count = len(frame_data.hands)
            self.hand_status.setText(f"👋 Hands: {hand_count}")
            
            # Update IMU status
            imu_count = len(frame_data.imu_data)
            self.imu_status.setText(f"📱 IMU: {imu_count} sensors")

            imu_text = ""
            for imu in frame_data.imu_data:
                imu_text += f"Watch: {imu.watch_name} ({imu.watch_ip})\n"
                imu_text += f"  Accel: ({imu.acceleration.x:.2f}, {imu.acceleration.y:.2f}, {imu.acceleration.z:.2f})\n"
                imu_text += f"  Gyro:  ({imu.gyroscope.x:.2f}, {imu.gyroscope.y:.2f}, {imu.gyroscope.z:.2f})\n"
            
            self.imu_list.setPlainText(imu_text)
            
            # Update status
            self.status_label.setText(f"✅ Receiving data - Frame {frame_data.frame_number}")
        
        def _periodic_update(self):
            """Periodic UI updates."""
            # Calculate FPS
            elapsed = time.time() - self.start_time
            fps = self.frame_count / elapsed if elapsed > 0 else 0
            
            self.fps_label.setText(f"FPS: {fps:.1f}")
            self.frame_count_label.setText(f"Frames: {self.frame_count}")
            
            # Check if we're still receiving data
            if self.last_frame_data:
                current_time = time.time() * 1000000  # Convert to microseconds
                time_since_last = (current_time - self.last_frame_data.timestamp_us) / 1000000
                
                if time_since_last > 2.0:  # No data for 2 seconds
                    self.status_label.setText("⚠️ No data received recently")
        
        def log_message(self, message: str):
            """Add a message to the activity log."""
            timestamp = time.strftime("%H:%M:%S")
            self.log_text.append(f"[{timestamp}] {message}")
            
            # Keep log size manageable
            if self.log_text.document().blockCount() > 100:
                cursor = self.log_text.textCursor()
                cursor.movePosition(cursor.MoveOperation.Start)
                cursor.select(cursor.SelectionType.BlockUnderCursor)
                cursor.removeSelectedText()
        
        def toggle_calibration_mode(self):
            """Toggle the visibility of the calibration video feed."""
            self.calibration_mode = not self.calibration_mode
            self.video_group.setVisible(self.calibration_mode)
            self.settings_widget.setVisible(self.calibration_mode)
            if self.calibration_mode:
                self.calibration_button.setText("Exit Calibration Mode")
            else:
                self.calibration_button.setText("Enter Calibration Mode")

        def toggle_raw_detections(self):
            """Force a UI update when the checkbox is toggled."""
            if self.last_frame_data:
                self._update_ui(self.last_frame_data)

        def update_video_feed(self, frame_data: juggler_pb2.FrameData):
            """Update the video feed with the new frame and bounding boxes."""
            image = QImage()
            image.loadFromData(frame_data.color_image_b64, "JPEG")
            pixmap = QPixmap.fromImage(image)

            painter = QPainter(pixmap)
            
            # Draw raw detections if checkbox is ticked
            if self.show_raw_detections_checkbox.isChecked():
                pen_raw = QPen(QColor(255, 0, 0, 100), 2)  # Semi-transparent red
                painter.setPen(pen_raw)
                for det in frame_data.raw_detections:
                    painter.drawRect(int(det.x), int(det.y), int(det.width), int(det.height))
                    # Render confidence score
                    painter.drawText(int(det.x), int(det.y) - 5, f"{det.confidence:.2f}")

            # Draw final tracked balls
            pen_tracked = QPen(QColor(0, 255, 0), 2)  # Solid green
            painter.setPen(pen_tracked)
            for ball in frame_data.balls:
                bbox = ball.bounding_box
                painter.drawRect(int(bbox.x), int(bbox.y), int(bbox.width), int(bbox.height))
                painter.drawText(int(bbox.x), int(bbox.y) + int(bbox.height) + 15, f"ID: {ball.id}")

            painter.end()

            self.video_pixmap_item.setPixmap(pixmap)
            self.video_view.fitInView(self.video_pixmap_item, Qt.AspectRatioMode.KeepAspectRatio)


        def record_clip(self):
            """Send a command to the engine to record a clip."""
            self.log_message("Sending record command to engine...")
            command = juggler_pb2.CommandRequest()
            command.type = juggler_pb2.CommandRequest.CommandType.RECORD_START
            
            try:
                response = self.zmq_client.send_command(command)
                if response.success:
                    self.log_message(f"✅ Record command acknowledged: {response.message}")
                else:
                    self.log_message(f"❌ Record command failed: {response.message}")
            except Exception as e:
                self.log_message(f"❌ Error sending record command: {e}")

        def toggle_continuous_recording(self):
            """Toggle continuous recording on/off."""
            if not self.is_continuous_recording:
                # Start continuous recording
                self.log_message("Starting continuous recording...")
                command = juggler_pb2.CommandRequest()
                command.type = juggler_pb2.CommandRequest.CommandType.RECORD_CONTINUOUS_START
                
                try:
                    response = self.zmq_client.send_command(command)
                    if response.success:
                        self.is_continuous_recording = True
                        self.continuous_record_button.setText("Stop Recording")
                        self.continuous_record_button.setChecked(True)
                        self.recording_status.setText("● Recording")
                        self.recording_status.setStyleSheet("color: #f44336; font-weight: bold;")
                        self.log_message(f"✅ Continuous recording started: {response.message}")
                    else:
                        self.log_message(f"❌ Failed to start continuous recording: {response.message}")
                except Exception as e:
                    self.log_message(f"❌ Error starting continuous recording: {e}")
            else:
                # Stop continuous recording
                self.log_message("Stopping continuous recording...")
                command = juggler_pb2.CommandRequest()
                command.type = juggler_pb2.CommandRequest.CommandType.RECORD_CONTINUOUS_STOP
                
                try:
                    response = self.zmq_client.send_command(command)
                    if response.success:
                        self.is_continuous_recording = False
                        self.continuous_record_button.setText("Start Recording")
                        self.continuous_record_button.setChecked(False)
                        self.recording_status.setText("● Not Recording")
                        self.recording_status.setStyleSheet("color: #666666; font-weight: bold;")
                        self.log_message(f"✅ Continuous recording stopped: {response.message}")
                    else:
                        self.log_message(f"❌ Failed to stop continuous recording: {response.message}")
                except Exception as e:
                    self.log_message(f"❌ Error stopping continuous recording: {e}")
        
        def keyPressEvent(self, event):
            """Handle keyboard events."""
            if event.key() == Qt.Key.Key_R:
                self.log_message("🎹 'R' key pressed - triggering recording")
                self.record_clip()
            else:
                # Pass other key events to the parent
                super().keyPressEvent(event)


class JuggleHubUI:
    """Main UI class that chooses between PyQt6 and console UI."""
    
    def __init__(self, config: dict, zmq_client: Optional['ZMQClient'] = None):
        self.config = config
        self.zmq_client = zmq_client
        
        if PYQT_AVAILABLE and config.get('enable_ui', True):
            # Create QApplication if it doesn't exist
            if not QApplication.instance():
                self.app = QApplication(sys.argv)
            else:
                self.app = QApplication.instance()
            
            self.main_window = JuggleHubMainWindow(config, self.zmq_client)
            self.ui_type = "pyqt6"
        else:
            self.console_ui = ConsoleUI(config)
            self.ui_type = "console"
            self.app = None
            self.main_window = None
    
    def update_frame_data(self, frame_data: juggler_pb2.FrameData):
        """Update with new frame data."""
        if self.ui_type == "pyqt6":
            self.main_window.update_frame_data(frame_data)
        else:
            self.console_ui.update_frame_data(frame_data)
    
    def run(self):
        """Run the UI."""
        if self.ui_type == "pyqt6":
            print("🖥️ Starting PyQt6 UI...")
            self.main_window.show()
            self.app.exec()
        else:
            self.console_ui.run()
    
    def cleanup(self):
        """Clean up UI resources."""
        print("🧹 Cleaning up UI...")
        
        if self.ui_type == "pyqt6":
            if self.main_window:
                self.main_window.close()
            if self.app:
                self.app.quit()
        else:
            self.console_ui.cleanup()
        
        print("✅ UI cleanup completed")


# Example usage and testing
if __name__ == "__main__":
    import threading
    
    def test_ui():
        """Test the UI with simulated data."""
        config = {
            'enable_ui': True,
            'debug': True
        }
        
        ui = JuggleHubUI(config)
        
        # Create test data in a separate thread
        def generate_test_data():
            time.sleep(2)  # Wait for UI to start
            
            for i in range(100):
                # Create test frame data
                frame_data = juggler_pb2.FrameData()
                frame_data.timestamp_us = int(time.time() * 1000000)
                frame_data.frame_number = i + 1
                frame_data.frame_width = 640
                frame_data.frame_height = 480
                
                # Add test ball
                ball = frame_data.balls.add()
                ball.id = i # Assign a simple track ID for testing
                ball.position.x = 0.1 * (i % 10 - 5)
                ball.position.y = 0.1 * ((i // 10) % 10 - 5)
                ball.position.z = 0.8 + 0.1 * (i % 5)
                ball.timestamp_us = frame_data.timestamp_us
                
                # Set system status
                frame_data.status.camera_connected = True
                frame_data.status.engine_running = True
                frame_data.status.fps = 30.0
                frame_data.status.mode = "tracking"
                
                ui.update_frame_data(frame_data)
                time.sleep(0.033)  # ~30 FPS
        
        # Start test data generation
        test_thread = threading.Thread(target=generate_test_data, daemon=True)
        test_thread.start()
        
        # Run UI
        ui.run()
    
    print("🧪 Testing JuggleHub UI...")
    test_ui()