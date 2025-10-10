# Trajectory Throw/Catch Thresholds Added to UI

**Date:** 2025-10-10  
**Status:** ✅ Implemented

## Overview

Added UI controls for the trajectory-based throw and catch distance thresholds that were previously only configurable in code. These are the thresholds you see in debug logs when throws and catches are detected.

## Problem

The trajectory-based tracking system uses two critical thresholds:
- **`throw_distance_threshold`**: Distance ball must be from hand to detect a throw (default: 0.20m = 20cm)
- **`catch_distance_threshold`**: Maximum distance from hand to detect a catch (default: 0.30m = 30cm)

These were hardcoded in [`TrackingSettings`](engine/include/SimpleBallTracker.hpp:141-142) and not exposed in the UI, making it difficult for users to tune them for their juggling style.

## Solution

Added two new sliders to the "🎯 Ball State Detection" section in the UI settings:

### 1. Throw Distance Threshold
- **Range**: 5-50 cm
- **Default**: 20 cm
- **Purpose**: When a ball moves this far from a hand, it's considered thrown
- **Effect**: 
  - Lower = more sensitive (detects throws earlier)
  - Higher = less sensitive (requires ball to be farther from hand)

### 2. Catch Distance Threshold
- **Range**: 10-50 cm  
- **Default**: 30 cm
- **Purpose**: When a ball gets within this distance of a hand, it's considered caught
- **Effect**:
  - Lower = stricter (must be very close to hand)
  - Higher = more lenient (catches from farther away)

## Implementation Details

### Changes Made

#### 1. UI Settings (`hub/components/ui_settings.py`)

Added two new sliders in [`create_throw_catch_section()`](hub/components/ui_settings.py:330):

```python
# Throw Distance Threshold
self.tc_throw_distance_threshold_slider, self.tc_throw_distance_threshold_label = self._create_slider_widget(
    parent_layout=layout,
    row=row,
    label_text="Throw Distance Threshold (cm)",
    tooltip_text="Distance ball must be from hand to detect a throw (trajectory-based).\n"
                 "Range: 5-50 cm. Default: 20 cm.\n"
                 "When ball moves this far from hand, it's considered thrown.\n"
                 "Lower = more sensitive (detects throws earlier)\n"
                 "Higher = less sensitive (requires ball to be farther)\n"
                 "⚠️ This is the threshold you see in debug logs!",
    range_min=5,
    range_max=50,
    initial_value=20,
    update_func=lambda v: self.update_setting('throw_distance_threshold', v / 100.0),
    is_float=False
)

# Catch Distance Threshold  
self.tc_catch_distance_threshold_slider, self.tc_catch_distance_threshold_label = self._create_slider_widget(
    parent_layout=layout,
    row=row,
    label_text="Catch Distance Threshold (cm)",
    tooltip_text="Maximum distance from hand to detect a catch (trajectory-based).\n"
                 "Range: 10-50 cm. Default: 30 cm.\n"
                 "When ball gets within this distance of hand, it's considered caught.\n"
                 "Lower = stricter (must be very close to hand)\n"
                 "Higher = more lenient (catches from farther away)\n"
                 "⚠️ Increase if catches are being missed!",
    range_min=10,
    range_max=50,
    initial_value=30,
    update_func=lambda v: self.update_setting('catch_distance_threshold', v / 100.0),
    is_float=False
)
```

#### 2. Settings Persistence

Added to [`get_current_settings()`](hub/components/ui_settings.py:1757):
```python
'throw_distance_threshold': self.tc_throw_distance_threshold_slider.value() / 100.0 if hasattr(self, 'tc_throw_distance_threshold_slider') else 0.20,
'catch_distance_threshold': self.tc_catch_distance_threshold_slider.value() / 100.0 if hasattr(self, 'tc_catch_distance_threshold_slider') else 0.30,
```

Added to [`apply_settings()`](hub/components/ui_settings.py:1883):
```python
if 'throw_distance_threshold' in settings and hasattr(self, 'tc_throw_distance_threshold_slider'):
    self.tc_throw_distance_threshold_slider.setValue(int(settings['throw_distance_threshold'] * 100))

if 'catch_distance_threshold' in settings and hasattr(self, 'tc_catch_distance_threshold_slider'):
    self.tc_catch_distance_threshold_slider.setValue(int(settings['catch_distance_threshold'] * 100))
```

Added to [`_send_all_settings_to_engine()`](hub/components/ui_settings.py:2169):
```python
if 'throw_distance_threshold' in settings:
    self.udp_client.send_setting('throw_distance_threshold', settings['throw_distance_threshold'])

if 'catch_distance_threshold' in settings:
    self.udp_client.send_setting('catch_distance_threshold', settings['catch_distance_threshold'])
```

## Usage

1. **Open Settings**: Navigate to the "🎯 Ball State Detection" section in the UI
2. **Adjust Throw Threshold**: Move the "Throw Distance Threshold" slider
   - If throws are detected too early: increase the value
   - If throws are detected too late: decrease the value
3. **Adjust Catch Threshold**: Move the "Catch Distance Threshold" slider
   - If catches are being missed: increase the value
   - If false catches occur: decrease the value
4. **Settings Auto-Save**: Changes are automatically saved and sent to the engine

## Debug Logs

When you see messages like:
```
THROW DETECTED: ball 0.201m from hand (threshold: 0.200m)
```

The `0.200m` threshold is now the value from the "Throw Distance Threshold" slider (20cm by default).

## Related Settings

### Legacy Setting (Kept for Backward Compatibility)
- **Min Throw Distance**: The old setting, now marked as `[LEGACY]` in the UI
- Still functional but users should use the new "Throw Distance Threshold" instead

## Benefits

1. **User Control**: Users can now tune throw/catch detection without editing code
2. **Real-time Adjustment**: Changes take effect immediately
3. **Persistent**: Settings are saved and restored on restart
4. **Clear Feedback**: Tooltips explain what each threshold does
5. **Debug Alignment**: The UI values match what you see in debug logs

## Testing Recommendations

1. Start with default values (20cm throw, 30cm catch)
2. If throws trigger too early, increase throw threshold to 25-30cm
3. If catches are missed, increase catch threshold to 35-40cm
4. If false catches occur, decrease catch threshold to 20-25cm
5. Monitor debug logs to see actual distances when events occur

## Related Files

- [`hub/components/ui_settings.py`](hub/components/ui_settings.py) - UI implementation
- [`engine/include/SimpleBallTracker.hpp`](engine/include/SimpleBallTracker.hpp:141-142) - Threshold definitions
- [`engine/src/SimpleBallTracker.cpp`](engine/src/SimpleBallTracker.cpp:2405-2422) - Throw detection logic
- [`engine/src/SimpleBallTracker.cpp`](engine/src/SimpleBallTracker.cpp:1838-1863) - Catch detection logic

## See Also

- [TRAJECTORY_PREDICTION_MISSING_FRAMES.md](TRAJECTORY_PREDICTION_MISSING_FRAMES.md) - Trajectory prediction improvements
- [UI_SETTINGS_CHANGE_CHECKLIST.md](UI_SETTINGS_CHANGE_CHECKLIST.md) - How to add/remove UI settings
- [TRACKING_TUNING_GUIDE.md](TRACKING_TUNING_GUIDE.md) - User-facing tuning guide