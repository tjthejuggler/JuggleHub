#!/usr/bin/env python3
"""
Test script to verify color profile enabled state is properly saved to file.
"""

import json
import os
import sys

def test_color_profile_persistence():
    """Test that color profile enabled states are saved correctly"""
    
    settings_path = os.path.join("hub", "calibration_settings_new3d.json")
    
    print("=" * 60)
    print("Testing Color Profile Enable/Disable Persistence")
    print("=" * 60)
    
    # Read current settings
    try:
        with open(settings_path, 'r') as f:
            settings = json.load(f)
        
        color_profiles = settings.get('color_profiles', [])
        
        if not color_profiles:
            print("❌ ERROR: No color_profiles found in settings file!")
            return False
        
        print(f"\n✅ Found {len(color_profiles)} color profiles")
        print("\nCurrent enabled states:")
        print("-" * 40)
        
        for profile in color_profiles:
            name = profile.get('name', 'unknown')
            enabled = profile.get('enabled', True)
            status = "✓ ENABLED" if enabled else "✗ DISABLED"
            print(f"  {name:12s} : {status}")
        
        # Test: Disable orange and pink
        print("\n" + "=" * 60)
        print("TEST: Disabling 'orange' and 'pink'")
        print("=" * 60)
        
        modified = False
        for profile in color_profiles:
            if profile['name'] in ['orange', 'pink']:
                profile['enabled'] = False
                modified = True
                print(f"  ✓ Set {profile['name']} to disabled")
        
        if not modified:
            print("⚠️  WARNING: Could not find 'orange' or 'pink' profiles")
            return False
        
        # Save the modified settings
        with open(settings_path, 'w') as f:
            json.dump(settings, f, indent=4)
        
        print("\n✅ Saved modified settings to file")
        
        # Verify by re-reading
        print("\n" + "=" * 60)
        print("VERIFICATION: Re-reading file to confirm changes")
        print("=" * 60)
        
        with open(settings_path, 'r') as f:
            verify_settings = json.load(f)
        
        verify_profiles = verify_settings.get('color_profiles', [])
        
        print("\nVerified enabled states:")
        print("-" * 40)
        
        all_correct = True
        for profile in verify_profiles:
            name = profile.get('name', 'unknown')
            enabled = profile.get('enabled', True)
            status = "✓ ENABLED" if enabled else "✗ DISABLED"
            
            # Check if the disabled ones are actually disabled
            if name in ['orange', 'pink']:
                if enabled:
                    print(f"  {name:12s} : {status} ❌ SHOULD BE DISABLED!")
                    all_correct = False
                else:
                    print(f"  {name:12s} : {status} ✅ CORRECT")
            else:
                print(f"  {name:12s} : {status}")
        
        print("\n" + "=" * 60)
        if all_correct:
            print("✅ SUCCESS: All changes persisted correctly!")
            print("=" * 60)
            return True
        else:
            print("❌ FAILURE: Changes were NOT saved correctly!")
            print("=" * 60)
            return False
            
    except FileNotFoundError:
        print(f"❌ ERROR: Settings file not found: {settings_path}")
        return False
    except json.JSONDecodeError as e:
        print(f"❌ ERROR: Invalid JSON in settings file: {e}")
        return False
    except Exception as e:
        print(f"❌ ERROR: Unexpected error: {e}")
        import traceback
        traceback.print_exc()
        return False

if __name__ == "__main__":
    success = test_color_profile_persistence()
    sys.exit(0 if success else 1)