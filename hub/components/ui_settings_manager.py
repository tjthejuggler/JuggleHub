"""
Settings Manager for JuggleHub UI.
Handles loading, saving, and migration of calibration settings.
"""

import os
import json
from datetime import datetime
from typing import Dict, Any, Optional


class SettingsManager:
    """Manages settings persistence for different tracker types."""
    
    def __init__(self, config_dir: str = "hub/config"):
        """
        Initialize the settings manager.
        
        Args:
            config_dir: Directory where settings files are stored
        """
        self.config_dir = config_dir
        self.settings_3d_file = os.path.join(config_dir, "calibration_settings_3d.json")
        self.settings_new3d_file = os.path.join(config_dir, "calibration_settings_new3d.json")
        self.settings_2d_file = os.path.join(config_dir, "calibration_settings_2d.json")
        self.legacy_settings_file = os.path.join(config_dir, "calibration_settings.json")
        
        # Ensure config directory exists
        os.makedirs(config_dir, exist_ok=True)
    
    def get_settings_file(self, tracker_type: str) -> str:
        """
        Get the settings file path for a specific tracker type.
        
        Args:
            tracker_type: Either "depth_based" (3D), "new_3d" (New 3D Kalman), or "simple_2d" (2D)
            
        Returns:
            Path to the settings file
        """
        if tracker_type == "simple_2d":
            return self.settings_2d_file
        elif tracker_type == "new_3d":
            return self.settings_new3d_file
        else:
            return self.settings_3d_file
    
    def load_settings(self, tracker_type: str) -> Optional[Dict[str, Any]]:
        """
        Load settings for a specific tracker type.
        
        Args:
            tracker_type: Either "depth_based" (3D), "new_3d" (New 3D Kalman), or "simple_2d" (2D)
            
        Returns:
            Settings dictionary or None if file doesn't exist
        """
        settings_file = self.get_settings_file(tracker_type)
        
        # Try to load tracker-specific settings
        if os.path.exists(settings_file):
            try:
                with open(settings_file, 'r') as f:
                    settings = json.load(f)
                print(f"✅ Loaded {tracker_type} settings from {settings_file}")
                if 'saved_at' in settings:
                    print(f"   Saved at: {settings['saved_at']}")
                return settings
            except Exception as e:
                print(f"❌ Error loading {tracker_type} settings: {e}")
                return None
        
        # Try to migrate from legacy settings
        if os.path.exists(self.legacy_settings_file):
            print(f"ℹ️ No {tracker_type} settings found, attempting migration from legacy file")
            return self._migrate_legacy_settings(tracker_type)
        
        print(f"ℹ️ No saved settings found for {tracker_type}")
        return None
    
    def save_settings(self, tracker_type: str, settings: Dict[str, Any]) -> bool:
        """
        Save settings for a specific tracker type.
        
        Args:
            tracker_type: Either "depth_based" (3D), "new_3d" (New 3D Kalman), or "simple_2d" (2D)
            settings: Settings dictionary to save
            
        Returns:
            True if successful, False otherwise
        """
        settings_file = self.get_settings_file(tracker_type)
        settings['saved_at'] = datetime.now().isoformat()
        settings['tracker_type'] = tracker_type
        
        try:
            with open(settings_file, 'w') as f:
                json.dump(settings, f, indent=2)
            print(f"✅ Settings saved to {settings_file}")
            return True
        except Exception as e:
            print(f"❌ Error saving settings: {e}")
            return False
    
    def _migrate_legacy_settings(self, tracker_type: str) -> Optional[Dict[str, Any]]:
        """
        Migrate settings from legacy calibration_settings.json file.
        
        Args:
            tracker_type: Target tracker type for migration
            
        Returns:
            Migrated settings dictionary or None if migration fails
        """
        try:
            with open(self.legacy_settings_file, 'r') as f:
                legacy_settings = json.load(f)
            
            print(f"📦 Migrating legacy settings to {tracker_type} format")
            
            # For 3D tracker, keep all settings
            if tracker_type == "depth_based":
                migrated = legacy_settings.copy()
                migrated['migrated_from_legacy'] = True
                migrated['migration_date'] = datetime.now().isoformat()
                
                # Save migrated settings
                self.save_settings(tracker_type, migrated)
                print(f"✅ Legacy settings migrated to {self.settings_3d_file}")
                return migrated
            
            # For 2D tracker, only keep common settings
            elif tracker_type == "simple_2d":
                migrated = self._extract_common_settings(legacy_settings)
                migrated['migrated_from_legacy'] = True
                migrated['migration_date'] = datetime.now().isoformat()
                
                # Save migrated settings
                self.save_settings(tracker_type, migrated)
                print(f"✅ Common settings migrated to {self.settings_2d_file}")
                return migrated
            
        except Exception as e:
            print(f"❌ Error migrating legacy settings: {e}")
            return None
    
    def _extract_common_settings(self, settings: Dict[str, Any]) -> Dict[str, Any]:
        """
        Extract only common settings that apply to both 3D and 2D trackers.
        
        Args:
            settings: Full settings dictionary
            
        Returns:
            Dictionary containing only common settings
        """
        common_keys = [
            # Camera settings
            'camera_settings_profile',
            'resolution',
            'fps',
            'depth_sensor_enabled',
            'tracking_system',
            
            # YOLO settings
            'enable_ball_detection',
            'ball_confidence_threshold',
            'ball_held_confidence_threshold',
            'nms_threshold',
            'show_raw_yolo_detections',
            'ball_processing_density',
            
            # Pose model
            'pose_model_enabled',
            'pose_processing_density',
            
            # UI state
            'collapsed_camera',
            'collapsed_yolo',
            'collapsed_pose',
            
            # Visualization toggles
            'viz_show_raw_detections',
            'viz_show_hand_tracking',
            'viz_show_ball_states',
            'viz_show_skeleton',
            'viz_show_tracked_boxes',
            'viz_show_unmatched_detections',
            'viz_show_tails',
            'viz_hide_video_feed',
        ]
        
        common_settings = {}
        for key in common_keys:
            if key in settings:
                common_settings[key] = settings[key]
        
        return common_settings
    
    def get_default_settings(self, tracker_type: str) -> Dict[str, Any]:
        """
        Get default settings for a specific tracker type.
        
        Args:
            tracker_type: Either "depth_based" (3D), "new_3d" (New 3D Kalman), or "simple_2d" (2D)
            
        Returns:
            Dictionary with default settings
        """
        # Common defaults for both trackers
        defaults = {
            'camera_settings_profile': 'default',
            'resolution': '640 x 480',
            'fps': 60,
            'depth_sensor_enabled': True,
            'tracking_system': tracker_type,
            'enable_ball_detection': True,
            'ball_confidence_threshold': 0.25,
            'ball_held_confidence_threshold': 0.25,
            'nms_threshold': 0.50,
            'show_raw_yolo_detections': False,
            'ball_processing_density': 50,
            'pose_model_enabled': True,
            'pose_processing_density': 50,
            'collapsed_camera': False,
            'collapsed_yolo': False,
            'collapsed_pose': False,
        }
        
        # Add 3D-specific defaults
        if tracker_type == "depth_based":
            defaults.update({
                'undetected_near_hand_threshold': 0.20,
                'min_frames_for_state_change': 3,
                'hand_distance_threshold': 0.25,
                'min_throw_distance': 0.20,
                'min_frames_before_catch': 3,
                'ignore_class': False,
                'max_tracker_distance_per_frame': 0.50,
                'tc_sound_on_catch': False,
                'tc_sound_on_throw': False,
                'tc_name_on_catch': False,
                'tc_name_on_throw': False,
                'temporal_consistency_bonus': 0.25,
                'spatial_threshold': 0.40,
                'color_sample_radius': 1,
                'max_euclidean_distance': 0.15,
                'min_euclidean_color_score': 0.30,
                'max_depth_jump_strict': 0.20,
                'min_color_confidence_override': 0.35,
                'min_ball_separation': 0.15,
                'min_hand_change_distance': 0.25,
                'override_ball_confidence_threshold': 0.70,
                'override_ball_color_threshold': 0.80,
                'override_ball_held_confidence_threshold': 0.70,
                'override_ball_held_color_threshold': 0.80,
                'held_color_search_radius': 120,
                'held_color_min_score': 0.30,
                'held_color_max_distance': 0.25,
                'traj_gravity': 9.81,
                'traj_time_step': 0.033,
                'traj_max_time': 3.0,
                'traj_search_radius': 0.15,
                'traj_min_points_for_prediction': 3,
                'traj_color_match_threshold': 0.50,
                'traj_yolo_confidence_threshold': 0.70,
                'throw_yolo_confidence_threshold': 0.50,
                'traj_velocity_estimation_time': 0.10,
                'traj_max_search_distance': 0.50,
                'show_hand_distance_threshold': True,
                'show_hand_velocity_zone': False,
                'hand_velocity_enabled': True,
                'hand_velocity_threshold': 1.0,
                'hand_velocity_confidence_reduction': 0.3,
                'hand_velocity_detection_radius': 0.15,
                'hand_velocity_distance_reduction': 0.10,
                'hand_velocity_ignore_class': False,
                'collapsed_throw_catch': False,
                'collapsed_color_tracker_weights': False,
                'collapsed_override_detection': False,
                'collapsed_held_color_blob': False,
                'collapsed_trajectory': False,
                'collapsed_hand_velocity': False,
                'collapsed_ball_profiles': False,
            })
        
        # Add New 3D Kalman-specific defaults
        elif tracker_type == "new_3d":
            defaults.update({
                # Kalman Filter settings
                'kalman_process_noise_pos': 0.01,
                'kalman_process_noise_vel': 0.1,
                'kalman_measurement_noise': 0.05,
                'kalman_max_prediction_time': 1.0,
                'kalman_velocity_smoothing': 0.3,
                
                # Association settings
                'assoc_max_distance': 0.30,
                'assoc_iou_threshold': 0.3,
                'assoc_color_weight': 0.4,
                'assoc_spatial_weight': 0.6,
                'assoc_max_missed_frames': 5,
                
                # State management settings
                'state_min_hits_to_confirm': 3,
                'state_max_age': 10,
                'state_confidence_decay': 0.95,
                'state_min_confidence': 0.3,
                
                # UI collapsed states
                'collapsed_new3d_kalman': False,
                'collapsed_new3d_association': False,
                'collapsed_new3d_state': False,
            })
        
        return defaults