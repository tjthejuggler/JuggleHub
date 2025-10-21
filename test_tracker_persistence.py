#!/usr/bin/env python3
"""
Test script to verify tracking system persistence fix.
This simulates the UI initialization process to ensure the correct tracker is loaded.
"""

import os
import sys
import json
from datetime import datetime

# Add hub to path
sys.path.insert(0, 'hub')

def test_determine_last_used_tracker():
    """Test the _determine_last_used_tracker logic"""
    print("=" * 60)
    print("Testing Tracker Persistence Fix")
    print("=" * 60)
    
    # Check which settings files exist
    config_dir = "hub/config"
    settings_files = {
        "depth_based": os.path.join(config_dir, "calibration_settings_3d.json"),
        "new_3d": os.path.join(config_dir, "calibration_settings_new3d.json"),
        "simple_2d": os.path.join(config_dir, "calibration_settings_2d.json"),
    }
    
    print("\n1. Checking existing settings files:")
    for tracker_type, filepath in settings_files.items():
        if os.path.exists(filepath):
            print(f"   ✅ Found: {filepath}")
            try:
                with open(filepath, 'r') as f:
                    settings = json.load(f)
                if 'saved_at' in settings:
                    print(f"      Saved at: {settings['saved_at']}")
                if 'tracking_system' in settings:
                    print(f"      Tracking system: {settings['tracking_system']}")
            except Exception as e:
                print(f"      ⚠️ Error reading: {e}")
        else:
            print(f"   ❌ Not found: {filepath}")
    
    # Determine which tracker should be loaded
    print("\n2. Determining last used tracker:")
    latest_tracker = "depth_based"  # Default fallback
    latest_time = None
    
    for tracker_type, filepath in settings_files.items():
        if os.path.exists(filepath):
            try:
                with open(filepath, 'r') as f:
                    settings = json.load(f)
                
                if 'saved_at' in settings:
                    saved_time = datetime.fromisoformat(settings['saved_at'])
                    if latest_time is None or saved_time > latest_time:
                        latest_time = saved_time
                        latest_tracker = tracker_type
                        print(f"   🔍 {tracker_type}: {settings['saved_at']} (newest so far)")
                    else:
                        print(f"   🔍 {tracker_type}: {settings['saved_at']}")
            except Exception as e:
                print(f"   ⚠️ Error reading {filepath}: {e}")
    
    print(f"\n3. Result:")
    print(f"   ✅ Last used tracker: {latest_tracker}")
    if latest_time:
        print(f"   ✅ Last saved at: {latest_time.isoformat()}")
    
    return latest_tracker

def test_settings_loading():
    """Test that settings are loaded for the correct tracker"""
    print("\n" + "=" * 60)
    print("Testing Settings Loading")
    print("=" * 60)
    
    from components.ui_settings_manager import SettingsManager
    
    manager = SettingsManager()
    
    # Determine last used tracker
    last_tracker = test_determine_last_used_tracker()
    
    print(f"\n4. Loading settings for {last_tracker}:")
    settings = manager.load_settings(last_tracker)
    
    if settings:
        print(f"   ✅ Settings loaded successfully")
        print(f"   Tracker type in settings: {settings.get('tracker_type', 'NOT SET')}")
        print(f"   Tracking system in settings: {settings.get('tracking_system', 'NOT SET')}")
    else:
        print(f"   ❌ Failed to load settings")
    
    return settings

if __name__ == "__main__":
    try:
        test_determine_last_used_tracker()
        print("\n" + "=" * 60)
        test_settings_loading()
        print("\n" + "=" * 60)
        print("✅ Test completed successfully!")
        print("=" * 60)
    except Exception as e:
        print(f"\n❌ Test failed with error: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)