# Color Calibration Slider Fix

**Date:** 2025-10-03
**Issue:** Hue range sliders in calibration mode were all incorrectly set to 0-180 (full range) instead of appropriate ranges based on the actual ball colors.

## Problem

When the Ball Profiles section was created in calibration mode, color profiles in `ball_settings.json` had default HSV values with hue ranges of 0-180:
- Min HSV: `[0.0, saturation, value]`
- Max HSV: `[180.0, 255.0, 255.0]`

This meant all hue sliders showed 0-180, making it impossible to distinguish between different colored balls.

**IMPORTANT:** The fix will automatically detect and update any profiles with the default 0-180 hue range when you restart the application and enter calibration mode.

## Solution

Added a new helper function `_calculate_hsv_range_from_rgb()` that:

1. **Converts RGB to HSV** using OpenCV's color conversion
2. **Calculates appropriate hue ranges** with ±15° tolerance around the target hue
3. **Handles red wrap-around** (hue values near 0° or 180° that wrap around the color wheel)
4. **Sets forgiving saturation/value ranges** to account for lighting variations

### Color-Specific Hue Ranges

Based on the RGB values in `color_profiles.json`, the calculated hue ranges are approximately:

- **Pink** (RGB: 233, 30, 99) → Hue: ~330-360° (wraps to 165-180 + 0-15)
- **Orange** (RGB: 255, 87, 34) → Hue: ~10-40°
- **Yellow** (RGB: 255, 235, 59) → Hue: ~40-70°
- **Green** (RGB: 76, 175, 80) → Hue: ~105-135°
- **Red** (RGB: 244, 67, 54) → Hue: ~0-15° (wraps around)
- **Blue** (RGB: 33, 150, 243) → Hue: ~195-225°
- **Purple** (RGB: 156, 39, 176) → Hue: ~270-300°
- **White** (RGB: 255, 255, 255) → Hue: ~0-30° (low saturation)

## Changes Made

### File: `hub/components/ui.py`

1. **Added `_calculate_hsv_range_from_rgb()` method** (lines 641-670)
   - Converts RGB to HSV using OpenCV
   - Calculates min/max hue with ±15° tolerance
   - Handles red wrap-around for hues near 0° or 180°
   - Sets appropriate saturation and value ranges

2. **Updated `create_ball_profiles_section()` method** (lines 672-710)
   - Now calls `_calculate_hsv_range_from_rgb()` for new profiles
   - Saves the calculated HSV ranges to `ball_settings.json`
   - Logs the calculated hue ranges for debugging

## Testing

To verify the fix:

1. **Restart the application** - The fix will automatically detect profiles with 0-180 hue ranges
2. **Enter calibration mode** - Open the Ball Profiles section
3. **Check the sliders** - They should now show appropriate hue ranges for each color
4. **Verify console output** - You should see messages like:
   ```
   ⚠️ Fixing profile 'green' with default 0-180 range
      RGB: [76, 175, 80] -> Hue range: 45.0-75.0
   ✅ Ball settings saved with updated HSV ranges
   ```

The fix automatically updates existing profiles that have the default 0-180 range, so you don't need to delete any files.

## Notes

- Existing profiles in `ball_settings.json` are **not overwritten** - only new profiles get calculated ranges
- If you want to recalculate ranges for existing profiles, delete `ball_settings.json` and restart
- The hue tolerance of ±15° can be adjusted in the `_calculate_hsv_range_from_rgb()` method if needed
- The auto-calibrate feature can still be used to fine-tune ranges based on actual ball samples

## Related Files

- `hub/components/ui.py` - Main UI component with slider initialization
- `hub/components/color_profile_manager.py` - Manages color profile definitions
- `hub/ball_settings.json` - Stores HSV ranges for each ball color (auto-generated)
- `hub/config/color_profiles.json` - Defines RGB values for each color