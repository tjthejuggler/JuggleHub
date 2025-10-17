"""
Common Settings Sections for JuggleHub UI.
Contains settings sections that are visible for BOTH 3D and 2D trackers.
"""

from PyQt6.QtWidgets import (QLabel, QComboBox, QPushButton, QGridLayout)
from PyQt6.QtCore import Qt
from .ui_widgets import CollapsibleGroupBox


class CommonSettingsSections:
    """Common settings sections visible for both 3D and 2D trackers."""
    
    def __init__(self, parent_widget, udp_client, zmq_client):
        """
        Initialize common settings sections.
        
        Args:
            parent_widget: Parent CalibrationSettingsWidget instance
            udp_client: UDP client for sending settings to engine
            zmq_client: ZMQ client for sending commands to engine
        """
        self.parent = parent_widget
        self.udp_client = udp_client
        self.zmq_client = zmq_client
    
    def create_camera_section(self):
        """Create the Camera Settings section (common to all trackers)"""
        section = CollapsibleGroupBox("📷 Camera Settings", collapsed=False)
        camera_layout = QGridLayout()
        section.get_content_layout().addLayout(camera_layout)
        
        # Camera settings dropdown
        camera_layout.addWidget(QLabel("Settings Profile:"), 0, 0)
        self.parent.camera_settings_combo = QComboBox()
        self.parent.populate_camera_settings()
        camera_layout.addWidget(self.parent.camera_settings_combo, 0, 1)
        
        # Resolution dropdown
        camera_layout.addWidget(QLabel("Resolution:"), 1, 0)
        self.parent.resolution_combo = QComboBox()
        self.parent.populate_resolution_options()
        self.parent.resolution_combo.currentTextChanged.connect(self.parent.on_resolution_changed)
        camera_layout.addWidget(self.parent.resolution_combo, 1, 1)
        
        # FPS dropdown
        camera_layout.addWidget(QLabel("Frame Rate (FPS):"), 2, 0)
        self.parent.fps_combo = QComboBox()
        self.parent.populate_fps_options()
        camera_layout.addWidget(self.parent.fps_combo, 2, 1)
        
        # Camera control buttons
        from PyQt6.QtWidgets import QHBoxLayout
        camera_control_layout = QHBoxLayout()
        
        # Stop camera button
        self.parent.stop_camera_button = QPushButton("Stop Camera")
        self.parent.stop_camera_button.clicked.connect(self.parent.stop_camera_feed)
        self.parent.stop_camera_button.setStyleSheet("""
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
        camera_control_layout.addWidget(self.parent.stop_camera_button)
        
        # Start camera button
        self.parent.start_camera_button = QPushButton("Start Camera")
        self.parent.start_camera_button.clicked.connect(self.parent.start_camera_feed)
        self.parent.start_camera_button.setStyleSheet("""
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
        self.parent.start_camera_button.setEnabled(True)
        camera_control_layout.addWidget(self.parent.start_camera_button)
        
        camera_layout.addLayout(camera_control_layout, 3, 0, 1, 2)
        
        # Camera status indicator
        self.parent.camera_status_label = QLabel("● Camera Stopped")
        self.parent.camera_status_label.setStyleSheet("color: #f44336; font-weight: bold;")
        camera_layout.addWidget(self.parent.camera_status_label, 4, 0, 1, 2)

        # IR Projector status
        self.parent.ir_status_label = QLabel("🔆 IR Projector: Unknown")
        camera_layout.addWidget(self.parent.ir_status_label, 5, 0, 1, 2)
        
        # Depth Sensor Toggle
        self.parent.depth_sensor_toggle = QPushButton("Enable Depth Sensor")
        self.parent.depth_sensor_toggle.setCheckable(True)
        self.parent.depth_sensor_toggle.setChecked(True)
        self.parent.depth_sensor_toggle.clicked.connect(self.parent.toggle_depth_sensor)
        self.parent.depth_sensor_toggle.setToolTip(
            "Enable or disable the RealSense depth sensor.\n"
            "When disabled, only RGB camera is used (saves power and processing).\n"
            "Note: Depth-based tracking requires this to be enabled."
        )
        camera_layout.addWidget(self.parent.depth_sensor_toggle, 6, 0, 1, 2)
        
        # Tracking System Selection
        camera_layout.addWidget(QLabel("Tracking System:"), 7, 0)
        self.parent.tracking_system_combo = QComboBox()
        self.parent.tracking_system_combo.addItem("Depth-Based 3D (Current)", "depth_based")
        self.parent.tracking_system_combo.addItem("New 3D Kalman ⭐", "new_3d")
        self.parent.tracking_system_combo.addItem("Simple 2D (New)", "simple_2d")
        self.parent.tracking_system_combo.currentIndexChanged.connect(self.parent.on_tracking_system_changed)
        self.parent.tracking_system_combo.setToolTip(
            "Select which tracking system to use:\n"
            "• Depth-Based 3D: Uses RealSense depth data for 3D tracking (current system)\n"
            "• Simple 2D: 2D-only tracking without depth (new system - to be implemented)"
        )
        camera_layout.addWidget(self.parent.tracking_system_combo, 7, 1)
        
        return section
    
    def create_yolo_section(self):
        """Create the YOLO Tracker Settings section (common to all trackers)"""
        section = CollapsibleGroupBox("🎯 YOLO Tracker Settings", collapsed=False)
        dnn_layout = QGridLayout()
        section.get_content_layout().addLayout(dnn_layout)

        # Class-specific confidence thresholds
        row = 0
        
        # Enable/Disable YOLO Ball Model toggle
        self.parent.use_dnn_tracker_toggle = QPushButton("Enable YOLO Ball Detection")
        self.parent.use_dnn_tracker_toggle.setCheckable(True)
        self.parent.use_dnn_tracker_toggle.setChecked(True)  # Enabled by default
        self.parent.use_dnn_tracker_toggle.clicked.connect(self.parent.toggle_dnn_tracker)
        
        # Send initial state to engine
        self.udp_client.send_setting('enable_ball_detection', 1)
        self.parent.use_dnn_tracker_toggle.setToolTip(
            "Enable or disable YOLO ball detection model.\n"
            "When disabled, no ball detection occurs (useful for performance testing).\n"
            "Disabling this should give you full camera FPS with no processing overhead."
        )
        self.parent.use_dnn_tracker_toggle.setStyleSheet("""
            QPushButton {
                background-color: #4CAF50;
                color: white;
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
            QPushButton:checked {
                background-color: #4CAF50;
            }
            QPushButton:!checked {
                background-color: #f44336;
            }
        """)
        dnn_layout.addWidget(self.parent.use_dnn_tracker_toggle, row, 0, 1, 3)
        row += 1
        
        # Info label
        info_label = QLabel("ℹ️ Set confidence thresholds per class type")
        info_label.setStyleSheet("color: #aaaaaa; font-size: 10px;")
        info_label.setWordWrap(True)
        dnn_layout.addWidget(info_label, row, 0, 1, 3)
        row += 1
        
        # Ball (in-air) confidence threshold
        self.parent.ball_confidence_slider, self.parent.ball_confidence_value_label = self.parent._create_slider_widget(
            parent_layout=dnn_layout,
            row=row,
            label_text="'Ball' Confidence",
            tooltip_text="Minimum confidence for 'ball' (in-air) detections.\n"
                         "Range: 0.00 to 1.00. Default: 0.25.\n"
                         "Lower values detect more balls but increase false positives.",
            range_min=0,
            range_max=100,
            initial_value=25,
            update_func=lambda v: self.parent.update_setting('ball_confidence_threshold', v / 100.0),
            is_float=True
        )
        row += 1
        
        # Ball_held confidence threshold
        self.parent.ball_held_confidence_slider, self.parent.ball_held_confidence_value_label = self.parent._create_slider_widget(
            parent_layout=dnn_layout,
            row=row,
            label_text="'Ball Held' Confidence",
            tooltip_text="Minimum confidence for 'ball_held' detections.\n"
                         "Range: 0.00 to 1.00. Default: 0.25.\n"
                         "Lower values detect more held balls but increase false positives.",
            range_min=0,
            range_max=100,
            initial_value=25,
            update_func=lambda v: self.parent.update_setting('ball_held_confidence_threshold', v / 100.0),
            is_float=True
        )
        row += 1

        # NMS threshold (applies to all classes)
        self.parent.nms_slider, self.parent.nms_value_label = self.parent._create_slider_widget(
            parent_layout=dnn_layout,
            row=row,
            label_text="NMS Threshold",
            tooltip_text="Non-Maximum Suppression threshold for merging overlapping boxes.\n"
                         "Range: 0.00 to 1.00. Default: 0.50.\n"
                         "Higher values allow more overlap.",
            range_min=0,
            range_max=100,
            initial_value=50,
            update_func=lambda v: self.parent.update_setting('nms_threshold', v / 100.0),
            is_float=True
        )
        row += 1
        
        # Visualization toggle for raw detections
        self.parent.show_raw_yolo_toggle = QPushButton("Show Raw YOLO Detections")
        self.parent.show_raw_yolo_toggle.setCheckable(True)
        self.parent.show_raw_yolo_toggle.setChecked(False)
        self.parent.show_raw_yolo_toggle.clicked.connect(lambda: self.parent.update_setting('show_raw_yolo_detections', 1 if self.parent.show_raw_yolo_toggle.isChecked() else 0))
        dnn_layout.addWidget(self.parent.show_raw_yolo_toggle, row, 0, 1, 3)
        row += 1
        
        # Info about visualization
        viz_info_label = QLabel("ℹ️ Raw detections shown as darker red squares (larger)")
        viz_info_label.setStyleSheet("color: #aaaaaa; font-size: 9px;")
        viz_info_label.setWordWrap(True)
        dnn_layout.addWidget(viz_info_label, row, 0, 1, 3)
        
        return section
    
    def create_pose_section(self):
        """Create the Pose Model Settings section (common to all trackers)"""
        section = CollapsibleGroupBox("🧍 Pose Model Settings", collapsed=False)
        pose_layout = QGridLayout()
        section.get_content_layout().addLayout(pose_layout)

        self.parent.pose_model_toggle = QPushButton("Enable Pose Model")
        self.parent.pose_model_toggle.setCheckable(True)
        self.parent.pose_model_toggle.setChecked(True)
        self.parent.pose_model_toggle.clicked.connect(self.parent.toggle_pose_model)
        pose_layout.addWidget(self.parent.pose_model_toggle, 0, 0, 1, 2)

        return section