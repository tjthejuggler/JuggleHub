"""
UI Component

Provides a simple UI for visualizing juggling data from the engine.
"""

import sys
import time
import threading
from typing import Optional, Dict, Any
import os

# Add the api directory to the path for protobuf imports
api_path = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(__file__))), 'api', 'v1')
sys.path.insert(0, api_path)

try:
    import juggler_pb2
except ImportError:
    print("❌ Error: Protocol Buffer files not found. Please run 'make generate-proto' first.")
    sys.exit(1)

# Try to import PyQt6, fall back to console UI if not available
try:
    from PyQt6.QtWidgets import (QApplication, QMainWindow, QWidget, QVBoxLayout, 
                                QHBoxLayout, QLabel, QTextEdit, QPushButton, 
                                QGroupBox, QGridLayout, QProgressBar)
    from PyQt6.QtCore import QTimer, pyqtSignal, QObject, Qt
    from PyQt6.QtGui import QFont, QPalette, QColor
    PYQT_AVAILABLE = True
except ImportError:
    print("⚠️ PyQt6 not available. Using console UI.")
    PYQT_AVAILABLE = False


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
                print(f"  🏀 {ball.color_name}: "
                      f"3D({ball.position_3d.x:.3f}, {ball.position_3d.y:.3f}, {ball.position_3d.z:.3f}) "
                      f"2D({ball.position_2d.x:.0f}, {ball.position_2d.y:.0f}) "
                      f"conf:{ball.confidence:.2f}")
            
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
    
    class JuggleHubMainWindow(QMainWindow):
        """Main window for JuggleHub UI."""
        
        def __init__(self, config: dict):
            super().__init__()
            self.config = config
            self.frame_count = 0
            self.start_time = time.time()
            self.last_frame_data: Optional[juggler_pb2.FrameData] = None
            
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
            
            content_layout.addWidget(ball_group)
            
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
            
            # Update ball information
            ball_count = len(frame_data.balls)
            self.ball_count_label.setText(f"Balls detected: {ball_count}")
            
            ball_text = ""
            for ball in frame_data.balls:
                ball_text += f"{ball.color_name}: "
                ball_text += f"3D({ball.position_3d.x:.3f}, {ball.position_3d.y:.3f}, {ball.position_3d.z:.3f}) "
                ball_text += f"conf:{ball.confidence:.2f}\n"
            
            self.ball_list.setPlainText(ball_text)
            
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


class JuggleHubUI:
    """Main UI class that chooses between PyQt6 and console UI."""
    
    def __init__(self, config: dict):
        self.config = config
        
        if PYQT_AVAILABLE and config.get('enable_ui', True):
            # Create QApplication if it doesn't exist
            if not QApplication.instance():
                self.app = QApplication(sys.argv)
            else:
                self.app = QApplication.instance()
            
            self.main_window = JuggleHubMainWindow(config)
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
                ball.id = f"test_ball_{i}"
                ball.color_name = ["red", "green", "blue", "yellow"][i % 4]
                ball.position_3d.x = 0.1 * (i % 10 - 5)
                ball.position_3d.y = 0.1 * ((i // 10) % 10 - 5)
                ball.position_3d.z = 0.8 + 0.1 * (i % 5)
                ball.position_2d.x = 320 + 10 * (i % 20 - 10)
                ball.position_2d.y = 240 + 10 * ((i // 20) % 20 - 10)
                ball.confidence = 0.8 + 0.2 * (i % 5) / 5
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