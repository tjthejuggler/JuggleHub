
# JuggleHub App Layer Architecture

**Last Updated:** 2025-10-02 18:42:00 CEST

## 🎯 Overview

The JuggleHub App Layer provides a plugin architecture for building independent applications that leverage the real-time juggling tracking engine. Apps run as separate windows, can be placed on different monitors, and have full control over engine features to minimize latency and maximize performance.

## 🏗️ Architecture Design

### Core Principles

1. **Independent Windows**: Apps run in separate OS windows (PyQt6 QMainWindow) that can be moved to different monitors and minimized independently
2. **Low Latency**: Apps subscribe to real-time data streams via ZMQ with minimal overhead
3. **Feature Control**: Apps can enable/disable engine features (DNN tracking, pose estimation, pattern recognition, etc.) to optimize performance
4. **Simple API**: Clean, high-level API abstracts ZMQ/Protobuf complexity
5. **Hot Reload**: Apps can be started/stopped without restarting the engine

### System Components

```
┌─────────────────────────────────────────────────────────────┐
│                     JuggleHub Main Window                    │
│  ┌────────────┐  ┌──────────────────────────────────────┐  │
│  │ File Menu  │  │ App Menu                              │  │
│  │            │  │  • Recent Apps                        │  │
│  │            │  │  • ─────────────                      │  │
│  │            │  │  • App Manager...                     │  │
│  └────────────┘  └──────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
                              │
                              │ Opens
                              ▼
                    ┌──────────────────┐
                    │  App Manager     │
                    │  ┌────────────┐  │
                    │  │ Catch      │  │ ◄── App Cards with
                    │  │ Counter    │  │     descriptions
                    │  │ [Launch]   │  │
                    │  ├────────────┤  │
                    │  │ Pattern    │  │
                    │  │ Analyzer   │  │
                    │  │ [Launch]   │  │
                    │  └────────────┘  │
                    └──────────────────┘
                              │
                              │ Launches
                              ▼
                    ┌──────────────────┐
                    │  Catch Counter   │  ◄── Independent
                    │  App Window      │      Window
                    │                  │
                    │  Catches: 42     │
                    │  [Restart]       │
                    └──────────────────┘
```

### Data Flow Architecture

```
┌──────────────────────────────────────────────────────────────┐
│                    C++ Engine Process                         │
│  ┌────────────┐  ┌──────────────┐  ┌──────────────────┐    │
│  │  Camera    │→ │  Trackers    │→ │  ZMQ Publisher   │    │
│  │  (D455)    │  │  (DNN/Color) │  │  (Frame Data)    │────┼──┐
│  └────────────┘  └──────────────┘  └──────────────────┘    │  │
│                         ▲                     ▲              │  │
│                         │                     │              │  │
│                  ┌──────┴─────────┐    ┌─────┴──────┐      │  │
│                  │ Feature Control │    │  Commands  │      │  │
│                  │   (Enable/      │    │  (ZMQ Req) │◄─────┼──┼──┐
│                  │    Disable)     │    └────────────┘      │  │  │
│                  └────────────────┘                         │  │  │
└──────────────────────────────────────────────────────────────┘  │  │
                                                                   │  │
┌──────────────────────────────────────────────────────────────┐  │  │
│                    Python Hub Process                         │  │  │
│  ┌────────────────────────────────────────────────────────┐  │  │  │
│  │              App Manager (hub/apps/manager.py)         │  │  │  │
│  │  • App Registry                                        │  │  │  │
│  │  • App Lifecycle Management                            │  │  │  │
│  │  • Window Management                                   │  │  │  │
│  └────────────────────────────────────────────────────────┘  │  │  │
│                         │                                     │  │  │
│                         │ Spawns                              │  │  │
│                         ▼                                     │  │  │
│  ┌────────────────────────────────────────────────────────┐  │  │  │
│  │         App Instance (Separate QMainWindow)            │  │  │  │
│  │  ┌──────────────────────────────────────────────────┐  │  │  │  │
│  │  │  AppAPI (hub/apps/api.py)                        │  │  │  │  │
│  │  │  • subscribe_to_data()                           │  │  │  │  │
│  │  │  • enable_feature()                              │──┼──┼──┼──┘
│  │  │  • disable_feature()                             │  │  │  │
│  │  │  • send_command()                                │  │  │  │
│  │  └──────────────────────────────────────────────────┘  │  │  │
│  │                         │                               │  │  │
│  │                         │ Uses                          │  │  │
│  │                         ▼                               │  │  │
│  │  ┌──────────────────────────────────────────────────┐  │  │  │
│  │  │  ZMQ Subscriber (Receives Frame Data)            │◄─┼──┼──┘
│  │  └──────────────────────────────────────────────────┘  │  │
│  └────────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────┘
```

## 📁 Directory Structure

```
JuggleHub/
├── hub/
│   ├── apps/
│   │   ├── __init__.py
│   │   ├── api.py                    # AppAPI - High-level app interface
│   │   ├── manager.py                # App Manager - Registry & lifecycle
│   │   ├── base.py                   # BaseApp - Abstract base class
│   │   ├── registry.json             # App metadata registry
│   │   │
│   │   ├── catch_counter/            # Example app
│   │   │   ├── __init__.py
│   │   │   ├── app.py                # Main app implementation
│   │   │   ├── ui.py                 # App UI (QMainWindow)
│   │   │   └── metadata.json         # App metadata
│   │   │
│   │   └── pattern_analyzer/         # Future app
│   │       ├── __init__.py
│   │       ├── app.py
│   │       ├── ui.py
│   │       └── metadata.json
│   │
│   └── components/
│       └── ui.py                     # Main hub UI (add App menu)
│
└── api/v1/
    └── juggler.proto                 # Add app-related messages
```

## 🔧 Implementation Details

### 1. App Metadata Format

Each app has a `metadata.json` file:

```json
{
  "id": "catch_counter",
  "name": "Catch Counter",
  "version": "1.0.0",
  "description": "Simple catch counting application with restart functionality",
  "author": "JuggleHub Team",
  "icon": "🎯",
  "category": "training",
  "required_features": [
    "throw_catch_detection"
  ],
  "optional_features": [
    "hand_tracking"
  ],
  "entry_point": "hub.apps.catch_counter.app:CatchCounterApp"
}
```

### 2. App Registry (`hub/apps/registry.json`)

```json
{
  "apps": [
    {
      "id": "catch_counter",
      "path": "hub/apps/catch_counter",
      "enabled": true,
      "last_used": "2025-10-02T18:30:00Z"
    }
  ],
  "recent_apps": ["catch_counter"],
  "max_recent": 5
}
```

### 3. BaseApp Abstract Class

```python
# hub/apps/base.py
from abc import ABC, abstractmethod
from PyQt6.QtWidgets import QMainWindow
from typing import Optional, List

class BaseApp(ABC):
    """Abstract base class for all JuggleHub apps."""
    
    def __init__(self, app_api: 'AppAPI'):
        self.api = app_api
        self.window: Optional[QMainWindow] = None
        self._running = False
    
    @abstractmethod
    def get_metadata(self) -> dict:
        """Return app metadata."""
        pass
    
    @abstractmethod
    def initialize(self):
        """Initialize the app (called once on startup)."""
        pass
    
    @abstractmethod
    def create_window(self) -> QMainWindow:
        """Create and return the app's main window."""
        pass
    
    @abstractmethod
    def on_frame_data(self, frame_data):
        """Handle incoming frame data from engine."""
        pass
    
    def start(self):
        """Start the app."""
        if not self._running:
            self.initialize()
            self.window = self.create_window()
            self._enable_required_features()
            self.api.subscribe_to_data(self.on_frame_data)
            self.window.show()
            self._running = True
    
    def stop(self):
        """Stop the app."""
        if self._running:
            self._disable_required_features()
            self.api.unsubscribe_from_data()
            if self.window:
                self.window.close()
            self._running = False
    
    def _enable_required_features(self):
        """Enable required engine features."""
        metadata = self.get_metadata()
        for feature in metadata.get('required_features', []):
            self.api.enable_feature(feature)
    
    def _disable_required_features(self):
        """Disable required engine features."""
        metadata = self.get_metadata()
        for feature in metadata.get('required_features', []):
            self.api.disable_feature(feature)
```

### 4. AppAPI Interface

```python
# hub/apps/api.py
import zmq
import juggler_pb2
from typing import Callable, Optional

class AppAPI:
    """High-level API for apps to interact with the engine."""
    
    def __init__(self, zmq_context: zmq.Context):
        self.context = zmq_context
        self.subscriber = self.context.socket(zmq.SUB)
        self.subscriber.connect("tcp://localhost:5555")
        self.subscriber.setsockopt(zmq.SUBSCRIBE, b"")
        
        self.commander = self.context.socket(zmq.REQ)
        self.commander.connect("tcp://localhost:5556")
        
        self._data_callback: Optional[Callable] = None
    
    def subscribe_to_data(self, callback: Callable):
        """Subscribe to frame data from engine."""
        self._data_callback = callback
        # Start background thread to receive data
        import threading
        self._receiver_thread = threading.Thread(
            target=self._receive_loop, daemon=True
        )
        self._receiver_thread.start()
    
    def unsubscribe_from_data(self):
        """Unsubscribe from frame data."""
        self._data_callback = None
    
    def _receive_loop(self):
        """Background thread to receive frame data."""
        while self._data_callback:
            try:
                message = self.subscriber.recv(flags=zmq.NOBLOCK)
                frame_data = juggler_pb2.FrameData()
                frame_data.ParseFromString(message)
                if self._data_callback:
                    self._data_callback(frame_data)
            except zmq.Again:
                pass
    
    def enable_feature(self, feature_name: str) -> bool:
        """Enable an engine feature."""
        command = juggler_pb2.CommandRequest()
        command.type = juggler_pb2.CommandRequest.CommandType.ENABLE_FEATURE
        command.feature_name = feature_name
        
        self.commander.send(command.SerializeToString())
        response_bytes = self.commander.recv()
        response = juggler_pb2.CommandResponse()
        response.ParseFromString(response_bytes)
        return response.success
    
    def disable_feature(self, feature_name: str) -> bool:
        """Disable an engine feature."""
        command = juggler_pb2.CommandRequest()
        command.type = juggler_pb2.CommandRequest.CommandType.DISABLE_FEATURE
        command.feature_name = feature_name
        
        self.commander.send(command.SerializeToString())
        response_bytes = self.commander.recv()
        response = juggler_pb2.CommandResponse()
        response.ParseFromString(response_bytes)
        return response.success
    
    def send_command(self, command: juggler_pb2.CommandRequest) -> juggler_pb2.CommandResponse:
        """Send a custom command to the engine."""
        self.commander.send(command.SerializeToString())
        response_bytes = self.commander.recv()
        response = juggler_pb2.CommandResponse()
        response.ParseFromString(response_bytes)
        return response
```

### 5. App Manager

```python
# hub/apps/manager.py
import json
import os
from typing import Dict, List, Optional
from datetime import datetime
from .base import BaseApp
from .api import AppAPI

class AppManager:
    """Manages app lifecycle, registry, and window management."""
    
    def __init__(self, zmq_context):
        self.zmq_context = zmq_context
        self.registry_path = "hub/apps/registry.json"
        self.apps_dir = "hub/apps"
        self.registry = self._load_registry()
        self.running_apps: Dict[str, BaseApp] = {}
    
    def _load_registry(self) -> dict:
        """Load app registry from disk."""
        if os.path.exists(self.registry_path):
            with open(self.registry_path, 'r') as f:
                return json.load(f)
        return {"apps": [], "recent_apps": [], "max_recent": 5}
    
    def _save_registry(self):
        """Save app registry to disk."""
        os.makedirs(os.path.dirname(self.registry_path), exist_ok=True)
        with open(self.registry_path, 'w') as f:
            json.dump(self.registry, f, indent=2)
    
    def discover_apps(self) -> List[dict]:
        """Discover all available apps."""
        apps = []
        for item in os.listdir(self.apps_dir):
            app_path = os.path.join(self.apps_dir, item)
            metadata_path = os.path.join(app_path, "metadata.json")
            
            if os.path.isdir(app_path) and os.path.exists(metadata_path):
                with open(metadata_path, 'r') as f:
                    metadata = json.load(f)
                    metadata['path'] = app_path
                    apps.append(metadata)
        return apps
    
    def launch_app(self, app_id: str) -> Optional[BaseApp]:
        """Launch an app by ID."""
        if app_id in self.running_apps:
            # App already running, bring to front
            self.running_apps[app_id].window.raise_()
            self.running_apps[app_id].window.activateWindow()
            return self.running_apps[app_id]
        
        # Find app metadata
        apps = self.discover_apps()
        app_metadata = next((a for a in apps if a['id'] == app_id), None)
        
        if not app_metadata:
            print(f"❌ App '{app_id}' not found")
            return None
        
        # Import and instantiate app
        try:
            entry_point = app_metadata['entry_point']
            module_path, class_name = entry_point.rsplit(':', 1)
            
            import importlib
            module = importlib.import_module(module_path)
            app_class = getattr(module, class_name)
            
            # Create app instance
            app_api = AppAPI(self.zmq_context)
            app = app_class(app_api)
            
            # Start app
            app.start()
            
            # Track running app
            self.running_apps[app_id] = app
            
            # Update recent apps
            self._update_recent_apps(app_id)
            
            print(f"✅ Launched app: {app_metadata['name']}")
            return app
            
        except Exception as e:
            print(f"❌ Failed to launch app '{app_id}': {e}")
            return None
    
    def close_app(self, app_id: str):
        """Close a running app."""
        if app_id in self.running_apps:
            self.running_apps[app_id].stop()
            del self.running_apps[app_id]
            print(f"✅ Closed app: {app_id}")
    
    def _update_recent_apps(self, app_id: str):
        """Update recent apps list."""
        recent = self.registry.get('recent_apps', [])
        if app_id in recent:
            recent.remove(app_id)
        recent.insert(0, app_id)
        self.registry['recent_apps'] = recent[:self.registry.get('max_recent', 5)]
        self._save_registry()
    
    def get_recent_apps(self) -> List[dict]:
        """Get list of recently used apps."""
        recent_ids = self.registry.get('recent_apps', [])
        all_apps = self.discover_apps()
        return [a for a in all_apps if a['id'] in recent_ids]
```

### 6. Catch Counter App Implementation

```python
# hub/apps/catch_counter/app.py
from PyQt6.QtWidgets import QMainWindow, QWidget, QVBoxLayout, QLabel, QPushButton
from PyQt6.QtCore import Qt, pyqtSignal, QObject
from PyQt6.QtGui import QFont
from ..base import BaseApp
import juggler_pb2

class CatchCounterSignals(QObject):
    """Signals for thread-safe UI updates."""
    catch_detected = pyqtSignal()

class CatchCounterApp(BaseApp):
    """Simple catch counting application."""
    
    def get_metadata(self) -> dict:
        return {
            "id": "catch_counter",
            "name": "Catch Counter",
            "version": "1.0.0",
            "description": "Counts catches with restart functionality",
            "required_features": ["throw_catch_detection"],
            "optional_features": []
        }
    
    def initialize(self):
        """Initialize the app."""
        self.catch_count = 0
        self.signals = CatchCounterSignals()
        self.signals.catch_detected.connect(self._increment_catch)
    
    def create_window(self) -> QMainWindow:
        """Create the app window."""
        window = QMainWindow()
        window.setWindowTitle("Catch Counter")
        window.setGeometry(100, 100, 400, 300)
        
        # Central widget
        central = QWidget()
        window.setCentralWidget(central)
        layout = QVBoxLayout(central)
        layout.setAlignment(Qt.AlignmentFlag.AlignCenter)
        
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
        layout.addWidget(self.counter_label)
        
        # Catches label
        catches_label = QLabel("catches")
        catches_label.setFont(QFont("Arial", 18))
        catches_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(catches_label)
        
        # Restart button
        restart_btn = QPushButton("Restart Counter")
        restart_btn.setFont(QFont("Arial", 14))
        restart_btn.clicked.connect(self._restart_counter)
        restart_btn.setStyleSheet("""
            QPushButton {
                background-color: #f44336;
                color: white;
                border: none;
                padding: 15px;
                border-radius: 8px;
                font-weight: bold;
            }
            QPushButton:hover {
                background-color: #da190b;
            }
        """)
        layout.addWidget(restart_btn)
        
        # Apply dark theme
        window.setStyleSheet("""
            QMainWindow, QWidget {
                background-color: #2b2b2b;
                color: #ffffff;
            }
        """)
        
        return window
    
    def on_frame_data(self, frame_data: juggler_pb2.FrameData):
        """Handle incoming frame data."""
        # Check for catch events
        for event in frame_data.throw_catch_events:
            if event.type == juggler_pb2.ThrowCatchEvent.CATCH:
                self.signals.catch_detected.emit()
    
    def _increment_catch(self):
        """Increment catch counter (thread-safe)."""
        self.catch_count += 1
        self.counter_label.setText(str(self.catch_count))
    
    def _restart_counter(self):
        """Reset the catch counter."""
        self.catch_count = 0
        self.counter_label.setText("0")
```

### 7. Protocol Buffer Extensions

Add to `api/v1/juggler.proto`:

```protobuf
// App-related messages
message AppFeatureControl {
  enum FeatureType {
    DNN_TRACKING = 0;
    COLOR_TRACKING = 1;
    POSE_ESTIMATION = 2;
    THROW_CATCH_DETECTION = 3;
    PATTERN_RECOGNITION = 4;  // Future feature
    HAND_TRACKING = 5;
  }
  
  FeatureType feature = 1;
  bool enabled = 2;
}

// Extend CommandRequest
message CommandRequest {
  enum CommandType {
    // ... existing types ...
    ENABLE_FEATURE = 20;
    DISABLE_FEATURE = 21;
  }
  
  // ... existing fields ...
  string feature_name = 30;
}

// Throw/Catch events for apps
message ThrowCatchEvent {
  enum EventType {
    THROW = 0;
    CATCH = 1;
  }
  
  EventType type = 1;
  int32 ball_id = 2;
  int32 hand_id = 3;
  uint64 timestamp_us = 4;
  Point3D position = 5;
}

// Add to FrameData
message FrameData {
  // ... existing fields ...
  repeated ThrowCatchEvent throw_catch_events = 20;
}
```

### 8. Main Hub UI Integration

Add App menu to `hub/components/ui.py`:

```python
def create_menu_bar(self):
    """Create the menu bar with File, App, and Help menus."""
    menubar = self.menuBar()
    
    # ... existing File menu ...
    
    # App menu
    app_menu = menubar.addMenu("&App")
    
    # Recent apps submenu
    self.recent_apps_menu = app_menu.addMenu("Recent Apps")
    self._populate_recent_apps()
    
    app_menu.addSeparator()
    
    # App Manager action
    app_manager_action = QAction("App &Manager...", self)
    app_manager_action.setShortcut("Ctrl+M")
    app_manager_action.triggered.connect(self.open_app_manager)
    app_menu.addAction(app_manager_action)
    
    # ... existing Help menu ...

def _populate_recent_apps(self):
    """Populate recent apps menu."""
    self.recent_apps_menu.clear()
    recent_apps = self.hub_instance.app_manager.get_recent_apps()
    
    if not recent_apps:
        no_apps_action = QAction("No recent apps", self)
        no_apps_action.setEnabled(False)
        self.recent_apps_menu.addAction(no_apps_action)
        return
    
    for app in recent_apps:
        action = QAction(f"{app['icon']} {app['name']}", self)
        action.triggered.connect(
            lambda checked, app_id=app['id']: self.launch_app(app_id)
        )
        self.recent_apps_menu.addAction(action)

def open_app_manager(self):
    """Open the app manager dialog."""
    from .app_manager_dialog import AppManagerDialog
    dialog = AppManagerDialog(self.hub_instance.app_manager, self)
    dialog.exec()
    # Refresh recent apps menu
    self._populate_recent_apps()

def launch_app(self, app_id: str):
    """Launch an app."""
    self.hub_instance.app_manager.launch_app(app_id)
    self._populate_recent_apps()
```

## 🎨 App Manager UI

The App Manager dialog shows all available apps in a grid layout with cards:

```python
# hub/components/app_manager_dialog.py
from PyQt6.QtWidgets import (QDialog, QVBoxLayout, QHBoxLayout, QLabel, 
                              QPushButton, QScrollArea, QWidget, QGridLayout)
from PyQt6.QtCore import Qt
from PyQt6.QtGui import QFont

class AppCard(QWidget):
    """Card widget for displaying app information."""
    
    def __init__(self, app_metadata: dict, manager, parent=None):
        super().__init__(parent)
        self.app_metadata = app_metadata
        self.manager = manager
        self._init_ui()
    
    def _init_ui(self):
        layout = QVBoxLayout(self)
        layout.setSpacing(10)
        
        # Icon and title
        header = QHBoxLayout()
        icon_label = QLabel(self.app_metadata.get('icon', '📦'))
        icon_label.setFont(QFont("Arial", 32))
        header.addWidget(icon_label)
        
        title_label = QLabel(self.app_metadata['name'])
        title_label.setFont(QFont("Arial", 16, QFont.Weight.Bold))
        header.addWidget(title_label)
        header.addStretch()
        layout.addLayout(header)
        
        # Description
        desc_label = QLabel(self.app_metadata.get('description', ''))
        desc_label.setWordWrap(True)
        desc_label.setStyleSheet("color: #aaaaaa;")
        layout.addWidget(desc_label)
        
        # Version and category
        info_layout = QHBoxLayout()
        version_label = QLabel(f"v{self.app_metadata.get('version', '1.0.0')}")
        version_label.setStyleSheet("color: #888888; font-size: 10px;")
        info_layout.addWidget(version_label)
        
        category_label = QLabel(f"• {self.app_metadata.get('category', 'general')}")
        category_label.setStyleSheet("color: #888888; font-size: 10px;")
        info_layout.addWidget(category_label)
        info_layout.addStretch()
        layout.addLayout(info_layout)
        
        # Launch button
        launch_btn = QPushButton("Launch")
        launch_btn.clicked.connect(self._launch_app)
        launch_btn.setStyleSheet("""
            QPushButton {
                background-color: #4CAF50;
                color: white;
                border: none;
                padding: 10px;
                border-radius: 5px;
                font-weight: bold;
            }
            QPushButton:hover {
                background-color: #45a049;
            }
        """)
        layout.addWidget(launch_btn)
        
        # Card styling
        self.setStyleSheet("""
            AppCard {
                background-color: #3a3a3a;
                border: 2px solid #555555;
                border-radius: 10px;
                padding: 15px;
            }
        """)
        self.setFixedSize(300, 200)
    
    def _launch_app(self):
        """Launch this app."""
        self.manager.launch_app(self.app_metadata['id'])
        self.parent().parent().parent().close()  # Close dialog

class AppManagerDialog(QDialog):
    """Dialog for managing and launching apps."""
    
    def __init__(self, app_manager, parent=None):
        super().__init__(parent)
        self.app_manager = app_manager
        self.setWindowTitle("App Manager")
        self.setGeometry(100, 100, 800, 600)
        self._init_ui()
    
    def _init_ui(self):
        layout = QVBoxLayout(self)
        
        # Title
        title = QLabel("🚀 JuggleHub Apps")
        title.setFont(QFont("Arial", 24, QFont.Weight.Bold))
        layout.addWidget(title)
        
        # Scroll area for app cards
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        
        # Container for cards
        container = QWidget()
        grid = QGridLayout(container)
        grid.setSpacing(20)
        
        # Discover and display apps
        apps = self.app_manager.discover_apps()
        row, col = 0, 0
        max_cols = 2
        
        for app in apps:
            card = AppCard(app, self.app_manager, self)
            grid.addWidget(card, row, col)
            col += 1
            if col >= max_cols:
                col = 0
                row += 1
        
        scroll.setWidget(container)
        layout.addWidget(scroll)
        
        # Close button
        close_btn = QPushButton("Close")
        close_btn.clicked.connect(self.close)
        layout.addWidget(close_btn)
        
        # Apply dark theme
        self.setStyle