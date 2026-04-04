"""
New 3D Tracker Settings Sections for JuggleHub UI.
Contains settings sections that are ONLY visible when new_3d tracker is selected.
"""

from PyQt6.QtWidgets import (QLabel, QPushButton, QGridLayout, QCheckBox, QVBoxLayout, QGroupBox, QComboBox)
from PyQt6.QtCore import Qt
from PyQt6.QtGui import QColor
import colorsys
from .ui_widgets import CollapsibleGroupBox
from .time_based_calibration import TimeBasedCalibration
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
        
        # Initialize time-based calibration system
        self.calibration_system = TimeBasedCalibration()
        self.calibration_system.state_changed.connect(self._on_calibration_state_changed)
        self.calibration_system.calibration_complete.connect(self._on_calibration_complete)
        self.calibration_system.calibration_error.connect(self._on_calibration_error)
        
        # Initialize baseline recording system
        self.baseline_recording = False
        self.baseline_start_time = 0
        self.baseline_detections = []  # List of (x, y, width, height) tuples
        self.exclusion_zones = []  # List of exclusion zone rectangles
    
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
        info_label.setStyleSheet("color: #aaaaaa; font-size: 9px;")
        info_label.setWordWrap(True)
        layout.addWidget(info_label, row, 0, 1, 4)
        row += 1
        
        # Column 1: Kalman Prediction
        label = QLabel("Kalman Prediction")
        label.setStyleSheet("font-size: 10px;")
        label.setToolTip("Display predicted ball positions from Kalman filter.\n"
                        "Shows where the tracker expects the ball to be.")
        layout.addWidget(label, row, 0)
        
        self.parent.new3d_show_kalman_prediction_toggle = QCheckBox()
        self.parent.new3d_show_kalman_prediction_toggle.setChecked(True)
        self.parent.new3d_show_kalman_prediction_toggle.stateChanged.connect(
            lambda state: self.parent.update_setting('show_kalman_prediction', 1 if state == Qt.CheckState.Checked.value else 0)
        )
        layout.addWidget(self.parent.new3d_show_kalman_prediction_toggle, row, 1)
        
        # Column 2: Held Radius
        label = QLabel("Held Radius")
        label.setStyleSheet("font-size: 10px;")
        label.setToolTip("Display the held detection radius around wrists.\n"
                        "Shows the zone where balls are considered held.")
        layout.addWidget(label, row, 2)
        
        self.parent.new3d_show_held_radius_toggle = QCheckBox()
        self.parent.new3d_show_held_radius_toggle.setChecked(True)
        self.parent.new3d_show_held_radius_toggle.stateChanged.connect(
            lambda state: self.parent.update_setting('show_held_radius', 1 if state == Qt.CheckState.Checked.value else 0)
        )
        layout.addWidget(self.parent.new3d_show_held_radius_toggle, row, 3)
        row += 1
        
        # Column 1: Association Lines
        label = QLabel("Association Lines")
        label.setStyleSheet("font-size: 10px;")
        label.setToolTip("Display lines connecting detections to tracked balls.\n"
                        "Useful for debugging detection-to-track matching.")
        layout.addWidget(label, row, 0)
        
        self.parent.new3d_show_association_lines_toggle = QCheckBox()
        self.parent.new3d_show_association_lines_toggle.setChecked(True)
        self.parent.new3d_show_association_lines_toggle.stateChanged.connect(
            lambda state: self.parent.update_setting('show_association_lines', 1 if state == Qt.CheckState.Checked.value else 0)
        )
        layout.addWidget(self.parent.new3d_show_association_lines_toggle, row, 1)
        
        # Column 2: Depth Globs
        label = QLabel("Depth Globs")
        label.setStyleSheet("font-size: 10px;")
        label.setToolTip("Display depth glob detections when depth blob detection is enabled.\n"
                        "Shows where depth-based ball detection found potential balls.\n"
                        "Only visible when 'Enable Depth Blob Detection' is active.")
        layout.addWidget(label, row, 2)
        
        self.parent.new3d_show_depth_globs_toggle = QCheckBox()
        self.parent.new3d_show_depth_globs_toggle.setChecked(True)
        self.parent.new3d_show_depth_globs_toggle.stateChanged.connect(
            lambda state: self.parent.update_setting('show_depth_globs', 1 if state == Qt.CheckState.Checked.value else 0)
        )
        layout.addWidget(self.parent.new3d_show_depth_globs_toggle, row, 3)
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
            label_text="Min Surface Area (cm²)",
            tooltip_text="Minimum blob physical surface area in cm².\n"
                         "Range: 0-200 cm². Default: 50 cm².\n"
                         "This is DEPTH-AWARE: filters by actual 3D size, not pixel size.\n"
                         "Blobs are separated by depth FIRST, then filtered by surface area.\n"
                         "A small ball close to camera = same physical area as small ball far away.\n"
                         "Use this to filter out tiny noise while keeping balls at any distance.",
            range_min=0,
            range_max=200,
            initial_value=50,
            update_func=lambda v: self.parent.update_setting('depth_blob_min_area_px', v),
            is_float=False
        )
        row += 1
        
        # Max Surface Area Slider
        self.parent.new3d_depth_max_area_slider, self.parent.new3d_depth_max_area_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="Max Surface Area (cm²)",
            tooltip_text="Maximum blob physical surface area in cm².\n"
                         "Range: 0-200 cm². Default: 200 cm².\n"
                         "This is DEPTH-AWARE: filters by actual 3D size, not pixel size.\n"
                         "Blobs are separated by depth FIRST, then filtered by surface area.\n"
                         "A large person far away = same physical area as large person close.\n"
                         "Use this to filter out large objects (juggler, furniture) while keeping balls.",
            range_min=0,
            range_max=200,
            initial_value=200,
            update_func=lambda v: self.parent.update_setting('depth_blob_max_area_px', v),
            is_float=False
        )
        row += 1
        
        # Min Circularity Slider
        self.parent.new3d_depth_min_circularity_slider, self.parent.new3d_depth_min_circularity_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="Min Circularity",
            tooltip_text="Minimum circularity for blob shape filtering (0.0-1.0).\n"
                         "Range: 0.0-1.0. Default: 0.65.\n"
                         "Circularity = (4 × π × Area) / (Perimeter²)\n"
                         "1.0 = perfect circle, 0.785 = square, <0.7 = irregular shapes.\n"
                         "This filters out non-circular shapes like hands, fingers, and irregular objects.\n"
                         "Higher values = stricter (only very round shapes)\n"
                         "Lower values = more permissive (allows slightly irregular shapes)\n"
                         "Recommended: 0.6-0.7 for juggling balls (allows slight occlusion/motion blur)",
            range_min=0,
            range_max=100,
            initial_value=65,
            update_func=lambda v: self.parent.update_setting('depth_blob_min_circularity', v / 100.0),
            is_float=True
        )
        row += 1
        
        # Min Brightness Slider
        self.parent.new3d_depth_min_brightness_slider, self.parent.new3d_depth_min_brightness_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="Min Brightness (0-255)",
            tooltip_text="Minimum average brightness for blob detection (for LED balls).\n"
                         "Range: 0-255. Default: 0 (disabled).\n"
                         "This filters blobs by their average RGB brightness value.\n"
                         "Useful for LED juggling balls which are much brighter than regular balls.\n"
                         "0 = no brightness filtering (all blobs pass)\n"
                         "50 = filter out dim objects\n"
                         "100+ = only detect bright LED balls\n"
                         "Higher values = only very bright objects pass through.",
            range_min=0,
            range_max=255,
            initial_value=0,
            update_func=lambda v: self.parent.update_setting('depth_blob_min_brightness', v),
            is_float=False
        )
        row += 1
        
        # Max Whiteness Slider
        self.parent.new3d_depth_max_whiteness_slider, self.parent.new3d_depth_max_whiteness_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="Max Whiteness (0-255)",
            tooltip_text="Maximum whiteness for color sampling (filters bright pixels).\n"
                         "Range: 0-255. Default: 255 (no filtering).\n"
                         "This filters out overly white/bright pixels when determining blob color.\n"
                         "Useful for LED juggling balls where the brightest parts are too white.\n"
                         "Lower values = more aggressive filtering of bright pixels.\n"
                         "0 = only use completely black pixels (not recommended)\n"
                         "200 = filter out very bright pixels (good for LED balls)\n"
                         "255 = no filtering (include all pixels)",
            range_min=0,
            range_max=255,
            initial_value=255,
            update_func=lambda v: self.parent.update_setting('depth_blob_max_whiteness', v),
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
        
        # === COLOR-FIRST DETECTION (LED Ball Mode) ===
        color_separator = QLabel("─" * 50)
        color_separator.setStyleSheet("color: #555555;")
        layout.addWidget(color_separator, row, 0, 1, 3)
        row += 1
        
        color_header = QLabel("🌈 Color-First Detection (LED Ball Mode)")
        color_header.setStyleSheet("color: #FF9800; font-size: 11px; font-weight: bold;")
        layout.addWidget(color_header, row, 0, 1, 3)
        row += 1
        
        color_desc = QLabel("Uses calibrated color profiles to detect balls by color FIRST, then filters by depth/shape.\n"
                           "Much more robust for glowing LED balls — eliminates body/background false positives.")
        color_desc.setStyleSheet("color: #aaaaaa; font-size: 9px;")
        color_desc.setWordWrap(True)
        layout.addWidget(color_desc, row, 0, 1, 3)
        row += 1
        
        # Color Filter Enable Toggle
        label = QLabel("Enable Color-First Detection")
        label.setToolTip("When enabled, uses calibrated HSV color profiles to pre-filter blobs.\n"
                        "Only pixels matching a calibrated ball color AND within depth range are considered.\n"
                        "This dramatically reduces false positives from body parts and background.\n"
                        "Requires color calibration for each ball color.\n"
                        "Default: ON (recommended for LED balls)")
        layout.addWidget(label, row, 0)
        
        self.parent.new3d_color_filter_toggle = QPushButton("Color-First Detection ON")
        self.parent.new3d_color_filter_toggle.setCheckable(True)
        self.parent.new3d_color_filter_toggle.setChecked(True)
        self.parent.new3d_color_filter_toggle.clicked.connect(
            lambda: self._toggle_color_filter()
        )
        self.parent.new3d_color_filter_toggle.setStyleSheet("""
            QPushButton {
                background-color: #f44336;
                color: white;
                padding: 8px;
                border-radius: 4px;
                font-weight: bold;
            }
            QPushButton:hover { background-color: #d32f2f; }
            QPushButton:checked { background-color: #4CAF50; }
            QPushButton:checked:hover { background-color: #45a049; }
            QPushButton:!checked { background-color: #f44336; }
        """)
        layout.addWidget(self.parent.new3d_color_filter_toggle, row, 1, 1, 2)
        row += 1
        
        # Ball Color Preview Dropdown
        preview_label = QLabel("👁️ Preview Ball Color")
        preview_label.setToolTip("Select a ball color to preview its color+depth filter.\n"
                                "When 'Show Filtered Pixels' is ON, only pixels matching\n"
                                "this color's filter will be shown.\n"
                                "Use this to tune the filter until you see ONLY the ball.\n"
                                "Then switch to the next ball and repeat.\n"
                                "'All Colors' shows all detected ball pixels.")
        layout.addWidget(preview_label, row, 0)
        
        self.parent.new3d_preview_color_dropdown = QComboBox()
        self.parent.new3d_preview_color_dropdown.addItem("All Colors", "")
        # Populate with enabled ball colors from profiles
        if hasattr(self.parent, 'new3d_ball_profiles'):
            for profile in self.parent.new3d_ball_profiles:
                if profile.get('enabled', True):
                    name = profile['name']
                    self.parent.new3d_preview_color_dropdown.addItem(f"🔵 {name}", name)
        self.parent.new3d_preview_color_dropdown.currentIndexChanged.connect(
            lambda idx: self.parent.update_setting(
                'depth_blob_preview_color',
                self.parent.new3d_preview_color_dropdown.currentData() or ""
            )
        )
        self.parent.new3d_preview_color_dropdown.setStyleSheet("""
            QComboBox {
                background-color: #333333;
                color: white;
                padding: 6px;
                border-radius: 4px;
                border: 1px solid #555555;
                font-weight: bold;
            }
            QComboBox:hover { border-color: #FF9800; }
            QComboBox QAbstractItemView {
                background-color: #333333;
                color: white;
                selection-background-color: #FF9800;
            }
        """)
        layout.addWidget(self.parent.new3d_preview_color_dropdown, row, 1, 1, 2)
        row += 1
        
        preview_hint = QLabel("💡 Turn ON 'Show Filtered Pixels' above to see the preview.\n"
                             "Adjust Hue Tolerance / Min Saturation / Min Value until you see only the ball.")
        preview_hint.setStyleSheet("color: #FF9800; font-size: 9px;")
        preview_hint.setWordWrap(True)
        layout.addWidget(preview_hint, row, 0, 1, 3)
        row += 1
        
        # Hue Tolerance Slider
        self.parent.new3d_hue_tolerance_slider, self.parent.new3d_hue_tolerance_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="Hue Tolerance",
            tooltip_text="How much hue variation to allow around the calibrated color.\n"
                         "Range: 5-45. Default: 15.\n"
                         "Lower = stricter color matching (fewer false positives)\n"
                         "Higher = more permissive (catches more of the ball but may include other objects)\n"
                         "For bright LED balls, 10-20 works well.",
            range_min=5,
            range_max=45,
            initial_value=15,
            update_func=lambda v: self.parent.update_setting('depth_blob_hue_tolerance', v),
            is_float=False
        )
        row += 1
        
        # Saturation Minimum Slider
        self.parent.new3d_sat_minimum_slider, self.parent.new3d_sat_minimum_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="Min Saturation",
            tooltip_text="Minimum color saturation for the color mask.\n"
                         "Range: 20-200. Default: 80.\n"
                         "Higher = only strongly colored pixels (good for LED balls)\n"
                         "Lower = includes washed-out colors (may include skin/background)\n"
                         "LED balls typically have saturation > 100.",
            range_min=20,
            range_max=200,
            initial_value=80,
            update_func=lambda v: self.parent.update_setting('depth_blob_sat_minimum', v),
            is_float=False
        )
        row += 1
        
        # Value/Brightness Minimum Slider
        self.parent.new3d_val_minimum_slider, self.parent.new3d_val_minimum_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="Min Value (Brightness)",
            tooltip_text="Minimum brightness value for the color mask.\n"
                         "Range: 20-200. Default: 80.\n"
                         "Higher = only bright pixels (good for glowing LED balls)\n"
                         "Lower = includes darker areas (may include shadows)\n"
                         "LED balls are typically very bright (value > 100).",
            range_min=20,
            range_max=200,
            initial_value=80,
            update_func=lambda v: self.parent.update_setting('depth_blob_val_minimum', v),
            is_float=False
        )
        row += 1
        
        # Separator for baseline section
        separator = QLabel("─" * 50)
        separator.setStyleSheet("color: #555555;")
        layout.addWidget(separator, row, 0, 1, 3)
        row += 1
        
        # Baseline Exclusion Zones section
        baseline_info = QLabel("🎯 Baseline Exclusion Zones")
        baseline_info.setStyleSheet("color: #4CAF50; font-size: 11px; font-weight: bold;")
        layout.addWidget(baseline_info, row, 0, 1, 3)
        row += 1
        
        baseline_desc = QLabel("Set exclusion zones to ignore false positives around camera edges")
        baseline_desc.setStyleSheet("color: #aaaaaa; font-size: 9px;")
        baseline_desc.setWordWrap(True)
        layout.addWidget(baseline_desc, row, 0, 1, 3)
        row += 1
        
        # Set Baseline button
        self.parent.new3d_set_baseline_button = QPushButton("🎯 Set Baseline (5s)")
        self.parent.new3d_set_baseline_button.setStyleSheet("""
            QPushButton {
                background-color: #FF9800;
                color: white;
                padding: 10px;
                border-radius: 4px;
                font-weight: bold;
            }
            QPushButton:hover { background-color: #F57C00; }
            QPushButton:pressed { background-color: #E65100; }
        """)
        self.parent.new3d_set_baseline_button.clicked.connect(self._start_baseline_recording)
        layout.addWidget(self.parent.new3d_set_baseline_button, row, 0, 1, 2)
        
        # Clear Baseline button
        self.parent.new3d_clear_baseline_button = QPushButton("Clear Zones")
        self.parent.new3d_clear_baseline_button.setStyleSheet("""
            QPushButton {
                background-color: #f44336;
                color: white;
                padding: 10px;
                border-radius: 4px;
                font-weight: bold;
            }
            QPushButton:hover { background-color: #da190b; }
            QPushButton:pressed { background-color: #b71c1c; }
        """)
        self.parent.new3d_clear_baseline_button.clicked.connect(self._clear_baseline_zones)
        layout.addWidget(self.parent.new3d_clear_baseline_button, row, 2)
        row += 1
        
        # Baseline status label
        self.parent.new3d_baseline_status_label = QLabel("")
        self.parent.new3d_baseline_status_label.setStyleSheet("color: #4CAF50; font-size: 10px; font-weight: bold;")
        self.parent.new3d_baseline_status_label.setWordWrap(True)
        self.parent.new3d_baseline_status_label.setVisible(False)
        layout.addWidget(self.parent.new3d_baseline_status_label, row, 0, 1, 3)
        row += 1
        
        # Exclusion zones count label
        self.parent.new3d_exclusion_zones_label = QLabel("Exclusion zones: 0")
        self.parent.new3d_exclusion_zones_label.setStyleSheet("color: #2196F3; font-size: 9px;")
        layout.addWidget(self.parent.new3d_exclusion_zones_label, row, 0, 1, 3)
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
    
    def _toggle_color_filter(self):
        """Toggle color-first detection on/off"""
        is_enabled = self.parent.new3d_color_filter_toggle.isChecked()
        self.parent.new3d_color_filter_toggle.setText(
            "Color-First Detection ON" if is_enabled else "Color-First Detection OFF"
        )
        self.parent.update_setting('depth_blob_color_filter', 1 if is_enabled else 0)
    
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
            
            # Color sample square (on its own row)
            ball_layout.addWidget(QLabel("Color Sample:"), row, 0)
            color_sample = QLabel()
            color_sample.setFixedSize(30, 30)
            if avg_hue >= 0 and avg_sat >= 0:
                # Convert HSV to RGB for display (OpenCV uses H: 0-180, S: 0-255, V: 0-255)
                # Convert to 0-1 range for colorsys
                h_normalized = avg_hue / 180.0
                s_normalized = avg_sat / 255.0
                v_normalized = 0.8  # Use 80% brightness for visibility
                r, g, b = colorsys.hsv_to_rgb(h_normalized, s_normalized, v_normalized)
                color = QColor(int(r * 255), int(g * 255), int(b * 255))
                color_sample.setStyleSheet(f"background-color: {color.name()}; border: 2px solid #555;")
            else:
                color_sample.setStyleSheet("background-color: #2b2b2b; border: 2px solid #555;")
            ball_layout.addWidget(color_sample, row, 1, 1, 2)
            row += 1
            
            # Store label references for updates
            self.parent.new3d_ball_calibration_labels[ball_name] = {
                'hue': hue_value_label,
                'saturation': sat_value_label,
                'color_sample': color_sample
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
            
            # Calibrate button — click-based: hold up ball, click on it in video
            calibrate_button = QPushButton("🎯 Click to Calibrate")
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
            calibrate_button.clicked.connect(lambda checked, name=ball_name: self._start_click_calibration(name))
            ball_layout.addWidget(calibrate_button, row, 0, 1, 3)
            row += 1
            
            # Calibration status label (hidden by default)
            status_label = QLabel("")
            status_label.setStyleSheet("color: #FF9800; font-size: 10px; font-weight: bold;")
            status_label.setWordWrap(True)
            status_label.setVisible(False)
            ball_layout.addWidget(status_label, row, 0, 1, 3)
            
            # Store reference to status label for this ball
            if not hasattr(self.parent, 'new3d_calibration_status_labels'):
                self.parent.new3d_calibration_status_labels = {}
            self.parent.new3d_calibration_status_labels[ball_name] = status_label
            row += 1
            
            # Info label
            info_label = QLabel("ℹ️ Hold up the ball, click 'Calibrate', then click on the ball in the video")
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
    
    def _start_click_calibration(self, ball_name: str):
        """Start click-based calibration: user clicks on ball in video to sample its color."""
        print(f"🎯 Click calibration: waiting for click on {ball_name} ball in video")
        
        # Set the pending calibration flag on the parent widget
        # The video_view_clicked handler in ui.py will check this flag
        self.parent.pending_click_calibration = ball_name
        
        # Show status label
        if hasattr(self.parent, 'new3d_calibration_status_labels'):
            if ball_name in self.parent.new3d_calibration_status_labels:
                label = self.parent.new3d_calibration_status_labels[ball_name]
                label.setText(f"👆 Now click on the {ball_name} ball in the video...")
                label.setStyleSheet("color: #FF9800; font-size: 10px; font-weight: bold;")
                label.setVisible(True)
    
    def complete_click_calibration(self, ball_name: str, avg_hue: float, avg_sat: float):
        """Complete click-based calibration with sampled HSV values."""
        print(f"🎉 Click calibration complete for '{ball_name}': H={avg_hue:.1f}° S={avg_sat:.1f}")
        
        # Clear pending flag
        self.parent.pending_click_calibration = None
        
        # Save to calibration_settings_new3d.json
        import json
        import os
        settings_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "calibration_settings_new3d.json")
        settings_path = os.path.normpath(settings_path)
        
        try:
            with open(settings_path, 'r') as f:
                settings = json.load(f)
            
            # Update the color profile
            if 'color_profiles' in settings:
                for profile in settings['color_profiles']:
                    if profile['name'] == ball_name:
                        profile['avg_hue'] = float(avg_hue)
                        profile['avg_saturation'] = float(avg_sat)
                        print(f"💾 Updated {ball_name}: H={avg_hue:.1f}, S={avg_sat:.1f}")
                        break
            
            with open(settings_path, 'w') as f:
                json.dump(settings, f, indent=4)
            
            print(f"💾 Saved calibration to {settings_path}")
            
            # Update UI labels
            if hasattr(self.parent, 'new3d_ball_calibration_labels'):
                if ball_name in self.parent.new3d_ball_calibration_labels:
                    labels = self.parent.new3d_ball_calibration_labels[ball_name]
                    labels['hue'].setText(f"{avg_hue:.1f}°")
                    labels['hue'].setStyleSheet("color: #4CAF50; font-weight: bold;")
                    labels['saturation'].setText(f"{avg_sat:.1f}")
                    labels['saturation'].setStyleSheet("color: #4CAF50; font-weight: bold;")
                    
                    # Update color sample square
                    if 'color_sample' in labels:
                        h_normalized = avg_hue / 180.0
                        s_normalized = avg_sat / 255.0
                        v_normalized = 0.8
                        import colorsys
                        r, g, b = colorsys.hsv_to_rgb(h_normalized, s_normalized, v_normalized)
                        from PyQt6.QtGui import QColor
                        color = QColor(int(r * 255), int(g * 255), int(b * 255))
                        labels['color_sample'].setStyleSheet(f"background-color: {color.name()}; border: 2px solid #555;")
            
            # Show success in status label
            if hasattr(self.parent, 'new3d_calibration_status_labels'):
                if ball_name in self.parent.new3d_calibration_status_labels:
                    label = self.parent.new3d_calibration_status_labels[ball_name]
                    label.setText(f"✅ Calibrated! H={avg_hue:.1f}° S={avg_sat:.1f}")
                    label.setStyleSheet("color: #4CAF50; font-size: 10px; font-weight: bold;")
                    from PyQt6.QtCore import QTimer
                    QTimer.singleShot(5000, lambda: label.setVisible(False))
            
            # Tell engine to reload color profiles
            if self.zmq_client:
                try:
                    command = juggler_pb2.CommandRequest()
                    command.type = juggler_pb2.CommandRequest.CommandType.RELOAD_COLOR_PROFILES
                    response = self.zmq_client.send_command(command)
                    print(f"📤 Reload color profiles: {response.message}")
                except Exception as e:
                    print(f"⚠️ Could not reload engine profiles: {e}")
            
        except Exception as e:
            print(f"❌ Error saving click calibration: {e}")
            if hasattr(self.parent, 'new3d_calibration_status_labels'):
                if ball_name in self.parent.new3d_calibration_status_labels:
                    label = self.parent.new3d_calibration_status_labels[ball_name]
                    label.setText(f"❌ Error: {e}")
                    label.setStyleSheet("color: #f44336; font-size: 10px; font-weight: bold;")
    
    def cancel_click_calibration(self):
        """Cancel pending click calibration."""
        if hasattr(self.parent, 'pending_click_calibration') and self.parent.pending_click_calibration:
            ball_name = self.parent.pending_click_calibration
            self.parent.pending_click_calibration = None
            if hasattr(self.parent, 'new3d_calibration_status_labels'):
                if ball_name in self.parent.new3d_calibration_status_labels:
                    self.parent.new3d_calibration_status_labels[ball_name].setVisible(False)
    
    def _start_time_based_calibration(self, ball_name: str):
        """Start time-based calibration for a specific ball (legacy)"""
        print(f"🎨 Starting time-based calibration for {ball_name}")
        
        # Check if calibration is already active
        if self.calibration_system.is_active():
            print(f"⚠️ Calibration already in progress")
            return
        
        # Start calibration
        if self.calibration_system.start_calibration(ball_name):
            # Show status label
            if hasattr(self.parent, 'new3d_calibration_status_labels'):
                if ball_name in self.parent.new3d_calibration_status_labels:
                    self.parent.new3d_calibration_status_labels[ball_name].setVisible(True)
    
    def _on_calibration_state_changed(self, state: str, message: str):
        """Handle calibration state changes"""
        print(f"📊 Calibration state: {state} - {message}")
        
        # Update status label for current color
        if self.calibration_system.color_name and hasattr(self.parent, 'new3d_calibration_status_labels'):
            color_name = self.calibration_system.color_name
            if color_name in self.parent.new3d_calibration_status_labels:
                label = self.parent.new3d_calibration_status_labels[color_name]
                label.setText(message)
                label.setVisible(True)
                
                # Set color based on state
                if state == "preparation":
                    label.setStyleSheet("color: #FF9800; font-size: 10px; font-weight: bold;")
                elif state == "recording":
                    label.setStyleSheet("color: #f44336; font-size: 10px; font-weight: bold;")
                elif state == "processing":
                    label.setStyleSheet("color: #2196F3; font-size: 10px; font-weight: bold;")
                elif state == "complete":
                    label.setStyleSheet("color: #4CAF50; font-size: 10px; font-weight: bold;")
                elif state == "error":
                    label.setStyleSheet("color: #f44336; font-size: 10px; font-weight: bold;")
    
    def _on_calibration_complete(self, color_name: str, avg_hue: float, avg_sat: float, sample_count: int):
        """Handle calibration completion"""
        print(f"🎉 Calibration complete for '{color_name}': H={avg_hue:.1f}° S={avg_sat:.1f} ({sample_count} samples)")
        
        # Update UI labels
        if hasattr(self.parent, 'new3d_ball_calibration_labels'):
            if color_name in self.parent.new3d_ball_calibration_labels:
                labels = self.parent.new3d_ball_calibration_labels[color_name]
                
                # Update hue label
                labels['hue'].setText(f"{avg_hue:.1f}°")
                labels['hue'].setStyleSheet("color: #4CAF50; font-weight: bold;")
                
                # Update saturation label
                labels['saturation'].setText(f"{avg_sat:.1f}")
                labels['saturation'].setStyleSheet("color: #4CAF50; font-weight: bold;")
                
                # Update color sample square
                if 'color_sample' in labels:
                    h_normalized = avg_hue / 180.0
                    s_normalized = avg_sat / 255.0
                    v_normalized = 0.8
                    r, g, b = colorsys.hsv_to_rgb(h_normalized, s_normalized, v_normalized)
                    color = QColor(int(r * 255), int(g * 255), int(b * 255))
                    labels['color_sample'].setStyleSheet(f"background-color: {color.name()}; border: 2px solid #555;")
        
        # Send reload command to engine
        if self.zmq_client:
            print(f"🔄 Sending RELOAD_COLOR_PROFILES command to engine...")
            try:
                import juggler_pb2
                command = juggler_pb2.CommandRequest()
                command.type = juggler_pb2.CommandRequest.RELOAD_COLOR_PROFILES
                
                response = self.zmq_client.send_command(command)
                
                if response and response.success:
                    print(f"✅ Engine reloaded color profiles successfully!")
                else:
                    error_msg = response.message if response else "No response"
                    print(f"⚠️ Engine reload failed: {error_msg}")
            except Exception as e:
                print(f"❌ Error sending reload command: {e}")
        
        # Hide status label after 3 seconds
        if hasattr(self.parent, 'new3d_calibration_status_labels'):
            if color_name in self.parent.new3d_calibration_status_labels:
                from PyQt6.QtCore import QTimer
                QTimer.singleShot(3000, lambda: self.parent.new3d_calibration_status_labels[color_name].setVisible(False))
    
    def _on_calibration_error(self, error_message: str):
        """Handle calibration errors"""
        print(f"❌ Calibration error: {error_message}")
        
        # Hide status label after 3 seconds
        if self.calibration_system.color_name and hasattr(self.parent, 'new3d_calibration_status_labels'):
            color_name = self.calibration_system.color_name
            if color_name in self.parent.new3d_calibration_status_labels:
                from PyQt6.QtCore import QTimer
                QTimer.singleShot(3000, lambda: self.parent.new3d_calibration_status_labels[color_name].setVisible(False))
    
    def collect_depth_blob_colors(self, depth_blobs):
        """
        Collect color samples from depth blobs during calibration.
        
        This should be called from the main UI when depth blob data is received.
        
        Args:
            depth_blobs: List of depth blob detections with color information
        """
        if not self.calibration_system.is_recording():
            return
        
        # Extract color samples from depth blobs
        for blob in depth_blobs:
            if hasattr(blob, 'avg_hue') and hasattr(blob, 'avg_saturation'):
                # Add sample if it passes whiteness filter (already applied in engine)
                self.calibration_system.add_color_sample(blob.avg_hue, blob.avg_saturation)
    
    def _start_baseline_recording(self):
        """Start recording baseline exclusion zones for 5 seconds"""
        import time
        from PyQt6.QtCore import QTimer
        
        print("🎯 Starting baseline recording for 5 seconds...")
        
        # Reset baseline data
        self.baseline_detections = []
        self.baseline_recording = True
        self.baseline_start_time = time.time()
        
        # Update UI
        if hasattr(self.parent, 'new3d_baseline_status_label'):
            self.parent.new3d_baseline_status_label.setText("⏱️ Recording baseline... (5s)")
            self.parent.new3d_baseline_status_label.setStyleSheet("color: #f44336; font-size: 10px; font-weight: bold;")
            self.parent.new3d_baseline_status_label.setVisible(True)
        
        if hasattr(self.parent, 'new3d_set_baseline_button'):
            self.parent.new3d_set_baseline_button.setEnabled(False)
        
        # Set timer to stop recording after 5 seconds
        QTimer.singleShot(5000, self._finish_baseline_recording)
    
    def _finish_baseline_recording(self):
        """Finish baseline recording and create exclusion zones"""
        import time
        
        self.baseline_recording = False
        elapsed = time.time() - self.baseline_start_time
        
        print(f"✅ Baseline recording complete! Collected {len(self.baseline_detections)} detections in {elapsed:.1f}s")
        
        # Create exclusion zones from collected detections
        self._create_exclusion_zones()
        
        # Update UI
        if hasattr(self.parent, 'new3d_baseline_status_label'):
            self.parent.new3d_baseline_status_label.setText(f"✅ Baseline set! Created {len(self.exclusion_zones)} exclusion zones")
            self.parent.new3d_baseline_status_label.setStyleSheet("color: #4CAF50; font-size: 10px; font-weight: bold;")
            
            # Hide status after 3 seconds
            from PyQt6.QtCore import QTimer
            QTimer.singleShot(3000, lambda: self.parent.new3d_baseline_status_label.setVisible(False))
        
        if hasattr(self.parent, 'new3d_set_baseline_button'):
            self.parent.new3d_set_baseline_button.setEnabled(True)
        
        if hasattr(self.parent, 'new3d_exclusion_zones_label'):
            self.parent.new3d_exclusion_zones_label.setText(f"Exclusion zones: {len(self.exclusion_zones)}")
    
    def _create_exclusion_zones(self):
        """Create exclusion zones from baseline detections with small padding"""
        self.exclusion_zones = []
        
        # Add small padding around each detection (e.g., 10 pixels)
        padding = 10
        
        for detection in self.baseline_detections:
            x, y, w, h = detection
            zone = {
                'x': max(0, x - padding),
                'y': max(0, y - padding),
                'width': w + 2 * padding,
                'height': h + 2 * padding
            }
            self.exclusion_zones.append(zone)
        
        print(f"📦 Created {len(self.exclusion_zones)} exclusion zones:")
        for i, zone in enumerate(self.exclusion_zones):
            print(f"   Zone {i+1}: x={zone['x']:.0f}, y={zone['y']:.0f}, w={zone['width']:.0f}, h={zone['height']:.0f}")
        
        # Send exclusion zones to engine
        self._send_exclusion_zones_to_engine()
    
    def _send_exclusion_zones_to_engine(self):
        """Send exclusion zones to the engine via ZMQ command"""
        if not self.zmq_client:
            print("⚠️ ZMQ client not available - exclusion zones not sent to engine")
            return
        
        try:
            import juggler_pb2
            
            # Create command with exclusion zones
            command = juggler_pb2.CommandRequest()
            command.type = juggler_pb2.CommandRequest.CommandType.SET_EXCLUSION_ZONES
            
            # Add each exclusion zone to the command
            for zone in self.exclusion_zones:
                zone_msg = command.exclusion_zones.add()
                zone_msg.x = int(zone['x'])
                zone_msg.y = int(zone['y'])
                zone_msg.width = int(zone['width'])
                zone_msg.height = int(zone['height'])
            
            print(f"📤 Sending {len(self.exclusion_zones)} exclusion zones to engine...")
            response = self.zmq_client.send_command(command)
            
            if response and response.success:
                print(f"✅ Engine received exclusion zones successfully!")
            else:
                error_msg = response.message if response else "No response"
                print(f"⚠️ Engine failed to set exclusion zones: {error_msg}")
        except Exception as e:
            print(f"❌ Error sending exclusion zones to engine: {e}")
            import traceback
            print(traceback.format_exc())
    
    def _clear_baseline_zones(self):
        """Clear all exclusion zones"""
        print("🗑️ Clearing all exclusion zones...")
        
        self.baseline_detections = []
        self.exclusion_zones = []
        
        # Update UI
        if hasattr(self.parent, 'new3d_exclusion_zones_label'):
            self.parent.new3d_exclusion_zones_label.setText("Exclusion zones: 0")
        
        if hasattr(self.parent, 'new3d_baseline_status_label'):
            self.parent.new3d_baseline_status_label.setText("✅ Exclusion zones cleared")
            self.parent.new3d_baseline_status_label.setStyleSheet("color: #4CAF50; font-size: 10px; font-weight: bold;")
            self.parent.new3d_baseline_status_label.setVisible(True)
            
            # Hide status after 2 seconds
            from PyQt6.QtCore import QTimer
            QTimer.singleShot(2000, lambda: self.parent.new3d_baseline_status_label.setVisible(False))
        
        # Send empty exclusion zones to engine
        self._send_exclusion_zones_to_engine()
        
        print("✅ Exclusion zones cleared")
    
    def collect_baseline_detections(self, frame_data):
        """
        Collect depth blob detections during baseline recording.
        This should be called from the main UI when frame data is received.
        
        Args:
            frame_data: FrameData protobuf message containing raw_detections
        """
        if not self.baseline_recording:
            return
        
        # Collect all raw detections (depth blobs when depth blob detection is enabled)
        if hasattr(frame_data, 'raw_detections'):
            for detection in frame_data.raw_detections:
                # Store detection bounding box
                self.baseline_detections.append((
                    detection.x,
                    detection.y,
                    detection.width,
                    detection.height
                ))
            
            if len(frame_data.raw_detections) > 0:
                print(f"📦 Collected {len(frame_data.raw_detections)} detections (total: {len(self.baseline_detections)})")
    
    def is_baseline_recording(self):
        """Check if baseline recording is active"""
        return self.baseline_recording
    
    def get_exclusion_zones(self):
        """Get the current exclusion zones for visualization"""
        return self.exclusion_zones