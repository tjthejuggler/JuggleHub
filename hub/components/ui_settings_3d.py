
"""
3D Tracker Settings Sections for JuggleHub UI.
Contains settings sections that are ONLY visible when 3D tracker is selected.
"""

from PyQt6.QtWidgets import (QLabel, QPushButton, QGridLayout, QVBoxLayout, 
                              QGroupBox, QHBoxLayout)
from .ui_widgets import CollapsibleGroupBox
import juggler_pb2


class Tracker3DSettingsSections:
    """3D tracker-specific settings sections."""
    
    def __init__(self, parent_widget, udp_client, zmq_client):
        """
        Initialize 3D tracker settings sections.
        
        Args:
            parent_widget: Parent CalibrationSettingsWidget instance
            udp_client: UDP client for sending settings to engine
            zmq_client: ZMQ client for sending commands to engine
        """
        self.parent = parent_widget
        self.udp_client = udp_client
        self.zmq_client = zmq_client
    
    def create_ball_state_section(self):
        """Create the Ball State Detection settings section (3D only)"""
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
        
        # Separator
        layout.addWidget(QLabel("Distance Thresholds:"), row, 0, 1, 3)
        row += 1
        
        # Undetected near hand threshold (for occluded balls)
        self.parent.tc_undetected_near_hand_slider, self.parent.tc_undetected_near_hand_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="Undetected Near Hand (cm)",
            tooltip_text="Distance from hand where undetected ball is considered held (occluded).\n"
                         "Range: 10-40cm. Default: 20cm.\n"
                         "Larger threshold accounts for balls hidden by hands.",
            range_min=10,
            range_max=40,
            initial_value=20,
            update_func=lambda v: self.parent.update_setting('undetected_near_hand_threshold', v / 100.0),
            is_float=False
        )
        row += 1
        
        # Separator
        layout.addWidget(QLabel("State Change:"), row, 0, 1, 3)
        row += 1
        
        # State change debouncing
        self.parent.tc_min_frames_slider, self.parent.tc_min_frames_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="State Change Frames",
            tooltip_text="Number of consecutive frames required to confirm state change (held/in-air).\n"
                         "Range: 1-50 frames. Default: 3.\n"
                         "Higher values = more stable detection, slower response.",
            range_min=1,
            range_max=50,
            initial_value=3,
            update_func=lambda v: self.parent.update_setting('min_frames_for_state_change', v),
            is_float=False
        )
        row += 1

        # Unified Hand Distance Threshold
        self.parent.tc_hand_distance_threshold_slider, self.parent.tc_hand_distance_threshold_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="hand_distance_threshold (cm)",
            tooltip_text="Unified distance threshold for hand proximity detection.\n"
                         "Range: 5-50 cm. Default: 25 cm.\n"
                         "Used for both throw detection (ball leaving hand) and catch detection (ball reaching hand).\n"
                         "Lower = more sensitive (detects events earlier/closer)\n"
                         "Higher = less sensitive (requires ball to be farther/closer)\n"
                         "⚠️ This is the unified threshold you see in debug logs!",
            range_min=5,
            range_max=50,
            initial_value=25,
            update_func=lambda v: self.parent.update_setting('hand_distance_threshold', v / 100.0),
            is_float=False
        )
        row += 1
        
        # Min Frames Before Catch
        self.parent.tc_min_frames_before_catch_slider, self.parent.tc_min_frames_before_catch_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="Catch Cooldown Frames",
            tooltip_text="Minimum frames a ball must be in flight after a throw before the same hand can catch it again.\n"
                         "Range: 0-10 frames. Default: 3.\n"
                         "Prevents immediate re-catch of the same ball by the throwing hand.\n"
                         "0 = no cooldown (allows instant re-catch)\n"
                         "Higher values = longer cooldown period\n"
                         "⚠️ Set this based on your juggling speed and throw height!",
            range_min=0,
            range_max=10,
            initial_value=3,
            update_func=lambda v: self.parent.update_setting('min_frames_before_catch', v),
            is_float=False
        )
        row += 1

        # Min Throw Distance (LEGACY)
        self.parent.tc_min_throw_distance_slider, self.parent.tc_min_throw_distance_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="Min Throw Distance (cm) [LEGACY]",
            tooltip_text="LEGACY SETTING - Use 'Throw Distance Threshold' above instead.\n"
                         "Minimum distance ball must move from wrist to count as a throw/catch.\n"
                         "Range: 5-50 cm. Default: 20 cm.\n"
                         "Prevents false throw/catch events when ball is just being held.\n"
                         "Lower = more sensitive (may trigger false events)\n"
                         "Higher = less sensitive (may miss real throws)\n"
                         "⚠️ Set this based on your juggling style and hand movements!",
            range_min=5,
            range_max=50,
            initial_value=20,
            update_func=lambda v: self.parent.update_setting('min_throw_distance', v / 100.0),
            is_float=False
        )
        row += 1
        
        # Throw YOLO Confidence Threshold
        self.parent.throw_yolo_confidence_threshold_slider, self.parent.throw_yolo_confidence_threshold_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="Throw Detection Confidence",
            tooltip_text="Minimum YOLO confidence for throw detection (HELD→IN_FLIGHT transition).\n"
                         "Range: 0.0-1.0. Default: 0.50 (50%).\n"
                         "This is separate from trajectory tracking confidence.\n"
                         "Lower = more sensitive throw detection (catches throws earlier)\n"
                         "Higher = stricter requirements (may miss some throws)\n"
                         "⚠️ Should be LOWER than trajectory tracking confidence (70%)!",
            range_min=0,
            range_max=100,
            initial_value=50,
            update_func=lambda v: self.parent.update_setting('throw_yolo_confidence_threshold', v / 100.0),
            is_float=True
        )
        row += 1
        
        # Ignore Class Toggle
        self.parent.tc_ignore_class_toggle = QPushButton("Ignore Class (Treat ball/ball_held Same)")
        self.parent.tc_ignore_class_toggle.setCheckable(True)
        self.parent.tc_ignore_class_toggle.setChecked(False)
        self.parent.tc_ignore_class_toggle.clicked.connect(
            lambda: self.parent.update_setting('ignore_class',
                                           1 if self.parent.tc_ignore_class_toggle.isChecked() else 0))
        self.parent.tc_ignore_class_toggle.setToolTip(
            "When enabled, the tracking system ignores ML class distinctions.\n"
            "Both 'ball' and 'ball_held' classes are treated identically.\n"
            "This means:\n"
            "• No class-based filtering in detection matching\n"
            "• No class-based threshold differences\n"
            "• State determined purely by distance to hands\n"
            "Use this if YOLO class predictions are unreliable."
        )
        layout.addWidget(self.parent.tc_ignore_class_toggle, row, 0, 1, 3)
        row += 1
        
        # Separator
        layout.addWidget(QLabel("Tracker Distance Limits:"), row, 0, 1, 3)
        row += 1
        
        # Max tracker distance per frame
        self.parent.tc_max_tracker_distance_slider, self.parent.tc_max_tracker_distance_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="Max Tracker Distance (cm)",
            tooltip_text="Maximum distance a ball tracker can move between frames.\n"
                         "Range: 10-200cm. Default: 50cm.\n"
                         "Prevents trackers from flickering to far away balls.\n"
                         "Lower = stricter tracking, Higher = allows faster movement.",
            range_min=10,
            range_max=200,
            initial_value=50,
            update_func=lambda v: self.parent.update_setting('max_tracker_distance_per_frame', v / 100.0),
            is_float=False
        )
        row += 1
        
        # Separator
        layout.addWidget(QLabel("Sound Effects:"), row, 0, 1, 3)
        row += 1
        
        # Sound on catches toggle with test button
        self.parent.tc_sound_on_catch_toggle = QPushButton("Sound on Catches")
        self.parent.tc_sound_on_catch_toggle.setCheckable(True)
        self.parent.tc_sound_on_catch_toggle.setChecked(False)
        self.parent.tc_sound_on_catch_toggle.clicked.connect(lambda: self.parent.update_setting('tc_sound_on_catch', 1 if self.parent.tc_sound_on_catch_toggle.isChecked() else 0))
        layout.addWidget(self.parent.tc_sound_on_catch_toggle, row, 0, 1, 2)
        
        # Test catch sound button
        self.parent.tc_test_catch_sound_button = QPushButton("🔊 Test")
        self.parent.tc_test_catch_sound_button.setMaximumWidth(80)
        self.parent.tc_test_catch_sound_button.clicked.connect(self.parent.test_catch_sound)
        self.parent.tc_test_catch_sound_button.setStyleSheet("""
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
        layout.addWidget(self.parent.tc_test_catch_sound_button, row, 2)
        row += 1
        
        # Sound on throws toggle with test button
        self.parent.tc_sound_on_throw_toggle = QPushButton("Sound on Throws")
        self.parent.tc_sound_on_throw_toggle.setCheckable(True)
        self.parent.tc_sound_on_throw_toggle.setChecked(False)
        self.parent.tc_sound_on_throw_toggle.clicked.connect(lambda: self.parent.update_setting('tc_sound_on_throw', 1 if self.parent.tc_sound_on_throw_toggle.isChecked() else 0))
        layout.addWidget(self.parent.tc_sound_on_throw_toggle, row, 0, 1, 2)
        
        # Test throw sound button
        self.parent.tc_test_throw_sound_button = QPushButton("🔊 Test")
        self.parent.tc_test_throw_sound_button.setMaximumWidth(80)
        self.parent.tc_test_throw_sound_button.clicked.connect(self.parent.test_throw_sound)
        self.parent.tc_test_throw_sound_button.setStyleSheet("""
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
        layout.addWidget(self.parent.tc_test_throw_sound_button, row, 2)
        row += 1
        
        # Name on catches toggle with test button
        self.parent.tc_name_on_catch_toggle = QPushButton("Name on Catches")
        self.parent.tc_name_on_catch_toggle.setCheckable(True)
        self.parent.tc_name_on_catch_toggle.setChecked(False)
        self.parent.tc_name_on_catch_toggle.clicked.connect(lambda: self.parent.update_setting('tc_name_on_catch', 1 if self.parent.tc_name_on_catch_toggle.isChecked() else 0))
        layout.addWidget(self.parent.tc_name_on_catch_toggle, row, 0, 1, 2)
        
        # Test catch name button
        self.parent.tc_test_catch_name_button = QPushButton("🔊 Test")
        self.parent.tc_test_catch_name_button.setMaximumWidth(80)
        self.parent.tc_test_catch_name_button.clicked.connect(self.parent.test_catch_name)
        self.parent.tc_test_catch_name_button.setStyleSheet("""
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
        layout.addWidget(self.parent.tc_test_catch_name_button, row, 2)
        row += 1
        
        # Name on throws toggle with test button
        self.parent.tc_name_on_throw_toggle = QPushButton("Name on Throws")
        self.parent.tc_name_on_throw_toggle.setCheckable(True)
        self.parent.tc_name_on_throw_toggle.setChecked(False)
        self.parent.tc_name_on_throw_toggle.clicked.connect(lambda: self.parent.update_setting('tc_name_on_throw', 1 if self.parent.tc_name_on_throw_toggle.isChecked() else 0))
        layout.addWidget(self.parent.tc_name_on_throw_toggle, row, 0, 1, 2)
        
        # Test throw name button
        self.parent.tc_test_throw_name_button = QPushButton("🔊 Test")
        self.parent.tc_test_throw_name_button.setMaximumWidth(80)
        self.parent.tc_test_throw_name_button.clicked.connect(self.parent.test_throw_name)
        self.parent.tc_test_throw_name_button.setStyleSheet("""
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
        layout.addWidget(self.parent.tc_test_throw_name_button, row, 2)
        
        return section
    
    def create_color_tracker_section(self):
        """Create the Color Tracker Weights section (3D only)"""
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
        
        # Separator for euclidean matching settings
        layout.addWidget(QLabel("Temporal Consistency (prevents ball identity swaps):"), row, 0, 1, 3)
        row += 1
        
        # Temporal Consistency Bonus
        self.parent.ct_temporal_consistency_bonus_slider, self.parent.ct_temporal_consistency_bonus_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="Temporal Consistency Bonus",
            tooltip_text="Bonus to reduce effective distance for detections near previous position.\n"
                         "Range: 0.0-1.0. Default: 0.25.\n"
                         "Higher values create stronger 'stickiness' to prevent identity swaps.\n"
                         "⚠️ Increase to 0.40-0.50 to fix the yellow ball tracking issue!",
            range_min=0,
            range_max=100,
            initial_value=25,
            update_func=lambda v: self.parent.update_setting('temporal_consistency_bonus', v / 100.0),
            is_float=True
        )
        row += 1
        
        # Spatial Threshold
        self.parent.ct_spatial_threshold_slider, self.parent.ct_spatial_threshold_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="Spatial Threshold (cm)",
            tooltip_text="Maximum distance to apply temporal consistency bonus.\n"
                         "Range: 10-100cm. Default: 40cm.\n"
                         "Larger values apply the bonus over greater distances.\n"
                         "⚠️ Increase to 60-70cm to fix the yellow ball tracking issue!",
            range_min=10,
            range_max=100,
            initial_value=40,
            update_func=lambda v: self.parent.update_setting('spatial_threshold', v / 100.0),
            is_float=False
        )
        row += 1
        
        # Color Sample Radius
        self.parent.ct_color_sample_radius_slider, self.parent.ct_color_sample_radius_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="Color Sample Radius (pixels)",
            tooltip_text="Radius for color sampling from detection center in the euclidean color matching system.\n"
                         "Range: 1-5 pixels. Default: 1 (3x3 sample).\n"
                         "Radius of 1 = 3x3 pixel sample (9 pixels total)\n"
                         "Radius of 2 = 5x5 pixel sample (25 pixels total)\n"
                         "Radius of 3 = 7x7 pixel sample (49 pixels total)\n"
                         "Lower values = faster processing and more precise color detection from exact center.\n"
                         "Higher values = more robust to noise but slower and may include surrounding colors.\n"
                         "⚠️ Increasing this will reduce FPS! Only increase if color detection is unreliable.",
            range_min=1,
            range_max=5,
            initial_value=1,
            update_func=lambda v: self.parent.update_setting('color_sample_radius', v),
            is_float=False
        )
        row += 1
        
        # Separator for identity swap prevention settings
        layout.addWidget(QLabel("Identity Swap Prevention:"), row, 0, 1, 3)
        row += 1
        
        # Max Euclidean Distance
        self.parent.ct_max_euclidean_distance_slider, self.parent.ct_max_euclidean_distance_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="Max Euclidean Distance",
            tooltip_text="Maximum euclidean color distance to accept a match.\n"
                         "Range: 0.00-0.50. Default: 0.15.\n"
                         "Rejects matches with poor color similarity.\n"
                         "Lower = stricter color matching (prevents identity swaps).\n"
                         "Set to 0 to disable this check.\n"
                         "⚠️ Increase to 0.15-0.20 to prevent trackers from swapping identities!",
            range_min=0,
            range_max=50,
            initial_value=15,
            update_func=lambda v: self.parent.update_setting('max_euclidean_distance', v / 100.0),
            is_float=True
        )
        row += 1
        
        # Min Euclidean Color Score
        self.parent.ct_min_euclidean_color_score_slider, self.parent.ct_min_euclidean_color_score_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="Min Euclidean Color Score",
            tooltip_text="Minimum color match score to accept a euclidean match.\n"
                         "Range: 0.00-1.00. Default: 0.30.\n"
                         "Requires at least 30% color similarity.\n"
                         "Higher = stricter color matching (prevents identity swaps).\n"
                         "Set to 0 to disable this check.\n"
                         "⚠️ Increase to 0.30-0.40 to prevent trackers from swapping identities!",
            range_min=0,
            range_max=100,
            initial_value=30,
            update_func=lambda v: self.parent.update_setting('min_euclidean_color_score', v / 100.0),
            is_float=True
        )
        row += 1
        
        # Max Depth Jump Strict
        self.parent.ct_max_depth_jump_strict_slider, self.parent.ct_max_depth_jump_strict_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="Max Depth Jump Strict (cm)",
            tooltip_text="Stricter maximum depth change per frame for Kalman updates.\n"
                         "Range: 0-50cm. Default: 20cm.\n"
                         "Rejects detections with suspicious depth jumps.\n"
                         "Prevents sensor errors from corrupting Kalman filter.\n"
                         "Set to 0 to use default 30cm threshold.\n"
                         "⚠️ Set to 20cm for stricter depth validation!",
            range_min=0,
            range_max=50,
            initial_value=20,
            update_func=lambda v: self.parent.update_setting('max_depth_jump_strict', v / 100.0),
            is_float=False
        )
        row += 1
        
        # Separator for ball separation and hand change settings
        layout.addWidget(QLabel("Ball Separation & Hand Change:"), row, 0, 1, 3)
        row += 1
        
        # Min Color Confidence Override
        self.parent.ct_min_color_confidence_override_slider, self.parent.ct_min_color_confidence_override_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="Min Color Confidence Override",
            tooltip_text="Minimum color match confidence required for override (0.0-1.0).\n"
                         "Range: 0.0-1.0. Default: 0.35.\n"
                         "Higher values require stronger color match to override tracker.\n"
                         "Lower values allow weaker color matches to override.",
            range_min=0,
            range_max=100,
            initial_value=35,
            update_func=lambda v: self.parent.update_setting('min_color_confidence_override', v / 100.0),
            is_float=True
        )
        row += 1
        
        # Min Ball Separation
        self.parent.ct_min_ball_separation_slider, self.parent.ct_min_ball_separation_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="Min Ball Separation (cm)",
            tooltip_text="Minimum separation between balls in meters (except same hand).\n"
                         "Range: 5-50cm. Default: 15cm.\n"
                         "Prevents balls from being placed too close together.\n"
                         "Lower values allow balls to be closer (may cause confusion).\n"
                         "Higher values enforce more separation (more conservative).",
            range_min=5,
            range_max=50,
            initial_value=15,
            update_func=lambda v: self.parent.update_setting('min_ball_separation', v / 100.0),
            is_float=False
        )
        row += 1
        
        # Min Hand Change Distance
        self.parent.ct_min_hand_change_distance_slider, self.parent.ct_min_hand_change_distance_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="Min Hand Change Distance (cm)",
            tooltip_text="Minimum movement distance for hand change detection (meters).\n"
                         "Range: 10-50cm. Default: 25cm.\n"
                         "Distance a ball must move to be considered as changing hands.\n"
                         "Lower values detect hand changes more easily.\n"
                         "Higher values require more movement to confirm hand change.",
            range_min=10,
            range_max=50,
            initial_value=25,
            update_func=lambda v: self.parent.update_setting('min_hand_change_distance', v / 100.0),
            is_float=False
        )
        row += 1
        
        return section
    
    def create_override_detection_section(self):
        """Create the Override Detection Settings section (3D only)"""
        section = CollapsibleGroupBox("⚡ Override Detection", collapsed=False)
        layout = QGridLayout()
        section.get_content_layout().addLayout(layout)
        
        row = 0
        
        # Info label
        info_label = QLabel("ℹ️ Force tracker placement when high-confidence detections exist\n"
                           "Separate thresholds for 'ball' (in-air) and 'ball_held' detections")
        info_label.setStyleSheet("color: #aaaaaa; font-size: 10px;")
        info_label.setWordWrap(True)
        layout.addWidget(info_label, row, 0, 1, 3)
        row += 1
        
        # Separator for 'ball' (in-air) class
        separator_label = QLabel("'Ball' (In-Air) Override Thresholds:")
        separator_label.setStyleSheet("font-weight: bold; color: #4CAF50;")
        layout.addWidget(separator_label, row, 0, 1, 3)
        row += 1
        
        # Ball confidence threshold
        self.parent.od_ball_confidence_slider, self.parent.od_ball_confidence_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="'Ball' Min Confidence",
            tooltip_text="Minimum YOLO confidence for 'ball' (in-air) detections to override tracker.\n"
                         "Range: 0.00-1.00. Default: 0.70.\n"
                         "Lower values = more aggressive override for in-air balls.\n"
                         "Use lower threshold if in-air balls are not being tracked reliably.",
            range_min=0,
            range_max=100,
            initial_value=70,
            update_func=lambda v: self.parent.update_setting('override_ball_confidence_threshold', v / 100.0),
            is_float=True
        )
        row += 1
        
        # Ball color threshold
        self.parent.od_ball_color_slider, self.parent.od_ball_color_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="'Ball' Min Color Score",
            tooltip_text="Minimum color match score for 'ball' (in-air) detections to override tracker.\n"
                         "Range: 0.00-1.00. Default: 0.80.\n"
                         "Lower values = more lenient color matching for in-air balls.\n"
                         "Combine with confidence for robust in-air detection.",
            range_min=0,
            range_max=100,
            initial_value=80,
            update_func=lambda v: self.parent.update_setting('override_ball_color_threshold', v / 100.0),
            is_float=True
        )
        row += 1
        
        # Separator for 'ball_held' class
        separator_label2 = QLabel("'Ball Held' Override Thresholds:")
        separator_label2.setStyleSheet("font-weight: bold; color: #FF9800;")
        layout.addWidget(separator_label2, row, 0, 1, 3)
        row += 1
        
        # Ball_held confidence threshold
        self.parent.od_ball_held_confidence_slider, self.parent.od_ball_held_confidence_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="'Ball Held' Min Confidence",
            tooltip_text="Minimum YOLO confidence for 'ball_held' detections to override tracker.\n"
                         "Range: 0.00-1.00. Default: 0.70.\n"
                         "Higher values = stricter override for held balls (reduces false positives).\n"
                         "Use higher threshold to avoid tracking hands/clothing as held balls.",
            range_min=0,
            range_max=100,
            initial_value=70,
            update_func=lambda v: self.parent.update_setting('override_ball_held_confidence_threshold', v / 100.0),
            is_float=True
        )
        row += 1

        
        # Ball_held color threshold
        self.parent.od_ball_held_color_slider, self.parent.od_ball_held_color_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="'Ball Held' Min Color Score",
            tooltip_text="Minimum color match score for 'ball_held' detections to override tracker.\n"
                         "Range: 0.00-1.00. Default: 0.80.\n"
                         "Higher values = stricter color matching for held balls.\n"
                         "Prevents tracking wrong objects when ball is in hand.",
            range_min=0,
            range_max=100,
            initial_value=80,
            update_func=lambda v: self.parent.update_setting('override_ball_held_color_threshold', v / 100.0),
            is_float=True
        )
        row += 1
        
        # Info about how it works
        how_it_works_label = QLabel("💡 How it works: The system uses class-specific thresholds to override trackers. "
                                    "'Ball' (in-air) and 'Ball Held' detections can have different confidence and color "
                                    "requirements, allowing you to tune each independently for optimal tracking.")
        how_it_works_label.setStyleSheet("color: #4CAF50; font-size: 9px; font-style: italic;")
        how_it_works_label.setWordWrap(True)
        layout.addWidget(how_it_works_label, row, 0, 1, 3)
        
        return section
    
    def create_held_color_blob_section(self):
        """Create the Held Color Blob Detection section (3D only)"""
        section = CollapsibleGroupBox("🤲 Held Ball Color Detection", collapsed=False)
        layout = QGridLayout()
        section.get_content_layout().addLayout(layout)
        
        row = 0
        
        # Info label
        info_label = QLabel("ℹ️ Control how the system searches for color blobs when a ball is marked as held\n"
                           "These settings prevent trackers from jumping to wrong objects (like pants)")
        info_label.setStyleSheet("color: #aaaaaa; font-size: 10px;")
        info_label.setWordWrap(True)
        layout.addWidget(info_label, row, 0, 1, 3)
        row += 1
        
        # Search radius
        self.parent.hcb_search_radius_slider, self.parent.hcb_search_radius_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="Search Radius (pixels)",
            tooltip_text="Radius in pixels to search for color blob around hand when ball is held.\n"
                         "Range: 40-200 pixels. Default: 120 pixels.\n"
                         "Larger values search wider area but may find wrong objects.\n"
                         "Smaller values are more precise but may miss the ball.\n"
                         "⚠️ Reduce to 80-100px if tracker jumps to wrong objects!",
            range_min=40,
            range_max=200,
            initial_value=120,
            update_func=lambda v: self.parent.update_setting('held_color_search_radius', v),
            is_float=False
        )
        row += 1
        
        # Minimum color score
        self.parent.hcb_min_color_score_slider, self.parent.hcb_min_color_score_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="Min Color Match Score",
            tooltip_text="Minimum color match score to accept a color blob when ball is held.\n"
                         "Range: 0.00-1.00. Default: 0.30.\n"
                         "Higher values = stricter color matching (fewer false positives).\n"
                         "Lower values = more lenient (may track wrong objects).\n"
                         "⚠️ Increase to 0.40-0.50 if tracker jumps to pants/clothing!",
            range_min=0,
            range_max=100,
            initial_value=30,
            update_func=lambda v: self.parent.update_setting('held_color_min_score', v / 100.0),
            is_float=True
        )
        row += 1
        
        # Maximum distance from hand
        self.parent.hcb_max_distance_slider, self.parent.hcb_max_distance_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="Max Distance from Hand (cm)",
            tooltip_text="Maximum distance from hand to accept a color blob when ball is held.\n"
                         "Range: 10-50cm. Default: 25cm.\n"
                         "Prevents tracking distant objects that match the color.\n"
                         "Lower values = stricter proximity requirement.\n"
                         "⚠️ Reduce to 15-20cm if tracker jumps to distant objects!",
            range_min=10,
            range_max=50,
            initial_value=25,
            update_func=lambda v: self.parent.update_setting('held_color_max_distance', v / 100.0),
            is_float=False
        )
        row += 1
        
        # Info about how it works
        how_it_works_label = QLabel("💡 How it works: When a ball is marked as held, the system searches for "
                                    "a color blob near the hand. These settings ensure it only accepts blobs "
                                    "that match the ball's color well AND are close to the hand, preventing "
                                    "false matches with clothing or other objects.")
        how_it_works_label.setStyleSheet("color: #4CAF50; font-size: 9px; font-style: italic;")
        how_it_works_label.setWordWrap(True)
        layout.addWidget(how_it_works_label, row, 0, 1, 3)
        
        return section
    
    def create_trajectory_section(self):
        """Create the Trajectory Settings section (3D only)"""
        section = CollapsibleGroupBox("🎯 Trajectory Settings", collapsed=False)
        layout = QGridLayout()
        section.get_content_layout().addLayout(layout)
        
        row = 0
        
        # Info label
        info_label = QLabel("ℹ️ Configure trajectory prediction physics and search parameters")
        info_label.setStyleSheet("color: #aaaaaa; font-size: 10px;")
        info_label.setWordWrap(True)
        layout.addWidget(info_label, row, 0, 1, 3)
        row += 1
        
        # Gravity
        self.parent.traj_gravity_slider, self.parent.traj_gravity_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="Gravity (m/s²)",
            tooltip_text="Gravitational acceleration for trajectory prediction.\n"
                         "Range: 5.0-15.0 m/s². Default: 9.81 m/s².\n"
                         "Earth gravity is 9.81 m/s². Adjust if needed for calibration.",
            range_min=50,
            range_max=150,
            initial_value=98,
            update_func=lambda v: self.parent.update_setting('traj_gravity', v / 10.0),
            is_float=True
        )
        row += 1
        
        # Time Step
        self.parent.traj_time_step_slider, self.parent.traj_time_step_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="Time Step (ms)",
            tooltip_text="Time between trajectory prediction points.\n"
                         "Range: 0.01-0.10 s. Default: 0.033 s (30 FPS).\n"
                         "Smaller = more points, smoother curve, slower computation.",
            range_min=10,
            range_max=100,
            initial_value=33,
            update_func=lambda v: self.parent.update_setting('traj_time_step', v / 1000.0),
            is_float=False
        )
        row += 1
        
        # Max Duration
        self.parent.traj_max_time_slider, self.parent.traj_max_time_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="Max Duration (s)",
            tooltip_text="Maximum trajectory prediction duration.\n"
                         "Range: 1.0-5.0 s. Default: 3.0 s.\n"
                         "Longer = more predicted points, but may be less accurate.",
            range_min=10,
            range_max=50,
            initial_value=30,
            update_func=lambda v: self.parent.update_setting('traj_max_time', v / 10.0),
            is_float=True
        )
        row += 1
        
        # Search Radius
        self.parent.traj_search_radius_slider, self.parent.traj_search_radius_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="Search Radius (cm)",
            tooltip_text="Search radius along trajectory for ball detection.\n"
                         "Range: 0.05-0.50 m. Default: 0.15 m (15 cm).\n"
                         "Larger = more forgiving but may match wrong objects.",
            range_min=5,
            range_max=50,
            initial_value=15,
            update_func=lambda v: self.parent.update_setting('traj_search_radius', v / 100.0),
            is_float=False
        )
        row += 1
        
        # Min Points for Prediction
        self.parent.traj_min_points_for_prediction_slider, self.parent.traj_min_points_for_prediction_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="Min Points for Prediction",
            tooltip_text="Minimum trajectory points before full physics prediction.\n"
                         "Range: 2-5 points. Default: 3.\n"
                         "2 points = linear, 3+ = parabolic arc with gravity.",
            range_min=2,
            range_max=5,
            initial_value=3,
            update_func=lambda v: self.parent.update_setting('traj_min_points_for_prediction', v),
            is_float=False
        )
        row += 1
        
        # Color Match Threshold
        self.parent.traj_color_match_threshold_slider, self.parent.traj_color_match_threshold_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="Color Match Threshold",
            tooltip_text="Minimum color match score for trajectory verification.\n"
                         "Range: 0.0-1.0. Default: 0.50 (50%).\n"
                         "Higher = stricter color matching, fewer false positives.",
            range_min=0,
            range_max=100,
            initial_value=50,
            update_func=lambda v: self.parent.update_setting('traj_color_match_threshold', v / 100.0),
            is_float=True
        )
        row += 1
        
        # YOLO Confidence Threshold for Normal Tracking
        self.parent.traj_yolo_confidence_threshold_slider, self.parent.traj_yolo_confidence_threshold_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="YOLO Confidence Threshold",
            tooltip_text="Minimum YOLO confidence for normal throw detection (non-override).\n"
                         "Range: 0.0-1.0. Default: 0.70 (70%).\n"
                         "This is separate from override thresholds and used for regular tracking.\n"
                         "Lower = more sensitive throw detection, Higher = stricter requirements.",
            range_min=0,
            range_max=100,
            initial_value=70,
            update_func=lambda v: self.parent.update_setting('traj_yolo_confidence_threshold', v / 100.0),
            is_float=True
        )
        row += 1
        
        # Velocity Estimation Time
        self.parent.traj_velocity_estimation_time_slider, self.parent.traj_velocity_estimation_time_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="Velocity Estimation Time (ms)",
            tooltip_text="Time window for initial velocity estimation.\n"
                         "Range: 0.05-0.30 s. Default: 0.10 s.\n"
                         "Shorter = more responsive, Longer = more stable.",
            range_min=5,
            range_max=30,
            initial_value=10,
            update_func=lambda v: self.parent.update_setting('traj_velocity_estimation_time', v / 100.0),
            is_float=False
        )
        row += 1
        
        # Max Search Distance
        self.parent.traj_max_search_distance_slider, self.parent.traj_max_search_distance_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="Max Search Distance (cm)",
            tooltip_text="Maximum distance to search along trajectory.\n"
                         "Range: 0.20-1.00 m. Default: 0.50 m (50 cm).\n"
                         "Limits how far ahead we look for the ball.",
            range_min=20,
            range_max=100,
            initial_value=50,
            update_func=lambda v: self.parent.update_setting('traj_max_search_distance', v / 100.0),
            is_float=False
        )
        row += 1
        
        # Separator for visualization toggles
        layout.addWidget(QLabel("Threshold Visualization:"), row, 0, 1, 3)
        row += 1
        
        # Show unified hand_distance_threshold toggle
        self.parent.show_hand_distance_threshold_toggle = QPushButton("show_hand_distance_threshold")
        self.parent.show_hand_distance_threshold_toggle.setCheckable(True)
        self.parent.show_hand_distance_threshold_toggle.setChecked(True)
        self.parent.show_hand_distance_threshold_toggle.clicked.connect(
            lambda: self.parent.update_setting('show_hand_distance_threshold',
                                           1 if self.parent.show_hand_distance_threshold_toggle.isChecked() else 0))
        layout.addWidget(self.parent.show_hand_distance_threshold_toggle, row, 0, 1, 3)
        row += 1
        
        # Show hand velocity zone toggle
        self.parent.show_hand_velocity_zone_toggle = QPushButton("show_hand_velocity_zone")
        self.parent.show_hand_velocity_zone_toggle.setCheckable(True)
        self.parent.show_hand_velocity_zone_toggle.setChecked(False)
        self.parent.show_hand_velocity_zone_toggle.clicked.connect(
            lambda: self.parent.update_setting('show_hand_velocity_zone',
                                           1 if self.parent.show_hand_velocity_zone_toggle.isChecked() else 0))
        layout.addWidget(self.parent.show_hand_velocity_zone_toggle, row, 0, 1, 3)
        row += 1
        
        # Info about visualization
        viz_info_label = QLabel("ℹ️ Blue circle = unified hand distance threshold (around hands)")
        viz_info_label.setStyleSheet("color: #aaaaaa; font-size: 9px;")
        viz_info_label.setWordWrap(True)
        layout.addWidget(viz_info_label, row, 0, 1, 3)
        
        return section
    
    def create_hand_velocity_section(self):
        """Create the Hand Velocity Tracking section (3D only)"""
        section = CollapsibleGroupBox("🤚 Hand Velocity Tracking", collapsed=False)
        layout = QGridLayout()
        section.get_content_layout().addLayout(layout)
        
        row = 0
        
        # Info label
        info_label = QLabel("ℹ️ Use hand movement to predict throws and lower detection thresholds")
        info_label.setStyleSheet("color: #aaaaaa; font-size: 10px;")
        info_label.setWordWrap(True)
        layout.addWidget(info_label, row, 0, 1, 3)
        row += 1
        
        # Enable toggle
        self.parent.hand_velocity_enabled_toggle = QPushButton("Enable Hand Velocity Tracking")
        self.parent.hand_velocity_enabled_toggle.setCheckable(True)
        self.parent.hand_velocity_enabled_toggle.setChecked(True)
        self.parent.hand_velocity_enabled_toggle.clicked.connect(
            lambda: self.parent.update_setting('hand_velocity_enabled',
                                           1 if self.parent.hand_velocity_enabled_toggle.isChecked() else 0))
        layout.addWidget(self.parent.hand_velocity_enabled_toggle, row, 0, 1, 3)
        row += 1
        
        # Velocity Threshold
        self.parent.hand_velocity_threshold_slider, self.parent.hand_velocity_threshold_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="Velocity Threshold (m/s)",
            tooltip_text="Minimum hand speed to trigger enhanced throw detection.\n"
                         "Range: 0.1-5.0 m/s. Default: 1.0 m/s.\n"
                         "Lower = more sensitive (triggers with slower hand movement)\n"
                         "Higher = less sensitive (requires faster hand movement)",
            range_min=10,
            range_max=500,
            initial_value=100,
            update_func=lambda v: self.parent.update_setting('hand_velocity_threshold', v / 100.0),
            is_float=True
        )
        row += 1
        
        # Confidence Reduction
        self.parent.hand_velocity_confidence_reduction_slider, self.parent.hand_velocity_confidence_reduction_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="Confidence Reduction",
            tooltip_text="Amount to reduce confidence threshold when hand is moving fast.\n"
                         "Range: 0.0-0.9. Default: 0.3 (30% reduction).\n"
                         "Higher = more aggressive detection (much lower thresholds)\n"
                         "Lower = more conservative detection\n"
                         "⚠️ Values above 0.7 may cause false positives!",
            range_min=0,
            range_max=90,
            initial_value=30,
            update_func=lambda v: self.parent.update_setting('hand_velocity_confidence_reduction', v / 100.0),
            is_float=True
        )
        row += 1
        
        # Detection Radius
        self.parent.hand_velocity_detection_radius_slider, self.parent.hand_velocity_detection_radius_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="Detection Radius (cm)",
            tooltip_text="Radius of detection zone in direction of hand movement.\n"
                         "Range: 5-100 cm. Default: 15 cm.\n"
                         "Defines the area where reduced thresholds apply.\n"
                         "Larger = wider detection zone, smaller = more precise\n"
                         "⚠️ Very large values may affect unrelated detections!",
            range_min=5,
            range_max=100,
            initial_value=15,
            update_func=lambda v: self.parent.update_setting('hand_velocity_detection_radius', v / 100.0),
            is_float=False
        )
        row += 1
        
        # Distance Reduction
        self.parent.hand_velocity_distance_reduction_slider, self.parent.hand_velocity_distance_reduction_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="Distance Reduction (cm)",
            tooltip_text="Reduced hand_distance_threshold when hand velocity zone is active.\n"
                         "Range: 0-30 cm. Default: 10 cm.\n"
                         "This is the effective hand_distance_threshold used for throw detection\n"
                         "when the ball is within the velocity zone (moving hand direction).\n"
                         "0 = no distance requirement (immediate throw detection)\n"
                         "Lower = allows throws to be detected closer to the hand\n"
                         "Higher = requires ball to be farther from hand even in velocity zone\n"
                         "⚠️ Should be lower than normal hand_distance_threshold (25cm)!",
            range_min=0,
            range_max=30,
            initial_value=10,
            update_func=lambda v: self.parent.update_setting('hand_velocity_distance_reduction', v / 100.0),
            is_float=False
        )
        row += 1
        
        # Ignore Class Toggle
        self.parent.hand_velocity_ignore_class_toggle = QPushButton("Ignore Class Requirement")
        self.parent.hand_velocity_ignore_class_toggle.setCheckable(True)
        self.parent.hand_velocity_ignore_class_toggle.setChecked(False)
        self.parent.hand_velocity_ignore_class_toggle.clicked.connect(
            lambda: self.parent.update_setting('hand_velocity_ignore_class',
                                           1 if self.parent.hand_velocity_ignore_class_toggle.isChecked() else 0))
        self.parent.hand_velocity_ignore_class_toggle.setToolTip(
            "When enabled, detections in the velocity direction don't need to be 'ball' class.\n"
            "This allows 'ball_held' detections to trigger throws when hand is moving fast.\n"
            "Use this if throws are being missed due to class misclassification.")
        layout.addWidget(self.parent.hand_velocity_ignore_class_toggle, row, 0, 1, 3)
        row += 1
        
        # Info about how it works
        how_it_works_label = QLabel("💡 How it works: The system tracks the last 3 hand positions to calculate velocity. "
                                    "When a hand holding a ball moves faster than the threshold, detection requirements "
                                    "are lowered for balls appearing in the direction of movement. This helps catch throws "
                                    "earlier, even with lower confidence detections.")
        how_it_works_label.setStyleSheet("color: #4CAF50; font-size: 9px; font-style: italic;")
        how_it_works_label.setWordWrap(True)
        layout.addWidget(how_it_works_label, row, 0, 1, 3)
        
        return section
    
    def create_ball_profiles_section(self):
        """Create the Ball Profiles section (3D only)"""
        section = CollapsibleGroupBox("🎨 Ball Profiles", collapsed=False)
        layout = QVBoxLayout()
        section.get_content_layout().addLayout(layout)
        
        # Load ball profiles from ball_settings.json
        import json
        import os
        ball_settings_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "ball_settings.json")
        ball_settings_path = os.path.normpath(ball_settings_path)
        
        try:
            with open(ball_settings_path, 'r') as f:
                self.parent.ball_profiles = json.load(f)
            print(f"✅ Loaded ball profiles from {ball_settings_path}")
            print(f"   Profiles loaded: {list(self.parent.ball_profiles.keys())}")
        except Exception as e:
            print(f"❌ Error loading ball_settings.json: {e}")
            self.parent.ball_profiles = {}
        
        # Also get profiles from ColorProfileManager
        from .color_profile_manager import ColorProfileManager
        color_manager = ColorProfileManager()
        
        # Merge profiles - calculate proper HSV ranges for new colors
        profiles_updated = False
        for profile in color_manager.profiles:
            ball_name = profile['name']
            
            # Check if profile needs updating
            needs_update = False
            if ball_name not in self.parent.ball_profiles:
                needs_update = True
                print(f"⚠️ Adding missing profile '{ball_name}'")
            else:
                # Check if it has the default 0-180 hue range
                existing_min_hue = self.parent.ball_profiles[ball_name]['min_hsv'][0]
                existing_max_hue = self.parent.ball_profiles[ball_name]['max_hsv'][0]
                if existing_min_hue == 0.0 and existing_max_hue == 180.0:
                    needs_update = True
                    print(f"⚠️ Fixing profile '{ball_name}' with default 0-180 range")
                else:
                    print(f"✓ Profile '{ball_name}' already has custom hue range: {existing_min_hue:.1f}-{existing_max_hue:.1f}")
            
            if needs_update:
                # Calculate HSV range from RGB color
                rgb = profile.get('rgb', [255, 255, 255])
                min_hsv, max_hsv = self.parent._calculate_hsv_range_from_rgb(rgb)
                
                print(f"   RGB: {rgb} -> Hue range: {min_hsv[0]:.1f}-{max_hsv[0]:.1f}")
                
                # Preserve enabled state if profile already exists
                enabled = self.parent.ball_profiles[ball_name].get('enabled', True) if ball_name in self.parent.ball_profiles else profile.get('enabled', True)
                
                self.parent.ball_profiles[ball_name] = {
                    'enabled': enabled,
                    'min_hsv': min_hsv,
                    'max_hsv': max_hsv
                }
                profiles_updated = True
        
        # Save updated ball_settings.json if we updated any profiles
        if profiles_updated:
            try:
                with open(ball_settings_path, 'w') as f:
                    json.dump(self.parent.ball_profiles, f, indent=4)
                print(f"✅ Ball settings saved with updated HSV ranges")
            except Exception as e:
                print(f"❌ Error saving ball_settings.json: {e}")
        else:
            print(f"ℹ️ No profile updates needed")
        
        # Store checkbox references
        self.parent.ball_checkboxes = {}
        self.parent.ball_hue_sliders = {}
        
        # Create a widget for each ball profile
        for ball_name in sorted(self.parent.ball_profiles.keys()):
            ball_group = QGroupBox(ball_name.capitalize())
            ball_layout = QGridLayout(ball_group)
            
            # Checkbox for enabling/disabling this ball
            checkbox = QPushButton(f"Track {ball_name.capitalize()}")
            checkbox.setCheckable(True)
            is_enabled = self.parent.ball_profiles[ball_name].get('enabled', True)
            checkbox.setChecked(is_enabled)
            checkbox.clicked.connect(lambda checked, name=ball_name: self.parent.toggle_ball_tracking(name, checked))
            self.parent.ball_checkboxes[ball_name] = checkbox
            ball_layout.addWidget(checkbox, 0, 0, 1, 3)
            
            # Get current calibration values
            hsv_data = self.parent.ball_profiles[ball_name]
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
            if not hasattr(self.parent, 'ball_calibration_labels'):
                self.parent.ball_calibration_labels = {}
            self.parent.ball_calibration_labels[ball_name] = {
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