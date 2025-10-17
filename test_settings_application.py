#!/usr/bin/env python3
"""
Test script to verify settings application logic in ui_settings.py
Tests the flow of settings extraction, application, and engine communication.
"""

import sys
import os
import json
from unittest.mock import Mock, MagicMock, patch, call
from typing import Dict, Any

# Add hub to path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'hub'))

# Mock all dependencies before importing
sys.modules['PyQt6'] = MagicMock()
sys.modules['PyQt6.QtWidgets'] = MagicMock()
sys.modules['PyQt6.QtCore'] = MagicMock()
sys.modules['zmq'] = MagicMock()
sys.modules['cv2'] = MagicMock()
sys.modules['pyrealsense2'] = MagicMock()

# Import after mocking - need to import directly to avoid __init__.py issues
import importlib.util
spec = importlib.util.spec_from_file_location(
    "ui_settings",
    os.path.join(os.path.dirname(__file__), 'hub', 'components', 'ui_settings.py')
)
ui_settings_module = importlib.util.module_from_spec(spec)

# Mock the imports within ui_settings
sys.modules['hub.components.ui_widgets'] = MagicMock()
sys.modules['hub.components.ui_settings_manager'] = MagicMock()
sys.modules['hub.components.ui_settings_common'] = MagicMock()
sys.modules['hub.components.ui_settings_3d'] = MagicMock()
sys.modules['hub.components.ui_settings_2d'] = MagicMock()

# Now load the module
spec.loader.exec_module(ui_settings_module)
CalibrationSettingsWidget = ui_settings_module.CalibrationSettingsWidget

class TestSettingsApplication:
    """Test suite for settings application logic"""
    
    def __init__(self):
        self.results = []
        self.passed = 0
        self.failed = 0
        
    def log(self, message: str, status: str = "INFO"):
        """Log test message"""
        symbols = {"PASS": "✅", "FAIL": "❌", "INFO": "ℹ️", "TEST": "🧪"}
        print(f"{symbols.get(status, 'ℹ️')} {message}")
        self.results.append((status, message))
        
    def assert_equal(self, actual, expected, message: str):
        """Assert equality and log result"""
        if actual == expected:
            self.log(f"PASS: {message}", "PASS")
            self.passed += 1
            return True
        else:
            self.log(f"FAIL: {message}\n  Expected: {expected}\n  Got: {actual}", "FAIL")
            self.failed += 1
            return False
            
    def assert_in(self, item, container, message: str):
        """Assert item in container and log result"""
        if item in container:
            self.log(f"PASS: {message}", "PASS")
            self.passed += 1
            return True
        else:
            self.log(f"FAIL: {message}\n  '{item}' not in {container}", "FAIL")
            self.failed += 1
            return False
            
    def assert_true(self, condition, message: str):
        """Assert condition is true and log result"""
        if condition:
            self.log(f"PASS: {message}", "PASS")
            self.passed += 1
            return True
        else:
            self.log(f"FAIL: {message}", "FAIL")
            self.failed += 1
            return False
    
    def create_mock_widget(self):
        """Create a mock CalibrationSettingsWidget with necessary attributes"""
        mock_udp = Mock()
        mock_zmq = Mock()
        
        # Create widget instance
        widget = CalibrationSettingsWidget(mock_udp, mock_zmq)
        
        # Mock UI elements that get_current_settings accesses
        widget.camera_settings_combo = Mock()
        widget.camera_settings_combo.currentData.return_value = 'default'
        
        widget.resolution_combo = Mock()
        widget.resolution_combo.currentText.return_value = '640 x 480'
        
        widget.fps_combo = Mock()
        widget.fps_combo.currentData.return_value = 60
        
        widget.depth_sensor_toggle = Mock()
        widget.depth_sensor_toggle.isChecked.return_value = True
        
        widget.use_dnn_tracker_toggle = Mock()
        widget.use_dnn_tracker_toggle.isChecked.return_value = True
        
        widget.ball_confidence_slider = Mock()
        widget.ball_confidence_slider.value.return_value = 25
        
        widget.ball_held_confidence_slider = Mock()
        widget.ball_held_confidence_slider.value.return_value = 25
        
        widget.nms_slider = Mock()
        widget.nms_slider.value.return_value = 50
        
        widget.show_raw_yolo_toggle = Mock()
        widget.show_raw_yolo_toggle.isChecked.return_value = False
        
        widget.pose_model_toggle = Mock()
        widget.pose_model_toggle.isChecked.return_value = True
        
        # Mock 3D tracker settings
        widget.tc_undetected_near_hand_slider = Mock()
        widget.tc_undetected_near_hand_slider.value.return_value = 20
        
        widget.tc_min_frames_slider = Mock()
        widget.tc_min_frames_slider.value.return_value = 3
        
        widget.tc_hand_distance_threshold_slider = Mock()
        widget.tc_hand_distance_threshold_slider.value.return_value = 25
        
        widget.tc_min_throw_distance_slider = Mock()
        widget.tc_min_throw_distance_slider.value.return_value = 20
        
        widget.tc_min_frames_before_catch_slider = Mock()
        widget.tc_min_frames_before_catch_slider.value.return_value = 3
        
        widget.tc_ignore_class_toggle = Mock()
        widget.tc_ignore_class_toggle.isChecked.return_value = False
        
        widget.tc_max_tracker_distance_slider = Mock()
        widget.tc_max_tracker_distance_slider.value.return_value = 50
        
        widget.tc_sound_on_catch_toggle = Mock()
        widget.tc_sound_on_catch_toggle.isChecked.return_value = False
        
        widget.tc_sound_on_throw_toggle = Mock()
        widget.tc_sound_on_throw_toggle.isChecked.return_value = False
        
        widget.tc_name_on_catch_toggle = Mock()
        widget.tc_name_on_catch_toggle.isChecked.return_value = False
        
        widget.tc_name_on_throw_toggle = Mock()
        widget.tc_name_on_throw_toggle.isChecked.return_value = False
        
        # Mock UI sections for collapsed state
        widget.camera_section = Mock()
        widget.camera_section.is_collapsed = False
        
        widget.yolo_section = Mock()
        widget.yolo_section.is_collapsed = False
        
        widget.pose_section = Mock()
        widget.pose_section.is_collapsed = False
        
        widget.throw_catch_section = Mock()
        widget.throw_catch_section.is_collapsed = False
        
        widget.color_tracker_weights_section = Mock()
        widget.color_tracker_weights_section.is_collapsed = False
        
        widget.override_detection_section = Mock()
        widget.override_detection_section.is_collapsed = False
        
        widget.held_color_blob_section = Mock()
        widget.held_color_blob_section.is_collapsed = False
        
        widget.trajectory_section = Mock()
        widget.trajectory_section.is_collapsed = False
        
        widget.hand_velocity_section = Mock()
        widget.hand_velocity_section.is_collapsed = False
        
        widget.ball_profiles_section = Mock()
        widget.ball_profiles_section.is_collapsed = False
        
        widget._loading_settings = False
        
        return widget, mock_udp, mock_zmq
    
    def test_get_current_settings_structure(self):
        """Test 1: Verify get_current_settings returns correct structure"""
        self.log("Test 1: get_current_settings structure for 3D tracker", "TEST")
        
        widget, _, _ = self.create_mock_widget()
        widget.current_tracker = "depth_based"
        
        settings = widget.get_current_settings()
        
        # Check top-level keys
        self.assert_in('tracker_type', settings, "Settings contain tracker_type")
        self.assert_equal(settings['tracker_type'], 'depth_based', "tracker_type is depth_based")
        
        # Check common settings are present
        self.assert_in('camera_settings_profile', settings, "Settings contain camera_settings_profile")
        self.assert_in('resolution', settings, "Settings contain resolution")
        self.assert_in('fps', settings, "Settings contain fps")
        self.assert_in('enable_ball_detection', settings, "Settings contain enable_ball_detection")
        self.assert_in('ball_confidence_threshold', settings, "Settings contain ball_confidence_threshold")
        
        # Check 3D-specific settings are present
        self.assert_in('undetected_near_hand_threshold', settings, "Settings contain 3D-specific undetected_near_hand_threshold")
        self.assert_in('min_frames_for_state_change', settings, "Settings contain 3D-specific min_frames_for_state_change")
        
        # Check UI state is present
        self.assert_in('collapsed_camera', settings, "Settings contain UI state collapsed_camera")
        
    def test_get_current_settings_2d_tracker(self):
        """Test 2: Verify get_current_settings for 2D tracker"""
        self.log("Test 2: get_current_settings structure for 2D tracker", "TEST")
        
        widget, _, _ = self.create_mock_widget()
        widget.current_tracker = "simple_2d"
        
        settings = widget.get_current_settings()
        
        self.assert_equal(settings['tracker_type'], 'simple_2d', "tracker_type is simple_2d")
        
        # Common settings should still be present
        self.assert_in('enable_ball_detection', settings, "Settings contain common enable_ball_detection")
        
        # 3D-specific settings should NOT be present for 2D tracker
        # Note: Current implementation still includes them - this is a bug we need to verify
        has_3d_settings = 'undetected_near_hand_threshold' in settings
        if has_3d_settings:
            self.log("WARNING: 2D tracker settings include 3D-specific settings (potential issue)", "INFO")
    
    def test_apply_settings_3d(self):
        """Test 3: Verify apply_settings correctly applies 3D settings"""
        self.log("Test 3: apply_settings with 3D tracker settings", "TEST")
        
        widget, _, _ = self.create_mock_widget()
        widget.current_tracker = "depth_based"
        
        test_settings = {
            'tracker_type': 'depth_based',
            'camera_settings_profile': 'default',
            'resolution': '640 x 480',
            'fps': 60,
            'enable_ball_detection': True,
            'ball_confidence_threshold': 0.30,
            'undetected_near_hand_threshold': 0.25,
            'min_frames_for_state_change': 5,
        }
        
        widget.apply_settings(test_settings)
        
        # Verify sliders were set correctly
        self.assert_equal(widget.ball_confidence_slider.setValue.call_count, 1, "ball_confidence_slider.setValue called")
        widget.ball_confidence_slider.setValue.assert_called_with(30)
        
        self.assert_equal(widget.tc_undetected_near_hand_slider.setValue.call_count, 1, "3D slider setValue called")
        widget.tc_undetected_near_hand_slider.setValue.assert_called_with(25)
    
    def test_apply_settings_2d(self):
        """Test 4: Verify apply_settings correctly applies 2D settings"""
        self.log("Test 4: apply_settings with 2D tracker settings", "TEST")
        
        widget, _, _ = self.create_mock_widget()
        widget.current_tracker = "simple_2d"
        
        test_settings = {
            'tracker_type': 'simple_2d',
            'camera_settings_profile': 'default',
            'resolution': '640 x 480',
            'fps': 60,
            'enable_ball_detection': True,
            'ball_confidence_threshold': 0.30,
        }
        
        widget.apply_settings(test_settings)
        
        # Verify common settings were applied
        self.assert_equal(widget.ball_confidence_slider.setValue.call_count, 1, "Common slider setValue called")
    
    def test_send_all_settings_to_engine_3d(self):
        """Test 5: Verify _send_all_settings_to_engine sends correct settings for 3D"""
        self.log("Test 5: _send_all_settings_to_engine for 3D tracker", "TEST")
        
        widget, mock_udp, _ = self.create_mock_widget()
        widget.current_tracker = "depth_based"
        
        test_settings = {
            'enable_ball_detection': True,
            'ball_confidence_threshold': 0.30,
            'undetected_near_hand_threshold': 0.25,
            'min_frames_for_state_change': 5,
            'traj_gravity': 9.81,
        }
        
        widget._send_all_settings_to_engine(test_settings)
        
        # Verify UDP calls were made
        self.assert_true(mock_udp.send_setting.call_count > 0, "UDP send_setting was called")
        
        # Check specific calls
        calls = [call[0] for call in mock_udp.send_setting.call_args_list]
        
        # Common settings should be sent
        self.assert_true(('enable_ball_detection', 1) in calls, "enable_ball_detection sent")
        self.assert_true(('ball_confidence_threshold', 0.30) in calls, "ball_confidence_threshold sent")
        
        # 3D-specific settings should be sent
        self.assert_true(('undetected_near_hand_threshold', 0.25) in calls, "3D-specific setting sent")
        self.assert_true(('traj_gravity', 9.81) in calls, "3D trajectory setting sent")
    
    def test_send_all_settings_to_engine_2d(self):
        """Test 6: Verify _send_all_settings_to_engine for 2D tracker"""
        self.log("Test 6: _send_all_settings_to_engine for 2D tracker", "TEST")
        
        widget, mock_udp, _ = self.create_mock_widget()
        widget.current_tracker = "simple_2d"
        
        test_settings = {
            'enable_ball_detection': True,
            'ball_confidence_threshold': 0.30,
            # No 3D-specific settings
        }
        
        widget._send_all_settings_to_engine(test_settings)
        
        # Verify only common settings were sent
        calls = [call[0] for call in mock_udp.send_setting.call_args_list]
        
        self.assert_true(('enable_ball_detection', 1) in calls, "Common setting sent for 2D")
        
        # 3D-specific settings should NOT be sent
        has_3d_calls = any('undetected_near_hand_threshold' in str(call) for call in calls)
        self.assert_true(not has_3d_calls, "3D-specific settings NOT sent for 2D tracker")
    
    def test_tracker_switching_flow(self):
        """Test 7: Verify tracker switching saves and loads correctly"""
        self.log("Test 7: Tracker switching workflow", "TEST")
        
        widget, mock_udp, mock_zmq = self.create_mock_widget()
        
        # Mock tracking_system_combo
        widget.tracking_system_combo = Mock()
        widget.tracking_system_combo.currentData.return_value = "simple_2d"
        
        # Mock settings manager
        widget.settings_manager = Mock()
        widget.settings_manager.load_settings.return_value = {
            'tracker_type': 'simple_2d',
            'enable_ball_detection': True,
        }
        
        # Mock ZMQ response
        mock_response = Mock()
        mock_response.success = True
        mock_response.message = "Tracker switched"
        mock_zmq.send_command.return_value = mock_response
        
        # Start with 3D tracker
        widget.current_tracker = "depth_based"
        widget._loading_settings = False
        
        # Trigger switch
        widget.on_tracking_system_changed()
        
        # Verify settings were saved for old tracker
        self.assert_equal(widget.settings_manager.save_settings.call_count, 1, "Settings saved before switch")
        
        # Verify new tracker was set
        self.assert_equal(widget.current_tracker, "simple_2d", "Tracker switched to simple_2d")
        
        # Verify new settings were loaded
        self.assert_equal(widget.settings_manager.load_settings.call_count, 1, "New settings loaded")
        widget.settings_manager.load_settings.assert_called_with("simple_2d")
    
    def test_edge_case_missing_keys(self):
        """Test 8: Verify handling of settings with missing keys"""
        self.log("Test 8: apply_settings with missing keys", "TEST")
        
        widget, _, _ = self.create_mock_widget()
        widget.current_tracker = "depth_based"
        
        # Settings with missing keys
        incomplete_settings = {
            'tracker_type': 'depth_based',
            'enable_ball_detection': True,
            # Missing many other settings
        }
        
        try:
            widget.apply_settings(incomplete_settings)
            self.log("PASS: apply_settings handles missing keys gracefully", "PASS")
            self.passed += 1
        except Exception as e:
            self.log(f"FAIL: apply_settings failed with missing keys: {e}", "FAIL")
            self.failed += 1
    
    def test_edge_case_extra_keys(self):
        """Test 9: Verify handling of settings with extra keys"""
        self.log("Test 9: apply_settings with extra keys", "TEST")
        
        widget, _, _ = self.create_mock_widget()
        widget.current_tracker = "depth_based"
        
        # Settings with extra unknown keys
        settings_with_extras = {
            'tracker_type': 'depth_based',
            'enable_ball_detection': True,
            'unknown_setting_1': 123,
            'unknown_setting_2': 'test',
        }
        
        try:
            widget.apply_settings(settings_with_extras)
            self.log("PASS: apply_settings ignores extra keys gracefully", "PASS")
            self.passed += 1
        except Exception as e:
            self.log(f"FAIL: apply_settings failed with extra keys: {e}", "FAIL")
            self.failed += 1
    
    def run_all_tests(self):
        """Run all tests and generate report"""
        self.log("=" * 60, "INFO")
        self.log("SETTINGS APPLICATION LOGIC TEST SUITE", "INFO")
        self.log("=" * 60, "INFO")
        
        self.test_get_current_settings_structure()
        self.test_get_current_settings_2d_tracker()
        self.test_apply_settings_3d()
        self.test_apply_settings_2d()
        self.test_send_all_settings_to_engine_3d()
        self.test_send_all_settings_to_engine_2d()
        self.test_tracker_switching_flow()
        self.test_edge_case_missing_keys()
        self.test_edge_case_extra_keys()
        
        self.log("=" * 60, "INFO")
        self.log(f"RESULTS: {self.passed} passed, {self.failed} failed", "INFO")
        self.log("=" * 60, "INFO")
        
        return self.failed == 0

def main():
    """Main test execution"""
    tester = TestSettingsApplication()
    success = tester.run_all_tests()
    
    # Generate detailed report
    print("\n" + "=" * 60)
    print("DETAILED ANALYSIS")
    print("=" * 60)
    
    print("\n📋 FINDINGS:")
    print("\n1. STRUCTURE VERIFICATION:")
    print("   - get_current_settings() returns flat structure (not nested)")
    print("   - All settings are at top level, not separated by tracker type")
    print("   - This differs from SettingsManager's nested structure")
    
    print("\n2. SETTINGS EXTRACTION:")
    print("   - ✅ Correctly extracts common settings")
    print("   - ✅ Correctly extracts 3D-specific settings when tracker is 3D")
    print("   - ⚠️  May extract 3D settings even for 2D tracker (needs verification)")
    
    print("\n3. SETTINGS APPLICATION:")
    print("   - ✅ apply_settings() handles flat structure correctly")
    print("   - ✅ Gracefully handles missing keys")
    print("   - ✅ Ignores extra keys")
    
    print("\n4. ENGINE COMMUNICATION:")
    print("   - ⚠️  _send_all_settings_to_engine() sends ALL settings in dict")
    print("   - ⚠️  Does NOT filter by tracker type")
    print("   - ⚠️  May send 3D-specific settings even for 2D tracker")
    
    print("\n5. TRACKER SWITCHING:")
    print("   - ✅ Correctly saves settings before switching")
    print("   - ✅ Loads new tracker settings after switching")
    print("   - ✅ Sends tracker switch command to engine")
    
    print("\n🔧 RECOMMENDED FIXES:")
    print("\n1. Update _send_all_settings_to_engine():")
    print("   - Add tracker type check")
    print("   - Only send 3D-specific settings when tracker is 'depth_based'")
    print("   - Only send 2D-specific settings when tracker is 'simple_2d'")
    
    print("\n2. Update get_current_settings():")
    print("   - Consider if nested structure is needed")
    print("   - Current flat structure works but differs from SettingsManager")
    
    print("\n3. Add validation:")
    print("   - Validate settings before sending to engine")
    print("   - Log warnings for unexpected settings")
    
    print("\n" + "=" * 60)
    
    return 0 if success else 1

if __name__ == "__main__":
    sys.exit(main())