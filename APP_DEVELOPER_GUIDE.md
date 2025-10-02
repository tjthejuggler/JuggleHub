# JuggleHub App Developer Guide

**Last Updated**: 2025-10-02

This guide provides complete instructions for developing apps for the JuggleHub platform. Apps are independent applications that run in separate windows and communicate with the JuggleHub engine via ZeroMQ.

---

## Table of Contents

1. [Overview](#overview)
2. [Architecture](#architecture)
3. [Quick Start](#quick-start)
4. [App Structure](#app-structure)
5. [BaseApp API Reference](#baseapp-api-reference)
6. [AppAPI Reference](#appapi-reference)
7. [Protocol Buffer Messages](#protocol-buffer-messages)
8. [Best Practices](#best-practices)
9. [Debugging](#debugging)
10. [Example Apps](#example-apps)

---

## Overview

### What are JuggleHub Apps?

JuggleHub apps are independent PyQt6 applications that:
- Run in separate windows (can be moved to different monitors)
- Receive real-time data from the tracking engine via ZeroMQ
- Can control engine features (enable/disable tracking components)
- Have full access to frame data, ball positions, hand tracking, IMU data, etc.
- Are automatically discovered and managed by the hub

### Use Cases

- **Analytics**: Track catches, throws, patterns, statistics
- **Visualization**: Custom 3D views, trajectory plots, heatmaps
- **Training**: Real-time feedback, form analysis, coaching tools
- **Recording**: Custom video recording with overlays
- **Games**: Interactive juggling games and challenges

---

## Architecture

### System Components

```
┌─────────────────────────────────────────────────────────────┐
│                        JuggleHub Hub                         │
│  ┌────────────┐  ┌──────────────┐  ┌──────────────────┐   │
│  │ Main UI    │  │ App Manager  │  │ ZMQ Context      │   │
│  └────────────┘  └──────────────┘  └──────────────────┘   │
└─────────────────────────────────────────────────────────────┘
                            │
                            │ ZMQ (tcp://localhost:5555 & 5565)
                            │
┌─────────────────────────────────────────────────────────────┐
│                     C++ Tracking Engine                      │
│  ┌────────────┐  ┌──────────────┐  ┌──────────────────┐   │
│  │ Camera     │  │ YOLO Tracker │  │ Pose Estimation  │   │
│  │ RealSense  │  │ ByteTrack    │  │ Throw/Catch Det. │   │
│  └────────────┘  └──────────────┘  └──────────────────┘   │
└─────────────────────────────────────────────────────────────┘
                            │
                            │ ZMQ Publish
                            ↓
┌─────────────────────────────────────────────────────────────┐
│                      Your App (PyQt6)                        │
│  ┌────────────┐  ┌──────────────┐  ┌──────────────────┐   │
│  │ App Window │  │ AppAPI       │  │ Business Logic   │   │
│  │ (QMainWin) │  │ (ZMQ Client) │  │ (Your Code)      │   │
│  └────────────┘  └──────────────┘  └──────────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

### Communication Flow

1. **Data Stream**: Engine publishes `FrameData` messages via ZMQ PUB socket (port 5555)
2. **Commands**: Apps send commands via ZMQ REQ socket (port 5565)
3. **Thread Safety**: AppAPI handles threading, your app receives callbacks on background thread
4. **UI Updates**: Use Qt signals to update UI from background thread

---

## Quick Start

### 1. Create App Directory

```bash
mkdir -p hub/apps/my_app
cd hub/apps/my_app
```

### 2. Create `metadata.json`

```json
{
  "id": "my_app",
  "name": "My Awesome App",
  "version": "1.0.0",
  "description": "A brief description of what your app does",
  "author": "Your Name",
  "category": "analytics",
  "icon": "📊",
  "entry_point": "apps.my_app.app:MyApp",
  "required_features": [
    "throw_catch_detection"
  ]
}
```

### 3. Create `__init__.py`

```python
"""My App Package"""
```

### 4. Create `app.py`

```python
"""
My Awesome App

Description of your app.
"""

from PyQt6.QtWidgets import QMainWindow, QWidget, QVBoxLayout, QLabel
from PyQt6.QtCore import Qt, pyqtSignal, QObject
from PyQt6.QtGui import QFont
from apps.base import BaseApp


class DataSignal(QObject):
    """Signal emitter for thread-safe UI updates."""
    data_received = pyqtSignal(object)


class MyApp(BaseApp):
    """My awesome application."""
    
    def get_metadata(self) -> dict:
        """Return app metadata."""
        return {
            "id": "my_app",
            "name": "My Awesome App",
            "version": "1.0.0"
        }
    
    def initialize(self):
        """Initialize the app."""
        self.signal_emitter = DataSignal()
        self.signal_emitter.data_received.connect(self._update_ui)
        
        # Enable required features
        self.api.enable_feature("throw_catch_detection")
    
    def create_window(self) -> QMainWindow:
        """Create and return the app window."""
        window = QMainWindow()
        window.setWindowTitle("My Awesome App")
        window.setGeometry(100, 100, 600, 400)
        
        # Create UI
        central = QWidget()
        window.setCentralWidget(central)
        layout = QVBoxLayout(central)
        
        self.label = QLabel("Waiting for data...")
        self.label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(self.label)
        
        # Apply dark theme
        window.setStyleSheet("""
            QMainWindow, QWidget {
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
        # Emit signal for thread-safe UI update
        self.signal_emitter.data_received.emit(frame_data)
    
    def _update_ui(self, frame_data):
        """Update UI (called on main thread)."""
        ball_count = len(frame_data.balls)
        self.label.setText(f"Tracking {ball_count} balls")
    
    def cleanup(self):
        """Clean up resources."""
        self.api.disable_feature("throw_catch_detection")
```

### 5. Test Your App

1. Restart the hub: `./scripts/run_hub.sh --use-venv --device GPU`
2. Open App Manager: Click `App → App Manager...` (or press Ctrl+M)
3. Your app should appear in the grid
4. Click "Launch" to test it

---

## App Structure

### Required Files

```
hub/apps/my_app/
├── __init__.py          # Python package marker
├── metadata.json        # App metadata (required)
└── app.py              # Main app code (required)
```

### metadata.json Schema

```json
{
  "id": "string",              // Unique identifier (lowercase, underscores)
  "name": "string",            // Display name
  "version": "string",         // Semantic version (e.g., "1.0.0")
  "description": "string",     // Brief description (1-2 sentences)
  "author": "string",          // Your name or organization
  "category": "string",        // Category: analytics, visualization, training, etc.
  "icon": "string",            // Emoji icon for display
  "entry_point": "string",     // Module path: "apps.my_app.app:MyAppClass"
  "required_features": [       // Engine features to enable
    "throw_catch_detection",
    "pose_estimation"
  ]
}
```

### Available Engine Features

- `"dnn_tracking"` - YOLO object detection
- `"pose_estimation"` - Human pose tracking
- `"throw_catch_detection"` - Throw/catch event detection
- `"color_tracking"` - Color-based ball tracking

---

## BaseApp API Reference

### Abstract Methods (Must Implement)

#### `get_metadata() -> dict`

Return app metadata dictionary.

```python
def get_metadata(self) -> dict:
    return {
        "id": "my_app",
        "name": "My App",
        "version": "1.0.0"
    }
```

#### `initialize()`

Initialize app state, create signal emitters, enable features.

```python
def initialize(self):
    self.counter = 0
    self.signal_emitter = MySignal()
    self.api.enable_feature("throw_catch_detection")
```

#### `create_window() -> QMainWindow`

Create and return the app's main window. Do NOT call `show()` - the framework handles this.

```python
def create_window(self) -> QMainWindow:
    window = QMainWindow()
    window.setWindowTitle("My App")
    # ... create UI ...
    return window
```

#### `on_frame_data(frame_data: FrameData)`

Process incoming frame data. Called on background thread - use signals for UI updates!

```python
def on_frame_data(self, frame_data):
    # Process data
    ball_count = len(frame_data.balls)
    
    # Emit signal for UI update
    self.signal_emitter.update.emit(ball_count)
```

### Optional Methods

#### `cleanup()`

Clean up resources when app stops. Disable features, close connections, etc.

```python
def cleanup(self):
    self.api.disable_feature("throw_catch_detection")
    # Close any open files, connections, etc.
```

### Provided Methods (Do Not Override)

#### `start()`

Starts the app. Called by AppManager. Do not override.

#### `stop()`

Stops the app. Called by AppManager. Do not override.

### Properties

- `self.api: AppAPI` - API for communicating with engine
- `self.window: QMainWindow` - The app's main window (set after `create_window()`)
- `self._running: bool` - Whether app is currently running

---

## AppAPI Reference

### Data Streaming

#### `subscribe_to_data(callback: Callable)`

Subscribe to frame data stream from engine.

```python
def initialize(self):
    # Subscription is automatic - just implement on_frame_data()
    pass
```

#### `unsubscribe_from_data()`

Unsubscribe from data stream. Called automatically on app stop.

### Feature Control

#### `enable_feature(feature_name: str) -> bool`

Enable an engine feature.

```python
success = self.api.enable_feature("throw_catch_detection")
if success:
    print("Feature enabled")
```

#### `disable_feature(feature_name: str) -> bool`

Disable an engine feature.

```python
success = self.api.disable_feature("throw_catch_detection")
```

### Custom Commands

#### `send_command(command: CommandRequest) -> CommandResponse`

Send a custom command to the engine.

```python
import juggler_pb2

command = juggler_pb2.CommandRequest()
command.type = juggler_pb2.CommandRequest.CommandType.CAMERA_START
response = self.api.send_command(command)

if response.success:
    print(f"Success: {response.message}")
```

---

## Protocol Buffer Messages

### FrameData

Main message containing all tracking data for a single frame.

```python
frame_data.frame_number        # int: Frame number
frame_data.timestamp_us        # int: Timestamp in microseconds
frame_data.balls               # List[Ball]: Tracked balls
frame_data.hands               # List[Hand]: Detected hands
frame_data.imu_data            # List[IMUData]: IMU sensor data
frame_data.throw_catch_events  # List[ThrowCatchEvent]: Throw/catch events
frame_data.color_image_b64     # bytes: JPEG-encoded color image
```

### Ball

Ball tracking data.

```python
ball.id                    # int: Tracker ID
ball.logical_id            # int: Persistent logical ID
ball.position              # Point3D: 3D position (x, y, z in meters)
ball.velocity              # Point3D: 3D velocity (m/s)
ball.projected_pos_2d      # Point2D: 2D pixel position
ball.bounding_box_2d       # BoundingBox2D: 2D bounding box
ball.confidence            # float: Detection confidence
ball.status                # enum: TRACKED, PREDICTED, OCCLUDED
```

### Hand

Hand tracking data.

```python
hand.side                  # string: "left" or "right"
hand.position_2d           # Point2D: Wrist position in pixels
hand.position_3d           # Point3D: Wrist position in 3D (meters)
hand.keypoints             # List[Keypoint]: Body keypoints
hand.confidence            # float: Detection confidence
```

### ThrowCatchEvent

Throw or catch event.

```python
event.event_type           # enum: THROW or CATCH
event.ball_id              # int: Ball logical ID
event.hand_side            # string: "left" or "right"
event.timestamp_us         # int: Event timestamp
event.confidence           # float: Event confidence
```

### IMUData

IMU sensor data from smartwatch.

```python
imu.watch_name             # string: Watch identifier
imu.watch_ip               # string: Watch IP address
imu.acceleration           # Point3D: Acceleration (m/s²)
imu.gyroscope              # Point3D: Angular velocity (rad/s)
imu.timestamp_us           # int: Timestamp
```

---

## Best Practices

### Thread Safety

**CRITICAL**: `on_frame_data()` is called on a background thread. Never update UI directly!

❌ **Wrong**:
```python
def on_frame_data(self, frame_data):
    self.label.setText("Update")  # CRASH! Wrong thread!
```

✅ **Correct**:
```python
class MySignal(QObject):
    update = pyqtSignal(str)

def initialize(self):
    self.signal = MySignal()
    self.signal.update.connect(self._update_label)

def on_frame_data(self, frame_data):
    self.signal.update.emit("Update")  # Safe!

def _update_label(self, text):
    self.label.setText(text)  # Called on main thread
```

### Performance

- **Filter Data**: Only process data you need
- **Throttle Updates**: Don't update UI every frame if not necessary
- **Use Efficient Data Structures**: Avoid O(n²) operations in `on_frame_data()`

```python
def initialize(self):
    self.frame_count = 0
    self.update_interval = 10  # Update UI every 10 frames

def on_frame_data(self, frame_data):
    self.frame_count += 1
    
    # Only update UI every 10 frames
    if self.frame_count % self.update_interval == 0:
        self.signal.update.emit(frame_data)
```

### Resource Management

Always clean up in `cleanup()`:

```python
def cleanup(self):
    # Disable features
    self.api.disable_feature("throw_catch_detection")
    
    # Close files
    if hasattr(self, 'log_file'):
        self.log_file.close()
    
    # Stop timers
    if hasattr(self, 'timer'):
        self.timer.stop()
```

### Error Handling

Handle errors gracefully:

```python
def on_frame_data(self, frame_data):
    try:
        # Process data
        result = self.process(frame_data)
        self.signal.update.emit(result)
    except Exception as e:
        print(f"Error processing frame: {e}")
        # Don't crash the app!
```

---

## Debugging

### Enable Debug Logging

```python
def initialize(self):
    self.debug = True

def on_frame_data(self, frame_data):
    if self.debug:
        print(f"Frame {frame_data.frame_number}: {len(frame_data.balls)} balls")
```

### Check Feature Enablement

```python
def initialize(self):
    success = self.api.enable_feature("throw_catch_detection")
    if not success:
        print("⚠️ Failed to enable throw_catch_detection")
```

### Verify Data Reception

```python
def initialize(self):
    self.frame_count = 0

def on_frame_data(self, frame_data):
    self.frame_count += 1
    if self.frame_count % 30 == 0:  # Every second at 30 FPS
        print(f"Received {self.frame_count} frames")
```

### Common Issues

1. **App doesn't appear in App Manager**
   - Check `metadata.json` exists and is valid JSON
   - Verify `entry_point` path is correct
   - Ensure `__init__.py` exists

2. **App doesn't launch**
   - Check console for import errors
   - Verify all imports use `apps.` prefix (not `hub.apps.`)
   - Ensure BaseApp is imported correctly

3. **UI doesn't update**
   - Check you're using Qt signals for thread-safe updates
   - Verify signal is connected in `initialize()`
   - Make sure you're emitting signals, not updating UI directly

4. **No data received**
   - Check engine is running and publishing data
   - Verify ZMQ ports are correct (5555 for data, 5565 for commands)
   - Ensure `on_frame_data()` is implemented

---

## Example Apps

### Catch Counter (Reference Implementation)

Location: `hub/apps/catch_counter/`

Features:
- Counts catch events
- Large display with restart button
- Thread-safe UI updates
- Feature control (enables throw_catch_detection)

### Creating More Apps

Ideas for apps you can build:

1. **Pattern Analyzer**: Detect and classify juggling patterns (cascade, fountain, etc.)
2. **Throw Height Tracker**: Track maximum throw heights and display statistics
3. **Drop Counter**: Count and log drops with timestamps
4. **3D Visualizer**: Real-time 3D visualization of ball trajectories
5. **Training Timer**: Timed practice sessions with goals and feedback
6. **Video Recorder**: Custom recording with overlays and annotations
7. **Metronome**: Audio/visual metronome synced to throw timing
8. **Heatmap**: Visualize where balls spend most time
9. **Multiplayer**: Compare stats with other jugglers in real-time
10. **AI Coach**: Provide real-time feedback on form and technique

---

## API Changelog

### Version 1.0.0 (2025-10-02)

- Initial release
- BaseApp abstract class
- AppAPI with ZMQ communication
- AppManager with discovery and lifecycle
- Protocol Buffer integration
- Feature control API
- Catch Counter reference app

---

## Support

For questions or issues:
1. Check this guide first
2. Review the Catch Counter example app
3. Check console output for error messages
4. Review `APP_LAYER_ARCHITECTURE.md` for system design

---

**Happy App Development!** 🎯