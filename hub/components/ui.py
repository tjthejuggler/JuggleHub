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
import subprocess
import platform

# Import extracted components
from .ui_network import UdpClient
from .ui_console import ConsoleUI
from .color_profile_manager import ColorProfileManager, ColorProfileDialog

# Ball management components removed - using legacy color tracking only
BALL_MANAGEMENT_AVAILABLE = False


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
                                 QMenuBar, QFileDialog, QScrollArea, QTabWidget)
    from PyQt6.QtCore import QTimer, pyqtSignal, QObject, Qt
    from PyQt6.QtGui import QFont, QPalette, QColor, QPixmap, QImage, QPen, QPainter, QKeySequence, QBrush, QAction
    PYQT_AVAILABLE = True
except ImportError:
    print("⚠️ PyQt6 not available. Using console UI.")
    PYQT_AVAILABLE = False

# Import PyQt-dependent components
if PYQT_AVAILABLE:
    from .ui_widgets import FrameDataSignal, CollapsibleGroupBox
    from .ui_settings import CalibrationSettingsWidget


if PYQT_AVAILABLE:
    # Note: CollapsibleGroupBox and FrameDataSignal are now imported from ui_widgets
    # Note: CalibrationSettingsWidget is now imported from ui_settings

    class CollapsibleGroupBox(QWidget):
        """
        A collapsible group box that mimics QGroupBox appearance.
        
        Features:
        - Clickable header with expand/collapse icon
        - Maintains QGroupBox styling
        - Remembers collapsed state in settings
        """
        
        def __init__(self, title: str, parent=None, collapsed: bool = False):
            super().__init__(parent)
            self.title = title
            self.is_collapsed = collapsed
            
            # Main layout
            main_layout = QVBoxLayout(self)
            main_layout.setContentsMargins(0, 0, 0, 5)
            main_layout.setSpacing(0)
            
            # Header button (clickable title)
            self.header_button = QPushButton(f"▼ {title}")
            self.header_button.setCheckable(True)
            self.header_button.setChecked(not collapsed)
            self.header_button.clicked.connect(self.toggle_collapsed)
            self.header_button.setStyleSheet("""
                QPushButton {
                    text-align: left;
                    padding: 8px;
                    border: 2px solid #555555;
                    border-radius: 5px 5px 0 0;
                    background-color: #3a3a3a;
                    font-weight: bold;
                }
                QPushButton:hover {
                    background-color: #4a4a4a;
                }
            """)
            main_layout.addWidget(self.header_button)
            
            # Content container
            self.content_widget = QWidget()
            self.content_widget.setObjectName("CollapsibleContent")
            self.content_layout = QVBoxLayout(self.content_widget)
            self.content_layout.setContentsMargins(10, 10, 10, 10)
            self.content_widget.setStyleSheet("""
                QWidget#CollapsibleContent {
                    border: 2px solid #555555;
                    border-top: none;
                    border-radius: 0 0 5px 5px;
                    background-color: #2b2b2b;
                }
            """)
            main_layout.addWidget(self.content_widget)
            
            # Set initial state
            if collapsed:
                self.content_widget.hide()
                self.header_button.setText(f"▶ {title}")
        
        def toggle_collapsed(self):
            self.is_collapsed = not self.is_collapsed
            if self.is_collapsed:
                self.content_widget.hide()
                self.header_button.setText(f"▶ {self.title}")
            else:
                self.content_widget.show()
                self.header_button.setText(f"▼ {self.title}")
        
        def get_content_layout(self):
            """Returns the layout where child widgets should be added"""
            return self.content_layout

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
            # Main layout for the widget
            main_layout = QVBoxLayout(self)
            main_layout.setContentsMargins(0, 0, 0, 0)
            
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
            
            # Create scroll area
            scroll_area = QScrollArea()
            scroll_area.setWidgetResizable(True)
            scroll_area.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
            scroll_area.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAsNeeded)
            scroll_area.setStyleSheet("""
                QScrollArea {
                    border: none;
                    background-color: #2b2b2b;
                }
                QScrollBar:vertical {
                    border: none;
                    background: #1e1e1e;
                    width: 12px;
                    margin: 0px;
                }
                QScrollBar::handle:vertical {
                    background: #555555;
                    min-height: 20px;
                    border-radius: 6px;
                }
                QScrollBar::handle:vertical:hover {
                    background: #666666;
                }
            """)
            
            # Container widget for all collapsible sections
            container_widget = QWidget()
            container_layout = QVBoxLayout(container_widget)
            container_layout.setSpacing(10)
            container_layout.setContentsMargins(5, 5, 5, 5)
            
            # Add all collapsible sections
            self.camera_section = self.create_camera_section()
            container_layout.addWidget(self.camera_section)
            
            self.yolo_section = self.create_yolo_section()
            container_layout.addWidget(self.yolo_section)
            
            self.bytetrack_section = self.create_bytetrack_section()
            container_layout.addWidget(self.bytetrack_section)
            
            self.pose_section = self.create_pose_section()
            container_layout.addWidget(self.pose_section)
            
            self.throw_catch_section = self.create_throw_catch_section()
            container_layout.addWidget(self.throw_catch_section)
            
            self.adaptive_color_section = self.create_adaptive_color_section()
            container_layout.addWidget(self.adaptive_color_section)
            
            self.ball_profiles_section = self.create_ball_profiles_section()
            container_layout.addWidget(self.ball_profiles_section)
            
            # Add stretch to push sections to top
            container_layout.addStretch()
            
            # Set container as scroll area widget
            scroll_area.setWidget(container_widget)
            
            # Add scroll area to main layout
            main_layout.addWidget(scroll_area)

        def create_camera_section(self):
            """Create the Camera Settings section"""
            section = CollapsibleGroupBox("📷 Camera Settings", collapsed=False)
            camera_layout = QGridLayout()
            section.get_content_layout().addLayout(camera_layout)
            
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
            
            return section

        def create_yolo_section(self):
            """Create the YOLO Tracker Settings section"""
            section = CollapsibleGroupBox("🎯 YOLO Tracker Settings", collapsed=False)
            dnn_layout = QGridLayout()
            section.get_content_layout().addLayout(dnn_layout)

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
            
            return section

        def create_bytetrack_section(self):
            """Create the ByteTrack Settings section"""
            section = CollapsibleGroupBox("🔍 ByteTrack Settings", collapsed=False)
            bytetrack_layout = QGridLayout()
            section.get_content_layout().addLayout(bytetrack_layout)

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

            return section

        def create_pose_section(self):
            """Create the Pose Model Settings section"""
            section = CollapsibleGroupBox("🧍 Pose Model Settings", collapsed=False)
            pose_layout = QGridLayout()
            section.get_content_layout().addLayout(pose_layout)

            self.pose_model_toggle = QPushButton("Enable Pose Model")
            self.pose_model_toggle.setCheckable(True)
            self.pose_model_toggle.setChecked(True)
            self.pose_model_toggle.clicked.connect(self.toggle_pose_model)
            pose_layout.addWidget(self.pose_model_toggle, 0, 0, 1, 2)

            return section

        def create_throw_catch_section(self):
            """Create the Throw/Catch Detection settings section"""
            section = CollapsibleGroupBox("🎯 Ball State Detection", collapsed=False)
            layout = QGridLayout()
            section.get_content_layout().addLayout(layout)
            
            row = 0
            
            # Info label
            info_label = QLabel("ℹ️ Configure how ball state (held/in-air) is determined")
            info_label.setStyleSheet("color: #aaaaaa; font-size: 10px;")
            info_label.setWordWrap(True)
            layout.addWidget(info_label, row, 0, 1, 3)
            row += 1
            
            # Detection weights section (for YOLO-detected balls)
            layout.addWidget(QLabel("YOLO Detection Weights:"), row, 0, 1, 3)
            row += 1
            
            # ML ball (in-air) weight
            self.tc_ml_ball_weight_slider, self.tc_ml_ball_weight_label = self._create_slider_widget(
                parent_layout=layout,
                row=row,
                label_text="ML 'Ball' Weight",
                tooltip_text="Weight for ML model detecting ball as 'in-air'.\n"
                             "Range: 0.0-1.0. Default: 0.4.\n"
                             "Higher = trust ML more for in-air detection.",
                range_min=0,
                range_max=100,
                initial_value=40,
                update_func=lambda v: self.update_setting('ml_ball_weight', v / 100.0),
                is_float=True
            )
            row += 1
            
            # ML ball_held weight
            self.tc_ml_ball_held_weight_slider, self.tc_ml_ball_held_weight_label = self._create_slider_widget(
                parent_layout=layout,
                row=row,
                label_text="ML 'Ball Held' Weight",
                tooltip_text="Weight for ML model detecting ball as 'held'.\n"
                             "Range: 0.0-1.0. Default: 0.4.\n"
                             "Higher = trust ML more for held detection.",
                range_min=0,
                range_max=100,
                initial_value=40,
                update_func=lambda v: self.update_setting('ml_ball_held_weight', v / 100.0),
                is_float=True
            )
            row += 1
            
            # Wrist proximity weight
            self.tc_wrist_proximity_weight_slider, self.tc_wrist_proximity_weight_label = self._create_slider_widget(
                parent_layout=layout,
                row=row,
                label_text="Wrist Proximity Weight",
                tooltip_text="Weight for wrist proximity detection.\n"
                             "Range: 0.0-1.0. Default: 0.2.\n"
                             "Higher = trust proximity more.",
                range_min=0,
                range_max=100,
                initial_value=20,
                update_func=lambda v: self.update_setting('wrist_proximity_weight', v / 100.0),
                is_float=True
            )
            row += 1
            
            # Separator
            layout.addWidget(QLabel("Distance Thresholds:"), row, 0, 1, 3)
            row += 1
            
            # Wrist proximity threshold (for YOLO-detected balls)
            self.tc_wrist_proximity_slider, self.tc_wrist_proximity_label = self._create_slider_widget(
                parent_layout=layout,
                row=row,
                label_text="Detected Ball Proximity (cm)",
                tooltip_text="Distance between YOLO-detected ball and wrist to consider as held.\n"
                             "Range: 5-30cm. Default: 15cm.\n"
                             "Lower = stricter, Higher = more lenient.",
                range_min=5,
                range_max=30,
                initial_value=15,
                update_func=lambda v: self.update_setting('wrist_proximity_threshold', v / 100.0),  # Convert cm to m
                is_float=False  # Display as integer cm
            )
            row += 1
            
            # Undetected near hand threshold (for occluded balls)
            self.tc_undetected_near_hand_slider, self.tc_undetected_near_hand_label = self._create_slider_widget(
                parent_layout=layout,
                row=row,
                label_text="Undetected Near Hand (cm)",
                tooltip_text="Distance from hand where undetected ball is considered held (occluded).\n"
                             "Range: 10-40cm. Default: 20cm.\n"
                             "Larger threshold accounts for balls hidden by hands.",
                range_min=10,
                range_max=40,
                initial_value=20,
                update_func=lambda v: self.update_setting('undetected_near_hand_threshold', v / 100.0),  # Convert cm to m
                is_float=False  # Display as integer cm
            )
            row += 1
            
            # Separator
            layout.addWidget(QLabel("State Change:"), row, 0, 1, 3)
            row += 1
            
            # State change debouncing
            self.tc_min_frames_slider, self.tc_min_frames_label = self._create_slider_widget(
                parent_layout=layout,
                row=row,
                label_text="State Change Frames",
                tooltip_text="Number of consecutive frames required to confirm state change (held/in-air).\n"
                             "Range: 1-10 frames. Default: 3.\n"
                             "Higher values = more stable detection, slower response.",
                range_min=1,
                range_max=10,
                initial_value=3,
                update_func=lambda v: self.update_setting('min_frames_for_state_change', v),
                is_float=False
            )
            row += 1
            
            # Separator
            layout.addWidget(QLabel("Sound Effects:"), row, 0, 1, 3)
            row += 1
            
            # Sound on catches toggle with test button
            self.tc_sound_on_catch_toggle = QPushButton("Sound on Catches")
            self.tc_sound_on_catch_toggle.setCheckable(True)
            self.tc_sound_on_catch_toggle.setChecked(False)
            self.tc_sound_on_catch_toggle.clicked.connect(lambda: self.update_setting('tc_sound_on_catch', 1 if self.tc_sound_on_catch_toggle.isChecked() else 0))
            layout.addWidget(self.tc_sound_on_catch_toggle, row, 0, 1, 2)
            
            # Test catch sound button
            self.tc_test_catch_sound_button = QPushButton("🔊 Test")
            self.tc_test_catch_sound_button.setMaximumWidth(80)
            self.tc_test_catch_sound_button.clicked.connect(self.test_catch_sound)
            self.tc_test_catch_sound_button.setStyleSheet("""
                QPushButton {
                    background-color: #4CAF50;
                    color: white;
                    padding: 5px;
                    border-radius: 3px;
                    font-weight: bold;
                }
                QPushButton:hover { background-color: #45a049; }
                QPushButton:pressed { background-color: #2e7d32; }
            """)
            layout.addWidget(self.tc_test_catch_sound_button, row, 2)
            row += 1
            
            # Sound on throws toggle with test button
            self.tc_sound_on_throw_toggle = QPushButton("Sound on Throws")
            self.tc_sound_on_throw_toggle.setCheckable(True)
            self.tc_sound_on_throw_toggle.setChecked(False)
            self.tc_sound_on_throw_toggle.clicked.connect(lambda: self.update_setting('tc_sound_on_throw', 1 if self.tc_sound_on_throw_toggle.isChecked() else 0))
            layout.addWidget(self.tc_sound_on_throw_toggle, row, 0, 1, 2)
            
            # Test throw sound button
            self.tc_test_throw_sound_button = QPushButton("🔊 Test")
            self.tc_test_throw_sound_button.setMaximumWidth(80)
            self.tc_test_throw_sound_button.clicked.connect(self.test_throw_sound)
            self.tc_test_throw_sound_button.setStyleSheet("""
                QPushButton {
                    background-color: #4CAF50;
                    color: white;
                    padding: 5px;
                    border-radius: 3px;
                    font-weight: bold;
                }
                QPushButton:hover { background-color: #45a049; }
                QPushButton:pressed { background-color: #2e7d32; }
            """)
            layout.addWidget(self.tc_test_throw_sound_button, row, 2)
            
            return section
        
        def create_adaptive_color_section(self):
            """Create the Adaptive Color Tracking section"""
            section = CollapsibleGroupBox("🎨 Adaptive Color Tracking", collapsed=False)
            layout = QGridLayout()
            section.get_content_layout().addLayout(layout)
            
            row = 0
            
            # Enable/Disable adaptive system
            self.adaptive_enabled_toggle = QPushButton("Enable Adaptive Ranges")
            self.adaptive_enabled_toggle.setCheckable(True)
            self.adaptive_enabled_toggle.setChecked(True)
            self.adaptive_enabled_toggle.clicked.connect(lambda: self.update_setting('adaptive_enabled', 1 if self.adaptive_enabled_toggle.isChecked() else 0))
            layout.addWidget(self.adaptive_enabled_toggle, row, 0, 1, 3)
            row += 1
            
            # Success rate thresholds
            layout.addWidget(QLabel("Adaptation Thresholds:"), row, 0, 1, 3)
            row += 1
            
            self.adaptive_success_thresh_slider, self.adaptive_success_thresh_label = self._create_slider_widget(
                parent_layout=layout,
                row=row,
                label_text="Success Threshold",
                tooltip_text="Success rate above which ranges contract.\n"
                             "Range: 0-100%. Default: 70%.\n"
                             "Higher = more aggressive contraction.",
                range_min=50,
                range_max=95,
                initial_value=70,
                update_func=lambda v: self.update_setting('adaptive_success_threshold', v / 100.0),
                is_float=True
            )
            row += 1
            
            self.adaptive_failure_thresh_slider, self.adaptive_failure_thresh_label = self._create_slider_widget(
                parent_layout=layout,
                row=row,
                label_text="Failure Threshold",
                tooltip_text="Success rate below which ranges expand.\n"
                             "Range: 0-50%. Default: 30%.\n"
                             "Lower = more aggressive expansion.",
                range_min=5,
                range_max=50,
                initial_value=30,
                update_func=lambda v: self.update_setting('adaptive_failure_threshold', v / 100.0),
                is_float=True
            )
            row += 1
            
            # Adaptation rates
            layout.addWidget(QLabel("Adaptation Rates:"), row, 0, 1, 3)
            row += 1
            
            self.adaptive_expansion_slider, self.adaptive_expansion_label = self._create_slider_widget(
                parent_layout=layout,
                row=row,
                label_text="Expansion Step (degrees)",
                tooltip_text="How much to expand hue range when failing.\n"
                             "Range: 1-10 degrees. Default: 2.\n"
                             "Higher = faster adaptation but less stable.",
                range_min=1,
                range_max=10,
                initial_value=2,
                update_func=lambda v: self.update_setting('adaptive_expansion_step', v),
                is_float=False
            )
            row += 1
            
            self.adaptive_contraction_slider, self.adaptive_contraction_label = self._create_slider_widget(
                parent_layout=layout,
                row=row,
                label_text="Contraction Step (degrees)",
                tooltip_text="How much to contract hue range when succeeding.\n"
                             "Range: 0.5-5 degrees. Default: 1.\n"
                             "Higher = faster adaptation but may lose tracking.",
                range_min=5,
                range_max=50,
                initial_value=10,
                update_func=lambda v: self.update_setting('adaptive_contraction_step', v / 10.0),
                is_float=True
            )
            row += 1
            
            # History window
            self.adaptive_history_slider, self.adaptive_history_label = self._create_slider_widget(
                parent_layout=layout,
                row=row,
                label_text="History Window (frames)",
                tooltip_text="Number of frames to track for success rate.\n"
                             "Range: 30-120 frames. Default: 60.\n"
                             "Higher = more stable but slower to adapt.",
                range_min=30,
                range_max=120,
                initial_value=60,
                update_func=lambda v: self.update_setting('adaptive_history_window', v),
                is_float=False
            )
            row += 1
            
            # Status display
            layout.addWidget(QLabel("Current Status:"), row, 0, 1, 3)
            row += 1
            
            self.adaptive_status_text = QTextEdit()
            self.adaptive_status_text.setMaximumHeight(150)
            self.adaptive_status_text.setReadOnly(True)
            self.adaptive_status_text.setPlainText("Adaptive color tracking status will appear here...")
            layout.addWidget(self.adaptive_status_text, row, 0, 1, 3)
            row += 1
            
            # Reset button
            reset_button = QPushButton("Reset to Defaults")
            reset_button.clicked.connect(self.reset_adaptive_settings)
            reset_button.setStyleSheet("""
                QPushButton {
                    background-color: #f44336;
                    color: white;
                    padding: 8px;
                    border-radius: 4px;
                    font-weight: bold;
                }
                QPushButton:hover { background-color: #da190b; }
            """)
            layout.addWidget(reset_button, row, 0, 1, 3)
            
            return section
        
        def reset_adaptive_settings(self):
            """Reset adaptive color settings to defaults"""
            self.adaptive_success_thresh_slider.setValue(70)
            self.adaptive_failure_thresh_slider.setValue(30)
            self.adaptive_expansion_slider.setValue(2)
            self.adaptive_contraction_slider.setValue(10)
            self.adaptive_history_slider.setValue(60)
            print("✅ Adaptive color settings reset to defaults")

        def _calculate_hsv_range_from_rgb(self, rgb):
            """Calculate appropriate HSV range from RGB color values."""
            import cv2
            import numpy as np
            
            # Convert RGB to HSV
            rgb_array = np.uint8([[rgb]])  # Shape: (1, 1, 3)
            hsv_array = cv2.cvtColor(rgb_array, cv2.COLOR_RGB2HSV)
            h, s, v = hsv_array[0][0]
            
            # Define hue tolerance based on color characteristics
            # Colors near red (hue ~0 or ~180) need special handling due to wrap-around
            hue_tolerance = 15  # degrees
            
            # Calculate min/max hue with wrap-around handling
            min_hue = float(max(0, h - hue_tolerance))
            max_hue = float(min(180, h + hue_tolerance))
            
            # For colors near red (hue < 15 or hue > 165), we need to handle wrap-around
            if h < 15:
                # Red wraps around: use range like [165, 180] + [0, 15]
                min_hue = float(max(0, 180 - (15 - h)))
                max_hue = float(h + hue_tolerance)
            elif h > 165:
                # Red wraps around: use range like [165, 180] + [0, 15]
                min_hue = float(h - hue_tolerance)
                max_hue = float(min(15, h + hue_tolerance - 180))
            
            # Saturation and value ranges (more forgiving)
            min_s = float(max(30, s - 80))
            max_s = 255.0
            min_v = float(max(30, v - 80))
            max_v = 255.0
            
            return [min_hue, min_s, min_v], [max_hue, max_s, max_v]
        
        def create_ball_profiles_section(self):
            """Create the Ball Profiles section for tracking configuration"""
            section = CollapsibleGroupBox("🎨 Ball Profiles", collapsed=False)
            layout = QVBoxLayout()
            section.get_content_layout().addLayout(layout)
            
            # Load ball profiles from ball_settings.json
            import json
            import os
            ball_settings_path = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "ball_settings.json")
            
            try:
                with open(ball_settings_path, 'r') as f:
                    self.ball_profiles = json.load(f)
                print(f"✅ Loaded ball profiles from {ball_settings_path}")
                print(f"   Profiles loaded: {list(self.ball_profiles.keys())}")
            except Exception as e:
                print(f"❌ Error loading ball_settings.json: {e}")
                self.ball_profiles = {}
            
            # Also get profiles from ColorProfileManager to ensure we have all colors
            from components.color_profile_manager import ColorProfileManager
            color_manager = ColorProfileManager()
            
            # Merge profiles - calculate proper HSV ranges for new colors OR fix existing ones with default 0-180 range
            profiles_updated = False
            for profile in color_manager.profiles:
                ball_name = profile['name']
                
                # Check if profile needs updating (new or has default 0-180 hue range)
                needs_update = False
                if ball_name not in self.ball_profiles:
                    needs_update = True
                    print(f"⚠️ Adding missing profile '{ball_name}'")
                else:
                    # Check if it has the default 0-180 hue range (needs fixing)
                    existing_min_hue = self.ball_profiles[ball_name]['min_hsv'][0]
                    existing_max_hue = self.ball_profiles[ball_name]['max_hsv'][0]
                    if existing_min_hue == 0.0 and existing_max_hue == 180.0:
                        needs_update = True
                        print(f"⚠️ Fixing profile '{ball_name}' with default 0-180 range")
                    else:
                        print(f"✓ Profile '{ball_name}' already has custom hue range: {existing_min_hue:.1f}-{existing_max_hue:.1f}")
                
                if needs_update:
                    # Calculate HSV range from RGB color
                    rgb = profile.get('rgb', [255, 255, 255])
                    min_hsv, max_hsv = self._calculate_hsv_range_from_rgb(rgb)
                    
                    print(f"   RGB: {rgb} -> Hue range: {min_hsv[0]:.1f}-{max_hsv[0]:.1f}")
                    
                    # Preserve enabled state if profile already exists
                    enabled = self.ball_profiles[ball_name].get('enabled', True) if ball_name in self.ball_profiles else profile.get('enabled', True)
                    
                    self.ball_profiles[ball_name] = {
                        'enabled': enabled,
                        'min_hsv': min_hsv,
                        'max_hsv': max_hsv
                    }
                    profiles_updated = True
            
            # Save updated ball_settings.json if we updated any profiles
            if profiles_updated:
                try:
                    with open(ball_settings_path, 'w') as f:
                        json.dump(self.ball_profiles, f, indent=4)
                    print(f"✅ Ball settings saved with updated HSV ranges")
                except Exception as e:
                    print(f"❌ Error saving ball_settings.json: {e}")
            else:
                print(f"ℹ️ No profile updates needed")
            
            # Store checkbox and slider references
            self.ball_checkboxes = {}
            self.ball_hue_sliders = {}
            
            # Create a widget for each ball profile
            for ball_name in sorted(self.ball_profiles.keys()):
                ball_group = QGroupBox(ball_name.capitalize())
                ball_layout = QGridLayout(ball_group)
                
                # Checkbox for enabling/disabling this ball
                checkbox = QPushButton(f"Track {ball_name.capitalize()}")
                checkbox.setCheckable(True)
                # Read enabled state from ball_settings.json
                is_enabled = self.ball_profiles[ball_name].get('enabled', True)
                checkbox.setChecked(is_enabled)
                checkbox.clicked.connect(lambda checked, name=ball_name: self.toggle_ball_tracking(name, checked))
                self.ball_checkboxes[ball_name] = checkbox
                ball_layout.addWidget(checkbox, 0, 0, 1, 3)
                
                # Get current HSV values - use the actual hue values from ball_settings.json
                hsv_data = self.ball_profiles[ball_name]
                min_hsv = hsv_data.get('min_hsv', [0, 0, 0])
                max_hsv = hsv_data.get('max_hsv', [180, 255, 255])
                
                print(f"🔍 DEBUG {ball_name}: hsv_data = {hsv_data}")
                print(f"🔍 DEBUG {ball_name}: min_hsv = {min_hsv}, max_hsv = {max_hsv}")
                
                # Extract hue values (first element of HSV arrays)
                min_hue_value = int(min_hsv[0])
                max_hue_value = int(max_hsv[0])
                
                print(f"🔍 DEBUG {ball_name}: Setting sliders to min={min_hue_value}, max={max_hue_value}")
                
                # Min Hue slider
                ball_layout.addWidget(QLabel("Min Hue:"), 1, 0)
                min_hue_slider = QSlider(Qt.Orientation.Horizontal)
                min_hue_slider.setRange(0, 180)
                min_hue_slider.setValue(min_hue_value)
                ball_layout.addWidget(min_hue_slider, 1, 1)
                
                print(f"🔍 DEBUG {ball_name}: Min slider actual value after setValue: {min_hue_slider.value()}")
                
                min_hue_label = QLabel(f"{min_hue_value}")
                min_hue_label.setMinimumWidth(40)
                ball_layout.addWidget(min_hue_label, 1, 2)
                
                # Max Hue slider
                ball_layout.addWidget(QLabel("Max Hue:"), 2, 0)
                max_hue_slider = QSlider(Qt.Orientation.Horizontal)
                max_hue_slider.setRange(0, 180)
                max_hue_slider.setValue(max_hue_value)
                ball_layout.addWidget(max_hue_slider, 2, 1)
                
                print(f"🔍 DEBUG {ball_name}: Max slider actual value after setValue: {max_hue_slider.value()}")
                
                max_hue_label = QLabel(f"{max_hue_value}")
                max_hue_label.setMinimumWidth(40)
                ball_layout.addWidget(max_hue_label, 2, 2)
                
                # Connect sliders to update functions
                min_hue_slider.valueChanged.connect(
                    lambda value, name=ball_name, label=min_hue_label: self.update_ball_hue(name, 'min', value, label)
                )
                max_hue_slider.valueChanged.connect(
                    lambda value, name=ball_name, label=max_hue_label: self.update_ball_hue(name, 'max', value, label)
                )
                
                # Store slider references
                self.ball_hue_sliders[ball_name] = {
                    'min': min_hue_slider,
                    'max': max_hue_slider,
                    'min_label': min_hue_label,
                    'max_label': max_hue_label
                }
                
                # Info label about wrapping
                info_label = QLabel("ℹ️ Hue wraps: if max < min, uses values outside the range")
                info_label.setStyleSheet("color: #aaaaaa; font-size: 9px;")
                info_label.setWordWrap(True)
                ball_layout.addWidget(info_label, 3, 0, 1, 3)
                
                layout.addWidget(ball_group)
            
            # Auto-calibrate button
            auto_cal_button = QPushButton("🎯 Auto-Calibrate from Current Colors")
            auto_cal_button.setStyleSheet("""
                QPushButton {
                    background-color: #FF9800;
                    color: white;
                    padding: 10px;
                    border-radius: 4px;
                    font-weight: bold;
                }
                QPushButton:hover { background-color: #F57C00; }
            """)
            auto_cal_button.clicked.connect(self.auto_calibrate_hues)
            layout.addWidget(auto_cal_button)
            
            return section

        def toggle_ball_tracking(self, ball_name: str, enabled: bool):
            """Toggle tracking for a specific ball color"""
            print(f"{'Enabling' if enabled else 'Disabling'} tracking for {ball_name}")
            
            # Update the ball_profiles dict
            if ball_name in self.ball_profiles:
                self.ball_profiles[ball_name]['enabled'] = enabled
                # Save to ball_settings.json
                self.save_ball_settings()
            
            # Send command to engine via UDP
            self.udp_client.send_setting(f"track_{ball_name}", 1 if enabled else 0)
            
            # Auto-save settings
            if not self._loading_settings:
                self.save_settings()

        def update_ball_hue(self, ball_name: str, hue_type: str, value: int, label: QLabel):
            """Update hue value for a ball profile"""
            label.setText(str(value))
            
            # Update the ball_profiles dict
            if ball_name in self.ball_profiles:
                if hue_type == 'min':
                    self.ball_profiles[ball_name]['min_hsv'][0] = float(value)
                else:
                    self.ball_profiles[ball_name]['max_hsv'][0] = float(value)
                
                # Send to engine
                self.udp_client.send_setting(f"{ball_name}_{hue_type}_hue", value)
                
                # Save ball_settings.json
                self.save_ball_settings()
                
                # Auto-save calibration settings
                if not self._loading_settings:
                    self.save_settings()

        def save_ball_settings(self):
            """Save ball profiles to ball_settings.json"""
            import json
            import os
            ball_settings_path = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "ball_settings.json")
            
            try:
                with open(ball_settings_path, 'w') as f:
                    json.dump(self.ball_profiles, f, indent=4)
                print(f"✅ Ball settings saved to {ball_settings_path}")
            except Exception as e:
                print(f"❌ Error saving ball settings: {e}")

        def auto_calibrate_hues(self):
            """Auto-calibrate hue ranges from current color calibration"""
            # This will trigger the existing color calibration system to update hue ranges
            # for all enabled balls
            print("🎯 Auto-calibrating hue ranges from current ball colors...")
            
            # Send command to engine to recalculate hue ranges from current samples
            command = juggler_pb2.CommandRequest()
            command.type = juggler_pb2.CommandRequest.CommandType.ENABLE_FEATURE
            command.feature_name = "recalculate_hue_ranges"
            
            try:
                response = self.zmq_client.send_command(command)
                if response.success:
                    print("✅ Hue ranges auto-calibrated successfully")
                    # Reload ball settings to update UI
                    self.reload_ball_profiles()
                else:
                    print(f"❌ Auto-calibration failed: {response.message}")
            except Exception as e:
                print(f"❌ Error during auto-calibration: {e}")

        def reload_ball_profiles(self):
            """Reload ball profiles from ball_settings.json and update sliders"""
            import json
            import os
            ball_settings_path = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "ball_settings.json")
            
            try:
                with open(ball_settings_path, 'r') as f:
                    self.ball_profiles = json.load(f)
                
                # Update slider values
                for ball_name, sliders in self.ball_hue_sliders.items():
                    if ball_name in self.ball_profiles:
                        hsv_data = self.ball_profiles[ball_name]
                        min_hsv = hsv_data.get('min_hsv', [0, 0, 0])
                        max_hsv = hsv_data.get('max_hsv', [180, 255, 255])
                        
                        sliders['min'].setValue(int(min_hsv[0]))
                        sliders['max'].setValue(int(max_hsv[0]))
                        sliders['min_label'].setText(str(int(min_hsv[0])))
                        sliders['max_label'].setText(str(int(max_hsv[0])))
                
                print("✅ Ball profiles reloaded")
            except Exception as e:
                print(f"❌ Error reloading ball profiles: {e}")

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
                'pose_model_toggle', 'camera_settings_combo', 'resolution_combo', 'fps_combo',
                # Tracking weight sliders
                'tc_ml_ball_weight_slider', 'tc_ml_ball_held_weight_slider',
                'tc_wrist_proximity_weight_slider',
                # Distance and state sliders
                'tc_wrist_proximity_slider', 'tc_undetected_near_hand_slider', 'tc_min_frames_slider',
                # Throw/catch sound toggles
                'tc_sound_on_catch_toggle', 'tc_sound_on_throw_toggle'
            ]
            
            for attr in required_attrs:
                if not hasattr(self, attr):
                    return {}
            
            settings = {
                'camera_settings_profile': self.camera_settings_combo.currentData(),
                'resolution': self.resolution_combo.currentText(),
                'fps': self.fps_combo.currentData(),
                'confidence_threshold': self.confidence_slider.value() / 100.0,
                'nms_threshold': self.nms_slider.value() / 100.0,
                'track_buffer': self.track_buffer_slider.value(),
                'track_thresh': self.track_thresh_slider.value() / 100.0,
                'high_thresh': self.high_thresh_slider.value() / 100.0,
                'match_thresh': self.match_thresh_slider.value() / 100.0,
                'pose_model_enabled': self.pose_model_toggle.isChecked(),
                
                # Tracking Detection settings
                'ml_ball_weight': self.tc_ml_ball_weight_slider.value() / 100.0,
                'ml_ball_held_weight': self.tc_ml_ball_held_weight_slider.value() / 100.0,
                'wrist_proximity_weight': self.tc_wrist_proximity_weight_slider.value() / 100.0,
                'wrist_proximity_threshold': self.tc_wrist_proximity_slider.value() / 100.0,  # cm to m
                'undetected_near_hand_threshold': self.tc_undetected_near_hand_slider.value() / 100.0,  # cm to m
                'min_frames_for_state_change': self.tc_min_frames_slider.value(),
                'tc_sound_on_catch': self.tc_sound_on_catch_toggle.isChecked(),
                'tc_sound_on_throw': self.tc_sound_on_throw_toggle.isChecked(),
                
                # Collapsed states for UI persistence
                'collapsed_camera': self.camera_section.is_collapsed,
                'collapsed_yolo': self.yolo_section.is_collapsed,
                'collapsed_bytetrack': self.bytetrack_section.is_collapsed,
                'collapsed_pose': self.pose_section.is_collapsed,
                'collapsed_throw_catch': self.throw_catch_section.is_collapsed,
                'collapsed_adaptive_color': self.adaptive_color_section.is_collapsed if hasattr(self, 'adaptive_color_section') else False,
                'collapsed_ball_profiles': self.ball_profiles_section.is_collapsed if hasattr(self, 'ball_profiles_section') else False,
                
                # Adaptive color settings
                'adaptive_enabled': self.adaptive_enabled_toggle.isChecked() if hasattr(self, 'adaptive_enabled_toggle') else True,
                'adaptive_success_threshold': self.adaptive_success_thresh_slider.value() / 100.0 if hasattr(self, 'adaptive_success_thresh_slider') else 0.7,
                'adaptive_failure_threshold': self.adaptive_failure_thresh_slider.value() / 100.0 if hasattr(self, 'adaptive_failure_thresh_slider') else 0.3,
                'adaptive_expansion_step': self.adaptive_expansion_slider.value() if hasattr(self, 'adaptive_expansion_slider') else 2,
                'adaptive_contraction_step': self.adaptive_contraction_slider.value() / 10.0 if hasattr(self, 'adaptive_contraction_slider') else 1.0,
                'adaptive_history_window': self.adaptive_history_slider.value() if hasattr(self, 'adaptive_history_slider') else 60,
                'adaptive_min_confidence': self.adaptive_min_confidence_slider.value() / 100.0 if hasattr(self, 'adaptive_min_confidence_slider') else 0.1
            }
            
            # Add ball profile settings
            if hasattr(self, 'ball_checkboxes') and hasattr(self, 'ball_hue_sliders'):
                ball_tracking = {}
                ball_hues = {}
                
                for ball_name, checkbox in self.ball_checkboxes.items():
                    ball_tracking[ball_name] = checkbox.isChecked()
                
                for ball_name, sliders in self.ball_hue_sliders.items():
                    ball_hues[ball_name] = {
                        'min_hue': sliders['min'].value(),
                        'max_hue': sliders['max'].value()
                    }
                
                settings['ball_tracking_enabled'] = ball_tracking
                settings['ball_hue_ranges'] = ball_hues
            
            return settings

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
            
            # Tracking Detection settings
            if 'ml_ball_weight' in settings:
                self.tc_ml_ball_weight_slider.setValue(int(settings['ml_ball_weight'] * 100))
            
            if 'ml_ball_held_weight' in settings:
                self.tc_ml_ball_held_weight_slider.setValue(int(settings['ml_ball_held_weight'] * 100))
            
            if 'wrist_proximity_weight' in settings:
                self.tc_wrist_proximity_weight_slider.setValue(int(settings['wrist_proximity_weight'] * 100))
            
            if 'wrist_proximity_threshold' in settings:
                self.tc_wrist_proximity_slider.setValue(int(settings['wrist_proximity_threshold'] * 100))  # m to cm
            
            if 'undetected_near_hand_threshold' in settings:
                self.tc_undetected_near_hand_slider.setValue(int(settings['undetected_near_hand_threshold'] * 100))  # m to cm
            
            if 'min_frames_for_state_change' in settings:
                self.tc_min_frames_slider.setValue(settings['min_frames_for_state_change'])
            
            if 'tc_sound_on_catch' in settings:
                self.tc_sound_on_catch_toggle.setChecked(settings['tc_sound_on_catch'])
            
            if 'tc_sound_on_throw' in settings:
                self.tc_sound_on_throw_toggle.setChecked(settings['tc_sound_on_throw'])
            
            # Restore collapsed states
            if 'collapsed_camera' in settings:
                if settings['collapsed_camera'] != self.camera_section.is_collapsed:
                    self.camera_section.toggle_collapsed()
            
            if 'collapsed_yolo' in settings:
                if settings['collapsed_yolo'] != self.yolo_section.is_collapsed:
                    self.yolo_section.toggle_collapsed()
            
            if 'collapsed_bytetrack' in settings:
                if settings['collapsed_bytetrack'] != self.bytetrack_section.is_collapsed:
                    self.bytetrack_section.toggle_collapsed()
            
            if 'collapsed_pose' in settings:
                if settings['collapsed_pose'] != self.pose_section.is_collapsed:
                    self.pose_section.toggle_collapsed()
            
            if 'collapsed_throw_catch' in settings:
                if settings['collapsed_throw_catch'] != self.throw_catch_section.is_collapsed:
                    self.throw_catch_section.toggle_collapsed()
            
            if 'collapsed_adaptive_color' in settings and hasattr(self, 'adaptive_color_section'):
                if settings['collapsed_adaptive_color'] != self.adaptive_color_section.is_collapsed:
                    self.adaptive_color_section.toggle_collapsed()
            
            if 'collapsed_ball_profiles' in settings and hasattr(self, 'ball_profiles_section'):
                if settings['collapsed_ball_profiles'] != self.ball_profiles_section.is_collapsed:
                    self.ball_profiles_section.toggle_collapsed()
            
            # Restore adaptive color settings
            if 'adaptive_enabled' in settings and hasattr(self, 'adaptive_enabled_toggle'):
                self.adaptive_enabled_toggle.setChecked(settings['adaptive_enabled'])
            
            if 'adaptive_success_threshold' in settings and hasattr(self, 'adaptive_success_thresh_slider'):
                self.adaptive_success_thresh_slider.setValue(int(settings['adaptive_success_threshold'] * 100))
            
            if 'adaptive_failure_threshold' in settings and hasattr(self, 'adaptive_failure_thresh_slider'):
                self.adaptive_failure_thresh_slider.setValue(int(settings['adaptive_failure_threshold'] * 100))
            
            if 'adaptive_expansion_step' in settings and hasattr(self, 'adaptive_expansion_slider'):
                self.adaptive_expansion_slider.setValue(settings['adaptive_expansion_step'])
            
            if 'adaptive_contraction_step' in settings and hasattr(self, 'adaptive_contraction_slider'):
                self.adaptive_contraction_slider.setValue(int(settings['adaptive_contraction_step'] * 10))
            
            if 'adaptive_history_window' in settings and hasattr(self, 'adaptive_history_slider'):
                self.adaptive_history_slider.setValue(settings['adaptive_history_window'])
            
            if 'adaptive_min_confidence' in settings and hasattr(self, 'adaptive_min_confidence_slider'):
                self.adaptive_min_confidence_slider.setValue(int(settings['adaptive_min_confidence'] * 100))
            
            # Restore ball profile settings
            if 'ball_tracking_enabled' in settings and hasattr(self, 'ball_checkboxes'):
                for ball_name, enabled in settings['ball_tracking_enabled'].items():
                    if ball_name in self.ball_checkboxes:
                        self.ball_checkboxes[ball_name].setChecked(enabled)
            
            # NOTE: We do NOT load ball_hue_ranges from calibration_settings.json anymore
            # Ball hue ranges should only come from ball_settings.json, which is loaded
            # when the Ball Profiles section is created. This prevents old saved settings
            # from overwriting the correct calculated ranges.
            # if 'ball_hue_ranges' in settings and hasattr(self, 'ball_hue_sliders'):
            #     for ball_name, hue_data in settings['ball_hue_ranges'].items():
            #         if ball_name in self.ball_hue_sliders:
            #             self.ball_hue_sliders[ball_name]['min'].setValue(hue_data['min_hue'])
            #             self.ball_hue_sliders[ball_name]['max'].setValue(hue_data['max_hue'])

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
        
        def test_catch_sound(self):
            """Play a test sound for catch events"""
            self.play_system_sound(frequency=800, duration=100)
            # Also send to engine
            self.update_setting('tc_test_catch_sound', 1)
            print("🔊 Playing catch test sound (800 Hz)")
        
        def test_throw_sound(self):
            """Play a test sound for throw events"""
            self.play_system_sound(frequency=1200, duration=100)
            # Also send to engine
            self.update_setting('tc_test_throw_sound', 1)
            print("🔊 Playing throw test sound (1200 Hz)")
        
        def play_system_sound(self, frequency=1000, duration=100):
            """Play a simple beep sound using system commands"""
            def play_in_thread():
                try:
                    system = platform.system()
                    if system == "Linux":
                        # Use paplay with a generated sine wave
                        subprocess.run([
                            'paplay', '--raw',
                            '/dev/stdin'
                        ], input=self.generate_sine_wave(frequency, duration),
                        timeout=1, check=False)
                    elif system == "Darwin":  # macOS
                        # Use afplay with a generated audio file
                        subprocess.run(['afplay', '/System/Library/Sounds/Pop.aiff'],
                                     timeout=1, check=False)
                    elif system == "Windows":
                        # Use winsound
                        import winsound
                        winsound.Beep(frequency, duration)
                except Exception as e:
                    print(f"⚠️ Could not play sound: {e}")
                    # Fallback: print to console
                    print(f"\a")  # Terminal bell
            
            # Play sound in background thread to avoid blocking UI
            threading.Thread(target=play_in_thread, daemon=True).start()
        
        def generate_sine_wave(self, frequency=1000, duration=100):
            """Generate a sine wave for audio playback"""
            sample_rate = 44100
            num_samples = int(sample_rate * duration / 1000)
            t = np.linspace(0, duration / 1000, num_samples, False)
            wave = np.sin(2 * np.pi * frequency * t)
            # Convert to 16-bit PCM
            audio = (wave * 32767).astype(np.int16)
            return audio.tobytes()

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
            
            # Ball management removed - using legacy color tracking only
            
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
            # Maximize window on startup
            self.showMaximized()
            
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
            
            # Left panel - Ball tracking (will extend down to activity log)
            left_panel_layout = QVBoxLayout()
            
            ball_group = QGroupBox("🏀 Ball Tracking")
            ball_layout = QVBoxLayout(ball_group)
            
            self.ball_count_label = QLabel("Balls detected: 0")
            ball_layout.addWidget(self.ball_count_label)
            
            self.ball_list = QTextEdit()
            self.ball_list.setReadOnly(True)
            ball_layout.addWidget(self.ball_list)
            
            # Calibration mode is now always on - no button needed

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

            left_panel_layout.addWidget(ball_group)

            # Center panel - Video Feed and Calibration Visualization
            center_panel_layout = QVBoxLayout()
            
            self.video_group = QGroupBox("📹 Camera Feed")
            self.video_layout = QVBoxLayout(self.video_group)
            self.video_scene = QGraphicsScene()
            self.video_view = QGraphicsView(self.video_scene)
            
            # Configure video view to minimize extra space
            self.video_view.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
            self.video_view.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
            self.video_view.setAlignment(Qt.AlignmentFlag.AlignCenter)
            self.video_view.setMaximumHeight(400)  # Limit height to about 2/3
            
            self.video_pixmap_item = QGraphicsPixmapItem()
            self.video_scene.addItem(self.video_pixmap_item)
            self.video_view.mousePressEvent = self.video_view_clicked
            self.video_layout.addWidget(self.video_view)
            
            center_panel_layout.addWidget(self.video_group)
            
            # Set calibration mode to always on
            self.calibration_mode = True
            
            # Right panel - Calibration Visualization (extends to right of Ball Tracking)
            right_panel_layout = QVBoxLayout()
            
            calibration_group = QGroupBox("🎨 Calibration & Visualization")
            calibration_layout = QVBoxLayout(calibration_group)
            
            # --- Color Profile Controls ---
            color_profile_layout = QHBoxLayout()
            color_profile_layout.addWidget(QLabel("Color Profile:"))
            
            self.color_profile_combo = QComboBox()
            self.populate_color_profiles()
            color_profile_layout.addWidget(self.color_profile_combo)
            
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
            color_profile_layout.addWidget(self.set_color_profile_button)
            calibration_layout.addLayout(color_profile_layout)
            
            # Status label
            self.color_profile_status_label = QLabel("Select a color profile and click 'Set Color Profile', then click on a ball in the video.")
            self.color_profile_status_label.setWordWrap(True)
            calibration_layout.addWidget(self.color_profile_status_label)
            
            # --- Visualization Toggles (Pipeline Steps) ---
            # Row 1: Early pipeline steps
            toggles_row1 = QHBoxLayout()
            toggles_row1.setSpacing(5)
            
            self.show_kalman_predictions_toggle = QPushButton("2. Kalman Predictions")
            self.show_kalman_predictions_toggle.setCheckable(True)
            self.show_kalman_predictions_toggle.setChecked(False)
            self.show_kalman_predictions_toggle.clicked.connect(self.toggle_overlays)
            self.show_kalman_predictions_toggle.setMaximumWidth(170)
            self.show_kalman_predictions_toggle.setToolTip("Show predicted positions from Kalman filters (blue circles)")
            toggles_row1.addWidget(self.show_kalman_predictions_toggle)
            
            self.show_raw_detections_toggle = QPushButton("3. YOLO Detections")
            self.show_raw_detections_toggle.setCheckable(True)
            self.show_raw_detections_toggle.setChecked(False)
            self.show_raw_detections_toggle.clicked.connect(self.toggle_overlays)
            self.show_raw_detections_toggle.setMaximumWidth(160)
            self.show_raw_detections_toggle.setToolTip("Show raw YOLO detections (red boxes)")
            toggles_row1.addWidget(self.show_raw_detections_toggle)
            
            self.show_filtered_detections_toggle = QPushButton("4. Filtered Detections")
            self.show_filtered_detections_toggle.setCheckable(True)
            self.show_filtered_detections_toggle.setChecked(False)
            self.show_filtered_detections_toggle.clicked.connect(self.toggle_overlays)
            self.show_filtered_detections_toggle.setMaximumWidth(170)
            self.show_filtered_detections_toggle.setToolTip("Show rejected detections (gray crossed out)")
            toggles_row1.addWidget(self.show_filtered_detections_toggle)
            
            toggles_row1.addStretch()
            calibration_layout.addLayout(toggles_row1)
            
            # Row 2: Matching and initialization
            toggles_row2 = QHBoxLayout()
            toggles_row2.setSpacing(5)
            
            self.show_associations_toggle = QPushButton("5. 3D Matching")
            self.show_associations_toggle.setCheckable(True)
            self.show_associations_toggle.setChecked(False)
            self.show_associations_toggle.clicked.connect(self.toggle_overlays)
            self.show_associations_toggle.setMaximumWidth(140)
            self.show_associations_toggle.setToolTip("Show tracker-detection associations (green lines)")
            toggles_row2.addWidget(self.show_associations_toggle)
            
            self.show_new_trackers_toggle = QPushButton("6. Auto-Init")
            self.show_new_trackers_toggle.setCheckable(True)
            self.show_new_trackers_toggle.setChecked(False)
            self.show_new_trackers_toggle.clicked.connect(self.toggle_overlays)
            self.show_new_trackers_toggle.setMaximumWidth(120)
            self.show_new_trackers_toggle.setToolTip("Show newly initialized trackers (yellow stars)")
            toggles_row2.addWidget(self.show_new_trackers_toggle)
            
            self.show_hand_tracking_toggle = QPushButton("7. Hand Tracking")
            self.show_hand_tracking_toggle.setCheckable(True)
            self.show_hand_tracking_toggle.setChecked(False)
            self.show_hand_tracking_toggle.clicked.connect(self.toggle_overlays)
            self.show_hand_tracking_toggle.setMaximumWidth(150)
            self.show_hand_tracking_toggle.setToolTip("Show hand detections (purple boxes)")
            toggles_row2.addWidget(self.show_hand_tracking_toggle)
            
            toggles_row2.addStretch()
            calibration_layout.addLayout(toggles_row2)
            
            # Row 3: State and occlusion
            toggles_row3 = QHBoxLayout()
            toggles_row3.setSpacing(5)
            
            self.show_ball_states_toggle = QPushButton("8. Throw/Catch States")
            self.show_ball_states_toggle.setCheckable(True)
            self.show_ball_states_toggle.setChecked(False)
            self.show_ball_states_toggle.clicked.connect(self.toggle_overlays)
            self.show_ball_states_toggle.setMaximumWidth(180)
            self.show_ball_states_toggle.setToolTip("Show ball states (orange indicators with labels)")
            toggles_row3.addWidget(self.show_ball_states_toggle)
            
            self.show_occlusion_toggle = QPushButton("9. Occlusion")
            self.show_occlusion_toggle.setCheckable(True)
            self.show_occlusion_toggle.setChecked(False)
            self.show_occlusion_toggle.clicked.connect(self.toggle_overlays)
            self.show_occlusion_toggle.setMaximumWidth(130)
            self.show_occlusion_toggle.setToolTip("Show occlusion states")
            toggles_row3.addWidget(self.show_occlusion_toggle)
            
            self.show_skeleton_toggle = QPushButton("10. Pose Skeleton")
            self.show_skeleton_toggle.setCheckable(True)
            self.show_skeleton_toggle.setChecked(False)
            self.show_skeleton_toggle.clicked.connect(self.toggle_overlays)
            self.show_skeleton_toggle.setMaximumWidth(150)
            self.show_skeleton_toggle.setToolTip("Show pose estimation keypoints (cyan skeleton)")
            toggles_row3.addWidget(self.show_skeleton_toggle)
            
            toggles_row3.addStretch()
            calibration_layout.addLayout(toggles_row3)
            
            # Row 4: Color tracking and final results
            toggles_row4 = QHBoxLayout()
            toggles_row4.setSpacing(5)
            
            self.show_color_search_toggle = QPushButton("11. Color Search")
            self.show_color_search_toggle.setCheckable(True)
            self.show_color_search_toggle.setChecked(False)
            self.show_color_search_toggle.clicked.connect(self.toggle_overlays)
            self.show_color_search_toggle.setMaximumWidth(150)
            self.show_color_search_toggle.setToolTip("Show color tracking search regions")
            toggles_row4.addWidget(self.show_color_search_toggle)
            
            self.show_color_tracker_toggle = QPushButton("11. Color Tracking")
            self.show_color_tracker_toggle.setCheckable(True)
            self.show_color_tracker_toggle.setChecked(True)
            self.show_color_tracker_toggle.clicked.connect(self.toggle_overlays)
            self.show_color_tracker_toggle.setMaximumWidth(160)
            self.show_color_tracker_toggle.setToolTip("Show color-tracked balls (colored circles)")
            toggles_row4.addWidget(self.show_color_tracker_toggle)
            
            self.show_tracked_boxes_toggle = QPushButton("12. Final Trackers")
            self.show_tracked_boxes_toggle.setCheckable(True)
            self.show_tracked_boxes_toggle.setChecked(False)
            self.show_tracked_boxes_toggle.clicked.connect(self.toggle_overlays)
            self.show_tracked_boxes_toggle.setMaximumWidth(150)
            self.show_tracked_boxes_toggle.setToolTip("Show final persistent tracker boxes (thick green)")
            toggles_row4.addWidget(self.show_tracked_boxes_toggle)
            
            toggles_row4.addStretch()
            calibration_layout.addLayout(toggles_row4)
            
            # Row 5: Unmatched and utility
            toggles_row5 = QHBoxLayout()
            toggles_row5.setSpacing(5)
            
            self.show_unmatched_detections_toggle = QPushButton("13. Unmatched")
            self.show_unmatched_detections_toggle.setCheckable(True)
            self.show_unmatched_detections_toggle.setChecked(True)
            self.show_unmatched_detections_toggle.clicked.connect(self.toggle_overlays)
            self.show_unmatched_detections_toggle.setMaximumWidth(140)
            self.show_unmatched_detections_toggle.setToolTip("Show unmatched detections (yellow boxes)")
            toggles_row5.addWidget(self.show_unmatched_detections_toggle)
            
            self.show_tails_toggle = QPushButton("Show Tails")
            self.show_tails_toggle.setCheckable(True)
            self.show_tails_toggle.setChecked(False)
            self.show_tails_toggle.clicked.connect(self.toggle_overlays)
            self.show_tails_toggle.setMaximumWidth(110)
            self.show_tails_toggle.setToolTip("Show tracker history trails")
            toggles_row5.addWidget(self.show_tails_toggle)
            
            self.hide_video_feed_toggle = QPushButton("Hide Video Feed")
            self.hide_video_feed_toggle.setCheckable(True)
            self.hide_video_feed_toggle.setChecked(False)
            self.hide_video_feed_toggle.clicked.connect(self.toggle_overlays)
            self.hide_video_feed_toggle.setToolTip("Hide the video feed but keep overlays visible")
            self.hide_video_feed_toggle.setMaximumWidth(140)
            toggles_row5.addWidget(self.hide_video_feed_toggle)
            
            toggles_row5.addStretch()
            calibration_layout.addLayout(toggles_row5)

            # --- Tail Length Slider ---
            tail_layout = QHBoxLayout()
            tail_layout.addWidget(QLabel("Tail Length:"))
            self.tail_length_slider = QSlider(Qt.Orientation.Horizontal)
            self.tail_length_slider.setRange(10, 200)
            self.tail_length_slider.setValue(50)
            self.tail_length_slider.valueChanged.connect(self.update_tail_length)
            tail_layout.addWidget(self.tail_length_slider)
            self.tail_length_label = QLabel("50 frames")
            tail_layout.addWidget(self.tail_length_label)
            calibration_layout.addLayout(tail_layout)
            
            # Add Calibration Visualization under Camera Feed
            center_panel_layout.addWidget(calibration_group)
            
            content_layout.addLayout(center_panel_layout, 2)
            
            # Right panel - Tracking Settings and System Status
            # Add Tracking Settings directly (no tab wrapper)
            settings_group = QGroupBox("⚙️ Tracking Settings")
            settings_layout = QVBoxLayout(settings_group)
            
            # Create settings widget and add it directly
            self.settings_widget = CalibrationSettingsWidget(self.udp_client, self.zmq_client, self.hub_instance)
            settings_layout.addWidget(self.settings_widget)
            
            right_panel_layout.addWidget(settings_group)
            
            # System Status section
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
            
            right_panel_layout.addWidget(system_group)
            
            # Add all panels to content layout
            content_layout.addLayout(left_panel_layout)
            content_layout.addLayout(right_panel_layout)
            
            main_layout.addLayout(content_layout)
            
            # Bottom panel - Log
            log_group = QGroupBox("📝 Activity Log")
            log_layout = QVBoxLayout(log_group)
            
            # Add control buttons for the log
            log_controls_layout = QHBoxLayout()
            
            self.log_paused = True  # Start paused by default
            self.pause_log_button = QPushButton("▶ Resume Log")
            self.pause_log_button.setCheckable(True)
            self.pause_log_button.setChecked(True)  # Start checked (paused)
            self.pause_log_button.clicked.connect(self.toggle_log_pause)
            self.pause_log_button.setStyleSheet("""
                QPushButton {
                    background-color: #555;
                    color: white;
                    border: 1px solid #777;
                    padding: 5px;
                    border-radius: 3px;
                }
                QPushButton:checked {
                    background-color: #f44336;
                    border-color: #d32f2f;
                }
            """)
            log_controls_layout.addWidget(self.pause_log_button)
            
            self.clear_log_button = QPushButton("🗑 Clear Log")
            self.clear_log_button.clicked.connect(self.clear_log)
            log_controls_layout.addWidget(self.clear_log_button)
            
            log_controls_layout.addStretch()
            log_layout.addLayout(log_controls_layout)
            
            self.log_text = QTextEdit()
            self.log_text.setMaximumHeight(150)
            self.log_text.setReadOnly(True)
            log_layout.addWidget(self.log_text)
            
            main_layout.addWidget(log_group)
            
            self.apply_dark_theme()
            self.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
        
        def create_menu_bar(self):
            """Create the menu bar with File, App, and Help menus."""
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
            color_profiles_action.triggered.connect(self.open_color_profile_manager)
            file_menu.addAction(color_profiles_action)
            
            # App menu
            app_menu = menubar.addMenu("&App")
            
            # Recent Apps submenu
            self.recent_apps_menu = app_menu.addMenu("&Recent Apps")
            self.update_recent_apps_menu()
            
            app_menu.addSeparator()
            
            # App Manager action
            app_manager_action = QAction("App &Manager...", self)
            app_manager_action.setShortcut("Ctrl+M")
            app_manager_action.triggered.connect(self.open_app_manager)
            app_menu.addAction(app_manager_action)
        
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
           
            # Help menu
            help_menu = menubar.addMenu("&Help")
            
            # About action
            about_action = QAction("&About", self)
            about_action.triggered.connect(self.show_about_dialog)
            help_menu.addAction(about_action)
        
        def update_recent_apps_menu(self):
            """Update the Recent Apps submenu with recently used apps."""
            self.recent_apps_menu.clear()
            
            if not hasattr(self.hub_instance, 'app_manager') or not self.hub_instance.app_manager:
                no_apps_action = QAction("No recent apps", self)
                no_apps_action.setEnabled(False)
                self.recent_apps_menu.addAction(no_apps_action)
                return
            
            recent_apps = self.hub_instance.app_manager.get_recent_apps(5)
            
            if not recent_apps:
                no_apps_action = QAction("No recent apps", self)
                no_apps_action.setEnabled(False)
                self.recent_apps_menu.addAction(no_apps_action)
            else:
                for app_id in recent_apps:
                    app_info = self.hub_instance.app_manager.get_app_info(app_id)
                    if app_info:
                        action = QAction(app_info['name'], self)
                        action.triggered.connect(lambda checked, aid=app_id: self.launch_app(aid))
                        self.recent_apps_menu.addAction(action)
        
        def open_app_manager(self):
            """Open the App Manager dialog."""
            if not hasattr(self.hub_instance, 'app_manager') or not self.hub_instance.app_manager:
                QMessageBox.warning(self, "App Manager", "App Manager is not initialized.")
                return
            
            from .app_manager_dialog import AppManagerDialog
            dialog = AppManagerDialog(self.hub_instance.app_manager, self)
            dialog.app_launched.connect(self.on_app_launched)
            dialog.exec()
            
            # Update recent apps menu after dialog closes
            self.update_recent_apps_menu()
        
        def launch_app(self, app_id: str):
            """Launch an app by its ID."""
            if not hasattr(self.hub_instance, 'app_manager') or not self.hub_instance.app_manager:
                QMessageBox.warning(self, "Launch App", "App Manager is not initialized.")
                return
            
            try:
                self.hub_instance.app_manager.launch_app(app_id)
                self.log_message(f"✅ Launched app: {app_id}")
                self.update_recent_apps_menu()
            except Exception as e:
                QMessageBox.critical(self, "Launch Error", f"Failed to launch app '{app_id}':\n{str(e)}")
                self.log_message(f"❌ Failed to launch app '{app_id}': {e}")
        
        def on_app_launched(self, app_id: str):
            """Handle app launched signal from App Manager dialog."""
            self.log_message(f"✅ App launched: {app_id}")
            self.update_recent_apps_menu()
        
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
            
            # Check for throw/catch events and play sounds if enabled
            if hasattr(frame_data, 'throw_catch_events'):
                for event in frame_data.throw_catch_events:
                    if event.type == juggler_pb2.ThrowCatchEvent.CATCH:
                        if self.settings_widget.tc_sound_on_catch_toggle.isChecked():
                            self.settings_widget.play_system_sound(frequency=800, duration=100)
                            self.log_message(f"🔊 CATCH sound played for Ball {event.ball_id} by Hand {event.hand_id}")
                    elif event.type == juggler_pb2.ThrowCatchEvent.THROW:
                        if self.settings_widget.tc_sound_on_throw_toggle.isChecked():
                            self.settings_widget.play_system_sound(frequency=1200, duration=100)
                            self.log_message(f"🔊 THROW sound played for Ball {event.ball_id} by Hand {event.hand_id}")
            
            ball_count = len(frame_data.balls)
            self.ball_count_label.setText(f"Balls detected: {ball_count}")
            
            # Get color map from profile manager for text coloring
            color_name_map = self.color_profile_manager.get_color_map()
            
            # Build HTML formatted text with colored ball information
            ball_html = ""
            # Define a mapping from enum to string for display
            status_map = {
                juggler_pb2.Ball.TRACKED: "Tracked",
                juggler_pb2.Ball.PREDICTED: "Predicted",
                juggler_pb2.Ball.OCCLUDED: "Occluded",
            }

            for ball in frame_data.balls:
                status_str = status_map.get(ball.status, "Unknown")
                
                # Get color information from color_tracked_balls
                color_ball = next((cb for cb in frame_data.color_tracked_balls if cb.logical_id == ball.logical_id), None)
                
                # Determine the color for this ball's text
                if color_ball and color_ball.color_name:
                    qcolor = color_name_map.get(color_ball.color_name.lower(), QColor(255, 255, 255))
                    # Convert QColor to hex for HTML
                    color_hex = qcolor.name()
                    state_str = "HELD" if color_ball.associated_wrist_id >= 0 else "IN AIR"
                    hand_str = f" by {'LEFT' if color_ball.associated_wrist_id == 0 else 'RIGHT'}" if color_ball.associated_wrist_id >= 0 else ""
                    color_info = f"Color: {color_ball.color_name.upper()}, State: {state_str}{hand_str}"
                else:
                    color_hex = "#ffffff"
                    color_info = "Color: Unknown, State: Unknown"
                
                # Create HTML formatted text with color
                ball_html += f'<span style="color: {color_hex};">'
                ball_html += f'Ball {ball.logical_id} ({status_str}): 3D({ball.position.x:.3f}, {ball.position.y:.3f}, {ball.position.z:.3f})<br>'
                ball_html += f'&nbsp;&nbsp;{color_info}<br>'
                
                # Determine ML detection status - check if there's an actual YOLO detection box
                # by looking for a matching raw_detection near this ball's position
                has_yolo_detection = False
                yolo_class_id = -1
                
                # Check if any raw_detection is close to this ball's 2D position
                if ball.projected_pos_2d and ball.projected_pos_2d.x > 0 and ball.projected_pos_2d.y > 0:
                    for raw_det in frame_data.raw_detections:
                        # Check if detection box contains the ball's projected position
                        det_center_x = raw_det.x + raw_det.width / 2
                        det_center_y = raw_det.y + raw_det.height / 2
                        ball_x = ball.projected_pos_2d.x
                        ball_y = ball.projected_pos_2d.y
                        
                        # Check if ball position is within detection box
                        if (raw_det.x <= ball_x <= raw_det.x + raw_det.width and
                            raw_det.y <= ball_y <= raw_det.y + raw_det.height):
                            has_yolo_detection = True
                            yolo_class_id = raw_det.class_id
                            break
                
                # Display ML detection status based on whether YOLO actually sees it
                if has_yolo_detection:
                    if yolo_class_id == 0:
                        ml_status = "ball (in-air)"
                    elif yolo_class_id == 1:
                        ml_status = "ball_held"
                    else:
                        ml_status = f"class_{yolo_class_id}"
                else:
                    # No YOLO detection box found
                    ml_status = "gone"
                
                ball_html += f'&nbsp;&nbsp;ML Detection: {ml_status}<br>'
                
                # Add distance to nearest wrist
                if hasattr(ball, 'distance_to_nearest_wrist') and ball.distance_to_nearest_wrist >= 0:
                    dist_cm = ball.distance_to_nearest_wrist * 100  # Convert m to cm
                    ball_html += f'&nbsp;&nbsp;Distance to Wrist: {dist_cm:.1f}cm<br>'
                else:
                    ball_html += f'&nbsp;&nbsp;Distance to Wrist: N/A<br>'
                
                ball_html += '</span>'
                
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

            self.ball_list.setHtml(ball_html)

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
        
        def toggle_log_pause(self):
            """Toggle the activity log pause state."""
            self.log_paused = self.pause_log_button.isChecked()
            if self.log_paused:
                self.pause_log_button.setText("▶ Resume Log")
            else:
                self.pause_log_button.setText("⏸ Pause Log")
        
        def clear_log(self):
            """Clear all messages from the activity log."""
            self.log_text.clear()
        
        def log_message(self, message: str):
            # Don't add messages if log is paused
            if self.log_paused:
                return
            
            timestamp = time.strftime("%H:%M:%S")
            self.log_text.append(f"[{timestamp}] {message}")
            if self.log_text.document().blockCount() > 100:
                cursor = self.log_text.textCursor()
                cursor.movePosition(cursor.MoveOperation.Start)
                cursor.select(cursor.SelectionType.BlockUnderCursor)
                cursor.removeSelectedText()
        
        # Calibration mode is now always on - no toggle needed

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

            # Check if video feed should be hidden
            if self.hide_video_feed_toggle.isChecked():
                # Create a blank black image with the same dimensions
                pixmap = QPixmap(image.width(), image.height())
                pixmap.fill(QColor(0, 0, 0))  # Fill with black
            else:
                # Use the actual video frame
                pixmap = QPixmap.fromImage(image)
            
            painter = QPainter(pixmap)
            
            # --- Draw Kalman Predictions (Step 2) ---
            if self.show_kalman_predictions_toggle.isChecked():
                painter.setPen(QPen(QColor(0, 0, 255, 150), 3))  # Blue
                painter.setBrush(Qt.BrushStyle.NoBrush)
                
                # Debug logging
                if len(frame_data.kalman_predictions) > 0:
                    self.log_message(f"[KALMAN VIZ] Rendering {len(frame_data.kalman_predictions)} Kalman predictions")
                
                # Get camera intrinsics from the first ball (if available)
                if frame_data.balls:
                    # We need intrinsics to project 3D to 2D
                    # For now, use a simple projection assuming we have the data
                    pass
                
                for i, pred in enumerate(frame_data.kalman_predictions):
                    # Project 3D predicted position to 2D
                    if pred.predicted_pos.z > 0:
                        # Simple projection (you may need to get actual intrinsics)
                        # For now, we'll use approximate values or get from first ball
                        # Assuming standard D455 intrinsics at 640x480
                        fx = 385.0  # Approximate
                        fy = 385.0
                        ppx = 320.0
                        ppy = 240.0
                        
                        x_2d = int((pred.predicted_pos.x * fx) / pred.predicted_pos.z + ppx)
                        y_2d = int((pred.predicted_pos.y * fy) / pred.predicted_pos.z + ppy)
                        
                        if i == 0:  # Log first prediction for debugging
                            self.log_message(f"[KALMAN VIZ]   Pred {i}: 3D({pred.predicted_pos.x:.3f}, {pred.predicted_pos.y:.3f}, {pred.predicted_pos.z:.3f}) -> 2D({x_2d}, {y_2d})")
                        
                        radius = 8
                        painter.drawEllipse(x_2d - radius, y_2d - radius, radius * 2, radius * 2)
                        
                        # Draw label
                        painter.setPen(QPen(QColor(255, 255, 255)))
                        painter.setFont(QFont("Arial", 8))
                        painter.drawText(x_2d + 10, y_2d, f"KF-{pred.logical_id}")
                        painter.setPen(QPen(QColor(0, 0, 255, 150), 3))
            
            # --- Draw YOLO Detections (Step 3) ---
            if self.show_raw_detections_toggle.isChecked():
                painter.setPen(QPen(QColor(255, 0, 0, 100), 2)) # Semi-transparent red
                for det in frame_data.raw_detections:
                    painter.drawRect(int(det.x), int(det.y), int(det.width), int(det.height))
            
            # --- Draw Filtered Detections (Step 4) ---
            if self.show_filtered_detections_toggle.isChecked():
                painter.setPen(QPen(QColor(128, 128, 128, 150), 2))  # Gray
                for det in frame_data.filtered_detections:
                    # Draw crossed-out box
                    x, y, w, h = int(det.box.x), int(det.box.y), int(det.box.width), int(det.box.height)
                    painter.drawRect(x, y, w, h)
                    painter.drawLine(x, y, x + w, y + h)
                    painter.drawLine(x + w, y, x, y + h)
            
            # --- Draw 3D Matching/Associations (Step 5) ---
            if self.show_associations_toggle.isChecked():
                painter.setPen(QPen(QColor(0, 255, 0, 200), 2))  # Green lines
                painter.setFont(QFont("Arial", 9, QFont.Weight.Bold))
                
                # Debug logging
                if len(frame_data.tracker_associations) > 0:
                    self.log_message(f"[3D MATCH VIZ] Rendering {len(frame_data.tracker_associations)} associations")
                
                for i, assoc in enumerate(frame_data.tracker_associations):
                    # Project 3D positions to 2D for visualization
                    # Get camera intrinsics
                    fx = frame_data.intrinsics.fx if frame_data.HasField('intrinsics') else 385.0
                    fy = frame_data.intrinsics.fy if frame_data.HasField('intrinsics') else 385.0
                    ppx = frame_data.intrinsics.ppx if frame_data.HasField('intrinsics') else 320.0
                    ppy = frame_data.intrinsics.ppy if frame_data.HasField('intrinsics') else 240.0
                    
                    # Project tracker position
                    if assoc.tracker_pos.z > 0:
                        tracker_x = int((assoc.tracker_pos.x * fx) / assoc.tracker_pos.z + ppx)
                        tracker_y = int((assoc.tracker_pos.y * fy) / assoc.tracker_pos.z + ppy)
                    else:
                        continue
                    
                    # Project detection position
                    if assoc.detection_pos.z > 0:
                        det_x = int((assoc.detection_pos.x * fx) / assoc.detection_pos.z + ppx)
                        det_y = int((assoc.detection_pos.y * fy) / assoc.detection_pos.z + ppy)
                    else:
                        continue
                    
                    if i == 0:  # Log first association for debugging
                        self.log_message(f"[3D MATCH VIZ]   Assoc {i}: Tracker {assoc.tracker_id} -> Det {assoc.detection_index}, dist={assoc.distance_3d:.3f}m")
                        self.log_message(f"[3D MATCH VIZ]   Tracker 2D: ({tracker_x}, {tracker_y}), Det 2D: ({det_x}, {det_y})")
                    
                    # Draw line from tracker to detection
                    painter.drawLine(tracker_x, tracker_y, det_x, det_y)
                    
                    # Draw distance label at midpoint
                    mid_x = (tracker_x + det_x) // 2
                    mid_y = (tracker_y + det_y) // 2
                    
                    # Draw background for text
                    distance_cm = assoc.distance_3d * 100  # Convert to cm
                    label_text = f"T{assoc.tracker_id}→D{assoc.detection_index} ({distance_cm:.1f}cm)"
                    
                    painter.setPen(QPen(QColor(0, 0, 0, 200)))
                    painter.setBrush(QBrush(QColor(0, 0, 0, 150)))
                    text_width = 120
                    text_height = 18
                    painter.drawRect(mid_x - text_width//2, mid_y - text_height//2, text_width, text_height)
                    
                    # Draw text
                    painter.setPen(QPen(QColor(0, 255, 0)))
                    painter.drawText(mid_x - text_width//2 + 5, mid_y + 5, label_text)
                    
                    # Reset pen for next line
                    painter.setPen(QPen(QColor(0, 255, 0, 200), 2))
            
            # --- Draw New Trackers/Auto-Init (Step 6) ---
            if self.show_new_trackers_toggle.isChecked():
                painter.setPen(QPen(QColor(255, 255, 0, 200), 3))  # Bright yellow
                painter.setBrush(QBrush(QColor(255, 255, 0, 100)))
                for ball in frame_data.balls:
                    if hasattr(ball, 'is_new_tracker') and ball.is_new_tracker:
                        # Draw star marker
                        center_x = int(ball.projected_pos_2d.x)
                        center_y = int(ball.projected_pos_2d.y)
                        size = 15
                        # Draw a simple star shape
                        painter.drawEllipse(center_x - size, center_y - size, size * 2, size * 2)
                        painter.drawText(center_x + size + 5, center_y, "NEW")

            # --- Draw Unmatched Detections (Step 13) ---
            if self.show_unmatched_detections_toggle.isChecked():
                painter.setPen(QPen(QColor(255, 255, 0, 150), 2)) # Yellow
                for det in frame_data.unmatched_detections:
                    painter.drawRect(int(det.x), int(det.y), int(det.width), int(det.height))

            # --- Draw ByteTrack Boxes (Step 12) ---
            if self.show_tracked_boxes_toggle.isChecked():
                painter.setPen(QPen(QColor(0, 255, 0, 200), 3))  # Thick green
                for obj in frame_data.balls:
                    if obj.status == juggler_pb2.Ball.TRACKED:
                        bbox = obj.bounding_box_2d
                        painter.drawRect(int(bbox.x), int(bbox.y), int(bbox.width), int(bbox.height))

            # --- Draw Hand Tracking (Step 7) ---
            if self.show_hand_tracking_toggle.isChecked():
                painter.setPen(QPen(QColor(128, 0, 128, 150), 2))  # Purple
                painter.setBrush(Qt.BrushStyle.NoBrush)
                for hand in frame_data.hands:
                    # Draw purple box around hand detection
                    if hasattr(hand, 'bounding_box_2d'):
                        bbox = hand.bounding_box_2d
                        painter.drawRect(int(bbox.x), int(bbox.y), int(bbox.width), int(bbox.height))
                    # Draw wrist position
                    center_x, center_y = int(hand.position_2d.x), int(hand.position_2d.y)
                    painter.drawEllipse(center_x - 5, center_y - 5, 10, 10)
                    # Label
                    painter.setPen(QPen(QColor(255, 255, 255)))
                    painter.setFont(QFont("Arial", 10, QFont.Weight.Bold))
                    side_label = "L" if hand.side == "left" else "R"
                    painter.drawText(center_x + 10, center_y, f"Hand-{side_label}")
                    painter.setPen(QPen(QColor(128, 0, 128, 150), 2))
            
            # --- Draw Ball States (Step 8) ---
            if self.show_ball_states_toggle.isChecked():
                painter.setFont(QFont("Arial", 9, QFont.Weight.Bold))
                for ball in frame_data.balls:
                    if hasattr(ball, 'throw_catch_state'):
                        center_x = int(ball.projected_pos_2d.x)
                        center_y = int(ball.projected_pos_2d.y)
                        
                        # Draw state indicator
                        state_text = ""
                        state_color = QColor(255, 165, 0)  # Orange
                        
                        if ball.throw_catch_state == juggler_pb2.Ball.THROWN:
                            state_text = "THROW"
                            state_color = QColor(255, 100, 0)
                        elif ball.throw_catch_state == juggler_pb2.Ball.CAUGHT:
                            state_text = "CATCH"
                            state_color = QColor(0, 255, 100)
                        elif ball.throw_catch_state == juggler_pb2.Ball.IN_FLIGHT:
                            state_text = "FLIGHT"
                            state_color = QColor(100, 150, 255)
                        
                        if state_text:
                            # Draw background rectangle
                            painter.setBrush(QBrush(QColor(0, 0, 0, 180)))
                            painter.setPen(QPen(state_color, 2))
                            text_width = 60
                            text_height = 20
                            painter.drawRect(center_x - text_width//2, center_y - 30, text_width, text_height)
                            
                            # Draw text
                            painter.setPen(QPen(state_color))
                            painter.drawText(center_x - text_width//2 + 5, center_y - 15, state_text)
            
            # --- Draw Occlusion (Step 9) ---
            if self.show_occlusion_toggle.isChecked():
                for ball in frame_data.balls:
                    if ball.status == juggler_pb2.Ball.OCCLUDED:
                        center_x = int(ball.projected_pos_2d.x)
                        center_y = int(ball.projected_pos_2d.y)
                        
                        # Draw red X over occluded balls
                        painter.setPen(QPen(QColor(255, 0, 0, 200), 3))
                        size = 15
                        painter.drawLine(center_x - size, center_y - size, center_x + size, center_y + size)
                        painter.drawLine(center_x + size, center_y - size, center_x - size, center_y + size)
                        
                        # Draw "OCCLUDED" label
                        painter.setFont(QFont("Arial", 9, QFont.Weight.Bold))
                        painter.setPen(QPen(QColor(255, 0, 0)))
                        painter.drawText(center_x + 20, center_y, "OCCLUDED")
            
            # --- Draw Color Search Regions (Step 11) ---
            if self.show_color_search_toggle.isChecked():
                painter.setPen(QPen(QColor(255, 255, 0, 100), 1, Qt.PenStyle.DotLine))  # Yellow dotted
                painter.setBrush(Qt.BrushStyle.NoBrush)
                for ball in frame_data.balls:
                    if hasattr(ball, 'color_search_region'):
                        region = ball.color_search_region
                        painter.drawRect(int(region.x), int(region.y), int(region.width), int(region.height))
            
            # --- Draw Color Trackers (Step 11 - NEW SIMPLIFIED SYSTEM) ---
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

            # --- Draw Pose Skeleton (Step 10) ---
            if self.show_skeleton_toggle.isChecked():
                self.log_message(f"UI: Drawing skeleton for {len(frame_data.hands)} hands")
                
                # Get frame dimensions for bounds checking
                frame_width = pixmap.width()
                frame_height = pixmap.height()
                
                # Draw hand wrist markers with bright cyan
                painter.setPen(QPen(QColor(0, 255, 255), 4)) # Cyan, thick line
                painter.setBrush(Qt.BrushStyle.NoBrush)
                for hand in frame_data.hands:
                    # Validate wrist position before drawing
                    if hand.position_2d.x > 0 and hand.position_2d.y > 0 and \
                       hand.position_2d.x < frame_width and hand.position_2d.y < frame_height:
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
                        painter.setPen(QPen(QColor(0, 255, 255), 4))
                
                # Draw all body keypoints
                painter.setPen(QPen(QColor(0, 255, 255, 200), 3)) # Cyan for skeleton
                painter.setBrush(QBrush(QColor(0, 255, 255, 150)))
                for hand in frame_data.hands:
                    self.log_message(f"UI: Hand has {len(hand.keypoints)} keypoints")
                    for i, kp in enumerate(hand.keypoints):
                        # Validate keypoint position and confidence
                        if kp.confidence > 0.3 and \
                           kp.pos_2d.x > 0 and kp.pos_2d.y > 0 and \
                           kp.pos_2d.x < frame_width and kp.pos_2d.y < frame_height:
                            self.log_message(f"UI: Drawing keypoint {i} at ({kp.pos_2d.x:.1f}, {kp.pos_2d.y:.1f})")
                            # Draw filled circle for keypoint
                            painter.drawEllipse(int(kp.pos_2d.x) - 4, int(kp.pos_2d.y) - 4, 8, 8)
                        else:
                            self.log_message(f"UI: Skipping keypoint {i} (confidence {kp.confidence:.2f} or invalid position)")
                    
                    # Draw skeleton connections if we have enough keypoints
                    if len(hand.keypoints) >= 17:  # YOLO pose has 17 keypoints
                        # Define skeleton connections (COCO format)
                        skeleton_pairs = [
                            (5, 6), (5, 7), (7, 9), (6, 8), (8, 10),  # Arms
                            (5, 11), (6, 12), (11, 12),  # Torso
                            (11, 13), (13, 15), (12, 14), (14, 16)  # Legs
                        ]
                        
                        painter.setPen(QPen(QColor(0, 255, 255, 150), 2))
                        for start_idx, end_idx in skeleton_pairs:
                            if start_idx < len(hand.keypoints) and end_idx < len(hand.keypoints):
                                kp_start = hand.keypoints[start_idx]
                                kp_end = hand.keypoints[end_idx]
                                # Validate BOTH keypoints before drawing line
                                if kp_start.confidence > 0.3 and kp_end.confidence > 0.3 and \
                                   kp_start.pos_2d.x > 0 and kp_start.pos_2d.y > 0 and \
                                   kp_end.pos_2d.x > 0 and kp_end.pos_2d.y > 0 and \
                                   kp_start.pos_2d.x < frame_width and kp_start.pos_2d.y < frame_height and \
                                   kp_end.pos_2d.x < frame_width and kp_end.pos_2d.y < frame_height:
                                    painter.drawLine(
                                        int(kp_start.pos_2d.x), int(kp_start.pos_2d.y),
                                        int(kp_end.pos_2d.x), int(kp_end.pos_2d.y)
                                    )

            painter.end()
            self.video_pixmap_item.setPixmap(pixmap)
            
            # Update scene rect to match pixmap size exactly
            self.video_scene.setSceneRect(self.video_pixmap_item.boundingRect())
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
            
            # Create visualization states from current toggle states
            viz_states = juggler_pb2.VisualizationStates()
            viz_states.show_kalman_predictions = self.show_kalman_predictions_toggle.isChecked()
            viz_states.show_raw_detections = self.show_raw_detections_toggle.isChecked()
            viz_states.show_filtered_detections = self.show_filtered_detections_toggle.isChecked()
            viz_states.show_associations = self.show_associations_toggle.isChecked()
            viz_states.show_new_trackers = self.show_new_trackers_toggle.isChecked()
            viz_states.show_hand_tracking = self.show_hand_tracking_toggle.isChecked()
            viz_states.show_ball_states = self.show_ball_states_toggle.isChecked()
            viz_states.show_occlusion = self.show_occlusion_toggle.isChecked()
            viz_states.show_skeleton = self.show_skeleton_toggle.isChecked()
            viz_states.show_color_search = self.show_color_search_toggle.isChecked()
            viz_states.show_color_tracker = self.show_color_tracker_toggle.isChecked()
            viz_states.show_tracked_boxes = self.show_tracked_boxes_toggle.isChecked()
            viz_states.show_unmatched_detections = self.show_unmatched_detections_toggle.isChecked()
            viz_states.show_tails = self.show_tails_toggle.isChecked()
            
            command = juggler_pb2.CommandRequest(
                type=juggler_pb2.CommandRequest.CommandType.RECORD_START,
                record_with_yolo_boxes=self.show_raw_detections_toggle.isChecked(),
                record_with_bytetrack_boxes=self.show_tracked_boxes_toggle.isChecked()
            )
            command.visualization_states.CopyFrom(viz_states)
            
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
            
            # Add visualization states if starting recording
            if is_starting:
                viz_states = juggler_pb2.VisualizationStates()
                viz_states.show_kalman_predictions = self.show_kalman_predictions_toggle.isChecked()
                viz_states.show_raw_detections = self.show_raw_detections_toggle.isChecked()
                viz_states.show_filtered_detections = self.show_filtered_detections_toggle.isChecked()
                viz_states.show_associations = self.show_associations_toggle.isChecked()
                viz_states.show_new_trackers = self.show_new_trackers_toggle.isChecked()
                viz_states.show_hand_tracking = self.show_hand_tracking_toggle.isChecked()
                viz_states.show_ball_states = self.show_ball_states_toggle.isChecked()
                viz_states.show_occlusion = self.show_occlusion_toggle.isChecked()
                viz_states.show_skeleton = self.show_skeleton_toggle.isChecked()
                viz_states.show_color_search = self.show_color_search_toggle.isChecked()
                viz_states.show_color_tracker = self.show_color_tracker_toggle.isChecked()
                viz_states.show_tracked_boxes = self.show_tracked_boxes_toggle.isChecked()
                viz_states.show_unmatched_detections = self.show_unmatched_detections_toggle.isChecked()
                viz_states.show_tails = self.show_tails_toggle.isChecked()
                command.visualization_states.CopyFrom(viz_states)
            
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
            # Ball management calibration removed - using legacy color tracking only
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
                                self.log_message(f"✅ Color profile '{color_name}' updated successfully from YOLO detection box")
                                self.color_profile_status_label.setText(f"✅ '{color_name}' profile set! Click 'Set Color Profile' again to calibrate another color.")
                                
                                # Reload ball profiles to update the hue sliders in Ball Profiles section
                                # Add a small delay to allow the C++ engine to save the file
                                if hasattr(self, 'settings_widget') and self.settings_widget:
                                    # Use QTimer to delay the reload slightly
                                    QTimer.singleShot(200, lambda: self._reload_ball_profiles_after_calibration(color_name))
                            else:
                                # Check if the error is about no YOLO box found
                                if "No YOLO detection box found" in response.message:
                                    self.log_message(f"❌ No ball detected at click location. Please click directly on a detected ball.")
                                    self.color_profile_status_label.setText(f"❌ No ball detected! Click on a ball with a YOLO detection box.")
                                    QMessageBox.warning(self, "No Ball Detected",
                                                       "No YOLO detection box found at the click location.\n\n"
                                                       "Please click directly on a detected ball (one with a red box around it).\n\n"
                                                       "Tip: Enable 'YOLO Detections' visualization to see where balls are detected.")
                                else:
                                    self.log_message(f"❌ Failed to update color profile: {response.message}")
                                    self.color_profile_status_label.setText(f"❌ Failed to set '{color_name}' profile: {response.message}")
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
        
        def _reload_ball_profiles_after_calibration(self, color_name: str):
            """Helper method to reload ball profiles after calibration with proper error handling"""
            try:
                if hasattr(self, 'settings_widget') and self.settings_widget:
                    self.settings_widget.reload_ball_profiles()
                    self.log_message(f"🎨 Ball profile hue sliders updated for '{color_name}'")
            except Exception as e:
                self.log_message(f"⚠️ Warning: Could not reload ball profiles: {e}")

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