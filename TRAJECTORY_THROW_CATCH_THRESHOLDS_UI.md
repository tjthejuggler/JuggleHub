# Hand Distance Threshold UI Control

**Date:** 2025-10-11 (Updated from 2025-10-10)
**Status:** ✅ Implemented & Consolidated

## Overview

The trajectory-based tracking system now uses a **unified hand distance threshold** that controls all hand-ball proximity checks. This replaces the previous separate throw and catch thresholds with a single, simpler control.

## Evolution

### Original Implementation (2025-10-10)
Added separate UI controls for throw and catch distance thresholds.

### Consolidation (2025-10-11)
Consolidated into a single **`hand_distance_threshold`** for simpler, more consistent behavior.

## Current System

The tracking system uses one critical threshold:
- **`hand_distance_threshold`**: Distance threshold for hand-ball proximity (default: 0.30m = 30cm)

This single threshold is used for:
- **Throw detection**: Ball moving away from hand beyond threshold
- **Catch detection**: Ball moving toward hand within threshold
- **Override state verification**: Determining if ball is HELD or IN_FLIGHT based on distance

See [TRACKING_THRESHOLD_CONSOLIDATION.md](TRACKING_THRESHOLD_CONSOLIDATION.md) for details on why this change was made.

## UI Control

### Hand Distance Threshold
- **Range**: 15-50 cm
- **Default**: 30 cm
- **Purpose**: Defines "how close must a ball be to a hand to be considered held"
- **Effect**:
  - Lower (15-25 cm) = stricter detection, requires closer proximity
  - Higher (35-50 cm) = more lenient, allows greater distance
  - Affects both throw and catch detection consistently

### Visualization Toggle
- **`show_hand_distance_threshold`**: Shows circles around hands at the threshold distance
- Helps visualize the detection zone for throws and catches

## Implementation Details

### Current Implementation

#### 1. UI Settings (`hub/components/ui_settings.py`)

Single slider in the "🎯 Ball State Detection" section:

```python
# Hand Distance Threshold (Unified)
self.hand_distance_threshold_slider, self.hand_distance_threshold_label = self._create_slider_widget(
    parent_layout=layout,
    row=row,
    label_text="Hand Distance Threshold (cm)",
    tooltip_text="Distance threshold for hand-ball proximity (unified).\n"
                 "Range: 15-50 cm. Default: 30 cm.\n"
                 "Defines 'how close must a ball be to a hand to be considered held'.\n"
                 "Used for throw detection (ball moving away) and catch detection (ball moving toward).\n"
                 "Lower = stricter (requires closer proximity)\n"
                 "Higher = more lenient (allows greater distance)\n"
                 "⚠️ This is the threshold you see in debug logs!",
    range_min=15,
    range_max=50,
    initial_value=30,
    update_func=lambda v: self.update_setting('hand_distance_threshold', v / 100.0),
    is_float=False
)

# Visualization toggle
self.show_hand_distance_threshold_checkbox = self._create_checkbox_widget(
    parent_layout=layout,
    row=row,
    label_text="Show Hand Distance Threshold",
    tooltip_text="Show circles around hands at the hand distance threshold.\n"
                 "Helps visualize the detection zone for throws and catches.",
    initial_value=True,
    update_func=lambda v: self.update_setting('show_hand_distance_threshold', v)
)
```

#### 2. Settings Persistence

Added to [`get_current_settings()`](hub/components/ui_settings.py):
```python
'hand_distance_threshold': self.hand_distance_threshold_slider.value() / 100.0 if hasattr(self, 'hand_distance_threshold_slider') else 0.30,
'show_hand_distance_threshold': self.show_hand_distance_threshold_checkbox.isChecked() if hasattr(self, 'show_hand_distance_threshold_checkbox') else True,
```

Added to [`apply_settings()`](hub/components/ui_settings.py):
```python
if 'hand_distance_threshold' in settings and hasattr(self, 'hand_distance_threshold_slider'):
    self.hand_distance_threshold_slider.setValue(int(settings['hand_distance_threshold'] * 100))

if 'show_hand_distance_threshold' in settings and hasattr(self, 'show_hand_distance_threshold_checkbox'):
    self.show_hand_distance_threshold_checkbox.setChecked(settings['show_hand_distance_threshold'])
```

Added to [`_send_all_settings_to_engine()`](hub/components/ui_settings.py):
```python
if 'hand_distance_threshold' in settings:
    self.udp_client.send_setting('hand_distance_threshold', settings['hand_distance_threshold'])

if 'show_hand_distance_threshold' in settings:
    self.udp_client.send_setting('show_hand_distance_threshold', settings['show_hand_distance_threshold'])
```

### Backward Compatibility

The engine automatically handles old setting names:
- `throw_distance_threshold` → automatically updates `hand_distance_threshold`
- `catch_distance_threshold` → automatically updates `hand_distance_threshold`
- `show_throw_distance_threshold` → automatically updates `show_hand_distance_threshold`
- `show_catch_distance_threshold` → automatically updates `show_hand_distance_threshold`

See [`SimpleBallTracker::updateSetting()`](engine/src/SimpleBallTracker.cpp:407-422) for implementation.

## Usage

1. **Open Settings**: Navigate to the "🎯 Ball State Detection" section in the UI
2. **Adjust Hand Distance Threshold**: Move the "Hand Distance Threshold" slider
   - If throws are detected too early OR catches are too lenient: decrease the value
   - If throws are detected too late OR catches are being missed: increase the value
3. **Enable Visualization**: Check "Show Hand Distance Threshold" to see circles around hands
4. **Settings Auto-Save**: Changes are automatically saved and sent to the engine

## Debug Logs

When you see messages like:
```
THROW DETECTED: ball 0.301m from hand (threshold: 0.300m)
CATCH DETECTED: ball 0.285m from hand (threshold: 0.300m)
Override: Ball set to HELD (distance-based: hand 0, dist=0.25m < threshold=0.30m)
```

The threshold value is from the "Hand Distance Threshold" slider (30cm by default).

## Tuning Guide

### Start with Default (30cm)
Good balance for most juggling patterns.

### Increase Threshold (35-50cm)
Use when:
- Catches are being missed frequently
- You juggle with wide hand movements
- You want more forgiving detection

### Decrease Threshold (15-25cm)
Use when:
- False catches occur (ball caught too early)
- You want stricter, more precise detection
- You juggle with tight, controlled patterns

## Benefits

1. **Simpler**: One threshold instead of two - easier to understand and tune
2. **Consistent**: Same threshold for all hand-ball proximity checks
3. **User Control**: Tune detection without editing code
4. **Real-time Adjustment**: Changes take effect immediately
5. **Persistent**: Settings are saved and restored on restart
6. **Clear Feedback**: Tooltips explain what the threshold does
7. **Debug Alignment**: UI values match what you see in debug logs
8. **Visualization**: See the threshold as circles around hands
9. **Backward Compatible**: Old settings automatically convert

## Why Consolidation?

The separate throw/catch thresholds were conceptually the same thing: "how close is close to a hand?"

**Problems with separate thresholds:**
- ❌ Two values to tune for the same concept
- ❌ Inconsistent behavior (throw: 20cm, catch: 30cm)
- ❌ Confusing for override state verification
- ❌ More complex UI

**Benefits of unified threshold:**
- ✅ Single source of truth
- ✅ Consistent behavior across all state transitions
- ✅ Simpler mental model
- ✅ Easier to tune
- ✅ Clearer code

See [TRACKING_THRESHOLD_CONSOLIDATION.md](TRACKING_THRESHOLD_CONSOLIDATION.md) for full details.

## Related Files

- [`hub/components/ui_settings.py`](hub/components/ui_settings.py) - UI implementation
- [`engine/include/SimpleBallTracker.hpp`](engine/include/SimpleBallTracker.hpp:149) - Unified threshold definition
- [`engine/src/SimpleBallTracker.cpp`](engine/src/SimpleBallTracker.cpp:407-422) - Settings update with backward compatibility
- [`engine/src/SimpleBallTracker.cpp`](engine/src/SimpleBallTracker.cpp:1021-1091) - Override state verification
- [`engine/src/SimpleBallTracker.cpp`](engine/src/SimpleBallTracker.cpp:2856-2920) - Throw detection logic
- [`engine/src/SimpleBallTracker.cpp`](engine/src/SimpleBallTracker.cpp:2101-2206) - Catch detection logic

## See Also

- [BALL_TRACKER_POSITION_LOGIC.md](BALL_TRACKER_POSITION_LOGIC.md) - Complete tracking logic documentation
- [TRACKING_THRESHOLD_CONSOLIDATION.md](TRACKING_THRESHOLD_CONSOLIDATION.md) - Changelog and migration guide
- [TRAJECTORY_PREDICTION_MISSING_FRAMES.md](TRAJECTORY_PREDICTION_MISSING_FRAMES.md) - Trajectory prediction improvements
- [UI_SETTINGS_CHANGE_CHECKLIST.md](UI_SETTINGS_CHANGE_CHECKLIST.md) - How to add/remove UI settings
- [TRACKING_TUNING_GUIDE.md](TRACKING_TUNING_GUIDE.md) - User-facing tuning guide