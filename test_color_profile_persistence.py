#!/usr/bin/env python3
"""
Test script to verify color profile persistence for New 3D tracker.
"""

import json
import os

def test_color_profile_persistence():
    """Test that color profile enabled/disabled states are persisted"""
    print("=" * 60)
    print("Testing Color Profile Persistence for New 3D Tracker")
    print("=" * 60)
    
    settings_file = "hub/config/calibration_settings_new3d.json"
    
    if not os.path.exists(settings_file):
        print(f"❌ Settings file not found: {settings_file}")
        return False
    
    # Load settings
    with open(settings_file, 'r') as f:
        settings = json.load(f)
    
    print(f"\n1. Loaded settings from: {settings_file}")
    print(f"   Tracker type: {settings.get('tracker_type', 'NOT SET')}")
    print(f"   Saved at: {settings.get('saved_at', 'NOT SET')}")
    
    # Check color profiles
    color_profiles = settings.get('color_profiles', [])
    print(f"\n2. Found {len(color_profiles)} color profiles:")
    
    for profile in color_profiles:
        name = profile.get('name', 'unknown')
        enabled = profile.get('enabled', False)
        status = "✅ ENABLED" if enabled else "❌ DISABLED"
        print(f"   {status}: {name}")
    
    # Test: Disable some profiles and save
    print(f"\n3. Testing profile modification:")
    print(f"   Disabling 'pink' and 'white' profiles...")
    
    for profile in color_profiles:
        if profile['name'] in ['pink', 'white']:
            profile['enabled'] = False
    
    # Save modified settings
    with open(settings_file, 'w') as f:
        json.dump(settings, f, indent=2)
    
    print(f"   ✅ Saved modified settings")
    
    # Reload and verify
    print(f"\n4. Reloading settings to verify persistence:")
    with open(settings_file, 'r') as f:
        reloaded_settings = json.load(f)
    
    reloaded_profiles = reloaded_settings.get('color_profiles', [])
    
    all_correct = True
    for profile in reloaded_profiles:
        name = profile.get('name', 'unknown')
        enabled = profile.get('enabled', False)
        expected = name not in ['pink', 'white']
        
        if enabled == expected:
            status = "✅ CORRECT"
        else:
            status = "❌ WRONG"
            all_correct = False
        
        state = "ENABLED" if enabled else "DISABLED"
        print(f"   {status}: {name} is {state}")
    
    # Restore original state (all enabled)
    print(f"\n5. Restoring all profiles to enabled state...")
    for profile in color_profiles:
        profile['enabled'] = True
    
    with open(settings_file, 'w') as f:
        json.dump(settings, f, indent=2)
    
    print(f"   ✅ Restored original settings")
    
    print("\n" + "=" * 60)
    if all_correct:
        print("✅ Color profile persistence test PASSED!")
    else:
        print("❌ Color profile persistence test FAILED!")
    print("=" * 60)
    
    return all_correct

if __name__ == "__main__":
    try:
        success = test_color_profile_persistence()
        exit(0 if success else 1)
    except Exception as e:
        print(f"\n❌ Test failed with error: {e}")
        import traceback
        traceback.print_exc()
        exit(1)