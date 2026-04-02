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
        
        # Auto Exposure toggle
        row = 7
        from PyQt6.QtWidgets import QCheckBox
        camera_layout.addWidget(QLabel("Auto Exposure:"), row, 0)
        self.parent.auto_exposure_toggle = QCheckBox()
        self.parent.auto_exposure_toggle.setChecked(True)  # Default to auto exposure enabled
        self.parent.auto_exposure_toggle.setToolTip(
            "Enable automatic exposure control.\n"
            "When enabled, camera adjusts exposure automatically.\n"
            "When disabled, use manual exposure slider below."
        )
        self.parent.auto_exposure_toggle.stateChanged.connect(
            lambda state: self.parent.toggle_auto_exposure(state)
        )
        camera_layout.addWidget(self.parent.auto_exposure_toggle, row, 1, 1, 2)
        row += 1
        
        # Manual Exposure slider (disabled by default when auto exposure is on)
        # Note: We create this manually instead of using _create_slider_widget to use sliderReleased
        from PyQt6.QtWidgets import QSlider
        exposure_label = QLabel("Manual Exposure (μs)")
        exposure_label.setToolTip(
            "Manual camera exposure setting.\n"
            "Range: 1-400 microseconds. Default: 100.\n"
            "Lower = darker image, Higher = brighter image.\n"
            "Only active when Auto Exposure is disabled."
        )
        camera_layout.addWidget(exposure_label, row, 0)
        
        self.parent.exposure_slider = QSlider(Qt.Orientation.Horizontal)
        self.parent.exposure_slider.setRange(1, 400)
        self.parent.exposure_slider.setValue(100)
        camera_layout.addWidget(self.parent.exposure_slider, row, 1)
        
        self.parent.exposure_label = QLabel()
        self.parent.exposure_label.setMinimumWidth(60)
        self.parent.exposure_label.setText("100")
        camera_layout.addWidget(self.parent.exposure_label, row, 2)
        
        # Update label on value change (for visual feedback while dragging)
        self.parent.exposure_slider.valueChanged.connect(
            lambda v: self.parent.exposure_label.setText(str(v))
        )
        
        # Only send to camera when slider is released (prevents lag)
        self.parent.exposure_slider.sliderReleased.connect(
            lambda: self.parent.update_camera_exposure(self.parent.exposure_slider.value())
        )
        
        # Disable manual exposure slider by default (auto exposure is on)
        self.parent.exposure_slider.setEnabled(False)
        row += 1
        
        # Manual Gain slider (for controlling sensor sensitivity)
        gain_label = QLabel("Camera Gain")
        gain_label.setToolTip(
            "Camera gain (ISO/sensitivity) setting.\n"
            "Range: 0-248. Default: 64.\n"
            "Lower gain = less sensitive, prevents bright objects from clipping to white.\n"
            "Higher gain = more sensitive, better for low light.\n"
            "Use lower gain to prevent juggling balls from appearing pure white."
        )
        camera_layout.addWidget(gain_label, row, 0)
        
        self.parent.gain_slider = QSlider(Qt.Orientation.Horizontal)
        self.parent.gain_slider.setRange(0, 248)
        self.parent.gain_slider.setValue(64)
        camera_layout.addWidget(self.parent.gain_slider, row, 1)
        
        self.parent.gain_label = QLabel()
        self.parent.gain_label.setMinimumWidth(60)
        self.parent.gain_label.setText("64")
        camera_layout.addWidget(self.parent.gain_label, row, 2)
        
        # Update label on value change (for visual feedback while dragging)
        self.parent.gain_slider.valueChanged.connect(
            lambda v: self.parent.gain_label.setText(str(v))
        )
        
        # Only send to camera when slider is released (prevents lag)
        self.parent.gain_slider.sliderReleased.connect(
            lambda: self.parent.update_camera_gain(self.parent.gain_slider.value())
        )
        row += 1
        
        # Auto White Balance toggle
        camera_layout.addWidget(QLabel("Auto White Balance:"), row, 0)
        from PyQt6.QtWidgets import QCheckBox
        self.parent.auto_white_balance_toggle = QCheckBox()
        self.parent.auto_white_balance_toggle.setChecked(True)  # Default to auto white balance enabled
        self.parent.auto_white_balance_toggle.setToolTip(
            "Enable automatic white balance control.\n"
            "When enabled, camera adjusts white balance automatically.\n"
            "When disabled, use manual white balance slider below."
        )
        self.parent.auto_white_balance_toggle.stateChanged.connect(
            lambda state: self.parent.toggle_auto_white_balance(state)
        )
        camera_layout.addWidget(self.parent.auto_white_balance_toggle, row, 1, 1, 2)
        row += 1
        
        # Manual White Balance slider (disabled by default when auto white balance is on)
        from PyQt6.QtWidgets import QSlider
        white_balance_label = QLabel("Manual White Balance (K)")
        white_balance_label.setToolTip(
            "Manual camera white balance setting.\n"
            "Range: 2800-6500 Kelvin. Default: 4600.\n"
            "Lower = warmer (more orange), Higher = cooler (more blue).\n"
            "Only active when Auto White Balance is disabled."
        )
        camera_layout.addWidget(white_balance_label, row, 0)
        
        self.parent.white_balance_slider = QSlider(Qt.Orientation.Horizontal)
        self.parent.white_balance_slider.setRange(2800, 6500)
        self.parent.white_balance_slider.setValue(4600)
        camera_layout.addWidget(self.parent.white_balance_slider, row, 1)
        
        self.parent.white_balance_label = QLabel()
        self.parent.white_balance_label.setMinimumWidth(60)
        self.parent.white_balance_label.setText("4600")
        camera_layout.addWidget(self.parent.white_balance_label, row, 2)
        
        # Update label on value change (for visual feedback while dragging)
        self.parent.white_balance_slider.valueChanged.connect(
            lambda v: self.parent.white_balance_label.setText(str(v))
        )
        
        # Only send to camera when slider is released (prevents lag)
        self.parent.white_balance_slider.sliderReleased.connect(
            lambda: self.parent.update_camera_white_balance(self.parent.white_balance_slider.value())
        )
        
        # Disable manual white balance slider by default (auto white balance is on)
        self.parent.white_balance_slider.setEnabled(False)
        row += 1
        
        # ========== BALL TRACKING MODE (prominent toggle) ==========
        ball_mode_label = QLabel("⚡ Ball Tracking Mode:")
        ball_mode_label.setStyleSheet("font-weight: bold; font-size: 12px;")
        camera_layout.addWidget(ball_mode_label, row, 0)
        
        self.parent.ball_tracking_mode_combo = QComboBox()
        self.parent.ball_tracking_mode_combo.addItem("⚡ Simple (Depth + LED Color)", "simple")
        self.parent.ball_tracking_mode_combo.addItem("🤖 YOLO Ball Detection", "yolo")
        self.parent.ball_tracking_mode_combo.setCurrentIndex(0)  # Default to simple
        self.parent.ball_tracking_mode_combo.currentIndexChanged.connect(self.parent.on_ball_tracking_mode_changed)
        self.parent.ball_tracking_mode_combo.setToolTip(
            "How balls are detected each frame:\n"
            "• Simple (Depth + LED Color): Uses depth camera to filter background,\n"
            "  then finds glowing LED balls by color. Fast, no AI model needed.\n"
            "• YOLO Ball Detection: Uses a trained AI model to detect balls.\n"
            "  More robust but heavier. Requires YOLO ball model to be loaded.\n\n"
            "NOTE: Skeleton/hand tracking always uses YOLO pose model regardless."
        )
        self.parent.ball_tracking_mode_combo.setStyleSheet("""
            QComboBox {
                padding: 6px;
                font-size: 12px;
                font-weight: bold;
                border: 2px solid #4CAF50;
                border-radius: 4px;
            }
        """)
        camera_layout.addWidget(self.parent.ball_tracking_mode_combo, row, 1)
        row += 1
        
        # Ball tracking mode info label
        self.parent.ball_tracking_mode_info = QLabel("⚡ Using depth + LED color detection (no YOLO ball model)")
        self.parent.ball_tracking_mode_info.setStyleSheet("color: #4CAF50; font-size: 10px; font-style: italic;")
        self.parent.ball_tracking_mode_info.setWordWrap(True)
        camera_layout.addWidget(self.parent.ball_tracking_mode_info, row, 0, 1, 2)
        row += 1

        # Hidden tracking system combo (kept for internal use / backward compatibility)
        # This is auto-set by ball_tracking_mode_combo and not shown to user
        self.parent.tracking_system_combo = QComboBox()
        self.parent.tracking_system_combo.addItem("Depth-Based 3D (Current)", "depth_based")
        self.parent.tracking_system_combo.addItem("New 3D Kalman ⭐", "new_3d")
        self.parent.tracking_system_combo.addItem("Simple 2D (New)", "simple_2d")
        self.parent.tracking_system_combo.currentIndexChanged.connect(self.parent.on_tracking_system_changed)
        self.parent.tracking_system_combo.setVisible(False)  # Hidden - controlled by ball tracking mode
        camera_layout.addWidget(self.parent.tracking_system_combo, row, 0, 1, 2)
        # Don't increment row since it's hidden
        
        # ========== PLAYBACK MODE SECTION ==========

        # Separator
        separator = QLabel("─" * 50)
        separator.setStyleSheet("color: #555555;")
        camera_layout.addWidget(separator, row, 0, 1, 2)
        row += 1

        # Input source selection
        camera_layout.addWidget(QLabel("Input Source:"), row, 0)
        self.parent.input_source_combo = QComboBox()
        self.parent.input_source_combo.addItem("🎥 Live Camera", "live")
        self.parent.input_source_combo.addItem("📁 Recording Playback", "playback")
        self.parent.input_source_combo.currentIndexChanged.connect(
            self.parent.on_input_source_changed)
        self.parent.input_source_combo.setToolTip(
            "Select input source:\n"
            "• Live Camera: Use RealSense camera feed\n"
            "• Recording Playback: Play back a recorded session"
        )
        camera_layout.addWidget(self.parent.input_source_combo, row, 1)
        row += 1

        # Playback directory selection (initially hidden)
        self.parent.playback_dir_label = QLabel("Recording Directory:")
        self.parent.playback_dir_label.setVisible(False)
        camera_layout.addWidget(self.parent.playback_dir_label, row, 0)

        from PyQt6.QtWidgets import QHBoxLayout
        playback_dir_layout = QHBoxLayout()
        self.parent.playback_dir_display = QLabel("No directory selected")
        self.parent.playback_dir_display.setStyleSheet(
            "background-color: #1e1e1e; padding: 5px; border-radius: 3px; color: #888888;")
        self.parent.playback_dir_display.setVisible(False)
        playback_dir_layout.addWidget(self.parent.playback_dir_display)

        self.parent.playback_browse_button = QPushButton("Browse...")
        self.parent.playback_browse_button.clicked.connect(
            self.parent.browse_playback_directory)
        self.parent.playback_browse_button.setVisible(False)
        self.parent.playback_browse_button.setStyleSheet("""
            QPushButton {
                background-color: #4CAF50;
                color: white;
                padding: 5px 10px;
                border-radius: 3px;
            }
            QPushButton:hover {
                background-color: #45a049;
            }
        """)
        playback_dir_layout.addWidget(self.parent.playback_browse_button)

        camera_layout.addLayout(playback_dir_layout, row, 1)
        row += 1

        # Playback controls container (initially hidden)
        from PyQt6.QtWidgets import QWidget, QSlider
        self.parent.playback_controls_widget = QWidget()
        playback_controls_layout = QGridLayout()
        self.parent.playback_controls_widget.setLayout(playback_controls_layout)
        self.parent.playback_controls_widget.setVisible(False)
        self.parent.playback_controls_widget.setStyleSheet("""
            QWidget {
                background-color: #1e1e1e;
                border-radius: 5px;
                padding: 5px;
            }
        """)

        # Frame info label
        self.parent.playback_frame_label = QLabel("Frame: 0 / 0")
        self.parent.playback_frame_label.setStyleSheet("color: #4CAF50; font-weight: bold;")
        playback_controls_layout.addWidget(self.parent.playback_frame_label, 0, 0, 1, 4)

        # Control buttons row
        self.parent.playback_step_back_button = QPushButton("◀ Step Back")
        self.parent.playback_step_back_button.clicked.connect(
            self.parent.playback_step_backward)
        self.parent.playback_step_back_button.setEnabled(False)  # Disabled until recording is loaded
        self.parent.playback_step_back_button.setStyleSheet("""
            QPushButton {
                background-color: #555555;
                color: white;
                padding: 5px;
                border-radius: 3px;
            }
            QPushButton:hover { background-color: #666666; }
            QPushButton:disabled {
                background-color: #333333;
                color: #666666;
            }
        """)
        playback_controls_layout.addWidget(self.parent.playback_step_back_button, 1, 0)

        self.parent.playback_play_pause_button = QPushButton("▶ Play")
        self.parent.playback_play_pause_button.setCheckable(True)
        self.parent.playback_play_pause_button.clicked.connect(
            self.parent.playback_toggle_play_pause)
        self.parent.playback_play_pause_button.setStyleSheet("""
            QPushButton {
                background-color: #4CAF50;
                color: white;
                padding: 5px;
                border-radius: 3px;
                font-weight: bold;
            }
            QPushButton:hover { background-color: #45a049; }
            QPushButton:checked {
                background-color: #f44336;
            }
            QPushButton:checked:hover {
                background-color: #da190b;
            }
        """)
        playback_controls_layout.addWidget(self.parent.playback_play_pause_button, 1, 1)

        self.parent.playback_step_forward_button = QPushButton("Step Forward ▶")
        self.parent.playback_step_forward_button.clicked.connect(
            self.parent.playback_step_forward)
        self.parent.playback_step_forward_button.setEnabled(False)  # Disabled until recording is loaded
        self.parent.playback_step_forward_button.setStyleSheet("""
            QPushButton {
                background-color: #555555;
                color: white;
                padding: 5px;
                border-radius: 3px;
            }
            QPushButton:hover { background-color: #666666; }
            QPushButton:disabled {
                background-color: #333333;
                color: #666666;
            }
        """)
        playback_controls_layout.addWidget(self.parent.playback_step_forward_button, 1, 2)

        self.parent.playback_stop_button = QPushButton("⏹ Stop")
        self.parent.playback_stop_button.clicked.connect(
            self.parent.playback_stop)
        self.parent.playback_stop_button.setStyleSheet("""
            QPushButton {
                background-color: #f44336;
                color: white;
                padding: 5px;
                border-radius: 3px;
            }
            QPushButton:hover { background-color: #da190b; }
        """)
        playback_controls_layout.addWidget(self.parent.playback_stop_button, 1, 3)

        # Speed control row
        playback_controls_layout.addWidget(QLabel("Speed:"), 2, 0)
        self.parent.playback_speed_slider = QSlider(Qt.Orientation.Horizontal)
        self.parent.playback_speed_slider.setRange(10, 200)  # 0.1x to 2.0x
        self.parent.playback_speed_slider.setValue(100)  # 1.0x default
        self.parent.playback_speed_slider.valueChanged.connect(
            self.parent.on_playback_speed_changed)
        playback_controls_layout.addWidget(self.parent.playback_speed_slider, 2, 1, 1, 2)

        self.parent.playback_speed_label = QLabel("1.0x")
        self.parent.playback_speed_label.setStyleSheet("color: #4CAF50; font-weight: bold;")
        playback_controls_layout.addWidget(self.parent.playback_speed_label, 2, 3)

        camera_layout.addWidget(self.parent.playback_controls_widget, row, 0, 1, 2)
        row += 1
        
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
        
        # Ball Processing Density Slider (same as pose processing density)
        self.parent.ball_density_slider, self.parent.ball_density_label = self.parent._create_slider_widget(
            parent_layout=dnn_layout,
            row=row,
            label_text="Processing Density (%)",
            tooltip_text="Percentage of frames to process with ball detection model.\n"
                         "100% = Every frame (real-time)\n"
                         "50% = Every 2nd frame (default, balanced)\n"
                         "33% = Every 3rd frame (power saver)\n"
                         "25% = Every 4th frame (low)\n"
                         "Range: 10-100%. Default: 50%.\n"
                         "Lower values save CPU/GPU but reduce ball detection smoothness.",
            range_min=10,
            range_max=100,
            initial_value=50,
            update_func=lambda v: self.parent.update_setting('ball_processing_density', v),
            is_float=False
        )
        row += 1
        
        # Density description label
        self.parent.ball_density_desc = QLabel("Balanced (Every 2nd frame)")
        self.parent.ball_density_desc.setStyleSheet("color: #4CAF50; font-size: 9px; font-style: italic;")
        dnn_layout.addWidget(self.parent.ball_density_desc, row, 1, 1, 2)
        row += 1
        
        # Connect slider to update description
        self.parent.ball_density_slider.valueChanged.connect(self._update_ball_density_description)
        
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

        row = 0
        
        # Info label
        from PyQt6.QtWidgets import QLabel
        info_label = QLabel("ℹ️ Configure pose estimation model and processing frequency")
        info_label.setStyleSheet("color: #aaaaaa; font-size: 10px;")
        info_label.setWordWrap(True)
        pose_layout.addWidget(info_label, row, 0, 1, 3)
        row += 1
        
        # Enable Pose Model Toggle
        label = QLabel("Enable Pose Model")
        label.setToolTip("Enable YOLO pose estimation for hand tracking.\n"
                        "When disabled, no hand detection occurs.")
        pose_layout.addWidget(label, row, 0)
        
        self.parent.pose_model_toggle = QPushButton("Enable Pose Model")
        self.parent.pose_model_toggle.setCheckable(True)
        self.parent.pose_model_toggle.setChecked(True)
        self.parent.pose_model_toggle.clicked.connect(self.parent.toggle_pose_model)
        self.parent.pose_model_toggle.setStyleSheet("""
            QPushButton {
                background-color: #4CAF50;
                color: white;
                padding: 8px;
                border-radius: 4px;
                font-weight: bold;
            }
            QPushButton:hover { background-color: #45a049; }
            QPushButton:pressed { background-color: #2e7d32; }
            QPushButton:checked { background-color: #4CAF50; }
            QPushButton:!checked { background-color: #f44336; }
        """)
        pose_layout.addWidget(self.parent.pose_model_toggle, row, 1, 1, 2)
        row += 1
        
        # Processing Density Slider
        self.parent.pose_density_slider, self.parent.pose_density_label = self.parent._create_slider_widget(
            parent_layout=pose_layout,
            row=row,
            label_text="Processing Density (%)",
            tooltip_text="Percentage of frames to process with pose model.\n"
                         "100% = Every frame (real-time)\n"
                         "50% = Every 2nd frame (default, balanced)\n"
                         "33% = Every 3rd frame (power saver)\n"
                         "25% = Every 4th frame (low)\n"
                         "Range: 10-100%. Default: 50%.\n"
                         "Lower values save CPU/GPU but reduce hand tracking smoothness.",
            range_min=10,
            range_max=100,
            initial_value=50,
            update_func=lambda v: self.parent.update_setting('pose_processing_density', v),
            is_float=False
        )
        row += 1
        
        # Density description label
        self.parent.pose_density_desc = QLabel("Balanced (Every 2nd frame)")
        self.parent.pose_density_desc.setStyleSheet("color: #4CAF50; font-size: 9px; font-style: italic;")
        pose_layout.addWidget(self.parent.pose_density_desc, row, 1, 1, 2)
        row += 1
        
        # Connect slider to update description
        self.parent.pose_density_slider.valueChanged.connect(self._update_density_description)
        
        return section
    
    def _update_density_description(self, value):
        """Update the density description label based on slider value"""
        if value >= 100:
            desc = "Real-time (Every frame)"
            color = "#FF9800"  # Orange
        elif value >= 90:
            desc = "Near Real-time (9/10 frames)"
            color = "#8BC34A"  # Light green
        elif value >= 75:
            desc = "High Quality (3/4 frames)"
            color = "#4CAF50"  # Green
        elif value >= 67:
            desc = "Balanced (2/3 frames)"
            color = "#4CAF50"  # Green
        elif value == 50:
            desc = "Balanced (Every 2nd frame)"
            color = "#4CAF50"  # Green
        elif value >= 33:
            desc = "Medium (Every 3rd frame)"
            color = "#2196F3"  # Blue
        elif value >= 25:
            desc = "Power Saver (Every 4th frame)"
            color = "#2196F3"  # Blue
        else:
            desc = f"Low ({value}% of frames)"
            color = "#9E9E9E"  # Gray
        
        self.parent.pose_density_desc.setText(desc)
        self.parent.pose_density_desc.setStyleSheet(f"color: {color}; font-size: 9px; font-style: italic;")
    
    def _update_ball_density_description(self, value):
        """Update the ball density description label based on slider value"""
        if value >= 100:
            desc = "Real-time (Every frame)"
            color = "#FF9800"  # Orange
        elif value >= 90:
            desc = "Near Real-time (9/10 frames)"
            color = "#8BC34A"  # Light green
        elif value >= 75:
            desc = "High Quality (3/4 frames)"
            color = "#4CAF50"  # Green
        elif value >= 67:
            desc = "Balanced (2/3 frames)"
            color = "#4CAF50"  # Green
        elif value == 50:
            desc = "Balanced (Every 2nd frame)"
            color = "#4CAF50"  # Green
        elif value >= 33:
            desc = "Medium (Every 3rd frame)"
            color = "#2196F3"  # Blue
        elif value >= 25:
            desc = "Power Saver (Every 4th frame)"
            color = "#2196F3"  # Blue
        else:
            desc = f"Low ({value}% of frames)"
            color = "#9E9E9E"  # Gray
        
        self.parent.ball_density_desc.setText(desc)
        self.parent.ball_density_desc.setStyleSheet(f"color: {color}; font-size: 9px; font-style: italic;")