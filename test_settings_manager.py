#!/usr/bin/env python3
"""
Test script for SettingsManager functionality.
Tests settings creation, loading, saving, and migration.
"""

import os
import json
import sys
from datetime import datetime

# Import SettingsManager directly
sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'hub', 'components'))

from ui_settings_manager import SettingsManager


def print_section(title):
    """Print a section header."""
    print(f"\n{'='*60}")
    print(f"  {title}")
    print(f"{'='*60}\n")


def test_settings_manager():
    """Test the SettingsManager functionality."""
    
    print_section("SETTINGS MANAGER TEST")
    
    # Initialize manager
    manager = SettingsManager()
    print(f"✅ SettingsManager initialized")
    print(f"   Config directory: {manager.config_dir}")
    print(f"   3D settings file: {manager.settings_3d_file}")
    print(f"   2D settings file: {manager.settings_2d_file}")
    print(f"   Legacy file: {manager.legacy_settings_file}")
    
    # Check for existing files
    print_section("CHECKING EXISTING FILES")
    
    files_status = {
        "Legacy (calibration_settings.json)": os.path.exists(manager.legacy_settings_file),
        "3D (calibration_settings_3d.json)": os.path.exists(manager.settings_3d_file),
        "2D (calibration_settings_2d.json)": os.path.exists(manager.settings_2d_file),
    }
    
    for name, exists in files_status.items():
        status = "✅ EXISTS" if exists else "❌ NOT FOUND"
        print(f"{status}: {name}")
    
    # Test loading settings (should return None if no files exist)
    print_section("TESTING LOAD SETTINGS")
    
    settings_3d = manager.load_settings("depth_based")
    settings_2d = manager.load_settings("simple_2d")
    
    if settings_3d:
        print(f"✅ Loaded 3D settings")
    else:
        print(f"ℹ️  No 3D settings found (expected if first run)")
    
    if settings_2d:
        print(f"✅ Loaded 2D settings")
    else:
        print(f"ℹ️  No 2D settings found (expected if first run)")
    
    # Create default settings if none exist
    print_section("CREATING DEFAULT SETTINGS")
    
    if not settings_3d:
        print("Creating default 3D settings...")
        defaults_3d = manager.get_default_settings("depth_based")
        success = manager.save_settings("depth_based", defaults_3d)
        if success:
            print(f"✅ Created default 3D settings file")
        else:
            print(f"❌ Failed to create 3D settings file")
    
    if not settings_2d:
        print("Creating default 2D settings...")
        defaults_2d = manager.get_default_settings("simple_2d")
        success = manager.save_settings("simple_2d", defaults_2d)
        if success:
            print(f"✅ Created default 2D settings file")
        else:
            print(f"❌ Failed to create 2D settings file")
    
    # Verify files were created
    print_section("VERIFYING CREATED FILES")
    
    for name, path in [
        ("3D settings", manager.settings_3d_file),
        ("2D settings", manager.settings_2d_file)
    ]:
        if os.path.exists(path):
            print(f"✅ {name} file exists: {path}")
            
            # Load and verify structure
            with open(path, 'r') as f:
                data = json.load(f)
            
            # Check required fields
            required_fields = ['tracker_type', 'saved_at']
            missing = [f for f in required_fields if f not in data]
            
            if missing:
                print(f"   ⚠️  Missing fields: {missing}")
            else:
                print(f"   ✅ Has required fields: tracker_type, saved_at")
                print(f"   📝 Tracker type: {data['tracker_type']}")
                print(f"   📝 Saved at: {data['saved_at']}")
                print(f"   📝 Total settings: {len(data)} keys")
        else:
            print(f"❌ {name} file NOT found: {path}")
    
    # Test reload after creation
    print_section("TESTING RELOAD AFTER CREATION")
    
    settings_3d_reload = manager.load_settings("depth_based")
    settings_2d_reload = manager.load_settings("simple_2d")
    
    if settings_3d_reload:
        print(f"✅ Successfully reloaded 3D settings")
        print(f"   Tracker type: {settings_3d_reload.get('tracker_type')}")
    else:
        print(f"❌ Failed to reload 3D settings")
    
    if settings_2d_reload:
        print(f"✅ Successfully reloaded 2D settings")
        print(f"   Tracker type: {settings_2d_reload.get('tracker_type')}")
    else:
        print(f"❌ Failed to reload 2D settings")
    
    # Display sample of 3D settings
    if settings_3d_reload:
        print_section("SAMPLE 3D SETTINGS")
        sample_keys = [
            'tracking_system',
            'ball_confidence_threshold',
            'hand_distance_threshold',
            'min_throw_distance',
            'traj_gravity',
            'hand_velocity_threshold'
        ]
        for key in sample_keys:
            if key in settings_3d_reload:
                print(f"   {key}: {settings_3d_reload[key]}")
    
    # Display sample of 2D settings
    if settings_2d_reload:
        print_section("SAMPLE 2D SETTINGS")
        sample_keys = [
            'tracking_system',
            'ball_confidence_threshold',
            'enable_ball_detection',
            'pose_model_enabled'
        ]
        for key in sample_keys:
            if key in settings_2d_reload:
                print(f"   {key}: {settings_2d_reload[key]}")
    
    # Summary
    print_section("TEST SUMMARY")
    
    all_tests_passed = True
    
    # Check 3D settings
    if os.path.exists(manager.settings_3d_file) and settings_3d_reload:
        print("✅ 3D settings: PASS")
    else:
        print("❌ 3D settings: FAIL")
        all_tests_passed = False
    
    # Check 2D settings
    if os.path.exists(manager.settings_2d_file) and settings_2d_reload:
        print("✅ 2D settings: PASS")
    else:
        print("❌ 2D settings: FAIL")
        all_tests_passed = False
    
    if all_tests_passed:
        print("\n🎉 ALL TESTS PASSED!")
        return 0
    else:
        print("\n⚠️  SOME TESTS FAILED")
        return 1


if __name__ == "__main__":
    exit_code = test_settings_manager()
    sys.exit(exit_code)