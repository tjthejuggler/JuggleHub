"""
Calibration settings widget for JuggleHub UI.
Refactored to use modular section components with dynamic visibility.
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
    from PyQt6.QtWidgets import (QWidget, QVBoxLayout, QLabel, QSlider, QPushButton, 
                                 QComboBox, QGridLayout, QScrollArea, QMessageBox)
    from PyQt6.QtCore import Qt
    PYQT_AVAILABLE = True
except ImportError:
    PYQT_AVAILABLE = False

if PYQT_AVAILABLE:
    from .ui_widgets import CollapsibleGroupBox
    from .ui_settings_manager import SettingsManager
    from .ui_settings_common import CommonSettingsSections
    from .ui_settings_3d import Tracker3DSettingsSections
    from .ui_settings_2d import Tracker2DSettingsSections
    from .ui_settings_new3d import New3DSettingsSections


if PYQT_AVAILABLE:
    class CalibrationSettingsWidget(QWidget):
        def __init__(self, udp_client, zmq_client, hub_instance=None, parent=None, main_window=None):
            super().__init__(parent)
            self.udp_client = udp_client
            self.zmq_client = zmq_client
            self.hub_instance = hub_instance
            self.main_window = main_window
            self.calibration_saves_dir = os.path.join("hub", "calibration_saves")
            self._loading_settings = True
            
            # Initialize settings manager
            self.settings_manager = SettingsManager()
            
            # Determine which tracker was last used by checking saved settings
            self.current_tracker = self._determine_last_used_tracker()
            
            # Ensure calibration_saves directory exists
            os.makedirs(self.calibration_saves_dir, exist_ok=True)
            
            # Initialize resolution-FPS mapping
            self.resolution_fps_map = {
                "1280 x 800": [60, 30, 15, 6],
                "1280 x 720": [60, 30, 15, 6],
                "960 x 540": [60, 30, 15, 6],
                "848 x 480": [90, 60, 30, 15, 6],
                "640 x 480": [60, 30, 15, 6],
                "640 x 360": [90, 60, 30, 15, 6],
                "424 x 240": [90, 60, 30, 15, 6],
                "320 x 240": [90, 60, 30, 15, 6]
            }
            
            # Create section handlers
            self.common_sections = CommonSettingsSections(self, udp_client, zmq_client)
            self.tracker_3d_sections = Tracker3DSettingsSections(self, udp_client, zmq_client)
            self.tracker_new3d_sections = New3DSettingsSections(self, udp_client, zmq_client)
            self.tracker_2d_sections = Tracker2DSettingsSections(self, udp_client, zmq_client)
            
            # Initialize UI
            self.init_ui()
            
            # Load settings after UI is initialized
            self.load_settings()
            
            # Allow auto-save
            self._loading_settings = False

        def _determine_last_used_tracker(self) -> str:
            """
            Determine which tracker was last used by checking saved settings files.
            Returns the tracker type that was most recently saved.
            """
            import os
            from datetime import datetime
            
            tracker_files = {
                "depth_based": self.settings_manager.settings_3d_file,
                "new_3d": self.settings_manager.settings_new3d_file,
                "simple_2d": self.settings_manager.settings_2d_file,
            }
            
            latest_tracker = "depth_based"  # Default fallback
            latest_time = None
            
            for tracker_type, filepath in tracker_files.items():
                if os.path.exists(filepath):
                    try:
                        with open(filepath, 'r') as f:
                            settings = json.load(f)
                        
                        # Check if this file has a saved_at timestamp
                        if 'saved_at' in settings:
                            saved_time = datetime.fromisoformat(settings['saved_at'])
                            if latest_time is None or saved_time > latest_time:
                                latest_time = saved_time
                                latest_tracker = tracker_type
                                print(f"🔍 Found {tracker_type} settings saved at {settings['saved_at']}")
                    except Exception as e:
                        print(f"⚠️ Error reading {filepath}: {e}")
            
            print(f"✅ Determined last used tracker: {latest_tracker}")
            return latest_tracker

        def init_ui(self):
            """Initialize the UI with modular sections"""
            main_layout = QVBoxLayout(self)
            main_layout.setContentsMargins(0, 0, 0, 0)
            
            # Create scroll area
            scroll_area = QScrollArea()
            scroll_area.setWidgetResizable(True)
            scroll_area.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
            scroll_area.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAsNeeded)
            scroll_area.setStyleSheet("""
                QScrollArea { border: none; background-color: #2b2b2b; }
                QScrollBar:vertical { border: none; background: #1e1e1e; width: 12px; margin: 0px; }
                QScrollBar::handle:vertical { background: #555555; min-height: 20px; border-radius: 6px; }
                QScrollBar::handle:vertical:hover { background: #666666; }
            """)
            
            # Container widget for all sections
            container_widget = QWidget()
            container_layout = QVBoxLayout(container_widget)
            container_layout.setSpacing(10)
            container_layout.setContentsMargins(5, 5, 5, 5)
            
            # Lists to track section widgets for visibility management
            self.common_section_widgets = []
            self.tracker_3d_section_widgets = []
            self.tracker_new3d_section_widgets = []
            self.tracker_2d_section_widgets = []
            
            # Add common sections (always visible)
            self.camera_section = self.common_sections.create_camera_section()
            container_layout.addWidget(self.camera_section)
            self.common_section_widgets.append(self.camera_section)
            
            self.yolo_section = self.common_sections.create_yolo_section()
            container_layout.addWidget(self.yolo_section)
            self.common_section_widgets.append(self.yolo_section)
            
            self.pose_section = self.common_sections.create_pose_section()
            container_layout.addWidget(self.pose_section)
            self.common_section_widgets.append(self.pose_section)
            
            # Add 3D tracker sections
            self.throw_catch_section = self.tracker_3d_sections.create_ball_state_section()
            container_layout.addWidget(self.throw_catch_section)
            self.tracker_3d_section_widgets.append(self.throw_catch_section)
            
            self.color_tracker_weights_section = self.tracker_3d_sections.create_color_tracker_section()
            container_layout.addWidget(self.color_tracker_weights_section)
            self.tracker_3d_section_widgets.append(self.color_tracker_weights_section)
            
            self.override_detection_section = self.tracker_3d_sections.create_override_detection_section()
            container_layout.addWidget(self.override_detection_section)
            self.tracker_3d_section_widgets.append(self.override_detection_section)
            
            self.held_color_blob_section = self.tracker_3d_sections.create_held_color_blob_section()
            container_layout.addWidget(self.held_color_blob_section)
            self.tracker_3d_section_widgets.append(self.held_color_blob_section)
            
            self.trajectory_section = self.tracker_3d_sections.create_trajectory_section()
            container_layout.addWidget(self.trajectory_section)
            self.tracker_3d_section_widgets.append(self.trajectory_section)
            
            self.hand_velocity_section = self.tracker_3d_sections.create_hand_velocity_section()
            container_layout.addWidget(self.hand_velocity_section)
            self.tracker_3d_section_widgets.append(self.hand_velocity_section)
            
            self.ball_profiles_section = self.tracker_3d_sections.create_ball_profiles_section()
            container_layout.addWidget(self.ball_profiles_section)
            self.tracker_3d_section_widgets.append(self.ball_profiles_section)
            
            # Add New 3D Kalman tracker sections
            self.new3d_physics_section = self.tracker_new3d_sections.create_physics_section()
            container_layout.addWidget(self.new3d_physics_section)
            self.tracker_new3d_section_widgets.append(self.new3d_physics_section)
            
            self.new3d_tracking_logic_section = self.tracker_new3d_sections.create_tracking_logic_section()
            container_layout.addWidget(self.new3d_tracking_logic_section)
            self.tracker_new3d_section_widgets.append(self.new3d_tracking_logic_section)
            
            self.new3d_association_section = self.tracker_new3d_sections.create_association_section()
            container_layout.addWidget(self.new3d_association_section)
            self.tracker_new3d_section_widgets.append(self.new3d_association_section)
            
            self.new3d_hand_velocity_section = self.tracker_new3d_sections.create_hand_velocity_section()
            container_layout.addWidget(self.new3d_hand_velocity_section)
            self.tracker_new3d_section_widgets.append(self.new3d_hand_velocity_section)
            
            self.new3d_visualization_section = self.tracker_new3d_sections.create_visualization_section()
            container_layout.addWidget(self.new3d_visualization_section)
            self.tracker_new3d_section_widgets.append(self.new3d_visualization_section)
            
            self.new3d_audio_indicators_section = self.tracker_new3d_sections.create_audio_indicators_section()
            container_layout.addWidget(self.new3d_audio_indicators_section)
            self.tracker_new3d_section_widgets.append(self.new3d_audio_indicators_section)
            
            # Add Ball Profiles section
            self.new3d_ball_profiles_section = self.tracker_new3d_sections.create_ball_profiles_section()
            container_layout.addWidget(self.new3d_ball_profiles_section)
            self.tracker_new3d_section_widgets.append(self.new3d_ball_profiles_section)
            
            # Add Color Calibration section
            self.new3d_color_calibration_section = self.tracker_new3d_sections.create_color_calibration_section()
            container_layout.addWidget(self.new3d_color_calibration_section)
            self.tracker_new3d_section_widgets.append(self.new3d_color_calibration_section)
            
            # Add 2D tracker sections (currently none, but ready for future)
            # When 2D sections are added, append them to self.tracker_2d_section_widgets
            
            # Initially show sections based on current tracker
            self.hide_all_tracker_sections()
            self.show_tracker_sections(self.current_tracker)
            
            # Add stretch to push sections to top
            container_layout.addStretch()
            
            # Set container as scroll area widget
            scroll_area.setWidget(container_widget)
            main_layout.addWidget(scroll_area)

        def hide_all_tracker_sections(self):
            """Hide all tracker-specific sections"""
            for section in self.tracker_3d_section_widgets:
                section.setVisible(False)
            for section in self.tracker_new3d_section_widgets:
                section.setVisible(False)
            for section in self.tracker_2d_section_widgets:
                section.setVisible(False)

        def show_tracker_sections(self, tracker_type: str):
            """Show sections for specified tracker"""
            if tracker_type == "depth_based":
                for section in self.tracker_3d_section_widgets:
                    section.setVisible(True)
            elif tracker_type == "new_3d":
                for section in self.tracker_new3d_section_widgets:
                    section.setVisible(True)
            elif tracker_type == "simple_2d":
                for section in self.tracker_2d_section_widgets:
                    section.setVisible(True)

        def on_tracking_system_changed(self, index=None):
            """Handle tracking system selection change
            
            Args:
                index: The new index (from currentIndexChanged signal), optional
            """
            new_tracker = self.tracking_system_combo.currentData()
            
            if new_tracker == self.current_tracker:
                return  # No change
            
            print(f"🔄 Switching from {self.current_tracker} to {new_tracker}")
            
            # Save current tracker settings before switching
            if not self._loading_settings:
                current_settings = self.get_current_settings()
                self.settings_manager.save_settings(self.current_tracker, current_settings)
            
            # Update current tracker
            self.current_tracker = new_tracker
            
            # Hide all tracker sections
            self.hide_all_tracker_sections()
            
            # Show new tracker sections
            self.show_tracker_sections(new_tracker)
            
            # Load and apply new tracker settings
            new_settings = self.settings_manager.load_settings(new_tracker)
            if new_settings:
                self._loading_settings = True
                self.apply_settings(new_settings)
                self._loading_settings = False
            
            # Send tracker switch command to engine
            command = juggler_pb2.CommandRequest()
            command.type = juggler_pb2.CommandRequest.CommandType.SET_TRACKER_TYPE
            command.tracker_type = new_tracker
            
            try:
                response = self.zmq_client.send_command(command)
                if response.success:
                    print(f"✅ {response.message}")
                    # Note: Settings are already saved above before switching
                    # No need to save again here - that was causing wrong file to be saved
                else:
                    print(f"❌ Failed to switch tracker: {response.message}")
                    QMessageBox.warning(self, "Tracker Switch Failed",
                                      f"Failed to switch tracking system:\n{response.message}")
            except Exception as e:
                print(f"❌ Error switching tracker: {e}")
                QMessageBox.critical(self, "Error", f"Error switching tracking system:\n{str(e)}")

        def get_current_settings(self) -> dict:
            """Get current settings structured by tracker type"""
            settings = {
                'tracker_type': self.current_tracker,
                'camera_settings_profile': self.camera_settings_combo.currentData() if hasattr(self, 'camera_settings_combo') else 'default',
                'resolution': self.resolution_combo.currentText() if hasattr(self, 'resolution_combo') else '640 x 480',
                'fps': self.fps_combo.currentData() if hasattr(self, 'fps_combo') else 60,
                'depth_sensor_enabled': self.depth_sensor_toggle.isChecked() if hasattr(self, 'depth_sensor_toggle') else True,
                'tracking_system': self.current_tracker,
                'enable_ball_detection': self.use_dnn_tracker_toggle.isChecked() if hasattr(self, 'use_dnn_tracker_toggle') else True,
                'ball_confidence_threshold': self.ball_confidence_slider.value() / 100.0 if hasattr(self, 'ball_confidence_slider') else 0.25,
                'ball_held_confidence_threshold': self.ball_held_confidence_slider.value() / 100.0 if hasattr(self, 'ball_held_confidence_slider') else 0.25,
                'nms_threshold': self.nms_slider.value() / 100.0 if hasattr(self, 'nms_slider') else 0.50,
                'show_raw_yolo_detections': self.show_raw_yolo_toggle.isChecked() if hasattr(self, 'show_raw_yolo_toggle') else False,
                'pose_model_enabled': self.pose_model_toggle.isChecked() if hasattr(self, 'pose_model_toggle') else True,
            }
            
            # Add 3D-specific settings if current tracker is 3D
            if self.current_tracker == "depth_based":
                settings.update(self._get_3d_tracker_settings())
            
            # Add New 3D Kalman-specific settings if current tracker is new_3d
            elif self.current_tracker == "new_3d":
                settings.update(self._get_new3d_tracker_settings())
            
            # Add 2D-specific settings if current tracker is 2D
            elif self.current_tracker == "simple_2d":
                settings.update(self._get_2d_tracker_settings())
            
            # Add UI state
            settings.update(self._get_ui_state())
            
            return settings

        def _get_3d_tracker_settings(self) -> dict:
            """Get 3D tracker-specific settings"""
            return {
                'undetected_near_hand_threshold': self.tc_undetected_near_hand_slider.value() / 100.0 if hasattr(self, 'tc_undetected_near_hand_slider') else 0.20,
                'min_frames_for_state_change': self.tc_min_frames_slider.value() if hasattr(self, 'tc_min_frames_slider') else 3,
                'hand_distance_threshold': self.tc_hand_distance_threshold_slider.value() / 100.0 if hasattr(self, 'tc_hand_distance_threshold_slider') else 0.25,
                'min_throw_distance': self.tc_min_throw_distance_slider.value() / 100.0 if hasattr(self, 'tc_min_throw_distance_slider') else 0.20,
                'min_frames_before_catch': self.tc_min_frames_before_catch_slider.value() if hasattr(self, 'tc_min_frames_before_catch_slider') else 3,
                'ignore_class': self.tc_ignore_class_toggle.isChecked() if hasattr(self, 'tc_ignore_class_toggle') else False,
                'max_tracker_distance_per_frame': self.tc_max_tracker_distance_slider.value() / 100.0 if hasattr(self, 'tc_max_tracker_distance_slider') else 0.50,
                'tc_sound_on_catch': self.tc_sound_on_catch_toggle.isChecked() if hasattr(self, 'tc_sound_on_catch_toggle') else False,
                'tc_sound_on_throw': self.tc_sound_on_throw_toggle.isChecked() if hasattr(self, 'tc_sound_on_throw_toggle') else False,
                'tc_name_on_catch': self.tc_name_on_catch_toggle.isChecked() if hasattr(self, 'tc_name_on_catch_toggle') else False,
                'tc_name_on_throw': self.tc_name_on_throw_toggle.isChecked() if hasattr(self, 'tc_name_on_throw_toggle') else False,
                'temporal_consistency_bonus': self.ct_temporal_consistency_bonus_slider.value() / 100.0 if hasattr(self, 'ct_temporal_consistency_bonus_slider') else 0.25,
                'spatial_threshold': self.ct_spatial_threshold_slider.value() / 100.0 if hasattr(self, 'ct_spatial_threshold_slider') else 0.40,
                'color_sample_radius': self.ct_color_sample_radius_slider.value() if hasattr(self, 'ct_color_sample_radius_slider') else 1,
                'max_euclidean_distance': self.ct_max_euclidean_distance_slider.value() / 100.0 if hasattr(self, 'ct_max_euclidean_distance_slider') else 0.15,
                'min_euclidean_color_score': self.ct_min_euclidean_color_score_slider.value() / 100.0 if hasattr(self, 'ct_min_euclidean_color_score_slider') else 0.30,
                'max_depth_jump_strict': self.ct_max_depth_jump_strict_slider.value() / 100.0 if hasattr(self, 'ct_max_depth_jump_strict_slider') else 0.20,
                'min_color_confidence_override': self.ct_min_color_confidence_override_slider.value() / 100.0 if hasattr(self, 'ct_min_color_confidence_override_slider') else 0.35,
                'min_ball_separation': self.ct_min_ball_separation_slider.value() / 100.0 if hasattr(self, 'ct_min_ball_separation_slider') else 0.15,
                'min_hand_change_distance': self.ct_min_hand_change_distance_slider.value() / 100.0 if hasattr(self, 'ct_min_hand_change_distance_slider') else 0.25,
                'override_ball_confidence_threshold': self.od_ball_confidence_slider.value() / 100.0 if hasattr(self, 'od_ball_confidence_slider') else 0.70,
                'override_ball_color_threshold': self.od_ball_color_slider.value() / 100.0 if hasattr(self, 'od_ball_color_slider') else 0.80,
                'override_ball_held_confidence_threshold': self.od_ball_held_confidence_slider.value() / 100.0 if hasattr(self, 'od_ball_held_confidence_slider') else 0.70,
                'override_ball_held_color_threshold': self.od_ball_held_color_slider.value() / 100.0 if hasattr(self, 'od_ball_held_color_slider') else 0.80,
                'held_color_search_radius': self.hcb_search_radius_slider.value() if hasattr(self, 'hcb_search_radius_slider') else 120,
                'held_color_min_score': self.hcb_min_color_score_slider.value() / 100.0 if hasattr(self, 'hcb_min_color_score_slider') else 0.30,
                'held_color_max_distance': self.hcb_max_distance_slider.value() / 100.0 if hasattr(self, 'hcb_max_distance_slider') else 0.25,
                'traj_gravity': self.traj_gravity_slider.value() / 10.0 if hasattr(self, 'traj_gravity_slider') else 9.81,
                'traj_time_step': self.traj_time_step_slider.value() / 1000.0 if hasattr(self, 'traj_time_step_slider') else 0.033,
                'traj_max_time': self.traj_max_time_slider.value() / 10.0 if hasattr(self, 'traj_max_time_slider') else 3.0,
                'traj_search_radius': self.traj_search_radius_slider.value() / 100.0 if hasattr(self, 'traj_search_radius_slider') else 0.15,
                'traj_min_points_for_prediction': self.traj_min_points_for_prediction_slider.value() if hasattr(self, 'traj_min_points_for_prediction_slider') else 3,
                'traj_color_match_threshold': self.traj_color_match_threshold_slider.value() / 100.0 if hasattr(self, 'traj_color_match_threshold_slider') else 0.50,
                'traj_yolo_confidence_threshold': self.traj_yolo_confidence_threshold_slider.value() / 100.0 if hasattr(self, 'traj_yolo_confidence_threshold_slider') else 0.70,
                'throw_yolo_confidence_threshold': self.throw_yolo_confidence_threshold_slider.value() / 100.0 if hasattr(self, 'throw_yolo_confidence_threshold_slider') else 0.50,
                'traj_velocity_estimation_time': self.traj_velocity_estimation_time_slider.value() / 100.0 if hasattr(self, 'traj_velocity_estimation_time_slider') else 0.10,
                'traj_max_search_distance': self.traj_max_search_distance_slider.value() / 100.0 if hasattr(self, 'traj_max_search_distance_slider') else 0.50,
                'show_hand_distance_threshold': self.show_hand_distance_threshold_toggle.isChecked() if hasattr(self, 'show_hand_distance_threshold_toggle') else True,
                'show_hand_velocity_zone': self.show_hand_velocity_zone_toggle.isChecked() if hasattr(self, 'show_hand_velocity_zone_toggle') else False,
                'hand_velocity_enabled': self.hand_velocity_enabled_toggle.isChecked() if hasattr(self, 'hand_velocity_enabled_toggle') else True,
                'hand_velocity_threshold': self.hand_velocity_threshold_slider.value() / 100.0 if hasattr(self, 'hand_velocity_threshold_slider') else 1.0,
                'hand_velocity_confidence_reduction': self.hand_velocity_confidence_reduction_slider.value() / 100.0 if hasattr(self, 'hand_velocity_confidence_reduction_slider') else 0.3,
                'hand_velocity_detection_radius': self.hand_velocity_detection_radius_slider.value() / 100.0 if hasattr(self, 'hand_velocity_detection_radius_slider') else 0.15,
                'hand_velocity_distance_reduction': self.hand_velocity_distance_reduction_slider.value() / 100.0 if hasattr(self, 'hand_velocity_distance_reduction_slider') else 0.10,
                'hand_velocity_ignore_class': self.hand_velocity_ignore_class_toggle.isChecked() if hasattr(self, 'hand_velocity_ignore_class_toggle') else False,
            }

        def _get_new3d_tracker_settings(self) -> dict:
            """Get New 3D Kalman tracker-specific settings"""
            return {
                # Kalman Filter settings
                'kalman_process_noise_pos': self.kalman_process_noise_pos_slider.value() / 100.0 if hasattr(self, 'kalman_process_noise_pos_slider') else 0.01,
                'kalman_process_noise_vel': self.kalman_process_noise_vel_slider.value() / 100.0 if hasattr(self, 'kalman_process_noise_vel_slider') else 0.1,
                'kalman_measurement_noise': self.kalman_measurement_noise_slider.value() / 100.0 if hasattr(self, 'kalman_measurement_noise_slider') else 0.05,
                'kalman_max_prediction_time': self.kalman_max_prediction_time_slider.value() / 10.0 if hasattr(self, 'kalman_max_prediction_time_slider') else 1.0,
                'kalman_velocity_smoothing': self.kalman_velocity_smoothing_slider.value() / 100.0 if hasattr(self, 'kalman_velocity_smoothing_slider') else 0.3,
                
                # Association settings
                'assoc_max_distance': self.assoc_max_distance_slider.value() / 100.0 if hasattr(self, 'assoc_max_distance_slider') else 0.30,
                'assoc_iou_threshold': self.assoc_iou_threshold_slider.value() / 100.0 if hasattr(self, 'assoc_iou_threshold_slider') else 0.3,
                'assoc_color_weight': self.assoc_color_weight_slider.value() / 100.0 if hasattr(self, 'assoc_color_weight_slider') else 0.4,
                'assoc_spatial_weight': self.assoc_spatial_weight_slider.value() / 100.0 if hasattr(self, 'assoc_spatial_weight_slider') else 0.6,
                'assoc_max_missed_frames': self.assoc_max_missed_frames_slider.value() if hasattr(self, 'assoc_max_missed_frames_slider') else 5,
                
                # State management settings
                'state_min_hits_to_confirm': self.state_min_hits_to_confirm_slider.value() if hasattr(self, 'state_min_hits_to_confirm_slider') else 3,
                'state_max_age': self.state_max_age_slider.value() if hasattr(self, 'state_max_age_slider') else 10,
                'state_confidence_decay': self.state_confidence_decay_slider.value() / 100.0 if hasattr(self, 'state_confidence_decay_slider') else 0.95,
                'state_min_confidence': self.state_min_confidence_slider.value() / 100.0 if hasattr(self, 'state_min_confidence_slider') else 0.3,
                
                # Audio indicators
                'tc_sound_on_catch': self.new3d_sound_on_catch_toggle.isChecked() if hasattr(self, 'new3d_sound_on_catch_toggle') else False,
                'tc_sound_on_throw': self.new3d_sound_on_throw_toggle.isChecked() if hasattr(self, 'new3d_sound_on_throw_toggle') else False,
                'tc_name_on_catch': self.new3d_name_on_catch_toggle.isChecked() if hasattr(self, 'new3d_name_on_catch_toggle') else False,
                'tc_name_on_throw': self.new3d_name_on_throw_toggle.isChecked() if hasattr(self, 'new3d_name_on_throw_toggle') else False,
            }

        def _get_2d_tracker_settings(self) -> dict:
            """Get 2D tracker-specific settings"""
            return {}

        def _get_ui_state(self) -> dict:
            """Get UI collapsed states"""
            return {
                'collapsed_camera': self.camera_section.is_collapsed if hasattr(self, 'camera_section') else False,
                'collapsed_yolo': self.yolo_section.is_collapsed if hasattr(self, 'yolo_section') else False,
                'collapsed_pose': self.pose_section.is_collapsed if hasattr(self, 'pose_section') else False,
                'collapsed_throw_catch': self.throw_catch_section.is_collapsed if hasattr(self, 'throw_catch_section') else False,
                'collapsed_color_tracker_weights': self.color_tracker_weights_section.is_collapsed if hasattr(self, 'color_tracker_weights_section') else False,
                'collapsed_override_detection': self.override_detection_section.is_collapsed if hasattr(self, 'override_detection_section') else False,
                'collapsed_held_color_blob': self.held_color_blob_section.is_collapsed if hasattr(self, 'held_color_blob_section') else False,
                'collapsed_trajectory': self.trajectory_section.is_collapsed if hasattr(self, 'trajectory_section') else False,
                'collapsed_hand_velocity': self.hand_velocity_section.is_collapsed if hasattr(self, 'hand_velocity_section') else False,
                'collapsed_ball_profiles': self.ball_profiles_section.is_collapsed if hasattr(self, 'ball_profiles_section') else False,
                'collapsed_new3d_ball_profiles': self.new3d_ball_profiles_section.is_collapsed if hasattr(self, 'new3d_ball_profiles_section') else False,
            }

        def apply_settings(self, settings: dict):
            """Apply settings from dictionary to UI controls"""
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
            
            # Depth sensor
            if 'depth_sensor_enabled' in settings and hasattr(self, 'depth_sensor_toggle'):
                self.depth_sensor_toggle.setChecked(settings['depth_sensor_enabled'])
            
            # Tracking system - block signals to prevent recursion
            if 'tracking_system' in settings:
                tracker_type = settings['tracking_system']
                index = self.tracking_system_combo.findData(tracker_type)
                if index >= 0:
                    # Block signals to prevent triggering on_tracking_system_changed
                    self.tracking_system_combo.blockSignals(True)
                    self.tracking_system_combo.setCurrentIndex(index)
                    self.tracking_system_combo.blockSignals(False)
                    # Update current_tracker to match loaded settings
                    self.current_tracker = tracker_type
                    print(f"✅ Tracking system combo set to: {tracker_type}")
            
            # Ball detection
            if 'enable_ball_detection' in settings and hasattr(self, 'use_dnn_tracker_toggle'):
                is_enabled = settings['enable_ball_detection']
                self.use_dnn_tracker_toggle.setChecked(is_enabled)
                self.use_dnn_tracker_toggle.setText("Enable YOLO Ball Detection" if is_enabled else "YOLO Ball Detection DISABLED")
                self.udp_client.send_setting('enable_ball_detection', 1 if is_enabled else 0)
            
            # YOLO settings
            if 'ball_confidence_threshold' in settings:
                self.ball_confidence_slider.setValue(int(settings['ball_confidence_threshold'] * 100))
            if 'ball_held_confidence_threshold' in settings:
                self.ball_held_confidence_slider.setValue(int(settings['ball_held_confidence_threshold'] * 100))
            if 'nms_threshold' in settings:
                self.nms_slider.setValue(int(settings['nms_threshold'] * 100))
            if 'show_raw_yolo_detections' in settings and hasattr(self, 'show_raw_yolo_toggle'):
                self.show_raw_yolo_toggle.setChecked(settings['show_raw_yolo_detections'])
            
            # Pose model
            if 'pose_model_enabled' in settings:
                self.pose_model_toggle.setChecked(settings['pose_model_enabled'])
            
            # Apply 3D-specific settings
            if self.current_tracker == "depth_based":
                self._apply_3d_tracker_settings(settings)
            
            # Apply New 3D Kalman-specific settings
            elif self.current_tracker == "new_3d":
                self._apply_new3d_tracker_settings(settings)
            
            # Apply 2D-specific settings
            elif self.current_tracker == "simple_2d":
                self._apply_2d_tracker_settings(settings)
            
            # Restore collapsed states
            self._apply_ui_state(settings)

        def _apply_3d_tracker_settings(self, settings: dict):
            """Apply 3D tracker-specific settings"""
            # Ball State Detection
            if 'undetected_near_hand_threshold' in settings:
                self.tc_undetected_near_hand_slider.setValue(int(settings['undetected_near_hand_threshold'] * 100))
            if 'min_frames_for_state_change' in settings:
                self.tc_min_frames_slider.setValue(settings['min_frames_for_state_change'])
            if 'hand_distance_threshold' in settings and hasattr(self, 'tc_hand_distance_threshold_slider'):
                self.tc_hand_distance_threshold_slider.setValue(int(settings['hand_distance_threshold'] * 100))
            if 'min_throw_distance' in settings and hasattr(self, 'tc_min_throw_distance_slider'):
                self.tc_min_throw_distance_slider.setValue(int(settings['min_throw_distance'] * 100))
            if 'min_frames_before_catch' in settings and hasattr(self, 'tc_min_frames_before_catch_slider'):
                self.tc_min_frames_before_catch_slider.setValue(settings['min_frames_before_catch'])
            if 'max_tracker_distance_per_frame' in settings:
                self.tc_max_tracker_distance_slider.setValue(int(settings['max_tracker_distance_per_frame'] * 100))
            if 'tc_sound_on_catch' in settings:
                self.tc_sound_on_catch_toggle.setChecked(settings['tc_sound_on_catch'])
            if 'tc_sound_on_throw' in settings:
                self.tc_sound_on_throw_toggle.setChecked(settings['tc_sound_on_throw'])
            if 'tc_name_on_catch' in settings:
                self.tc_name_on_catch_toggle.setChecked(settings['tc_name_on_catch'])
            if 'tc_name_on_throw' in settings:
                self.tc_name_on_throw_toggle.setChecked(settings['tc_name_on_throw'])
            if 'ignore_class' in settings and hasattr(self, 'tc_ignore_class_toggle'):
                self.tc_ignore_class_toggle.setChecked(settings['ignore_class'])
            
            # Color Tracker Weights
            if 'temporal_consistency_bonus' in settings and hasattr(self, 'ct_temporal_consistency_bonus_slider'):
                self.ct_temporal_consistency_bonus_slider.setValue(int(settings['temporal_consistency_bonus'] * 100))
            if 'spatial_threshold' in settings and hasattr(self, 'ct_spatial_threshold_slider'):
                self.ct_spatial_threshold_slider.setValue(int(settings['spatial_threshold'] * 100))
            if 'color_sample_radius' in settings and hasattr(self, 'ct_color_sample_radius_slider'):
                self.ct_color_sample_radius_slider.setValue(settings['color_sample_radius'])
            if 'max_euclidean_distance' in settings and hasattr(self, 'ct_max_euclidean_distance_slider'):
                self.ct_max_euclidean_distance_slider.setValue(int(settings['max_euclidean_distance'] * 100))
            if 'min_euclidean_color_score' in settings and hasattr(self, 'ct_min_euclidean_color_score_slider'):
                self.ct_min_euclidean_color_score_slider.setValue(int(settings['min_euclidean_color_score'] * 100))
            if 'max_depth_jump_strict' in settings and hasattr(self, 'ct_max_depth_jump_strict_slider'):
                self.ct_max_depth_jump_strict_slider.setValue(int(settings['max_depth_jump_strict'] * 100))
            if 'min_color_confidence_override' in settings and hasattr(self, 'ct_min_color_confidence_override_slider'):
                self.ct_min_color_confidence_override_slider.setValue(int(settings['min_color_confidence_override'] * 100))
            if 'min_ball_separation' in settings and hasattr(self, 'ct_min_ball_separation_slider'):
                self.ct_min_ball_separation_slider.setValue(int(settings['min_ball_separation'] * 100))
            if 'min_hand_change_distance' in settings and hasattr(self, 'ct_min_hand_change_distance_slider'):
                self.ct_min_hand_change_distance_slider.setValue(int(settings['min_hand_change_distance'] * 100))
            
            # Override Detection settings
            if 'override_ball_confidence_threshold' in settings and hasattr(self, 'od_ball_confidence_slider'):
                self.od_ball_confidence_slider.setValue(int(settings['override_ball_confidence_threshold'] * 100))
            if 'override_ball_color_threshold' in settings and hasattr(self, 'od_ball_color_slider'):
                self.od_ball_color_slider.setValue(int(settings['override_ball_color_threshold'] * 100))
            if 'override_ball_held_confidence_threshold' in settings and hasattr(self, 'od_ball_held_confidence_slider'):
                self.od_ball_held_confidence_slider.setValue(int(settings['override_ball_held_confidence_threshold'] * 100))
            if 'override_ball_held_color_threshold' in settings and hasattr(self, 'od_ball_held_color_slider'):
                self.od_ball_held_color_slider.setValue(int(settings['override_ball_held_color_threshold'] * 100))
            
            # Held Color Blob Detection settings
            if 'held_color_search_radius' in settings and hasattr(self, 'hcb_search_radius_slider'):
                self.hcb_search_radius_slider.setValue(settings['held_color_search_radius'])
            if 'held_color_min_score' in settings and hasattr(self, 'hcb_min_color_score_slider'):
                self.hcb_min_color_score_slider.setValue(int(settings['held_color_min_score'] * 100))
            if 'held_color_max_distance' in settings and hasattr(self, 'hcb_max_distance_slider'):
                self.hcb_max_distance_slider.setValue(int(settings['held_color_max_distance'] * 100))
            
            # Trajectory Settings
            if 'traj_gravity' in settings and hasattr(self, 'traj_gravity_slider'):
                self.traj_gravity_slider.setValue(int(settings['traj_gravity'] * 10))
            if 'traj_time_step' in settings and hasattr(self, 'traj_time_step_slider'):
                self.traj_time_step_slider.setValue(int(settings['traj_time_step'] * 1000))
            if 'traj_max_time' in settings and hasattr(self, 'traj_max_time_slider'):
                self.traj_max_time_slider.setValue(int(settings['traj_max_time'] * 10))
            if 'traj_search_radius' in settings and hasattr(self, 'traj_search_radius_slider'):
                self.traj_search_radius_slider.setValue(int(settings['traj_search_radius'] * 100))
            if 'traj_min_points_for_prediction' in settings and hasattr(self, 'traj_min_points_for_prediction_slider'):
                self.traj_min_points_for_prediction_slider.setValue(settings['traj_min_points_for_prediction'])
            if 'traj_color_match_threshold' in settings and hasattr(self, 'traj_color_match_threshold_slider'):
                self.traj_color_match_threshold_slider.setValue(int(settings['traj_color_match_threshold'] * 100))
            if 'traj_yolo_confidence_threshold' in settings and hasattr(self, 'traj_yolo_confidence_threshold_slider'):
                self.traj_yolo_confidence_threshold_slider.setValue(int(settings['traj_yolo_confidence_threshold'] * 100))
            if 'throw_yolo_confidence_threshold' in settings and hasattr(self, 'throw_yolo_confidence_threshold_slider'):
                self.throw_yolo_confidence_threshold_slider.setValue(int(settings['throw_yolo_confidence_threshold'] * 100))
            if 'traj_velocity_estimation_time' in settings and hasattr(self, 'traj_velocity_estimation_time_slider'):
                self.traj_velocity_estimation_time_slider.setValue(int(settings['traj_velocity_estimation_time'] * 100))
            if 'traj_max_search_distance' in settings and hasattr(self, 'traj_max_search_distance_slider'):
                self.traj_max_search_distance_slider.setValue(int(settings['traj_max_search_distance'] * 100))
            
            # Threshold visualization toggles
            if 'show_hand_distance_threshold' in settings and hasattr(self, 'show_hand_distance_threshold_toggle'):
                self.show_hand_distance_threshold_toggle.setChecked(settings['show_hand_distance_threshold'])
            if 'show_hand_velocity_zone' in settings and hasattr(self, 'show_hand_velocity_zone_toggle'):
                self.show_hand_velocity_zone_toggle.setChecked(settings['show_hand_velocity_zone'])
            
            # Hand Velocity Tracking settings
            if 'hand_velocity_enabled' in settings and hasattr(self, 'hand_velocity_enabled_toggle'):
                self.hand_velocity_enabled_toggle.setChecked(settings['hand_velocity_enabled'])
            if 'hand_velocity_threshold' in settings and hasattr(self, 'hand_velocity_threshold_slider'):
                self.hand_velocity_threshold_slider.setValue(int(settings['hand_velocity_threshold'] * 100))
            if 'hand_velocity_confidence_reduction' in settings and hasattr(self, 'hand_velocity_confidence_reduction_slider'):
                self.hand_velocity_confidence_reduction_slider.setValue(int(settings['hand_velocity_confidence_reduction'] * 100))
            if 'hand_velocity_detection_radius' in settings and hasattr(self, 'hand_velocity_detection_radius_slider'):
                self.hand_velocity_detection_radius_slider.setValue(int(settings['hand_velocity_detection_radius'] * 100))
            if 'hand_velocity_distance_reduction' in settings and hasattr(self, 'hand_velocity_distance_reduction_slider'):
                self.hand_velocity_distance_reduction_slider.setValue(int(settings['hand_velocity_distance_reduction'] * 100))
            if 'hand_velocity_ignore_class' in settings and hasattr(self, 'hand_velocity_ignore_class_toggle'):
                self.hand_velocity_ignore_class_toggle.setChecked(settings['hand_velocity_ignore_class'])

        def _apply_new3d_tracker_settings(self, settings: dict):
            """Apply New 3D Kalman tracker-specific settings"""
            # Kalman Filter settings
            if 'kalman_process_noise_pos' in settings and hasattr(self, 'kalman_process_noise_pos_slider'):
                self.kalman_process_noise_pos_slider.setValue(int(settings['kalman_process_noise_pos'] * 100))
            if 'kalman_process_noise_vel' in settings and hasattr(self, 'kalman_process_noise_vel_slider'):
                self.kalman_process_noise_vel_slider.setValue(int(settings['kalman_process_noise_vel'] * 100))
            if 'kalman_measurement_noise' in settings and hasattr(self, 'kalman_measurement_noise_slider'):
                self.kalman_measurement_noise_slider.setValue(int(settings['kalman_measurement_noise'] * 100))
            if 'kalman_max_prediction_time' in settings and hasattr(self, 'kalman_max_prediction_time_slider'):
                self.kalman_max_prediction_time_slider.setValue(int(settings['kalman_max_prediction_time'] * 10))
            if 'kalman_velocity_smoothing' in settings and hasattr(self, 'kalman_velocity_smoothing_slider'):
                self.kalman_velocity_smoothing_slider.setValue(int(settings['kalman_velocity_smoothing'] * 100))
            
            # Association settings
            if 'assoc_max_distance' in settings and hasattr(self, 'assoc_max_distance_slider'):
                self.assoc_max_distance_slider.setValue(int(settings['assoc_max_distance'] * 100))
            if 'assoc_iou_threshold' in settings and hasattr(self, 'assoc_iou_threshold_slider'):
                self.assoc_iou_threshold_slider.setValue(int(settings['assoc_iou_threshold'] * 100))
            if 'assoc_color_weight' in settings and hasattr(self, 'assoc_color_weight_slider'):
                self.assoc_color_weight_slider.setValue(int(settings['assoc_color_weight'] * 100))
            if 'assoc_spatial_weight' in settings and hasattr(self, 'assoc_spatial_weight_slider'):
                self.assoc_spatial_weight_slider.setValue(int(settings['assoc_spatial_weight'] * 100))
            if 'assoc_max_missed_frames' in settings and hasattr(self, 'assoc_max_missed_frames_slider'):
                self.assoc_max_missed_frames_slider.setValue(settings['assoc_max_missed_frames'])
            
            # State management settings
            if 'state_min_hits_to_confirm' in settings and hasattr(self, 'state_min_hits_to_confirm_slider'):
                self.state_min_hits_to_confirm_slider.setValue(settings['state_min_hits_to_confirm'])
            if 'state_max_age' in settings and hasattr(self, 'state_max_age_slider'):
                self.state_max_age_slider.setValue(settings['state_max_age'])
            if 'state_confidence_decay' in settings and hasattr(self, 'state_confidence_decay_slider'):
                self.state_confidence_decay_slider.setValue(int(settings['state_confidence_decay'] * 100))
            if 'state_min_confidence' in settings and hasattr(self, 'state_min_confidence_slider'):
                self.state_min_confidence_slider.setValue(int(settings['state_min_confidence'] * 100))
            
            # Audio indicators
            if 'tc_sound_on_catch' in settings and hasattr(self, 'new3d_sound_on_catch_toggle'):
                self.new3d_sound_on_catch_toggle.setChecked(settings['tc_sound_on_catch'])
            if 'tc_sound_on_throw' in settings and hasattr(self, 'new3d_sound_on_throw_toggle'):
                self.new3d_sound_on_throw_toggle.setChecked(settings['tc_sound_on_throw'])
            if 'tc_name_on_catch' in settings and hasattr(self, 'new3d_name_on_catch_toggle'):
                self.new3d_name_on_catch_toggle.setChecked(settings['tc_name_on_catch'])
            if 'tc_name_on_throw' in settings and hasattr(self, 'new3d_name_on_throw_toggle'):
                self.new3d_name_on_throw_toggle.setChecked(settings['tc_name_on_throw'])

        def _apply_2d_tracker_settings(self, settings: dict):
            """Apply 2D tracker-specific settings"""
            # Placeholder for 2D tracker settings when implemented
            pass

        def _apply_ui_state(self, settings: dict):
            """Restore UI collapsed states"""
            if 'collapsed_camera' in settings and hasattr(self, 'camera_section'):
                if settings['collapsed_camera'] != self.camera_section.is_collapsed:
                    self.camera_section.toggle_collapsed()
            if 'collapsed_yolo' in settings and hasattr(self, 'yolo_section'):
                if settings['collapsed_yolo'] != self.yolo_section.is_collapsed:
                    self.yolo_section.toggle_collapsed()
            if 'collapsed_pose' in settings and hasattr(self, 'pose_section'):
                if settings['collapsed_pose'] != self.pose_section.is_collapsed:
                    self.pose_section.toggle_collapsed()
            if 'collapsed_throw_catch' in settings and hasattr(self, 'throw_catch_section'):
                if settings['collapsed_throw_catch'] != self.throw_catch_section.is_collapsed:
                    self.throw_catch_section.toggle_collapsed()
            if 'collapsed_color_tracker_weights' in settings and hasattr(self, 'color_tracker_weights_section'):
                if settings['collapsed_color_tracker_weights'] != self.color_tracker_weights_section.is_collapsed:
                    self.color_tracker_weights_section.toggle_collapsed()
            if 'collapsed_override_detection' in settings and hasattr(self, 'override_detection_section'):
                if settings['collapsed_override_detection'] != self.override_detection_section.is_collapsed:
                    self.override_detection_section.toggle_collapsed()
            if 'collapsed_held_color_blob' in settings and hasattr(self, 'held_color_blob_section'):
                if settings['collapsed_held_color_blob'] != self.held_color_blob_section.is_collapsed:
                    self.held_color_blob_section.toggle_collapsed()
            if 'collapsed_trajectory' in settings and hasattr(self, 'trajectory_section'):
                if settings['collapsed_trajectory'] != self.trajectory_section.is_collapsed:
                    self.trajectory_section.toggle_collapsed()
            if 'collapsed_hand_velocity' in settings and hasattr(self, 'hand_velocity_section'):
                if settings['collapsed_hand_velocity'] != self.hand_velocity_section.is_collapsed:
                    self.hand_velocity_section.toggle_collapsed()
            if 'collapsed_ball_profiles' in settings and hasattr(self, 'ball_profiles_section'):
                if settings['collapsed_ball_profiles'] != self.ball_profiles_section.is_collapsed:
                    self.ball_profiles_section.toggle_collapsed()
            if 'collapsed_new3d_ball_profiles' in settings and hasattr(self, 'new3d_ball_profiles_section'):
                if settings['collapsed_new3d_ball_profiles'] != self.new3d_ball_profiles_section.is_collapsed:
                    self.new3d_ball_profiles_section.toggle_collapsed()

        def load_settings(self):
            """Load settings for current tracker"""
            settings = self.settings_manager.load_settings(self.current_tracker)
            if settings:
                self._loading_settings = True
                self.apply_settings(settings)
                self._loading_settings = False
                print(f"✅ Settings loaded for {self.current_tracker}")
                
                # Send tracker switch command to engine FIRST
                print(f"📤 Sending tracker switch command to engine: {self.current_tracker}")
                command = juggler_pb2.CommandRequest()
                command.type = juggler_pb2.CommandRequest.CommandType.SET_TRACKER_TYPE
                command.tracker_type = self.current_tracker
                
                try:
                    response = self.zmq_client.send_command(command)
                    if response.success:
                        print(f"✅ Engine switched to {self.current_tracker}: {response.message}")
                    else:
                        print(f"❌ Failed to switch tracker in engine: {response.message}")
                except Exception as e:
                    print(f"❌ Error switching tracker in engine: {e}")
                
                # Send all settings to engine
                print("📤 Sending loaded settings to engine...")
                import time
                time.sleep(0.5)  # Brief delay to ensure engine is ready
                self._send_all_settings_to_engine(settings)
                print("✅ All settings sent to engine")
            else:
                print(f"ℹ️ No saved settings found for {self.current_tracker}")

        def save_settings(self):
            """Save current settings"""
            if self._loading_settings:
                return
            settings = self.get_current_settings()
            self.settings_manager.save_settings(self.current_tracker, settings)
            print(f"💾 Settings saved for {self.current_tracker}")

        def start_camera_feed(self):
            """Start the camera feed with selected settings"""
            try:
                selected_profile = self.camera_settings_combo.currentData()
                selected_resolution = self.resolution_combo.currentText()
                selected_fps = self.fps_combo.currentData()
                
                width, height = map(int, selected_resolution.split(' x '))
                settings_file_path = f"camera_settings/{selected_profile}.json"
                
                command = juggler_pb2.CommandRequest()
                command.type = juggler_pb2.CommandRequest.CommandType.CAMERA_START
                command.camera_settings_file = settings_file_path
                command.camera_width = width
                command.camera_height = height
                command.camera_fps = selected_fps
                
                response = self.zmq_client.send_command(command)
                if response.success:
                    print(f"✅ Camera started: {selected_resolution} @ {selected_fps} FPS")
                    if not self._loading_settings:
                        self.save_settings()
                else:
                    print(f"❌ Failed to start camera: {response.message}")
                    QMessageBox.critical(self, "Error", f"Failed to start camera:\n{response.message}")
            except Exception as e:
                print(f"❌ Error starting camera: {e}")
                QMessageBox.critical(self, "Error", f"Error starting camera:\n{str(e)}")

        def stop_camera_feed(self):
            """Stop the camera feed"""
            try:
                command = juggler_pb2.CommandRequest()
                command.type = juggler_pb2.CommandRequest.CommandType.CAMERA_STOP
                
                response = self.zmq_client.send_command(command)
                if response.success:
                    print(f"✅ Camera stopped")
                else:
                    print(f"❌ Failed to stop camera: {response.message}")
                    QMessageBox.critical(self, "Error", f"Failed to stop camera:\n{response.message}")
            except Exception as e:
                print(f"❌ Error stopping camera: {e}")
                QMessageBox.critical(self, "Error", f"Error stopping camera:\n{str(e)}")
        def update_ir_status(self, is_active: bool):
            """Update IR projector status indicator"""
            if hasattr(self, 'ir_status_label'):
                if is_active:
                    self.ir_status_label.setText("🔆 IR Projector: Active")
                    self.ir_status_label.setStyleSheet("color: #4caf50; font-weight: bold;")
                else:
                    self.ir_status_label.setText("🔆 IR Projector: Inactive")
                    self.ir_status_label.setStyleSheet("color: #f44336;")

        def populate_camera_settings(self):
            """Populate camera settings dropdown"""
            self.camera_settings_combo.clear()
            camera_settings_dir = os.path.join("..", "camera_settings")
            if os.path.exists(camera_settings_dir):
                for filename in os.listdir(camera_settings_dir):
                    if filename.endswith('.json'):
                        display_name = filename[:-5].replace('_', ' ').title()
                        profile_name = filename[:-5]
                        self.camera_settings_combo.addItem(display_name, profile_name)
            if self.camera_settings_combo.count() == 0:
                self.camera_settings_combo.addItem("Default", "default")
            default_index = self.camera_settings_combo.findData("default")
            if default_index >= 0:
                self.camera_settings_combo.setCurrentIndex(default_index)

        def on_camera_profile_changed(self):
            """Handle camera profile change"""
            if not self._loading_settings:
                self.save_settings()

        def on_resolution_changed(self):
            """Handle resolution change"""
            current_resolution = self.resolution_combo.currentText()
            if current_resolution in self.resolution_fps_map:
                fps_options = self.resolution_fps_map[current_resolution]
                self.fps_combo.clear()
                for fps in fps_options:
                    self.fps_combo.addItem(f"{fps} FPS", fps)
                default_index = self.fps_combo.findText("60 FPS")
                if default_index >= 0:
                    self.fps_combo.setCurrentIndex(default_index)
                elif self.fps_combo.count() > 0:
                    self.fps_combo.setCurrentIndex(0)

        def on_fps_changed(self):
            """Handle FPS change"""
            if not self._loading_settings:
                self.save_settings()

        def toggle_dnn_tracker(self):
            """Toggle YOLO ball detection"""
            is_enabled = self.use_dnn_tracker_toggle.isChecked()
            self.use_dnn_tracker_toggle.setText("Enable YOLO Ball Detection" if is_enabled else "YOLO Ball Detection DISABLED")
            self.udp_client.send_setting('enable_ball_detection', 1 if is_enabled else 0)
            print(f"✅ YOLO ball detection {'enabled' if is_enabled else 'disabled'}")
            if not self._loading_settings:
                self.save_settings()

        def toggle_pose_model(self):
            """Toggle pose model"""
            is_enabled = self.pose_model_toggle.isChecked()
            command = juggler_pb2.CommandRequest(
                type=juggler_pb2.CommandRequest.CommandType.SET_POSE_MODEL_ENABLED,
                pose_model_enabled=is_enabled
            )
            try:
                response = self.zmq_client.send_command(command)
                if response.success:
                    print(f"✅ Pose model {'enabled' if is_enabled else 'disabled'}")
                    if not self._loading_settings:
                        self.save_settings()
                else:
                    print(f"❌ Failed to toggle pose model: {response.message}")
            except Exception as e:
                print(f"❌ Error toggling pose model: {e}")

        def toggle_depth_sensor(self):
            """Toggle depth sensor"""
            is_enabled = self.depth_sensor_toggle.isChecked()
            command = juggler_pb2.CommandRequest()
            command.type = juggler_pb2.CommandRequest.CommandType.SET_DEPTH_SENSOR_ENABLED
            command.depth_sensor_enabled = is_enabled
            
            try:
                response = self.zmq_client.send_command(command)
                if response.success:
                    print(f"✅ Depth sensor {'enabled' if is_enabled else 'disabled'}")
                    if not self._loading_settings:
                        self.save_settings()
                else:
                    print(f"❌ Failed to toggle depth sensor: {response.message}")
                    QMessageBox.warning(self, "Depth Sensor Toggle Failed",
                                      f"Failed to toggle depth sensor:\n{response.message}")
            except Exception as e:
                print(f"❌ Error toggling depth sensor: {e}")
                QMessageBox.critical(self, "Error", f"Error toggling depth sensor:\n{str(e)}")

        def test_catch_sound(self):
            """Play test sound for catch events"""
            self.play_system_sound(frequency=800, duration=100)
            print("🔊 Playing catch test sound (800 Hz)")
        def populate_resolution_options(self):
            """Populate resolution dropdown with supported resolutions"""
            self.resolution_combo.clear()
            for resolution in self.resolution_fps_map.keys():
                self.resolution_combo.addItem(resolution)
            default_index = self.resolution_combo.findText("640 x 480")
            if default_index >= 0:
                self.resolution_combo.setCurrentIndex(default_index)

        def populate_fps_options(self):
            """Populate FPS dropdown based on selected resolution"""
            current_resolution = self.resolution_combo.currentText()
            if current_resolution in self.resolution_fps_map:
                fps_options = self.resolution_fps_map[current_resolution]
                self.fps_combo.clear()
                for fps in fps_options:
                    self.fps_combo.addItem(f"{fps} FPS", fps)
                default_index = self.fps_combo.findText("60 FPS")
                if default_index >= 0:
                    self.fps_combo.setCurrentIndex(default_index)
                elif self.fps_combo.count() > 0:
                    self.fps_combo.setCurrentIndex(0)


        def test_throw_sound(self):
            """Play test sound for throw events"""
            self.play_system_sound(frequency=1200, duration=100)
            print("🔊 Playing throw test sound (1200 Hz)")

        def play_system_sound(self, frequency=1000, duration=100):
            """Play a simple beep sound"""
            def play_in_thread():
                try:
                    system = platform.system()
                    if system == "Linux":
                        subprocess.run(['paplay', '--raw', '/dev/stdin'],
                                     input=self.generate_sine_wave(frequency, duration),
                                     timeout=1, check=False)
                    elif system == "Darwin":
                        subprocess.run(['afplay', '/System/Library/Sounds/Pop.aiff'],
                                     timeout=1, check=False)
                    elif system == "Windows":
                        import winsound
                        winsound.Beep(frequency, duration)
                except Exception as e:
                    print(f"⚠️ Could not play sound: {e}")
                    print(f"\a")
            threading.Thread(target=play_in_thread, daemon=True).start()

        def generate_sine_wave(self, frequency=1000, duration=100):
            """Generate sine wave for audio"""
            sample_rate = 44100
            num_samples = int(sample_rate * duration / 1000)
            t = np.linspace(0, duration / 1000, num_samples, False)
            wave = np.sin(2 * np.pi * frequency * t)
            audio = (wave * 32767).astype(np.int16)
            return audio.tobytes()

        def test_catch_name(self):
            """Play test sound for catch name announcement"""
            # Play a sample color name (e.g., "red")
            self.play_color_name("red")
            print("🔊 Playing catch name test sound (red)")

        def test_throw_name(self):
            """Play test sound for throw name announcement"""
            # Play a sample color name (e.g., "blue")
            self.play_color_name("blue")
            print("🔊 Playing throw name test sound (blue)")

        def play_color_name(self, color_name: str):
            """Play audio file for color name"""
            def play_in_thread():
                try:
                    # Look for audio file in hub/audio/color_names directory
                    audio_file = os.path.join(os.path.dirname(__file__), "..", "audio", "color_names", f"{color_name.lower()}.mp3")
                    
                    if not os.path.exists(audio_file):
                        print(f"⚠️ Audio file not found: {audio_file}")
                        # Fall back to system beep
                        self.play_system_sound(frequency=1000, duration=200)
                        return
                    
                    system = platform.system()
                    if system == "Linux":
                        # Try mpg123 first (recommended for MP3), fall back to ffplay
                        try:
                            subprocess.run(['mpg123', '-q', audio_file], timeout=2, check=True)
                        except (subprocess.CalledProcessError, FileNotFoundError):
                            subprocess.run(['ffplay', '-nodisp', '-autoexit', '-loglevel', 'quiet', audio_file], timeout=2, check=False)
                    elif system == "Darwin":
                        subprocess.run(['afplay', audio_file], timeout=2, check=False)
                    elif system == "Windows":
                        # Windows Media Player can handle MP3
                        subprocess.run(['powershell', '-c', f'(New-Object Media.SoundPlayer "{audio_file}").PlaySync()'], timeout=2, check=False)
                except Exception as e:
                    print(f"⚠️ Could not play color name audio: {e}")
                    # Fall back to system beep
                    self.play_system_sound(frequency=1000, duration=200)
            
            threading.Thread(target=play_in_thread, daemon=True).start()

        def auto_calibrate_hues(self):
            """Auto-calibrate hue ranges for all ball profiles"""
            print("🎨 Auto-calibrating hue ranges for all ball profiles...")
            # This would typically analyze the current ball_settings.json and adjust hue ranges
            # For now, just reload the profiles
            if hasattr(self, 'tracker_3d_sections'):
                # Trigger a reload of ball profiles in the UI
                print("✅ Ball profiles reloaded")
                QMessageBox.information(self, "Auto-Calibration",
                                      "Auto-calibration feature is not yet implemented.\n"
                                      "Please use manual calibration for each ball color.")
            
        def start_color_calibration(self, ball_name: str):
            """Start color calibration for a specific ball"""
            print(f"🎨 Starting color calibration for {ball_name}")
            # This would typically open a calibration dialog or mode
            # For now, just show a message
            QMessageBox.information(self, "Color Calibration",
                                  f"Color calibration for '{ball_name}' ball.\n\n"
                                  f"To calibrate:\n"
                                  f"1. Select '{ball_name}' from the Color Profile dropdown in the main UI\n"
                                  f"2. Click 'Set Color Profile'\n"
                                  f"3. Click on a {ball_name} ball in the video feed")

        def toggle_ball_tracking(self, ball_name: str, enabled: bool):
            """Toggle tracking for specific ball"""
            print(f"🔄 {'Enabling' if enabled else 'Disabling'} tracking for {ball_name}")
            self.udp_client.send_setting(f"track_{ball_name}", 1 if enabled else 0)
            if not self._loading_settings:
                self.save_settings()

        def save_ball_settings(self):
            """Save ball profiles to ball_settings.json"""
            # This is handled by the ball profile sections
            pass

        def reload_ball_profiles(self):
            """Reload ball profiles from file and update UI"""
            import json
            import os
            
            print(f"🔄 Reloading ball profiles for {self.current_tracker}")
            
            if self.current_tracker == "new_3d":
                # Reload New 3D tracker profiles
                settings_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "calibration_settings_new3d.json")
                settings_path = os.path.normpath(settings_path)
                
                try:
                    with open(settings_path, 'r') as f:
                        settings_data = json.load(f)
                        color_profiles = settings_data.get('color_profiles', [])
                    
                    print(f"✅ Loaded {len(color_profiles)} profiles from {settings_path}")
                    
                    # Update UI labels for each profile
                    if hasattr(self, 'new3d_ball_calibration_labels'):
                        for profile in color_profiles:
                            ball_name = profile['name']
                            if ball_name in self.new3d_ball_calibration_labels:
                                labels = self.new3d_ball_calibration_labels[ball_name]
                                
                                # Update hue label
                                avg_hue = profile.get('avg_hue', -1.0)
                                if avg_hue >= 0:
                                    labels['hue'].setText(f"{avg_hue:.1f}°")
                                    labels['hue'].setStyleSheet("color: #4CAF50; font-weight: bold;")
                                else:
                                    labels['hue'].setText("Not calibrated")
                                    labels['hue'].setStyleSheet("color: #f44336;")
                                
                                # Update saturation label
                                avg_sat = profile.get('avg_saturation', -1.0)
                                if avg_sat >= 0:
                                    labels['saturation'].setText(f"{avg_sat:.1f}")
                                    labels['saturation'].setStyleSheet("color: #4CAF50; font-weight: bold;")
                                else:
                                    labels['saturation'].setText("Not calibrated")
                                    labels['saturation'].setStyleSheet("color: #f44336;")
                                
                                print(f"   ✅ Updated UI for {ball_name}: H={avg_hue:.1f}° S={avg_sat:.1f}")
                    
                    print("✅ Ball profiles UI updated successfully")
                    
                except Exception as e:
                    print(f"❌ Error reloading New 3D profiles: {e}")
            
            elif self.current_tracker == "depth_based":
                # Reload 3D tracker profiles (if needed in the future)
                print("ℹ️ 3D tracker profile reload not yet implemented")
                pass
            
            else:
                print(f"ℹ️ No profile reload needed for {self.current_tracker}")

        def on_ball_profile_changed(self):
            """Handle ball profile change"""
            if not self._loading_settings:
                self.save_settings()

        def update_setting(self, key: str, value: Any):
            """Update a setting and send to engine"""
            self.udp_client.send_setting(key, value)
            if not self._loading_settings:
                self.save_settings()

        def _create_slider_widget(self, parent_layout, row, label_text, tooltip_text,
                                  range_min, range_max, initial_value,
                                  update_func, is_float=False):
            """Helper to create labeled slider with value display"""
            label = QLabel(label_text)
            label.setToolTip(tooltip_text)
            parent_layout.addWidget(label, row, 0)
            
            slider = QSlider(Qt.Orientation.Horizontal)
            slider.setRange(range_min, range_max)
            slider.setValue(initial_value)
            parent_layout.addWidget(slider, row, 1)

            value_label = QLabel()
            value_label.setMinimumWidth(40)
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
            on_value_changed(initial_value)
            
            return slider, value_label

        def _safe_get_slider_value(self, slider, default_value):
            """Safely get slider value, handling deleted Qt objects"""
            try:
                if slider is not None:
                    return slider.value()
            except RuntimeError:
                pass
            return default_value

        def _calculate_hsv_range_from_rgb(self, rgb):
            """Calculate HSV range from RGB color"""
            import cv2
            rgb_array = np.uint8([[rgb]])
            hsv_array = cv2.cvtColor(rgb_array, cv2.COLOR_RGB2HSV)
            h, s, v = hsv_array[0][0]
            
            hue_tolerance = 15
            min_hue = float(max(0, h - hue_tolerance))
            max_hue = float(min(180, h + hue_tolerance))
            
            if h < 15:
                min_hue = float(max(0, 180 - (15 - h)))
                max_hue = float(h + hue_tolerance)
            elif h > 165:
                min_hue = float(h - hue_tolerance)
                max_hue = float(min(15, h + hue_tolerance - 180))
            
            min_s = float(max(30, s - 80))
            max_s = 255.0
            min_v = float(max(30, v - 80))
            max_v = 255.0
            
            return [min_hue, min_s, min_v], [max_hue, max_s, max_v]
        def _send_all_settings_to_engine(self, settings: dict):
            """Send settings to engine via UDP, filtered by tracker type"""
            # Determine active tracker type
            tracker_type = settings.get('tracker_type', self.current_tracker)
            
            # Send common settings (always sent regardless of tracker type)
            if 'enable_ball_detection' in settings:
                self.udp_client.send_setting('enable_ball_detection', 1 if settings['enable_ball_detection'] else 0)
            
            # YOLO settings (common)
            if 'ball_confidence_threshold' in settings:
                self.udp_client.send_setting('ball_confidence_threshold', settings['ball_confidence_threshold'])
            if 'ball_held_confidence_threshold' in settings:
                self.udp_client.send_setting('ball_held_confidence_threshold', settings['ball_held_confidence_threshold'])
            if 'nms_threshold' in settings:
                self.udp_client.send_setting('nms_threshold', settings['nms_threshold'])
            if 'show_raw_yolo_detections' in settings:
                self.udp_client.send_setting('show_raw_yolo_detections', 1 if settings['show_raw_yolo_detections'] else 0)
            
            # Send tracker-specific settings based on active tracker
            if tracker_type == "depth_based":
                self._send_3d_tracker_settings(settings)
            elif tracker_type == "simple_2d":
                self._send_2d_tracker_settings(settings)
        
        def _send_3d_tracker_settings(self, settings: dict):
            """Send 3D tracker-specific settings to engine"""
            # Ball state detection
            if 'undetected_near_hand_threshold' in settings:
                self.udp_client.send_setting('undetected_near_hand_threshold', settings['undetected_near_hand_threshold'])
            if 'min_frames_for_state_change' in settings:
                self.udp_client.send_setting('min_frames_for_state_change', settings['min_frames_for_state_change'])
            if 'hand_distance_threshold' in settings:
                self.udp_client.send_setting('hand_distance_threshold', settings['hand_distance_threshold'])
            if 'min_throw_distance' in settings:
                self.udp_client.send_setting('min_throw_distance', settings['min_throw_distance'])
            if 'min_frames_before_catch' in settings:
                self.udp_client.send_setting('min_frames_before_catch', settings['min_frames_before_catch'])
            if 'max_tracker_distance_per_frame' in settings:
                self.udp_client.send_setting('max_tracker_distance_per_frame', settings['max_tracker_distance_per_frame'])
            if 'tc_sound_on_catch' in settings:
                self.udp_client.send_setting('tc_sound_on_catch', 1 if settings['tc_sound_on_catch'] else 0)
            if 'tc_sound_on_throw' in settings:
                self.udp_client.send_setting('tc_sound_on_throw', 1 if settings['tc_sound_on_throw'] else 0)
            if 'tc_name_on_catch' in settings:
                self.udp_client.send_setting('tc_name_on_catch', 1 if settings['tc_name_on_catch'] else 0)
            if 'tc_name_on_throw' in settings:
                self.udp_client.send_setting('tc_name_on_throw', 1 if settings['tc_name_on_throw'] else 0)
            if 'ignore_class' in settings:
                self.udp_client.send_setting('ignore_class', 1 if settings['ignore_class'] else 0)
            
            # Color tracker weights
            if 'temporal_consistency_bonus' in settings:
                self.udp_client.send_setting('temporal_consistency_bonus', settings['temporal_consistency_bonus'])
            if 'spatial_threshold' in settings:
                self.udp_client.send_setting('spatial_threshold', settings['spatial_threshold'])
            if 'color_sample_radius' in settings:
                self.udp_client.send_setting('color_sample_radius', settings['color_sample_radius'])
            if 'max_euclidean_distance' in settings:
                self.udp_client.send_setting('max_euclidean_distance', settings['max_euclidean_distance'])
            if 'min_euclidean_color_score' in settings:
                self.udp_client.send_setting('min_euclidean_color_score', settings['min_euclidean_color_score'])
            if 'max_depth_jump_strict' in settings:
                self.udp_client.send_setting('max_depth_jump_strict', settings['max_depth_jump_strict'])
            if 'min_color_confidence_override' in settings:
                self.udp_client.send_setting('min_color_confidence_override', settings['min_color_confidence_override'])
            if 'min_ball_separation' in settings:
                self.udp_client.send_setting('min_ball_separation', settings['min_ball_separation'])
            if 'min_hand_change_distance' in settings:
                self.udp_client.send_setting('min_hand_change_distance', settings['min_hand_change_distance'])
            
            # Override detection
            if 'override_ball_confidence_threshold' in settings:
                self.udp_client.send_setting('override_ball_confidence_threshold', settings['override_ball_confidence_threshold'])
            if 'override_ball_color_threshold' in settings:
                self.udp_client.send_setting('override_ball_color_threshold', settings['override_ball_color_threshold'])
            if 'override_ball_held_confidence_threshold' in settings:
                self.udp_client.send_setting('override_ball_held_confidence_threshold', settings['override_ball_held_confidence_threshold'])
            if 'override_ball_held_color_threshold' in settings:
                self.udp_client.send_setting('override_ball_held_color_threshold', settings['override_ball_held_color_threshold'])
            
            # Held color blob
            if 'held_color_search_radius' in settings:
                self.udp_client.send_setting('held_color_search_radius', settings['held_color_search_radius'])
            if 'held_color_min_score' in settings:
                self.udp_client.send_setting('held_color_min_score', settings['held_color_min_score'])
            if 'held_color_max_distance' in settings:
                self.udp_client.send_setting('held_color_max_distance', settings['held_color_max_distance'])
            
            # Trajectory settings
            if 'traj_gravity' in settings:
                self.udp_client.send_setting('traj_gravity', settings['traj_gravity'])
            if 'traj_time_step' in settings:
                self.udp_client.send_setting('traj_time_step', settings['traj_time_step'])
            if 'traj_max_time' in settings:
                self.udp_client.send_setting('traj_max_time', settings['traj_max_time'])
            if 'traj_search_radius' in settings:
                self.udp_client.send_setting('traj_search_radius', settings['traj_search_radius'])
            if 'traj_min_points_for_prediction' in settings:
                self.udp_client.send_setting('traj_min_points_for_prediction', settings['traj_min_points_for_prediction'])
            if 'traj_color_match_threshold' in settings:
                self.udp_client.send_setting('traj_color_match_threshold', settings['traj_color_match_threshold'])
            if 'traj_yolo_confidence_threshold' in settings:
                self.udp_client.send_setting('traj_yolo_confidence_threshold', settings['traj_yolo_confidence_threshold'])
            if 'throw_yolo_confidence_threshold' in settings:
                self.udp_client.send_setting('throw_yolo_confidence_threshold', settings['throw_yolo_confidence_threshold'])
            if 'traj_velocity_estimation_time' in settings:
                self.udp_client.send_setting('traj_velocity_estimation_time', settings['traj_velocity_estimation_time'])
            if 'traj_max_search_distance' in settings:
                self.udp_client.send_setting('traj_max_search_distance', settings['traj_max_search_distance'])
            
            # Hand velocity tracking
            if 'hand_velocity_enabled' in settings:
                self.udp_client.send_setting('hand_velocity_enabled', 1 if settings['hand_velocity_enabled'] else 0)
            if 'hand_velocity_threshold' in settings:
                self.udp_client.send_setting('hand_velocity_threshold', settings['hand_velocity_threshold'])
            if 'hand_velocity_confidence_reduction' in settings:
                self.udp_client.send_setting('hand_velocity_confidence_reduction', settings['hand_velocity_confidence_reduction'])
            if 'hand_velocity_detection_radius' in settings:
                self.udp_client.send_setting('hand_velocity_detection_radius', settings['hand_velocity_detection_radius'])
            if 'hand_velocity_distance_reduction' in settings:
                self.udp_client.send_setting('hand_velocity_distance_reduction', settings['hand_velocity_distance_reduction'])
            if 'hand_velocity_ignore_class' in settings:
                self.udp_client.send_setting('hand_velocity_ignore_class', 1 if settings['hand_velocity_ignore_class'] else 0)
            
            # Visualization toggles
            if 'show_hand_distance_threshold' in settings:
                self.udp_client.send_setting('show_hand_distance_threshold', 1 if settings['show_hand_distance_threshold'] else 0)
            if 'show_hand_velocity_zone' in settings:
                self.udp_client.send_setting('show_hand_velocity_zone', 1 if settings['show_hand_velocity_zone'] else 0)
        def _send_new3d_tracker_settings(self, settings: dict):
            """Send New 3D Kalman tracker-specific settings to engine"""
            # Kalman Filter settings
            if 'kalman_process_noise_pos' in settings:
                self.udp_client.send_setting('kalman_process_noise_pos', settings['kalman_process_noise_pos'])
            if 'kalman_process_noise_vel' in settings:
                self.udp_client.send_setting('kalman_process_noise_vel', settings['kalman_process_noise_vel'])
            if 'kalman_measurement_noise' in settings:
                self.udp_client.send_setting('kalman_measurement_noise', settings['kalman_measurement_noise'])
            if 'kalman_max_prediction_time' in settings:
                self.udp_client.send_setting('kalman_max_prediction_time', settings['kalman_max_prediction_time'])
            if 'kalman_velocity_smoothing' in settings:
                self.udp_client.send_setting('kalman_velocity_smoothing', settings['kalman_velocity_smoothing'])
            
            # Association settings
            if 'assoc_max_distance' in settings:
                self.udp_client.send_setting('assoc_max_distance', settings['assoc_max_distance'])
            if 'assoc_iou_threshold' in settings:
                self.udp_client.send_setting('assoc_iou_threshold', settings['assoc_iou_threshold'])
            if 'assoc_color_weight' in settings:
                self.udp_client.send_setting('assoc_color_weight', settings['assoc_color_weight'])
            if 'assoc_spatial_weight' in settings:
                self.udp_client.send_setting('assoc_spatial_weight', settings['assoc_spatial_weight'])
            if 'assoc_max_missed_frames' in settings:
                self.udp_client.send_setting('assoc_max_missed_frames', settings['assoc_max_missed_frames'])
            
            # State management settings
            if 'state_min_hits_to_confirm' in settings:
                self.udp_client.send_setting('state_min_hits_to_confirm', settings['state_min_hits_to_confirm'])
            if 'state_max_age' in settings:
                self.udp_client.send_setting('state_max_age', settings['state_max_age'])
            if 'state_confidence_decay' in settings:
                self.udp_client.send_setting('state_confidence_decay', settings['state_confidence_decay'])
            if 'state_min_confidence' in settings:
                self.udp_client.send_setting('state_min_confidence', settings['state_min_confidence'])
            
            # Audio indicators
            if 'tc_sound_on_catch' in settings:
                self.udp_client.send_setting('tc_sound_on_catch', 1 if settings['tc_sound_on_catch'] else 0)
            if 'tc_sound_on_throw' in settings:
                self.udp_client.send_setting('tc_sound_on_throw', 1 if settings['tc_sound_on_throw'] else 0)
            if 'tc_name_on_catch' in settings:
                self.udp_client.send_setting('tc_name_on_catch', 1 if settings['tc_name_on_catch'] else 0)
            if 'tc_name_on_throw' in settings:
                self.udp_client.send_setting('tc_name_on_throw', 1 if settings['tc_name_on_throw'] else 0)
        
        
        def _send_2d_tracker_settings(self, settings: dict):
            """Send 2D tracker-specific settings to engine"""
            # Currently no 2D-specific settings
            # This method is a placeholder for future 2D tracker settings
            pass
