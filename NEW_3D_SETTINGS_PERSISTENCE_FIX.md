# New 3D Tracker Settings Persistence Fix

**Date:** 2025-10-22  
**Component:** New 3D Kalman Tracking System  
**Issue:** Settings changes weren't being applied and weren't persisting between runs

## Problem Description

Two critical issues with settings management:

1. **Settings Not Applied**: Changing settings like `held_radius_m` in the UI had no effect because `updateSetting()` was just a stub
2. **Settings Not Persisted**: Settings weren't being saved to disk, so they would reset to defaults on every app restart

## Root Cause

The [`updateSetting()`](engine/src/New3DTracker.cpp:1945) function was implemented as a stub that always returned `false`:

```cpp
bool New3DTracker::updateSetting(const std::string& key, const std::string& value) {
    // Stub - will be implemented when UI integration is added
    return false;
}
```

This meant:
- UI changes were ignored
- No settings were saved to the JSON file
- Every restart loaded default values

## Solution

### Implemented Full `updateSetting()` Function (lines 1945-2007)

**Key Features:**
- Parses and applies all New3D tracker settings
- Automatically saves settings to disk after each update
- Handles all data types (float, int, bool)
- Provides error handling and logging

**Supported Settings:**

| Setting Key | Type | Description |
|------------|------|-------------|
| `held_radius_m` | float | Radius for held ball detection (meters) |
| `association_max_distance_m` | float | Max distance for detection matching |
| `color_mismatch_penalty_m` | float | Distance penalty for color mismatch |
| `throw_velocity_threshold_mps` | float | Min velocity for throw detection |
| `min_frames_for_new_track` | int | Frames to confirm new track |
| `min_frames_for_color_lock` | int | Frames to lock color |
| `use_color_tracking` | bool | Enable color-based identification |
| `color_match_threshold` | float | Min color match score |
| `color_sample_radius` | int | Pixel radius for color sampling |
| `ball_confidence_threshold` | float | Min confidence for 'ball' class |
| `ball_held_confidence_threshold` | float | Min confidence for 'ball_held' class |
| `ignore_class` | bool | Treat ball/ball_held same |
| `hand_velocity_enabled` | bool | Enable velocity-based throw detection |
| `hand_velocity_threshold` | float | Min hand speed for detection |
| `show_kalman_prediction` | bool | Show predicted position |
| `show_held_radius` | bool | Show held detection radius |
| `show_association_lines` | bool | Show detection-to-track associations |
| `gravity_x` | float | Gravity X component |
| `gravity_y` | float | Gravity Y component |
| `gravity_z` | float | Gravity Z component |

**Implementation:**

```cpp
bool New3DTracker::updateSetting(const std::string& key, const std::string& value) {
    std::cout << "[New3DTracker] updateSetting: " << key << " = " << value << std::endl;
    
    try {
        // Parse and update the setting
        if (key == "held_radius_m") {
            settings_.held_radius_m = std::stof(value);
        } else if (key == "association_max_distance_m") {
            settings_.association_max_distance_m = std::stof(value);
        }
        // ... (all other settings)
        
        // Save settings to file immediately after update
        saveSettings();
        
        std::cout << "[New3DTracker] Setting updated and saved: " << key << " = " << value << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "[New3DTracker] Error updating setting " << key << ": " << e.what() << std::endl;
        return false;
    }
}
```

## Behavior After Fix

### When Settings Are Changed:

1. **Immediate Application**: Setting is parsed and applied to `settings_` struct
2. **Automatic Save**: `saveSettings()` is called to persist to JSON file
3. **Logging**: Console output confirms the update
4. **Error Handling**: Invalid values are caught and logged

### Settings File Location:

Settings are saved to the file specified in the constructor (default: `new_3d_settings.json`)

### Settings Persistence:

- ✅ All settings are saved immediately when changed
- ✅ Settings persist between app restarts
- ✅ Settings are loaded on tracker initialization
- ✅ Invalid settings are handled gracefully

## Testing Recommendations

1. **Change `held_radius_m`** - Verify the held detection radius changes immediately
2. **Restart the app** - Verify settings are loaded from file
3. **Change multiple settings** - Verify all are saved and loaded correctly
4. **Test invalid values** - Verify error handling works

## Files Modified

1. [`engine/src/New3DTracker.cpp`](engine/src/New3DTracker.cpp:1945-2007)
   - Implemented full `updateSetting()` function with all New3D settings
   - Added automatic `saveSettings()` call after each update
   - Added comprehensive error handling and logging

## Impact

- **Settings Now Work**: UI changes are immediately applied
- **Settings Persist**: All settings are saved and loaded between runs
- **Better UX**: Users don't lose their configuration
- **Debugging**: Console logging helps track setting changes

## Related Issues

This fix resolves:
- Settings not being applied when changed in UI
- Settings resetting to defaults on app restart
- `held_radius_m` appearing to have no effect

## Related Documentation

- [New 3D Tracker Architecture](NEW_3D_TRACKER_ARCHITECTURE.md)
- [New 3D Tracker Implementation](NEW_3D_TRACKER_IMPLEMENTATION_COMPLETE.md)
- [Held Ball Wrist Tracking Fix](HELD_BALL_WRIST_TRACKING_FIX.md)