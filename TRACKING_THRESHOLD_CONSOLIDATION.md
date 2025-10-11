# Tracking Threshold Consolidation - Changelog & Migration Guide

**Date:** 2025-10-11  
**Status:** Completed  
**Impact:** Low (Backward Compatible)

## Summary

Consolidated separate `catch_distance_threshold` and `throw_distance_threshold` into a single unified `hand_distance_threshold` for simpler, more accurate ball tracking.

---

## What Changed

### Before: Separate Thresholds

Previously, the system used two separate thresholds:

```cpp
float throw_distance_threshold = 0.20f;  // Min distance to detect throw
float catch_distance_threshold = 0.30f;  // Max distance to detect catch
```

**Problems:**
- ❌ Two values to tune for the same concept ("how close is close to a hand?")
- ❌ Inconsistent: throw used 0.20m, catch used 0.30m
- ❌ Confusing: which threshold applies in override state verification?
- ❌ More complex UI: two sliders instead of one

### After: Unified Threshold

Now uses a single threshold:

```cpp
float hand_distance_threshold = 0.30f;  // Distance threshold for hand-ball proximity
```

**Benefits:**
- ✅ Single value to tune for all hand-ball proximity checks
- ✅ Consistent behavior across all state transitions
- ✅ Simpler mental model: "how close must a ball be to a hand?"
- ✅ Clearer code: one source of truth
- ✅ Simpler UI: single slider and toggle

---

## Why This Change?

### 1. Conceptual Clarity

The fundamental question is: **"How close must a ball be to a hand to be considered held?"**

This applies to:
- **Throw detection**: Ball moving away from hand beyond threshold
- **Catch detection**: Ball moving toward hand within threshold  
- **Override state verification**: Is ball near a hand or not?

Having two different thresholds for the same concept was confusing and unnecessary.

### 2. Override Logic Fix

The override detection logic needed to determine ball state (HELD vs IN_FLIGHT) based on distance from hands. With two thresholds, it was unclear which to use. The unified threshold provides a clear answer.

**Old Logic (Ambiguous):**
```cpp
// Which threshold to use here?
if (distance < catch_distance_threshold) {  // or throw_distance_threshold?
    state = HELD;
}
```

**New Logic (Clear):**
```cpp
// Single source of truth
if (distance < hand_distance_threshold) {
    state = HELD;
}
```

### 3. Simpler Tuning

Users now adjust one value that affects all hand-ball proximity checks consistently:
- Set threshold higher (e.g., 0.40m) → More forgiving catches, earlier throw detection
- Set threshold lower (e.g., 0.20m) → Stricter catches, later throw detection

### 4. Distance-Based State Verification

The override logic now uses **distance-based state verification** instead of relying on YOLO class_id:

```cpp
// Calculate distance from override detection to closest hand
float min_dist = distance_to_closest_hand(ball.position, hands);

// Distance-based state determination (ignore YOLO class_id)
if (min_dist < hand_distance_threshold) {
    ball.state = HELD;
    ball.held_by_hand_id = closest_hand_id;
} else {
    ball.state = IN_FLIGHT;
    ball.held_by_hand_id = -1;
}
```

This is more reliable than YOLO class predictions, which can be noisy.

---

## Migration Guide

### For Users

**No action required!** The system automatically handles migration.

#### Settings File Migration

**Old format (still works):**
```json
{
  "catch_distance_threshold": 0.30,
  "throw_distance_threshold": 0.20
}
```

**New format (recommended):**
```json
{
  "hand_distance_threshold": 0.30
}
```

When old settings are loaded:
1. System reads `catch_distance_threshold` or `throw_distance_threshold`
2. Automatically sets `hand_distance_threshold` to that value
3. Both old variables are updated to match for compatibility
4. No data loss, no manual conversion needed

#### UI Migration

**Old UI elements:**
- `show_catch_distance_threshold` toggle
- `show_throw_distance_threshold` toggle

**New UI elements:**
- `show_hand_distance_threshold` toggle (single unified control)

Old setting names automatically map to the new unified setting.

### For Developers

#### Code Changes

**Header File:** [`engine/include/SimpleBallTracker.hpp`](engine/include/SimpleBallTracker.hpp:146-153)

```cpp
struct TrackingSettings {
    // NEW: Unified threshold
    float hand_distance_threshold = 0.30f;    // Distance threshold for hand-ball proximity (m)
    
    // DEPRECATED: Legacy thresholds kept for backward compatibility
    float throw_distance_threshold = 0.20f;   // DEPRECATED: Use hand_distance_threshold instead
    float catch_distance_threshold = 0.30f;   // DEPRECATED: Use hand_distance_threshold instead
    
    // ... rest of settings
};
```

**Settings Update:** [`engine/src/SimpleBallTracker.cpp`](engine/src/SimpleBallTracker.cpp:407-422)

```cpp
// NEW: Unified hand_distance_threshold
else if (key == "hand_distance_threshold") {
    tracking_settings_.hand_distance_threshold = std::stof(value);
    // Also update legacy thresholds for backward compatibility
    tracking_settings_.catch_distance_threshold = std::stof(value);
    tracking_settings_.throw_distance_threshold = std::stof(value);
    return true;
}
// DEPRECATED: Legacy threshold names (auto-convert to unified)
else if (key == "catch_distance_threshold") {
    tracking_settings_.hand_distance_threshold = std::stof(value);
    tracking_settings_.catch_distance_threshold = std::stof(value);
    return true;
}
else if (key == "throw_distance_threshold") {
    tracking_settings_.hand_distance_threshold = std::stof(value);
    tracking_settings_.throw_distance_threshold = std::stof(value);
    return true;
}
```

#### Usage in Code

**Replace:**
```cpp
if (distance > tracking_settings_.throw_distance_threshold) {
    // throw logic
}
if (distance < tracking_settings_.catch_distance_threshold) {
    // catch logic
}
```

**With:**
```cpp
if (distance > tracking_settings_.hand_distance_threshold) {
    // throw logic
}
if (distance < tracking_settings_.hand_distance_threshold) {
    // catch logic
}
```

#### Visualization Settings

**Header File:** [`engine/include/SimpleBallTracker.hpp`](engine/include/SimpleBallTracker.hpp:228-231)

```cpp
struct TrajectoryVisualizationSettings {
    bool show_hand_distance_threshold = true;  // Show unified hand distance threshold circles
    
    // DEPRECATED: Keep for backward compatibility
    bool show_throw_distance_threshold = true;  // DEPRECATED: Use show_hand_distance_threshold
    bool show_catch_distance_threshold = true;  // DEPRECATED: Use show_hand_distance_threshold
    
    // ... rest of settings
};
```

**Settings Update:** [`engine/src/SimpleBallTracker.cpp`](engine/src/SimpleBallTracker.cpp:392-403)

```cpp
else if (key == "show_hand_distance_threshold") {
    viz_settings_.show_hand_distance_threshold = (value == "true" || value == "1");
    return true;
}
// DEPRECATED: Legacy visualization settings (auto-convert)
else if (key == "show_throw_distance_threshold") {
    viz_settings_.show_hand_distance_threshold = (value == "true" || value == "1");
    return true;
}
else if (key == "show_catch_distance_threshold") {
    viz_settings_.show_hand_distance_threshold = (value == "true" || value == "1");
    return true;
}
```

---

## Technical Details

### Override State Verification

The most significant change is in override detection logic:

**Location:** [`SimpleBallTracker::update()`](engine/src/SimpleBallTracker.cpp:1021-1091)

```cpp
// After override positions ball, verify state based on distance
float min_dist = std::numeric_limits<float>::max();
int closest_hand_id = -1;

for (const auto& hand : hands_) {
    if (!hand.is_visible) continue;
    float dist = cv::norm(ball.position - hand.wrist_pos_3d);
    if (dist < min_dist) {
        min_dist = dist;
        closest_hand_id = hand.id;
    }
}

// Use distance-based state determination (ignore YOLO class_id)
if (closest_hand_id >= 0 && min_dist < tracking_settings_.hand_distance_threshold) {
    // Ball is near a hand - set to HELD state
    ball.state = HELD;
    ball.held_by_hand_id = closest_hand_id;
} else {
    // Ball is far from hands - set to IN_FLIGHT state
    ball.state = IN_FLIGHT;
    ball.held_by_hand_id = -1;
}
```

**Key Points:**
- Uses distance, not YOLO class_id
- Single threshold for consistent behavior
- More reliable than class predictions
- Prevents state oscillation

### Throw Detection

**Location:** [`SimpleBallTracker::updateHeldBall()`](engine/src/SimpleBallTracker.cpp:2856-2920)

```cpp
// Ball must be beyond hand_distance_threshold from hand
if (dist_from_hand > tracking_settings_.hand_distance_threshold &&
    dist_from_ball < tracking_settings_.max_tracker_distance_per_frame) {
    
    // Also requires movement of at least 50% of threshold
    float min_movement_threshold = tracking_settings_.hand_distance_threshold * 0.5f;
    
    if (distance_moved >= min_movement_threshold) {
        // Initiate throw
    }
}
```

### Catch Detection

**Location:** [`SimpleBallTracker::updateInFlightBall()`](engine/src/SimpleBallTracker.cpp:2101-2206)

```cpp
// Ball must be within hand_distance_threshold of hand
if (dist_to_hand < tracking_settings_.hand_distance_threshold) {
    
    // Also requires ball has moved away from throw position
    float distance_from_throw = cv::norm(ball.position - ball.trajectory.throw_position);
    bool has_moved_away = (distance_from_throw >= tracking_settings_.hand_distance_threshold);
    
    if (has_moved_away) {
        // Initiate catch
    }
}
```

---

## Default Values

| Setting | Old Default | New Default | Notes |
|---------|-------------|-------------|-------|
| `hand_distance_threshold` | N/A | 0.30m | New unified threshold |
| `throw_distance_threshold` | 0.20m | 0.30m* | *Synced with unified threshold |
| `catch_distance_threshold` | 0.30m | 0.30m* | *Synced with unified threshold |

**Note:** The new default of 0.30m was chosen because:
1. It matches the old `catch_distance_threshold` (more conservative)
2. Provides good balance between sensitivity and accuracy
3. Works well across different juggling patterns
4. Can be tuned up or down based on user preference

---

## Testing

### Verification Steps

1. **Settings Migration:**
   - ✅ Old settings files load correctly
   - ✅ Old threshold names update unified threshold
   - ✅ New settings format works as expected

2. **Override Logic:**
   - ✅ Distance-based state verification works correctly
   - ✅ HELD state assigned when ball near hand
   - ✅ IN_FLIGHT state assigned when ball far from hands

3. **State Transitions:**
   - ✅ Throws detected correctly with unified threshold
   - ✅ Catches detected correctly with unified threshold
   - ✅ No false positives from threshold inconsistency

4. **UI Compatibility:**
   - ✅ Old visualization toggles map to new unified toggle
   - ✅ Single slider controls all hand-ball proximity checks
   - ✅ Threshold circles display correctly around hands

---

## Related Documentation

- [BALL_TRACKER_POSITION_LOGIC.md](BALL_TRACKER_POSITION_LOGIC.md) - Complete tracking logic documentation
- [TRAJECTORY_THROW_CATCH_THRESHOLDS_UI.md](TRAJECTORY_THROW_CATCH_THRESHOLDS_UI.md) - UI implementation (needs update)
- [TRACKING_SETTINGS_AUDIT.md](TRACKING_SETTINGS_AUDIT.md) - Settings audit document

---

## Future Considerations

### Potential Removal of Legacy Variables

In a future major version, we may remove the deprecated threshold variables:

```cpp
// Could be removed in v2.0:
float throw_distance_threshold;  // DEPRECATED
float catch_distance_threshold;  // DEPRECATED
```

**Timeline:** Not before 2026-01-01 to ensure sufficient migration period.

### Additional Consolidation Opportunities

Other threshold pairs that could be unified:
- `override_ball_confidence_threshold` / `override_ball_held_confidence_threshold`
- `override_ball_color_threshold` / `override_ball_held_color_threshold`

However, these serve different purposes (different YOLO classes) and may benefit from separate tuning.

---

## Questions?

For questions or issues related to this change:
1. Check [BALL_TRACKER_POSITION_LOGIC.md](BALL_TRACKER_POSITION_LOGIC.md) for detailed logic explanation
2. Review code comments in [`SimpleBallTracker.hpp`](engine/include/SimpleBallTracker.hpp) and [`SimpleBallTracker.cpp`](engine/src/SimpleBallTracker.cpp)
3. Test with different threshold values to find optimal setting for your use case

**Recommended starting value:** 0.30m (30cm)  
**Tuning range:** 0.15m - 0.50m depending on juggling style and hand size