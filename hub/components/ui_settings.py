"""
Calibration settings widget for JuggleHub UI.
"""

import os
import json
import subprocess
import platform
import threading
from datetime import datetime
from typing import Any

import numpy as np

try:
    import juggler_pb2
except ImportError:
    print("❌ Error: Protocol Buffer files not found. Please run 'make generate-proto' first.")
    import sys
    sys.exit(1)

try:
    from PyQt6.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, QLabel, 
                                 QSlider, QPushButton, QComboBox, QGridLayout,
                                 QScrollArea, QGroupBox, QTextEdit, QMessageBox)
    from PyQt6.QtCore import Qt, QTimer
    from PyQt6.QtGui import QFont
    PYQT_AVAILABLE = True
except ImportError:
    PYQT_AVAILABLE = False

if PYQT_AVAILABLE:
    from .ui_widgets import CollapsibleGroupBox


if PYQT_AVAILABLE:
    class CalibrationSettingsWidget(QWidget):
        def __init__(self, udp_client, zmq_client, hub_instance=None, parent=None):
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
            
            self.kalman_prediction_section = self.create_kalman_prediction_section()
            container_layout.addWidget(self.kalman_prediction_section)
            
            self.color_tracker_weights_section = self.create_color_tracker_weights_section()
            container_layout.addWidget(self.color_tracker_weights_section)
            
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
                             "Range: 1-50 frames. Default: 3.\n"
                             "Higher values = more stable detection, slower response.",
                range_min=1,
                range_max=50,
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
            row += 1
            
            # Name on catches toggle with test button
            self.tc_name_on_catch_toggle = QPushButton("Name on Catches")
            self.tc_name_on_catch_toggle.setCheckable(True)
            self.tc_name_on_catch_toggle.setChecked(False)
            self.tc_name_on_catch_toggle.clicked.connect(lambda: self.update_setting('tc_name_on_catch', 1 if self.tc_name_on_catch_toggle.isChecked() else 0))
            layout.addWidget(self.tc_name_on_catch_toggle, row, 0, 1, 2)
            
            # Test catch name button
            self.tc_test_catch_name_button = QPushButton("🔊 Test")
            self.tc_test_catch_name_button.setMaximumWidth(80)
            self.tc_test_catch_name_button.clicked.connect(self.test_catch_name)
            self.tc_test_catch_name_button.setStyleSheet("""
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
            layout.addWidget(self.tc_test_catch_name_button, row, 2)
            row += 1
            
            # Name on throws toggle with test button
            self.tc_name_on_throw_toggle = QPushButton("Name on Throws")
            self.tc_name_on_throw_toggle.setCheckable(True)
            self.tc_name_on_throw_toggle.setChecked(False)
            self.tc_name_on_throw_toggle.clicked.connect(lambda: self.update_setting('tc_name_on_throw', 1 if self.tc_name_on_throw_toggle.isChecked() else 0))
            layout.addWidget(self.tc_name_on_throw_toggle, row, 0, 1, 2)
            
            # Test throw name button
            self.tc_test_throw_name_button = QPushButton("🔊 Test")
            self.tc_test_throw_name_button.setMaximumWidth(80)
            self.tc_test_throw_name_button.clicked.connect(self.test_throw_name)
            self.tc_test_throw_name_button.setStyleSheet("""
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
            layout.addWidget(self.tc_test_throw_name_button, row, 2)
            
            return section
        
        def create_kalman_prediction_section(self):
            """Create the Kalman Prediction Settings section"""
            section = CollapsibleGroupBox("🎯 Kalman Prediction", collapsed=False)
            layout = QGridLayout()
            section.get_content_layout().addLayout(layout)
            
            row = 0
            
            # Info label
            info_label = QLabel("ℹ️ Configure prediction circle based on color detection history")
            info_label.setStyleSheet("color: #aaaaaa; font-size: 10px;")
            info_label.setWordWrap(True)
            layout.addWidget(info_label, row, 0, 1, 3)
            row += 1
            
            # Prediction history frames
            self.kp_prediction_history_slider, self.kp_prediction_history_label = self._create_slider_widget(
                parent_layout=layout,
                row=row,
                label_text="History Frames",
                tooltip_text="Number of recent color detections to use for prediction.\n"
                             "Range: 2-10 frames. Default: 5.\n"
                             "Higher = smoother but slower to adapt.",
                range_min=2,
                range_max=10,
                initial_value=5,
                update_func=lambda v: self.update_setting('prediction_history_frames', v),
                is_float=False
            )
            row += 1
            
            # Prediction radius
            self.kp_prediction_radius_slider, self.kp_prediction_radius_label = self._create_slider_widget(
                parent_layout=layout,
                row=row,
                label_text="Circle Radius (cm)",
                tooltip_text="Radius of prediction circle showing search region.\n"
                             "Range: 5-100cm. Default: 15cm.\n"
                             "Larger = wider search area.",
                range_min=5,
                range_max=100,
                initial_value=15,
                update_func=lambda v: self.update_setting('prediction_radius_m', v / 100.0),  # Convert cm to m
                is_float=False
            )
            row += 1
            
            return section
            
        def create_color_tracker_weights_section(self):
            """Create the Color Tracker Weights section"""
            section = CollapsibleGroupBox("🎯 Color Tracker Weights", collapsed=False)
            layout = QGridLayout()
            section.get_content_layout().addLayout(layout)
            
            row = 0
            
            # Info label
            info_label = QLabel("ℹ️ Control how the color tracker chooses which YOLO detection to assign to each ball")
            info_label.setStyleSheet("color: #aaaaaa; font-size: 10px;")
            info_label.setWordWrap(True)
            layout.addWidget(info_label, row, 0, 1, 3)
            row += 1
            
            # YOLO Confidence Weight
            self.ct_yolo_confidence_weight_slider, self.ct_yolo_confidence_weight_label = self._create_slider_widget(
                parent_layout=layout,
                row=row,
                label_text="YOLO Confidence Weight",
                tooltip_text="Weight for YOLO detection confidence score.\n"
                             "Range: 0.0-5.0. Default: 2.0.\n"
                             "Higher = prefer high-confidence detections.",
                range_min=0,
                range_max=50,
                initial_value=20,
                update_func=lambda v: self.update_setting('yolo_confidence_weight', v / 10.0),
                is_float=True
            )
            row += 1
            
            # YOLO Class Weight
            self.ct_yolo_class_weight_slider, self.ct_yolo_class_weight_label = self._create_slider_widget(
                parent_layout=layout,
                row=row,
                label_text="YOLO Class Weight",
                tooltip_text="Weight for YOLO class (ball vs ball_held).\n"
                             "Range: 0.0-5.0. Default: 3.0.\n"
                             "Higher = strongly prefer 'ball' over 'ball_held'.",
                range_min=0,
                range_max=50,
                initial_value=30,
                update_func=lambda v: self.update_setting('yolo_class_weight', v / 10.0),
                is_float=True
            )
            row += 1
            
            # Color Match Weight
            self.ct_color_match_weight_slider, self.ct_color_match_weight_label = self._create_slider_widget(
                parent_layout=layout,
                row=row,
                label_text="Color Match Weight",
                tooltip_text="Weight for color matching score.\n"
                             "Range: 0.0-5.0. Default: 1.0.\n"
                             "Higher = require better color match.",
                range_min=0,
                range_max=50,
                initial_value=10,
                update_func=lambda v: self.update_setting('color_match_weight', v / 10.0),
                is_float=True
            )
            row += 1
            
            # Kalman Proximity Weight
            self.ct_kalman_proximity_weight_slider, self.ct_kalman_proximity_weight_label = self._create_slider_widget(
                parent_layout=layout,
                row=row,
                label_text="Kalman Proximity Weight",
                tooltip_text="Weight for proximity to Kalman prediction.\n"
                             "Range: 0.0-5.0. Default: 0.0 (disabled).\n"
                             "Higher = strongly prefer detections near prediction.\n"
                             "⚠️ Set to 2.0-4.0 to fix the issue shown in your images!",
                range_min=0,
                range_max=50,
                initial_value=0,
                update_func=lambda v: self.update_setting('kalman_proximity_weight', v / 10.0),
                is_float=True
            )
            row += 1
            
            # Separator for override settings
            layout.addWidget(QLabel("Color Tracker Override Settings:"), row, 0, 1, 3)
            row += 1
            
            # Min YOLO Score Threshold
            self.ct_min_yolo_score_slider, self.ct_min_yolo_score_label = self._create_slider_widget(
                parent_layout=layout,
                row=row,
                label_text="Min YOLO Score Threshold",
                tooltip_text="Minimum total score for using YOLO detection as color tracker.\n"
                             "Range: 0.0-10.0. Default: 0.0 (always use YOLO if available).\n"
                             "If score is below this, use Kalman prediction instead.\n"
                             "Higher = require better YOLO detection to use it.",
                range_min=0,
                range_max=100,
                initial_value=0,
                update_func=lambda v: self.update_setting('min_yolo_score_threshold', v / 10.0),
                is_float=True
            )
            row += 1
            
            # Override Confidence Threshold
            self.ct_override_confidence_slider, self.ct_override_confidence_label = self._create_slider_widget(
                parent_layout=layout,
                row=row,
                label_text="Override Confidence Threshold",
                tooltip_text="Minimum YOLO confidence to force use even if score is below threshold.\n"
                             "Range: 0.0-1.0. Default: 0.7.\n"
                             "Helps 'unstick' Kalman predictions with high-confidence detections.",
                range_min=0,
                range_max=100,
                initial_value=70,
                update_func=lambda v: self.update_setting('override_confidence_threshold', v / 100.0),
                is_float=True
            )
            row += 1
            
            # Override Color Threshold
            self.ct_override_color_slider, self.ct_override_color_label = self._create_slider_widget(
                parent_layout=layout,
                row=row,
                label_text="Override Color Threshold",
                tooltip_text="Minimum color match score to force use even if score is below threshold.\n"
                             "Range: 0.0-1.0. Default: 0.8.\n"
                             "Requires good color match for override.",
                range_min=0,
                range_max=100,
                initial_value=80,
                update_func=lambda v: self.update_setting('override_color_threshold', v / 100.0),
                is_float=True
            )
            row += 1
            
            # Override Require Ball Class Toggle
            self.ct_override_require_ball_toggle = QPushButton("Override Requires 'Ball' Class")
            self.ct_override_require_ball_toggle.setCheckable(True)
            self.ct_override_require_ball_toggle.setChecked(True)
            self.ct_override_require_ball_toggle.clicked.connect(
                lambda: self.update_setting('override_require_ball_class', 1 if self.ct_override_require_ball_toggle.isChecked() else 0)
            )
            layout.addWidget(self.ct_override_require_ball_toggle, row, 0, 1, 3)
            row += 1
            
            # Add explanation
            explanation_label = QLabel(
                "💡 <b>How it works:</b><br>"
                "Each YOLO detection gets a score = (class_weight × confidence_weight × confidence) + "
                "(color_match_weight × color_score) + (kalman_proximity_weight × proximity_score)<br><br>"
                "The detection with the highest score is assigned to the ball.<br><br>"
                "<b>To fix your issue:</b> Increase Kalman Proximity Weight to 2.0-4.0 so the tracker "
                "prefers the detection closer to the prediction circle, even if it has lower confidence."
            )
            explanation_label.setStyleSheet("color: #aaaaaa; font-size: 10px; padding: 10px; background-color: #1e1e1e; border-radius: 5px;")
            explanation_label.setWordWrap(True)
            layout.addWidget(explanation_label, row, 0, 1, 3)
            row += 1
            
            return section
        

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
            # Path should be hub/ball_settings.json (one directory up from components/)
            ball_settings_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "ball_settings.json")
            ball_settings_path = os.path.normpath(ball_settings_path)
            
            try:
                with open(ball_settings_path, 'r') as f:
                    self.ball_profiles = json.load(f)
                print(f"✅ Loaded ball profiles from {ball_settings_path}")
                print(f"   Profiles loaded: {list(self.ball_profiles.keys())}")
            except Exception as e:
                print(f"❌ Error loading ball_settings.json: {e}")
                self.ball_profiles = {}
            
            # Also get profiles from ColorProfileManager to ensure we have all colors
            from .color_profile_manager import ColorProfileManager
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
                
                # Get current calibration values
                hsv_data = self.ball_profiles[ball_name]
                avg_hue = hsv_data.get('avg_hue', -1.0)
                avg_sat = hsv_data.get('avg_saturation', -1.0)
                
                # Display calibrated values (read-only)
                row = 1
                
                # Average Hue display
                ball_layout.addWidget(QLabel("Average Hue:"), row, 0)
                if avg_hue >= 0:
                    hue_value_label = QLabel(f"{avg_hue:.1f}°")
                    hue_value_label.setStyleSheet("color: #4CAF50; font-weight: bold;")
                else:
                    hue_value_label = QLabel("Not calibrated")
                    hue_value_label.setStyleSheet("color: #f44336;")
                ball_layout.addWidget(hue_value_label, row, 1, 1, 2)
                row += 1
                
                # Average Saturation display
                ball_layout.addWidget(QLabel("Average Saturation:"), row, 0)
                if avg_sat >= 0:
                    sat_value_label = QLabel(f"{avg_sat:.1f}")
                    sat_value_label.setStyleSheet("color: #4CAF50; font-weight: bold;")
                else:
                    sat_value_label = QLabel("Not calibrated")
                    sat_value_label.setStyleSheet("color: #f44336;")
                ball_layout.addWidget(sat_value_label, row, 1, 1, 2)
                row += 1
                
                # Store label references for updates
                if not hasattr(self, 'ball_calibration_labels'):
                    self.ball_calibration_labels = {}
                self.ball_calibration_labels[ball_name] = {
                    'hue': hue_value_label,
                    'saturation': sat_value_label
                }
                
                # Calibrate button
                calibrate_button = QPushButton("🎯 Calibrate Color")
                calibrate_button.setStyleSheet("""
                    QPushButton {
                        background-color: #2196F3;
                        color: white;
                        padding: 8px;
                        border-radius: 4px;
                        font-weight: bold;
                    }
                    QPushButton:hover { background-color: #1976D2; }
                    QPushButton:pressed { background-color: #0D47A1; }
                """)
                calibrate_button.clicked.connect(lambda checked, name=ball_name: self.start_color_calibration(name))
                ball_layout.addWidget(calibrate_button, row, 0, 1, 3)
                row += 1
                
                # Info label
                info_label = QLabel("ℹ️ Click on a ball in the video feed to calibrate")
                info_label.setStyleSheet("color: #aaaaaa; font-size: 9px;")
                info_label.setWordWrap(True)
                ball_layout.addWidget(info_label, row, 0, 1, 3)
                
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
            print(f"🔄 {'Enabling' if enabled else 'Disabling'} tracking for {ball_name}")
            
            # Update the ball_profiles dict
            if ball_name in self.ball_profiles:
                self.ball_profiles[ball_name]['enabled'] = enabled
                # Save to ball_settings.json
                self.save_ball_settings()
                print(f"💾 Updated ball_profiles and saved to file: {ball_name} enabled={enabled}")
            else:
                print(f"⚠️ WARNING: {ball_name} not found in ball_profiles!")
            
            # Send command to engine via UDP
            self.udp_client.send_setting(f"track_{ball_name}", 1 if enabled else 0)
            print(f"📤 Sent UDP command: track_{ball_name}={1 if enabled else 0}")
            
            # Auto-save settings
            if not self._loading_settings:
                self.save_settings()

        def start_color_calibration(self, ball_name: str):
            """Start color calibration for a specific ball"""
            print(f"🎯 Starting color calibration for {ball_name}")
            print(f"   Please click on a {ball_name} ball in the video feed")
            
            # TODO: This would typically trigger a mode in the UI where the next click
            # on the video feed is captured and sent to the engine for calibration
            # For now, just show a message
            from PyQt6.QtWidgets import QMessageBox
            QMessageBox.information(
                self,
                "Color Calibration",
                f"Color calibration for {ball_name} ball:\n\n"
                f"1. Make sure a {ball_name} ball is visible in the video feed\n"
                f"2. Click directly on the ball in the video window\n"
                f"3. The system will sample the center pixels and calibrate\n\n"
                f"Note: This feature requires the video overlay to be active."
            )

        def save_ball_settings(self):
            """Save ball profiles to ball_settings.json"""
            import json
            import os
            # Path should be hub/ball_settings.json (one directory up from components/)
            ball_settings_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "ball_settings.json")
            ball_settings_path = os.path.normpath(ball_settings_path)
            
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
            from PyQt6.QtWidgets import QApplication
            # Path should be hub/ball_settings.json (one directory up from components/)
            ball_settings_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "ball_settings.json")
            ball_settings_path = os.path.normpath(ball_settings_path)
            
            print(f"🔄 reload_ball_profiles() called - reading from {ball_settings_path}")
            
            # CRITICAL: Ensure the Ball Profiles section is expanded so sliders are visible
            if hasattr(self, 'ball_profiles_section') and self.ball_profiles_section.is_collapsed:
                print(f"⚠️ Ball Profiles section is collapsed - expanding it now")
                self.ball_profiles_section.toggle_collapsed()
                QApplication.processEvents()
            
            try:
                with open(ball_settings_path, 'r') as f:
                    self.ball_profiles = json.load(f)
                
                print(f"📖 Loaded ball profiles: {list(self.ball_profiles.keys())}")
                
                # Update calibration value displays
                if hasattr(self, 'ball_calibration_labels'):
                    for ball_name, labels in self.ball_calibration_labels.items():
                        if ball_name in self.ball_profiles:
                            hsv_data = self.ball_profiles[ball_name]
                            avg_hue = hsv_data.get('avg_hue', -1.0)
                            avg_sat = hsv_data.get('avg_saturation', -1.0)
                            
                            print(f"🎨 Updating {ball_name}: avg_hue={avg_hue}, avg_sat={avg_sat}")
                            
                            # Update hue label
                            if avg_hue >= 0:
                                labels['hue'].setText(f"{avg_hue:.1f}°")
                                labels['hue'].setStyleSheet("color: #4CAF50; font-weight: bold;")
                            else:
                                labels['hue'].setText("Not calibrated")
                                labels['hue'].setStyleSheet("color: #f44336;")
                            
                            # Update saturation label
                            if avg_sat >= 0:
                                labels['saturation'].setText(f"{avg_sat:.1f}")
                                labels['saturation'].setStyleSheet("color: #4CAF50; font-weight: bold;")
                            else:
                                labels['saturation'].setText("Not calibrated")
                                labels['saturation'].setStyleSheet("color: #f44336;")
                
                print("✅ Ball profiles reloaded and sent to engine")
            except Exception as e:
                print(f"❌ Error reloading ball profiles: {e}")
                import traceback
                traceback.print_exc()

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

        def _safe_get_slider_value(self, slider, default_value):
            """Safely get slider value, handling deleted Qt objects."""
            try:
                if slider is not None:
                    return slider.value()
            except RuntimeError:
                # Qt object has been deleted
                pass
            return default_value
        
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
                'tc_name_on_catch': self.tc_name_on_catch_toggle.isChecked(),
                'tc_name_on_throw': self.tc_name_on_throw_toggle.isChecked(),
                
                # Collapsed states for UI persistence
                'collapsed_camera': self.camera_section.is_collapsed,
                'collapsed_yolo': self.yolo_section.is_collapsed,
                'collapsed_bytetrack': self.bytetrack_section.is_collapsed,
                'collapsed_pose': self.pose_section.is_collapsed,
                'collapsed_throw_catch': self.throw_catch_section.is_collapsed,
                'collapsed_kalman_prediction': self.kalman_prediction_section.is_collapsed if hasattr(self, 'kalman_prediction_section') and self.kalman_prediction_section else False,
                'collapsed_color_tracker_weights': self.color_tracker_weights_section.is_collapsed if hasattr(self, 'color_tracker_weights_section') and self.color_tracker_weights_section else False,
                'collapsed_adaptive_color': self.adaptive_color_section.is_collapsed if hasattr(self, 'adaptive_color_section') else False,
                'collapsed_ball_profiles': self.ball_profiles_section.is_collapsed if hasattr(self, 'ball_profiles_section') else False,
                
                # Kalman Prediction settings
                'prediction_history_frames': self._safe_get_slider_value(self.kp_prediction_history_slider, 5) if hasattr(self, 'kp_prediction_history_slider') else 5,
                'prediction_radius_m': self._safe_get_slider_value(self.kp_prediction_radius_slider, 15) / 100.0 if hasattr(self, 'kp_prediction_radius_slider') else 0.15,
                
                # Color Tracker Weights
                'yolo_confidence_weight': self._safe_get_slider_value(self.ct_yolo_confidence_weight_slider, 20) / 10.0 if hasattr(self, 'ct_yolo_confidence_weight_slider') else 2.0,
                'yolo_class_weight': self._safe_get_slider_value(self.ct_yolo_class_weight_slider, 30) / 10.0 if hasattr(self, 'ct_yolo_class_weight_slider') else 3.0,
                'color_match_weight': self._safe_get_slider_value(self.ct_color_match_weight_slider, 10) / 10.0 if hasattr(self, 'ct_color_match_weight_slider') else 1.0,
                'kalman_proximity_weight': self._safe_get_slider_value(self.ct_kalman_proximity_weight_slider, 0) / 10.0 if hasattr(self, 'ct_kalman_proximity_weight_slider') else 0.0,
                
                # Color Tracker Override Settings
                'min_yolo_score_threshold': self._safe_get_slider_value(self.ct_min_yolo_score_slider, 0) / 10.0 if hasattr(self, 'ct_min_yolo_score_slider') else 0.0,
                'override_confidence_threshold': self._safe_get_slider_value(self.ct_override_confidence_slider, 70) / 100.0 if hasattr(self, 'ct_override_confidence_slider') else 0.7,
                'override_color_threshold': self._safe_get_slider_value(self.ct_override_color_slider, 80) / 100.0 if hasattr(self, 'ct_override_color_slider') else 0.8,
                'override_require_ball_class': self.ct_override_require_ball_toggle.isChecked() if hasattr(self, 'ct_override_require_ball_toggle') else True,
                
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
            
            if 'tc_name_on_catch' in settings:
                self.tc_name_on_catch_toggle.setChecked(settings['tc_name_on_catch'])
            
            if 'tc_name_on_throw' in settings:
                self.tc_name_on_throw_toggle.setChecked(settings['tc_name_on_throw'])
            
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
            
            if 'collapsed_kalman_prediction' in settings and hasattr(self, 'kalman_prediction_section'):
                if settings['collapsed_kalman_prediction'] != self.kalman_prediction_section.is_collapsed:
                    self.kalman_prediction_section.toggle_collapsed()
            
            # Kalman Prediction settings
            if 'prediction_history_frames' in settings and hasattr(self, 'kp_prediction_history_slider'):
                self.kp_prediction_history_slider.setValue(settings['prediction_history_frames'])
            
            if 'prediction_radius_m' in settings and hasattr(self, 'kp_prediction_radius_slider'):
                self.kp_prediction_radius_slider.setValue(int(settings['prediction_radius_m'] * 100))  # m to cm
            
            if 'collapsed_color_tracker_weights' in settings and hasattr(self, 'color_tracker_weights_section'):
                if settings['collapsed_color_tracker_weights'] != self.color_tracker_weights_section.is_collapsed:
                    self.color_tracker_weights_section.toggle_collapsed()
            
            # Color Tracker Weights settings
            if 'yolo_confidence_weight' in settings and hasattr(self, 'ct_yolo_confidence_weight_slider'):
                self.ct_yolo_confidence_weight_slider.setValue(int(settings['yolo_confidence_weight'] * 10))
            
            if 'yolo_class_weight' in settings and hasattr(self, 'ct_yolo_class_weight_slider'):
                self.ct_yolo_class_weight_slider.setValue(int(settings['yolo_class_weight'] * 10))
            
            if 'color_match_weight' in settings and hasattr(self, 'ct_color_match_weight_slider'):
                self.ct_color_match_weight_slider.setValue(int(settings['color_match_weight'] * 10))
            
            if 'kalman_proximity_weight' in settings and hasattr(self, 'ct_kalman_proximity_weight_slider'):
                self.ct_kalman_proximity_weight_slider.setValue(int(settings['kalman_proximity_weight'] * 10))
            
            # Color Tracker Override Settings
            if 'min_yolo_score_threshold' in settings and hasattr(self, 'ct_min_yolo_score_slider'):
                self.ct_min_yolo_score_slider.setValue(int(settings['min_yolo_score_threshold'] * 10))
            
            if 'override_confidence_threshold' in settings and hasattr(self, 'ct_override_confidence_slider'):
                self.ct_override_confidence_slider.setValue(int(settings['override_confidence_threshold'] * 100))
            
            if 'override_color_threshold' in settings and hasattr(self, 'ct_override_color_slider'):
                self.ct_override_color_slider.setValue(int(settings['override_color_threshold'] * 100))
            
            if 'override_require_ball_class' in settings and hasattr(self, 'ct_override_require_ball_toggle'):
                self.ct_override_require_ball_toggle.setChecked(settings['override_require_ball_class'])
            
            if 'collapsed_adaptive_color' in settings and hasattr(self, 'adaptive_color_section'):
                if settings['collapsed_adaptive_color'] != self.adaptive_color_section.is_collapsed:
                    self.adaptive_color_section.toggle_collapsed()
            
            if 'collapsed_ball_profiles' in settings and hasattr(self, 'ball_profiles_section'):
                if settings['collapsed_ball_profiles'] != self.ball_profiles_section.is_collapsed:
                    self.ball_profiles_section.toggle_collapsed()
            
            # Restore ball profile settings
            if 'ball_tracking_enabled' in settings and hasattr(self, 'ball_checkboxes'):
                for ball_name, enabled in settings['ball_tracking_enabled'].items():
                    if ball_name in self.ball_checkboxes:
                        self.ball_checkboxes[ball_name].setChecked(enabled)

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
                
                # CRITICAL FIX: Send all loaded settings to the engine
                # The UI sliders are now set, but the engine doesn't know about them yet
                # Add a delay to ensure the engine's UDP listener is fully initialized
                print("📤 Sending loaded settings to engine...")
                import time
                time.sleep(2.0)  # Wait 2 seconds for engine to be fully ready
                self._send_all_settings_to_engine(settings)
                print("✅ All settings sent to engine")
                
                return True
            except Exception as e:
                print(f"❌ Error loading settings: {e}")
                return False
            finally:
                # Always reset the flag
                self._loading_settings = False
        
        def _send_all_settings_to_engine(self, settings: dict):
            """Send all settings from the loaded configuration to the engine.
            
            This is called after loading settings to ensure the engine receives
            all configuration values, not just the UI slider positions.
            """
            # YOLO Tracker settings
            if 'confidence_threshold' in settings:
                self.udp_client.send_setting('confidence_threshold', settings['confidence_threshold'])
            
            if 'nms_threshold' in settings:
                self.udp_client.send_setting('nms_threshold', settings['nms_threshold'])
            
            # ByteTrack settings
            if 'track_buffer' in settings:
                self.udp_client.send_setting('track_buffer', settings['track_buffer'])
            
            if 'track_thresh' in settings:
                self.udp_client.send_setting('track_thresh', settings['track_thresh'])
            
            if 'high_thresh' in settings:
                self.udp_client.send_setting('high_thresh', settings['high_thresh'])
            
            if 'match_thresh' in settings:
                self.udp_client.send_setting('match_thresh', settings['match_thresh'])
            
            # Throw/Catch Detection settings
            if 'ml_ball_weight' in settings:
                self.udp_client.send_setting('ml_ball_weight', settings['ml_ball_weight'])
            
            if 'ml_ball_held_weight' in settings:
                self.udp_client.send_setting('ml_ball_held_weight', settings['ml_ball_held_weight'])
            
            if 'wrist_proximity_weight' in settings:
                self.udp_client.send_setting('wrist_proximity_weight', settings['wrist_proximity_weight'])
            
            if 'wrist_proximity_threshold' in settings:
                self.udp_client.send_setting('wrist_proximity_threshold', settings['wrist_proximity_threshold'])
            
            if 'undetected_near_hand_threshold' in settings:
                self.udp_client.send_setting('undetected_near_hand_threshold', settings['undetected_near_hand_threshold'])
            
            if 'min_frames_for_state_change' in settings:
                self.udp_client.send_setting('min_frames_for_state_change', settings['min_frames_for_state_change'])
            
            if 'tc_sound_on_catch' in settings:
                self.udp_client.send_setting('tc_sound_on_catch', 1 if settings['tc_sound_on_catch'] else 0)
            
            if 'tc_sound_on_throw' in settings:
                self.udp_client.send_setting('tc_sound_on_throw', 1 if settings['tc_sound_on_throw'] else 0)
            
            if 'tc_name_on_catch' in settings:
                self.udp_client.send_setting('tc_name_on_catch', 1 if settings['tc_name_on_catch'] else 0)
            
            if 'tc_name_on_throw' in settings:
                self.udp_client.send_setting('tc_name_on_throw', 1 if settings['tc_name_on_throw'] else 0)
            
            # Kalman Prediction settings
            if 'prediction_history_frames' in settings:
                self.udp_client.send_setting('prediction_history_frames', settings['prediction_history_frames'])
            
            if 'prediction_radius_m' in settings:
                self.udp_client.send_setting('prediction_radius_m', settings['prediction_radius_m'])
            
            # Color Tracker Weights
            if 'yolo_confidence_weight' in settings:
                self.udp_client.send_setting('yolo_confidence_weight', settings['yolo_confidence_weight'])
            
            if 'yolo_class_weight' in settings:
                self.udp_client.send_setting('yolo_class_weight', settings['yolo_class_weight'])
            
            if 'color_match_weight' in settings:
                self.udp_client.send_setting('color_match_weight', settings['color_match_weight'])
            
            if 'kalman_proximity_weight' in settings:
                self.udp_client.send_setting('kalman_proximity_weight', settings['kalman_proximity_weight'])
            
            # Color Tracker Override Settings
            if 'min_yolo_score_threshold' in settings:
                self.udp_client.send_setting('min_yolo_score_threshold', settings['min_yolo_score_threshold'])
            
            if 'override_confidence_threshold' in settings:
                self.udp_client.send_setting('override_confidence_threshold', settings['override_confidence_threshold'])
            
            if 'override_color_threshold' in settings:
                self.udp_client.send_setting('override_color_threshold', settings['override_color_threshold'])
            
            if 'override_require_ball_class' in settings:
                self.udp_client.send_setting('override_require_ball_class', 1 if settings['override_require_ball_class'] else 0)
            
            # Ball tracking enabled states
            if 'ball_tracking_enabled' in settings:
                for ball_name, enabled in settings['ball_tracking_enabled'].items():
                    self.udp_client.send_setting(f'track_{ball_name}', 1 if enabled else 0)
            
            # Ball hue ranges (send to engine, not just ball_settings.json)
            if 'ball_hue_ranges' in settings:
                for ball_name, hue_range in settings['ball_hue_ranges'].items():
                    if 'min_hue' in hue_range:
                        self.udp_client.send_setting(f'{ball_name}_min_hue', hue_range['min_hue'])
                    if 'max_hue' in hue_range:
                        self.udp_client.send_setting(f'{ball_name}_max_hue', hue_range['max_hue'])
        
        def test_catch_sound(self):
            """Play a test sound for catch events"""
            self.play_system_sound(frequency=800, duration=100)
            print("🔊 Playing catch test sound (800 Hz)")
        
        def test_throw_sound(self):
            """Play a test sound for throw events"""
            self.play_system_sound(frequency=1200, duration=100)
            print("🔊 Playing throw test sound (1200 Hz)")
        
        def test_catch_name(self):
            """Play a test color name for catch events"""
            # Play a random color name as test
            test_color = 'red'  # Default test color
            self.play_color_name(test_color)
            print(f"🔊 Playing catch test name: {test_color}")
        
        def test_throw_name(self):
            """Play a test color name for throw events"""
            # Play a random color name as test
            test_color = 'blue'  # Default test color
            self.play_color_name(test_color)
            print(f"🔊 Playing throw test name: {test_color}")
        
        def play_color_name(self, color_name: str):
            """Play audio file for the given color name"""
            def play_in_thread():
                try:
                    # Path to audio files: hub/audio/color_names/{color}.mp3
                    audio_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "audio", "color_names")
                    audio_file = os.path.join(audio_dir, f"{color_name.lower()}.mp3")
                    
                    if not os.path.exists(audio_file):
                        print(f"⚠️ Audio file not found: {audio_file}")
                        print(f"   Please add {color_name}.mp3 to hub/audio/color_names/")
                        return
                    
                    system = platform.system()
                    if system == "Linux":
                        # Use paplay for instant playback (same as beep sounds)
                        # This is much faster than mpg123/ffplay
                        try:
                            subprocess.Popen(['paplay', audio_file],
                                           stdout=subprocess.DEVNULL,
                                           stderr=subprocess.DEVNULL)
                        except FileNotFoundError:
                            # Fallback to mpg123 if paplay not available
                            try:
                                subprocess.Popen(['mpg123', '-q', audio_file],
                                               stdout=subprocess.DEVNULL,
                                               stderr=subprocess.DEVNULL)
                            except FileNotFoundError:
                                print("⚠️ Neither paplay nor mpg123 found. Install with: sudo apt install pulseaudio-utils")
                    elif system == "Darwin":  # macOS
                        subprocess.Popen(['afplay', audio_file],
                                       stdout=subprocess.DEVNULL,
                                       stderr=subprocess.DEVNULL)
                    elif system == "Windows":
                        # Use winsound for instant playback
                        import winsound
                        winsound.PlaySound(audio_file, winsound.SND_FILENAME | winsound.SND_ASYNC)
                except Exception as e:
                    print(f"⚠️ Could not play color name audio: {e}")
            
            # Play sound in background thread to avoid blocking UI
            threading.Thread(target=play_in_thread, daemon=True).start()
        
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