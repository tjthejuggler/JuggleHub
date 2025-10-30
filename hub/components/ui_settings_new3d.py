"""
New 3D Tracker Settings Sections for JuggleHub UI.
Contains settings sections that are ONLY visible when new_3d tracker is selected.
"""

from PyQt6.QtWidgets import (QLabel, QPushButton, QGridLayout, QCheckBox, QVBoxLayout, QGroupBox)
from PyQt6.QtCore import Qt
from .ui_widgets import CollapsibleGroupBox
import juggler_pb2


class New3DSettingsSections:
    """New 3D tracker-specific settings sections."""
    
    def __init__(self, parent_widget, udp_client, zmq_client):
        """
        Initialize New 3D tracker settings sections.
        
        Args:
            parent_widget: Parent CalibrationSettingsWidget instance
            udp_client: UDP client for sending settings to engine
            zmq_client: ZMQ client for sending commands to engine
        """
        self.parent = parent_widget
        self.udp_client = udp_client
        self.zmq_client = zmq_client
    
    def create_physics_section(self):
        """Create the Physics & Kalman Filter settings section"""
        section = CollapsibleGroupBox("⚛️ Physics & Kalman Filter", collapsed=False)
        layout = QGridLayout()
        section.get_content_layout().addLayout(layout)
        
        row = 0
        
        # Info label
        info_label = QLabel("ℹ️ Configure physics parameters for Kalman filter prediction")
        info_label.setStyleSheet("color: #aaaaaa; font-size: 10px;")
        info_label.setWordWrap(True)
        layout.addWidget(info_label, row, 0, 1, 3)
        row += 1
        
        # Held Radius
        self.parent.new3d_held_radius_slider, self.parent.new3d_held_radius_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="Held Radius (cm)",
            tooltip_text="Radius around wrist where ball is considered 'held'.\n"
                         "Range: 5-30cm. Default: 12cm.\n"
                         "Smaller = stricter held detection, Larger = more forgiving.",
            range_min=5,
            range_max=30,
            initial_value=12,
            update_func=lambda v: self.parent.update_setting('held_radius_m', v / 100.0),
            is_float=False
        )
        row += 1
        
        # Held Circle Offset
        self.parent.new3d_held_circle_offset_slider, self.parent.new3d_held_circle_offset_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="Held Circle Offset (cm)",
            tooltip_text="Distance from wrist towards hand center for held ball position.\n"
                         "Uses forearm skeleton direction to offset the held circle center.\n"
                         "Range: 0-15cm. Default: 5cm.\n"
                         "0cm = at wrist (old behavior), 5cm = towards palm, 10cm+ = in hand center.",
            range_min=0,
            range_max=15,
            initial_value=5,
            update_func=lambda v: self.parent.update_setting('held_circle_offset_cm', v),
            is_float=False
        )
        row += 1
        
        # Throw Velocity Threshold
        self.parent.new3d_throw_velocity_slider, self.parent.new3d_throw_velocity_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="Throw Velocity Threshold (m/s)",
            tooltip_text="Minimum relative velocity between ball and hand to trigger throw.\n"
                         "Range: 0.1-3.0 m/s. Default: 0.5 m/s.\n"
                         "Lower = more sensitive, Higher = requires faster throws.",
            range_min=10,
            range_max=300,
            initial_value=50,
            update_func=lambda v: self.parent.update_setting('throw_velocity_threshold_mps', v / 100.0),
            is_float=True
        )
        row += 1
        
        # Gravity
        self.parent.new3d_gravity_slider, self.parent.new3d_gravity_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="Gravity (m/s²)",
            tooltip_text="Gravitational acceleration for trajectory prediction.\n"
                         "Range: 5.0-15.0 m/s². Default: 9.81 m/s² (Earth gravity).\n"
                         "Adjust if calibration seems off.",
            range_min=50,
            range_max=150,
            initial_value=98,
            update_func=lambda v: self.parent.update_setting('gravity_y', -v / 10.0),
            is_float=True
        )
        row += 1
        
        return section
    
    def create_tracking_logic_section(self):
        """Create the Tracking Logic settings section"""
        section = CollapsibleGroupBox("🎯 Tracking Logic", collapsed=False)
        layout = QGridLayout()
        section.get_content_layout().addLayout(layout)
        
        row = 0
        
        # Info label
        info_label = QLabel("ℹ️ Configure tracking behavior and confirmation thresholds")
        info_label.setStyleSheet("color: #aaaaaa; font-size: 10px;")
        info_label.setWordWrap(True)
        layout.addWidget(info_label, row, 0, 1, 3)
        row += 1
        
        # NOTE: "Max Frames Unseen" removed - New3D tracker uses persistent balls that never delete
        # Balls stay tracked indefinitely, with HELD balls locked to wrist even when not visible
        
        # Min Frames for New Track
        self.parent.new3d_min_frames_new_track_slider, self.parent.new3d_min_frames_new_track_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="Min Frames for New Track",
            tooltip_text="Minimum consecutive frames to confirm a new track.\n"
                         "Range: 1-10 frames. Default: 3 frames.\n"
                         "Higher = fewer false positives, Lower = faster detection.",
            range_min=1,
            range_max=10,
            initial_value=3,
            update_func=lambda v: self.parent.update_setting('min_frames_for_new_track', v),
            is_float=False
        )
        row += 1
        
        # Min Frames for Color Lock
        self.parent.new3d_min_frames_color_lock_slider, self.parent.new3d_min_frames_color_lock_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="Min Frames for Color Lock",
            tooltip_text="Minimum frames before locking ball color identity.\n"
                         "Range: 1-20 frames. Default: 5 frames.\n"
                         "Higher = more stable color ID, Lower = faster color assignment.",
            range_min=1,
            range_max=20,
            initial_value=5,
            update_func=lambda v: self.parent.update_setting('min_frames_for_color_lock', v),
            is_float=False
        )
        row += 1
        
        return section
    
    def create_association_section(self):
        """Create the Detection Association settings section"""
        section = CollapsibleGroupBox("🔗 Detection Association", collapsed=False)
        layout = QGridLayout()
        section.get_content_layout().addLayout(layout)
        
        row = 0
        
        # Info label
        info_label = QLabel("ℹ️ Configure how detections are matched to tracked balls")
        info_label.setStyleSheet("color: #aaaaaa; font-size: 10px;")
        info_label.setWordWrap(True)
        layout.addWidget(info_label, row, 0, 1, 3)
        row += 1
        
        # Association Max Distance
        self.parent.new3d_association_max_distance_slider, self.parent.new3d_association_max_distance_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="Max Association Distance (m)",
            tooltip_text="Maximum distance for matching detections to tracks.\n"
                         "Range: 0.1-2.0 m. Default: 0.5 m.\n"
                         "Smaller = stricter matching, Larger = more flexible.",
            range_min=10,
            range_max=200,
            initial_value=50,
            update_func=lambda v: self.parent.update_setting('association_max_distance_m', v / 100.0),
            is_float=True
        )
        row += 1
        
        # Color Mismatch Penalty
        self.parent.new3d_color_mismatch_penalty_slider, self.parent.new3d_color_mismatch_penalty_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="Color Mismatch Penalty (m)",
            tooltip_text="Penalty distance added when detection color doesn't match ball color.\n"
                         "Range: 0.0-5.0 m. Default: 0.5 m.\n"
                         "Higher values = stronger preference for color-matched detections.\n"
                         "Set to 2.0+ to prevent wrong-color associations.\n"
                         "Set to 0.0 to disable color-based association entirely.",
            range_min=0,
            range_max=500,
            initial_value=50,
            update_func=lambda v: self.parent.update_setting('color_mismatch_penalty_m', v / 100.0),
            is_float=True
        )
        row += 1
        
        # Color Sample Radius
        self.parent.new3d_color_sample_radius_slider, self.parent.new3d_color_sample_radius_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="Color Sample Radius (pixels)",
            tooltip_text="Pixel radius for color sampling around detection center.\n"
                         "Range: 0-5 pixels. Default: 1 pixel (3x3 region).\n"
                         "0 = center pixel only, 1 = 3x3, 2 = 5x5, 3 = 7x7, etc.\n"
                         "Larger = more stable but may include edges/background.\n"
                         "Smaller = more precise but sensitive to noise.",
            range_min=0,
            range_max=5,
            initial_value=1,
            update_func=lambda v: self.parent.update_setting('color_sample_radius', v),
            is_float=False
        )
        row += 1
        
        # Min Saturation Threshold
        self.parent.new3d_min_saturation_threshold_slider, self.parent.new3d_min_saturation_threshold_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="Min Saturation Threshold",
            tooltip_text="Minimum saturation value to include pixel in color sampling.\n"
                         "Range: 0-255. Default: 50.\n"
                         "Filters out low-saturation pixels (grays/whites) that vary with lighting.\n"
                         "0 = include all pixels (no filtering)\n"
                         "50 = exclude very desaturated colors (recommended)\n"
                         "100+ = only use highly saturated colors\n"
                         "Higher = more stable but may reject valid ball colors.",
            range_min=0,
            range_max=255,
            initial_value=50,
            update_func=lambda v: self.parent.update_setting('min_saturation_threshold', v),
            is_float=False
        )
        row += 1
        
        return section
    
    def create_hand_velocity_section(self):
        """Create the Hand Velocity Tracking settings section"""
        section = CollapsibleGroupBox("✋ Hand Velocity Tracking", collapsed=False)
        layout = QGridLayout()
        section.get_content_layout().addLayout(layout)
        
        row = 0
        
        # Info label
        info_label = QLabel("ℹ️ Configure hand velocity-based throw detection enhancement")
        info_label.setStyleSheet("color: #aaaaaa; font-size: 10px;")
        info_label.setWordWrap(True)
        layout.addWidget(info_label, row, 0, 1, 3)
        row += 1
        
        # Hand Velocity Enabled Toggle
        label = QLabel("Enable Hand Velocity")
        label.setToolTip("Enable velocity-based throw detection.\n"
                        "When enabled, fast hand movements enhance throw detection.")
        layout.addWidget(label, row, 0)
        
        self.parent.new3d_hand_velocity_enabled_toggle = QCheckBox()
        self.parent.new3d_hand_velocity_enabled_toggle.setChecked(True)
        self.parent.new3d_hand_velocity_enabled_toggle.stateChanged.connect(
            lambda state: self.parent.update_setting('hand_velocity_enabled', 1 if state == Qt.CheckState.Checked.value else 0)
        )
        layout.addWidget(self.parent.new3d_hand_velocity_enabled_toggle, row, 1, 1, 2)
        row += 1
        
        # Hand Velocity Threshold
        self.parent.new3d_hand_velocity_threshold_slider, self.parent.new3d_hand_velocity_threshold_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="Hand Velocity Threshold (m/s)",
            tooltip_text="Minimum hand speed to trigger enhanced throw detection.\n"
                         "Range: 0.1-5.0 m/s. Default: 1.0 m/s.\n"
                         "Lower = more sensitive to hand movement.",
            range_min=10,
            range_max=500,
            initial_value=100,
            update_func=lambda v: self.parent.update_setting('hand_velocity_threshold', v / 100.0),
            is_float=True
        )
        row += 1
        
        return section
    
    def create_pose_model_section(self):
        """Create the Pose Model settings section (New 3D specific)"""
        section = CollapsibleGroupBox("🧍 Pose Model Settings", collapsed=False)
        layout = QGridLayout()
        section.get_content_layout().addLayout(layout)
        
        row = 0
        
        # Info label
        info_label = QLabel("ℹ️ Configure pose estimation model and processing frequency")
        info_label.setStyleSheet("color: #aaaaaa; font-size: 10px;")
        info_label.setWordWrap(True)
        layout.addWidget(info_label, row, 0, 1, 3)
        row += 1
        
        # Enable Pose Model Toggle
        label = QLabel("Enable Pose Model")
        label.setToolTip("Enable YOLO pose estimation for hand tracking.\n"
                        "When disabled, no hand detection occurs.")
        layout.addWidget(label, row, 0)
        
        self.parent.new3d_pose_model_toggle = QPushButton("Enable Pose Model")
        self.parent.new3d_pose_model_toggle.setCheckable(True)
        self.parent.new3d_pose_model_toggle.setChecked(True)
        self.parent.new3d_pose_model_toggle.clicked.connect(
            lambda: self._toggle_pose_model()
        )
        self.parent.new3d_pose_model_toggle.setStyleSheet("""
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
        layout.addWidget(self.parent.new3d_pose_model_toggle, row, 1, 1, 2)
        row += 1
        
        # Processing Density Slider
        self.parent.new3d_pose_density_slider, self.parent.new3d_pose_density_label = self.parent._create_slider_widget(
            parent_layout=layout,
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
        self.parent.new3d_pose_density_desc = QLabel("Balanced (Every 2nd frame)")
        self.parent.new3d_pose_density_desc.setStyleSheet("color: #4CAF50; font-size: 9px; font-style: italic;")
        layout.addWidget(self.parent.new3d_pose_density_desc, row, 1, 1, 2)
        row += 1
        
        # Connect slider to update description
        self.parent.new3d_pose_density_slider.valueChanged.connect(self._update_density_description)
        
        return section
    
    def _toggle_pose_model(self):
        """Toggle pose model on/off"""
        is_enabled = self.parent.new3d_pose_model_toggle.isChecked()
        self.parent.new3d_pose_model_toggle.setText("Enable Pose Model" if is_enabled else "Pose Model DISABLED")
        self.parent.update_setting('enable_pose_estimation', 1 if is_enabled else 0)
    
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
        
        self.parent.new3d_pose_density_desc.setText(desc)
        self.parent.new3d_pose_density_desc.setStyleSheet(f"color: {color}; font-size: 9px; font-style: italic;")
    
    def create_visualization_section(self):
        """Create the Visualization settings section"""
        section = CollapsibleGroupBox("👁️ Visualization", collapsed=False)
        layout = QGridLayout()
        section.get_content_layout().addLayout(layout)
        
        row = 0
        
        # Info label
        info_label = QLabel("ℹ️ Configure visual debugging overlays")
        info_label.setStyleSheet("color: #aaaaaa; font-size: 10px;")
        info_label.setWordWrap(True)
        layout.addWidget(info_label, row, 0, 1, 3)
        row += 1
        
        # Show Kalman Prediction
        label = QLabel("Show Kalman Prediction")
        label.setToolTip("Display predicted ball positions from Kalman filter.\n"
                        "Shows where the tracker expects the ball to be.")
        layout.addWidget(label, row, 0)
        
        self.parent.new3d_show_kalman_prediction_toggle = QCheckBox()
        self.parent.new3d_show_kalman_prediction_toggle.setChecked(True)
        self.parent.new3d_show_kalman_prediction_toggle.stateChanged.connect(
            lambda state: self.parent.update_setting('show_kalman_prediction', 1 if state == Qt.CheckState.Checked.value else 0)
        )
        layout.addWidget(self.parent.new3d_show_kalman_prediction_toggle, row, 1, 1, 2)
        row += 1
        
        # Show Held Radius
        label = QLabel("Show Held Radius")
        label.setToolTip("Display the held detection radius around wrists.\n"
                        "Shows the zone where balls are considered held.")
        layout.addWidget(label, row, 0)
        
        self.parent.new3d_show_held_radius_toggle = QCheckBox()
        self.parent.new3d_show_held_radius_toggle.setChecked(True)
        self.parent.new3d_show_held_radius_toggle.stateChanged.connect(
            lambda state: self.parent.update_setting('show_held_radius', 1 if state == Qt.CheckState.Checked.value else 0)
        )
        layout.addWidget(self.parent.new3d_show_held_radius_toggle, row, 1, 1, 2)
        row += 1
        
        # Show Association Lines
        label = QLabel("Show Association Lines")
        label.setToolTip("Display lines connecting detections to tracked balls.\n"
                        "Useful for debugging detection-to-track matching.")
        layout.addWidget(label, row, 0)
        
        self.parent.new3d_show_association_lines_toggle = QCheckBox()
        self.parent.new3d_show_association_lines_toggle.setChecked(True)
        self.parent.new3d_show_association_lines_toggle.stateChanged.connect(
            lambda state: self.parent.update_setting('show_association_lines', 1 if state == Qt.CheckState.Checked.value else 0)
        )
        layout.addWidget(self.parent.new3d_show_association_lines_toggle, row, 1, 1, 2)
        row += 1
        
        return section
    
    def create_audio_indicators_section(self):
        """Create the Audio Indicators settings section"""
        section = CollapsibleGroupBox("🔊 Audio Indicators", collapsed=False)
        layout = QGridLayout()
        section.get_content_layout().addLayout(layout)
        
        row = 0
        
        # Info label
        info_label = QLabel("ℹ️ Configure audio feedback for throw and catch events")
        info_label.setStyleSheet("color: #aaaaaa; font-size: 10px;")
        info_label.setWordWrap(True)
        layout.addWidget(info_label, row, 0, 1, 3)
        row += 1
        
        # Separator
        layout.addWidget(QLabel("Sound Effects:"), row, 0, 1, 3)
        row += 1
        
        # Sound on catches toggle with test button
        self.parent.new3d_sound_on_catch_toggle = QPushButton("Sound on Catches")
        self.parent.new3d_sound_on_catch_toggle.setCheckable(True)
        self.parent.new3d_sound_on_catch_toggle.setChecked(False)
        self.parent.new3d_sound_on_catch_toggle.clicked.connect(
            lambda: self.parent.update_setting('tc_sound_on_catch', 1 if self.parent.new3d_sound_on_catch_toggle.isChecked() else 0)
        )
        layout.addWidget(self.parent.new3d_sound_on_catch_toggle, row, 0, 1, 2)
        
        # Test catch sound button
        self.parent.new3d_test_catch_sound_button = QPushButton("🔊 Test")
        self.parent.new3d_test_catch_sound_button.setMaximumWidth(80)
        self.parent.new3d_test_catch_sound_button.clicked.connect(self.parent.test_catch_sound)
        self.parent.new3d_test_catch_sound_button.setStyleSheet("""
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
        layout.addWidget(self.parent.new3d_test_catch_sound_button, row, 2)
        row += 1
        
        # Sound on throws toggle with test button
        self.parent.new3d_sound_on_throw_toggle = QPushButton("Sound on Throws")
        self.parent.new3d_sound_on_throw_toggle.setCheckable(True)
        self.parent.new3d_sound_on_throw_toggle.setChecked(False)
        self.parent.new3d_sound_on_throw_toggle.clicked.connect(
            lambda: self.parent.update_setting('tc_sound_on_throw', 1 if self.parent.new3d_sound_on_throw_toggle.isChecked() else 0)
        )
        layout.addWidget(self.parent.new3d_sound_on_throw_toggle, row, 0, 1, 2)
        
        # Test throw sound button
        self.parent.new3d_test_throw_sound_button = QPushButton("🔊 Test")
        self.parent.new3d_test_throw_sound_button.setMaximumWidth(80)
        self.parent.new3d_test_throw_sound_button.clicked.connect(self.parent.test_throw_sound)
        self.parent.new3d_test_throw_sound_button.setStyleSheet("""
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
        layout.addWidget(self.parent.new3d_test_throw_sound_button, row, 2)
        row += 1
        
        # Name on catches toggle with test button
        self.parent.new3d_name_on_catch_toggle = QPushButton("Name on Catches")
        self.parent.new3d_name_on_catch_toggle.setCheckable(True)
        self.parent.new3d_name_on_catch_toggle.setChecked(False)
        self.parent.new3d_name_on_catch_toggle.clicked.connect(
            lambda: self.parent.update_setting('tc_name_on_catch', 1 if self.parent.new3d_name_on_catch_toggle.isChecked() else 0)
        )
        layout.addWidget(self.parent.new3d_name_on_catch_toggle, row, 0, 1, 2)
        
        # Test catch name button
        self.parent.new3d_test_catch_name_button = QPushButton("🔊 Test")
        self.parent.new3d_test_catch_name_button.setMaximumWidth(80)
        self.parent.new3d_test_catch_name_button.clicked.connect(self.parent.test_catch_name)
        self.parent.new3d_test_catch_name_button.setStyleSheet("""
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
        layout.addWidget(self.parent.new3d_test_catch_name_button, row, 2)
        row += 1
        
        # Name on throws toggle with test button
        self.parent.new3d_name_on_throw_toggle = QPushButton("Name on Throws")
        self.parent.new3d_name_on_throw_toggle.setCheckable(True)
        self.parent.new3d_name_on_throw_toggle.setChecked(False)
        self.parent.new3d_name_on_throw_toggle.clicked.connect(
            lambda: self.parent.update_setting('tc_name_on_throw', 1 if self.parent.new3d_name_on_throw_toggle.isChecked() else 0)
        )
        layout.addWidget(self.parent.new3d_name_on_throw_toggle, row, 0, 1, 2)
        
        # Test throw name button
        self.parent.new3d_test_throw_name_button = QPushButton("🔊 Test")
        self.parent.new3d_test_throw_name_button.setMaximumWidth(80)
        self.parent.new3d_test_throw_name_button.clicked.connect(self.parent.test_throw_name)
        self.parent.new3d_test_throw_name_button.setStyleSheet("""
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
        layout.addWidget(self.parent.new3d_test_throw_name_button, row, 2)
        row += 1
        
        return section
    
    def create_depth_blob_detection_section(self):
        """Create the Depth-Based Blob Detection settings section"""
        section = CollapsibleGroupBox("🔍 Depth-Based Blob Detection", collapsed=False)
        layout = QGridLayout()
        section.get_content_layout().addLayout(layout)
        
        row = 0
        
        # Info label
        info_label = QLabel("ℹ️ Alternative to YOLO: Detect balls using depth-based blob filtering")
        info_label.setStyleSheet("color: #aaaaaa; font-size: 10px;")
        info_label.setWordWrap(True)
        layout.addWidget(info_label, row, 0, 1, 3)
        row += 1
        
        # Enable Depth Blob Detection Toggle
        label = QLabel("Enable Depth Blob Detection")
        label.setToolTip("Enable depth-based blob detection as an alternative to YOLO.\n"
                        "When enabled, balls are detected using depth filtering instead of YOLO model.")
        layout.addWidget(label, row, 0)
        
        self.parent.new3d_depth_blob_enabled_toggle = QPushButton("Use Depth Blob Detection")
        self.parent.new3d_depth_blob_enabled_toggle.setCheckable(True)
        self.parent.new3d_depth_blob_enabled_toggle.setChecked(False)
        self.parent.new3d_depth_blob_enabled_toggle.clicked.connect(
            lambda: self._toggle_depth_blob_detection()
        )
        self.parent.new3d_depth_blob_enabled_toggle.setStyleSheet("""
            QPushButton {
                background-color: #f44336;
                color: white;
                padding: 8px;
                border-radius: 4px;
                font-weight: bold;
            }
            QPushButton:hover { background-color: #d32f2f; }
            QPushButton:pressed { background-color: #b71c1c; }
            QPushButton:checked { 
                background-color: #4CAF50;
            }
            QPushButton:checked:hover { 
                background-color: #45a049;
            }
            QPushButton:!checked { 
                background-color: #f44336;
            }
        """)
        layout.addWidget(self.parent.new3d_depth_blob_enabled_toggle, row, 1, 1, 2)
        row += 1
        
        # Min Distance Slider
        self.parent.new3d_depth_min_distance_slider, self.parent.new3d_depth_min_distance_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="Min Distance (cm)",
            tooltip_text="Minimum depth distance for blob detection.\n"
                         "Range: 10-200cm. Default: 30cm.\n"
                         "Blobs closer than this will be filtered out.",
            range_min=10,
            range_max=200,
            initial_value=30,
            update_func=lambda v: self.parent.update_setting('depth_blob_min_distance_cm', v),
            is_float=False
        )
        row += 1
        
        # Max Distance Slider
        self.parent.new3d_depth_max_distance_slider, self.parent.new3d_depth_max_distance_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="Max Distance (cm)",
            tooltip_text="Maximum depth distance for blob detection.\n"
                         "Range: 20-300cm. Default: 150cm.\n"
                         "Blobs farther than this will be filtered out.",
            range_min=20,
            range_max=300,
            initial_value=150,
            update_func=lambda v: self.parent.update_setting('depth_blob_max_distance_cm', v),
            is_float=False
        )
        row += 1
        
        # Min Surface Area Slider
        self.parent.new3d_depth_min_area_slider, self.parent.new3d_depth_min_area_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="Min Surface Area (pixels²)",
            tooltip_text="Minimum blob surface area in pixels.\n"
                         "Range: 10-1000 pixels². Default: 50 pixels².\n"
                         "Smaller blobs will be filtered out.",
            range_min=10,
            range_max=1000,
            initial_value=50,
            update_func=lambda v: self.parent.update_setting('depth_blob_min_area_px', v),
            is_float=False
        )
        row += 1
        
        # Max Surface Area Slider
        self.parent.new3d_depth_max_area_slider, self.parent.new3d_depth_max_area_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="Max Surface Area (pixels²)",
            tooltip_text="Maximum blob surface area in pixels.\n"
                         "Range: 100-5000 pixels². Default: 2000 pixels².\n"
                         "Larger blobs will be filtered out.",
            range_min=100,
            range_max=5000,
            initial_value=2000,
            update_func=lambda v: self.parent.update_setting('depth_blob_max_area_px', v),
            is_float=False
        )
        row += 1
        
        # Show Filtered Pixels Toggle
        label = QLabel("Show Filtered Pixels")
        label.setToolTip("Display RGB data for pixels that pass depth blob filters.\n"
                        "Useful for debugging and tuning filter parameters.")
        layout.addWidget(label, row, 0)
        
        self.parent.new3d_show_depth_filtered_toggle = QCheckBox()
        self.parent.new3d_show_depth_filtered_toggle.setChecked(True)
        self.parent.new3d_show_depth_filtered_toggle.stateChanged.connect(
            lambda state: self.parent.update_setting('show_depth_filtered_pixels', 1 if state == Qt.CheckState.Checked.value else 0)
        )
        layout.addWidget(self.parent.new3d_show_depth_filtered_toggle, row, 1, 1, 2)
        row += 1
        
        return section
    
    def _toggle_depth_blob_detection(self):
        """Toggle depth blob detection on/off"""
        is_enabled = self.parent.new3d_depth_blob_enabled_toggle.isChecked()
        self.parent.new3d_depth_blob_enabled_toggle.setText(
            "Depth Blob Detection ACTIVE" if is_enabled else "Use Depth Blob Detection"
        )
        self.parent.update_setting('enable_depth_blob_detection', 1 if is_enabled else 0)
        
        # Also toggle YOLO ball detection off when depth blob is enabled
        if is_enabled:
            print("⚠️ Depth blob detection enabled - YOLO ball detection will be disabled")
            self.parent.update_setting('enable_ball_detection', 0)
        else:
            print("ℹ️ Depth blob detection disabled - YOLO ball detection can be re-enabled")
    
    def create_color_calibration_section(self):
        """Create the Color Calibration section for New 3D Tracker"""
        section = CollapsibleGroupBox("🎨 Color Calibration", collapsed=False)
        layout = QVBoxLayout()
        section.get_content_layout().addLayout(layout)
        
        # Use shared ball profiles - ensure they're loaded first
        if not hasattr(self.parent, 'new3d_ball_profiles'):
            self._load_new3d_profiles()
        
        color_profiles = self.parent.new3d_ball_profiles
        
        # Store references for later use
        if not hasattr(self.parent, 'new3d_ball_checkboxes'):
            self.parent.new3d_ball_checkboxes = {}
        if not hasattr(self.parent, 'new3d_ball_calibration_labels'):
            self.parent.new3d_ball_calibration_labels = {}
        
        # Create a widget for each color profile
        for profile in color_profiles:
            ball_name = profile['name']
            ball_group = QGroupBox(ball_name.capitalize())
            ball_layout = QGridLayout(ball_group)
            
            # Checkbox for enabling/disabling this ball
            checkbox = QPushButton(f"Track {ball_name.capitalize()}")
            checkbox.setCheckable(True)
            is_enabled = profile.get('enabled', True)
            checkbox.setChecked(is_enabled)
            checkbox.clicked.connect(lambda checked, name=ball_name: self._toggle_new3d_ball_tracking(name, checked))
            self.parent.new3d_ball_checkboxes[ball_name] = checkbox
            ball_layout.addWidget(checkbox, 0, 0, 1, 3)
            
            # Get current calibration values
            avg_hue = profile.get('avg_hue', -1.0)
            avg_sat = profile.get('avg_saturation', -1.0)
            
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
            self.parent.new3d_ball_calibration_labels[ball_name] = {
                'hue': hue_value_label,
                'saturation': sat_value_label
            }
            
            # HSV Ranges display (min/max)
            min_hsv = profile.get('min_hsv', [0, 0, 0])
            max_hsv = profile.get('max_hsv', [180, 255, 255])
            
            # Min HSV display
            ball_layout.addWidget(QLabel("Min HSV:"), row, 0)
            min_hsv_label = QLabel(f"H:{min_hsv[0]:.0f} S:{min_hsv[1]:.0f} V:{min_hsv[2]:.0f}")
            min_hsv_label.setStyleSheet("color: #2196F3; font-size: 9px;")
            ball_layout.addWidget(min_hsv_label, row, 1, 1, 2)
            row += 1
            
            # Max HSV display
            ball_layout.addWidget(QLabel("Max HSV:"), row, 0)
            max_hsv_label = QLabel(f"H:{max_hsv[0]:.0f} S:{max_hsv[1]:.0f} V:{max_hsv[2]:.0f}")
            max_hsv_label.setStyleSheet("color: #2196F3; font-size: 9px;")
            ball_layout.addWidget(max_hsv_label, row, 1, 1, 2)
            row += 1
            
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
            calibrate_button.clicked.connect(lambda checked, name=ball_name: self.parent.start_color_calibration(name))
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
        auto_cal_button.clicked.connect(self.parent.auto_calibrate_hues)
        layout.addWidget(auto_cal_button)
        
        return section
    
    def create_ball_profiles_section(self):
        """Create the Ball Profiles section (New 3D only)"""
        section = CollapsibleGroupBox("🎨 Ball Profiles", collapsed=False)
        layout = QVBoxLayout()
        section.get_content_layout().addLayout(layout)
        
        # Use shared ball profiles - ensure they're loaded first
        if not hasattr(self.parent, 'new3d_ball_profiles'):
            self._load_new3d_profiles()
        
        # Store checkbox references
        self.parent.new3d_ball_checkboxes = {}
        
        # Create a widget for each ball profile
        for profile in self.parent.new3d_ball_profiles:
            ball_name = profile['name']
            ball_group = QGroupBox(ball_name.capitalize())
            ball_layout = QGridLayout(ball_group)
            
            # Checkbox for enabling/disabling this ball
            checkbox = QPushButton(f"Track {ball_name.capitalize()}")
            checkbox.setCheckable(True)
            is_enabled = profile.get('enabled', True)
            checkbox.setChecked(is_enabled)
            checkbox.clicked.connect(lambda checked, name=ball_name: self._toggle_new3d_ball_tracking(name, checked))
            self.parent.new3d_ball_checkboxes[ball_name] = checkbox
            ball_layout.addWidget(checkbox, 0, 0, 1, 3)
            
            # Get current calibration values
            avg_hue = profile.get('avg_hue', -1.0)
            avg_sat = profile.get('avg_saturation', -1.0)
            
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
            if not hasattr(self.parent, 'new3d_ball_calibration_labels'):
                self.parent.new3d_ball_calibration_labels = {}
            self.parent.new3d_ball_calibration_labels[ball_name] = {
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
            calibrate_button.clicked.connect(lambda checked, name=ball_name: self._start_new3d_color_calibration(name))
            ball_layout.addWidget(calibrate_button, row, 0, 1, 3)
            row += 1
            
            # Info label
            info_label = QLabel("ℹ️ Click 'Set Color Profile' in Visualization, then click on a ball")
            info_label.setStyleSheet("color: #aaaaaa; font-size: 9px;")
            info_label.setWordWrap(True)
            ball_layout.addWidget(info_label, row, 0, 1, 3)
            
            layout.addWidget(ball_group)
        
        # Info about the active roster system
        roster_info = QLabel("💡 Active Roster System: The tracker will only look for enabled colors. "
                            "Maximum one track per enabled color at any time. When a track is lost, "
                            "its color becomes available again.")
        roster_info.setStyleSheet("color: #4CAF50; font-size: 9px; font-style: italic; padding: 10px;")
        roster_info.setWordWrap(True)
        layout.addWidget(roster_info)
        
        return section
    
    def _load_new3d_profiles(self):
        """Load New 3D ball profiles from calibration_settings_new3d.json"""
        import json
        import os
        settings_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "calibration_settings_new3d.json")
        settings_path = os.path.normpath(settings_path)
        
        # Get default profiles from ColorProfileManager
        from .color_profile_manager import ColorProfileManager
        color_manager = ColorProfileManager()
        
        try:
            with open(settings_path, 'r') as f:
                settings_data = json.load(f)
                self.parent.new3d_ball_profiles = settings_data.get('color_profiles', [])
            
            # If no profiles exist, initialize from ColorProfileManager
            if not self.parent.new3d_ball_profiles:
                print(f"ℹ️ No color_profiles found in {settings_path}, initializing from ColorProfileManager")
                self.parent.new3d_ball_profiles = []
                for profile in color_manager.profiles:
                    self.parent.new3d_ball_profiles.append({
                        'name': profile['name'],
                        'enabled': profile.get('enabled', True),
                        'avg_hue': -1.0,
                        'avg_saturation': -1.0,
                        'min_hsv': [0.0, 0.0, 0.0],
                        'max_hsv': [180.0, 255.0, 255.0],
                        'min_hsv2': [-1.0, 0.0, 0.0],
                        'max_hsv2': [-1.0, 255.0, 255.0]
                    })
                # Save the initialized profiles
                self._save_new3d_profiles()
                print(f"✅ Initialized {len(self.parent.new3d_ball_profiles)} default color profiles")
            
            print(f"✅ Loaded {len(self.parent.new3d_ball_profiles)} ball profiles from {settings_path}")
            print(f"   Profiles loaded: {[p['name'] for p in self.parent.new3d_ball_profiles]}")
        except Exception as e:
            print(f"❌ Error loading calibration_settings_new3d.json: {e}")
            # Initialize with default profiles as fallback
            self.parent.new3d_ball_profiles = []
            for profile in color_manager.profiles:
                self.parent.new3d_ball_profiles.append({
                    'name': profile['name'],
                    'enabled': profile.get('enabled', True),
                    'avg_hue': -1.0,
                    'avg_saturation': -1.0,
                    'min_hsv': [0.0, 0.0, 0.0],
                    'max_hsv': [180.0, 255.0, 255.0],
                    'min_hsv2': [-1.0, 0.0, 0.0],
                    'max_hsv2': [-1.0, 255.0, 255.0]
                })
    
    def _toggle_new3d_ball_tracking(self, ball_name: str, enabled: bool):
        """Toggle tracking for specific ball in New 3D tracker"""
        print(f"🔄 {'Enabling' if enabled else 'Disabling'} tracking for {ball_name} (New 3D)")
        
        # Update the profile in memory
        for profile in self.parent.new3d_ball_profiles:
            if profile['name'] == ball_name:
                profile['enabled'] = enabled
                break
        
        # Save to calibration_settings_new3d.json
        self._save_new3d_profiles()
        
        print(f"✅ Saved {ball_name} tracking state: {'enabled' if enabled else 'disabled'}")
        
        # CRITICAL FIX: Send reload command to engine immediately
        if self.zmq_client:
            print(f"🔄 Sending RELOAD_COLOR_PROFILES command to engine...")
            try:
                import juggler_pb2
                command = juggler_pb2.CommandRequest()
                command.type = juggler_pb2.CommandRequest.RELOAD_COLOR_PROFILES
                
                # Send command and wait for response
                response = self.zmq_client.send_command(command)
                
                if response and response.success:
                    print(f"✅ Engine reloaded color profiles successfully!")
                    print(f"   {ball_name} tracking is now {'ACTIVE' if enabled else 'STOPPED'}")
                else:
                    error_msg = response.message if response else "No response"
                    print(f"⚠️ Engine reload failed: {error_msg}")
            except Exception as e:
                print(f"❌ Error sending reload command: {e}")
        else:
            print(f"⚠️ ZMQ client not available - changes will take effect on next engine restart")
        
        if not self.parent._loading_settings:
            self.parent.save_settings()
    
    def _start_new3d_color_calibration(self, ball_name: str):
        """Start color calibration for a specific ball in New 3D tracker"""
        print(f"🎨 Starting color calibration for {ball_name} (New 3D)")
        
        # This will be handled by the main window's color calibration system
        # which is already integrated with the visualization tab
        if hasattr(self.parent, 'main_window') and self.parent.main_window:
            # Set the active color profile in the visualization tab
            viz_tab = self.parent.main_window.visualization_tab
            if hasattr(viz_tab, 'color_profile_combo'):
                # Find and select this color in the dropdown
                index = viz_tab.color_profile_combo.findText(ball_name.capitalize())
                if index >= 0:
                    viz_tab.color_profile_combo.setCurrentIndex(index)
                    print(f"✅ Selected '{ball_name}' in color profile dropdown")
                    print(f"   Now click 'Set Color Profile' and then click on a {ball_name} ball in the video")
                else:
                    print(f"⚠️ Color '{ball_name}' not found in dropdown")
            else:
                print(f"⚠️ Visualization tab not fully initialized")
        else:
            print(f"⚠️ Main window reference not available")
    
    def _save_new3d_profiles(self):
        """Save New 3D ball profiles to calibration_settings_new3d.json"""
        import json
        import os
        settings_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "calibration_settings_new3d.json")
        settings_path = os.path.normpath(settings_path)
        
        try:
            # Load existing settings
            with open(settings_path, 'r') as f:
                settings_data = json.load(f)
            
            # Update color_profiles
            settings_data['color_profiles'] = self.parent.new3d_ball_profiles
            
            # Save back
            with open(settings_path, 'w') as f:
                json.dump(settings_data, f, indent=4)
            
            print(f"✅ Saved New 3D ball profiles to {settings_path}")
        except Exception as e:
            print(f"❌ Error saving New 3D profiles: {e}")