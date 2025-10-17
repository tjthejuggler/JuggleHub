#!/usr/bin/env python3
"""
Test script for legacy settings migration.
Creates a legacy settings file and tests migration to new format.
"""

import os
import json
import sys
import shutil
from datetime import datetime

# Import SettingsManager directly
sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'hub', 'components'))

from ui_settings_manager import SettingsManager


def print_section(title):
    """Print a section header."""
    print(f"\n{'='*60}")
    print(f"  {title}")
    print(f"{'='*60}\n")


def test_legacy_migration():
    """Test legacy settings migration."""
    
    print_section("LEGACY MIGRATION TEST")
    
    # Initialize manager
    manager = SettingsManager()
    
    # Create a legacy settings file with comprehensive 3D settings
    legacy_settings = {
        "camera_settings_profile": "legacy_profile",
        "resolution": "1280 x 720",
        "fps": 30,
        "depth_sensor_enabled": True,
        "tracking_system": "depth_based",
        "enable_ball_detection": True,
        "ball_confidence_threshold": 0.30,
        "ball_held_confidence_threshold": 0.30,
        "nms_threshold": 0.45,
        "show_raw_yolo_detections": True,
        "pose_model_enabled": True,
        "hand_distance_threshold": 0.30,
        "min_throw_distance": 0.25,
        "traj_gravity": 9.81,
        "hand_velocity_threshold": 1.5,
        "collapsed_camera": True,
        "collapsed_yolo": True,
        "collapsed_pose": False,
    }
    
    # Backup existing files if they exist
    print("Backing up existing settings files...")
    for filename in ["calibration_settings_3d.json", "calibration_settings_2d.json"]:
        filepath = os.path.join(manager.config_dir, filename)
        if os.path.exists(filepath):
            backup_path = f"{filepath}.test_backup"
            shutil.copy(filepath, backup_path)
            os.remove(filepath)
            print(f"   Backed up and removed: {filename}")
    
    # Create legacy settings file
    print("\nCreating legacy settings file...")
    with open(manager.legacy_settings_file, 'w') as f:
        json.dump(legacy_settings, f, indent=2)
    print(f"✅ Created legacy file: {manager.legacy_settings_file}")
    
    # Test migration for 3D tracker
    print_section("TESTING 3D MIGRATION")
    
    settings_3d = manager.load_settings("depth_based")
    
    if settings_3d:
        print("✅ Migration successful for 3D tracker")
        print(f"   Migrated from legacy: {settings_3d.get('migrated_from_legacy', False)}")
        print(f"   Migration date: {settings_3d.get('migration_date', 'N/A')}")
        print(f"   Tracker type: {settings_3d.get('tracker_type')}")
        print(f"   Resolution: {settings_3d.get('resolution')}")
        print(f"   FPS: {settings_3d.get('fps')}")
        print(f"   Ball confidence: {settings_3d.get('ball_confidence_threshold')}")
        
        # Verify legacy settings were preserved
        print("\nVerifying legacy settings preservation:")
        for key, value in legacy_settings.items():
            if key in settings_3d:
                if settings_3d[key] == value:
                    print(f"   ✅ {key}: {value}")
                else:
                    print(f"   ⚠️  {key}: expected {value}, got {settings_3d[key]}")
            else:
                print(f"   ❌ Missing key: {key}")
    else:
        print("❌ Migration failed for 3D tracker")
    
    # Test migration for 2D tracker
    print_section("TESTING 2D MIGRATION")
    
    settings_2d = manager.load_settings("simple_2d")
    
    if settings_2d:
        print("✅ Migration successful for 2D tracker")
        print(f"   Migrated from legacy: {settings_2d.get('migrated_from_legacy', False)}")
        print(f"   Migration date: {settings_2d.get('migration_date', 'N/A')}")
        print(f"   Tracker type: {settings_2d.get('tracker_type')}")
        print(f"   Resolution: {settings_2d.get('resolution')}")
        print(f"   FPS: {settings_2d.get('fps')}")
        
        # Verify only common settings were migrated
        print("\nVerifying common settings extraction:")
        common_keys = ['camera_settings_profile', 'resolution', 'fps', 'tracking_system', 
                      'enable_ball_detection', 'ball_confidence_threshold']
        for key in common_keys:
            if key in settings_2d:
                print(f"   ✅ {key}: {settings_2d[key]}")
            else:
                print(f"   ❌ Missing common key: {key}")
        
        # Verify 3D-specific settings were NOT migrated
        print("\nVerifying 3D-specific settings excluded:")
        excluded_keys = ['hand_distance_threshold', 'min_throw_distance', 'traj_gravity']
        for key in excluded_keys:
            if key not in settings_2d:
                print(f"   ✅ {key}: correctly excluded")
            else:
                print(f"   ⚠️  {key}: should not be in 2D settings")
    else:
        print("❌ Migration failed for 2D tracker")
    
    # Verify files were created
    print_section("VERIFYING MIGRATED FILES")
    
    files_created = []
    for name, path in [
        ("3D settings", manager.settings_3d_file),
        ("2D settings", manager.settings_2d_file)
    ]:
        if os.path.exists(path):
            print(f"✅ {name} file created: {path}")
            files_created.append(True)
        else:
            print(f"❌ {name} file NOT created: {path}")
            files_created.append(False)
    
    # Check if legacy file still exists (it should)
    if os.path.exists(manager.legacy_settings_file):
        print(f"✅ Legacy file preserved: {manager.legacy_settings_file}")
    else:
        print(f"⚠️  Legacy file removed (should be preserved)")
    
    # Cleanup
    print_section("CLEANUP")
    
    # Remove test files
    print("Removing test files...")
    for filename in ["calibration_settings.json", "calibration_settings_3d.json", 
                     "calibration_settings_2d.json"]:
        filepath = os.path.join(manager.config_dir, filename)
        if os.path.exists(filepath):
            os.remove(filepath)
            print(f"   Removed: {filename}")
    
    # Restore backups
    print("\nRestoring original files...")
    for filename in ["calibration_settings_3d.json", "calibration_settings_2d.json"]:
        filepath = os.path.join(manager.config_dir, filename)
        backup_path = f"{filepath}.test_backup"
        if os.path.exists(backup_path):
            shutil.move(backup_path, filepath)
            print(f"   Restored: {filename}")
    
    # Summary
    print_section("TEST SUMMARY")
    
    all_tests_passed = all(files_created) and settings_3d and settings_2d
    
    if all_tests_passed:
        print("✅ 3D migration: PASS")
        print("✅ 2D migration: PASS")
        print("✅ File creation: PASS")
        print("\n🎉 ALL MIGRATION TESTS PASSED!")
        return 0
    else:
        print("⚠️  SOME MIGRATION TESTS FAILED")
        return 1


if __name__ == "__main__":
    exit_code = test_legacy_migration()
    sys.exit(exit_code)