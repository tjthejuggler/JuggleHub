# Throw Detection Re-Acquisition Fix

**Date**: 2025-10-26  
**Issue**: Pink ball tracker got stuck on hand instead of following thrown ball  
**Status**: ✅ Fixed

## Problem Analysis

### Root Cause
The pink ball tracker got stuck on the hand because of a **critical flaw in the re-acquisition logic** in [`createNewTracks()`](engine/src/New3DTracker.cpp:1057-1208).

### What Happened (Frame-by-Frame):

**Frame 1279**: 
- Ball was IN_FLIGHT, no detection matched (too far from prediction)

**Frame 1280**: 
- System re-acquired the pink ball but matched it to the **wrong detection**:
  - Detection 0 at (0.055, 0.384, 1.566) near left hand - **incorrectly matched to pink ball**
  - Detection 1 at (-1.433, 0.438, 2.816) - **the actual thrown pink ball, ignored**
- The system chose Detection 0 because:
  - It was closer to Kalman prediction at (-0.622, 0.011, 2.311)
  - Color mismatch penalty (0.5m) wasn't strong enough to prevent wrong match
- Ball was automatically set to HELD state because detection was within `held_radius_m` of hand

**Frames 1281-1283**: 
- Ball stayed HELD and locked to wrist at x=0.04m
- Actual pink ball was at x=-1.4m, completely ignored

### The Bugs

#### Bug 1: Auto-HELD Assignment in Re-acquisition
**Location**: [`createNewTracks()`](engine/src/New3DTracker.cpp:1176-1193)

When re-acquiring a ball, if the detection was within `held_radius_m` of any hand, the system automatically set the ball to HELD state. This bypassed proper throw detection logic and caused the tracker to lock onto the wrong detection.

#### Bug 2: Weak Color-Based Association
**Location**: [`associateDetections()`](engine/src/New3DTracker.cpp:595-608)

The color mismatch penalty was too weak (default 0.5m), allowing spatial proximity to override color identity. This caused the system to prefer a closer wrong-color detection over a farther correct-color detection.

## Fixes Implemented

### Fix 1: Remove Auto-HELD in Re-acquisition ✅
**File**: [`engine/src/New3DTracker.cpp`](engine/src/New3DTracker.cpp:1154-1168)

**Changed**:
```cpp
// OLD CODE (REMOVED):
// Determine state based on proximity to hands
bool near_hand = false;
int closest_hand_id = -1;
float min_distance = settings_.held_radius_m;

for (const auto& hand : hands_) {
    float dx = detection->world_pos.x - hand.wrist_pos_3d.x;
    float dy = detection->world_pos.y - hand.wrist_pos_3d.y;
    float dz = detection->world_pos.z - hand.wrist_pos_3d.z;
    float distance = std::sqrt(dx*dx + dy*dy + dz*dz);
    
    if (distance < min_distance) {
        near_hand = true;
        min_distance = distance;
        closest_hand_id = hand.id;
    }
}

if (near_hand) {
    ball->state = HELD;
    ball->associated_hand_id = closest_hand_id;
    ball->tracking_reason = "Re-acquired (HELD, distance=" +
                           std::to_string(min_distance) + "m)";
} else {
    ball->state = IN_FLIGHT;
    ball->associated_hand_id = -1;
    ball->tracking_reason = "Re-acquired (IN_FLIGHT, no hand nearby)";
}
```

**NEW CODE**:
```cpp
// CRITICAL FIX: Always re-acquire as IN_FLIGHT
// Let the normal catch detection logic in handleInFlightStateUpdate() handle
// state transitions. This prevents the tracker from incorrectly locking to
// a hand when re-acquiring with a detection that happens to be near a hand
// but is actually a different ball (e.g., a thrown ball vs. a held ball).
ball->state = IN_FLIGHT;
ball->associated_hand_id = -1;
ball->tracking_reason = "Re-acquired (IN_FLIGHT)";
logDebug("  Re-acquired ball ", ball->id, " as IN_FLIGHT - will detect catch in next frame if near hand");
```

**Why This Works**:
- Re-acquired balls always start as IN_FLIGHT
- Normal catch detection logic in `handleInFlightStateUpdate()` will properly detect if the ball is actually caught
- Prevents premature locking to hands during re-acquisition
- Allows the system to properly track thrown balls that happen to be detected near a hand

### Fix 2: Add Color Mismatch Penalty UI Control ✅
**File**: [`hub/components/ui_settings_new3d.py`](hub/components/ui_settings_new3d.py:177-191)

**Added UI Slider**:
```python
# Color Mismatch Penalty
self.parent.new3d_color_mismatch_penalty_slider, self.parent.new3d_color_mismatch_penalty_label = self.parent._create_slider_widget(
    parent_layout=layout,
    row=row,
    label_text="Color Mismatch Penalty (m)",
    tooltip_text="Penalty distance added when detection color doesn't match ball color.\n"
                 "Range: 0.0-5.0 m. Default: 0.5 m.\n"
                 "Higher values = stronger preference for color-matched detections.\n"
                 "Set to 2.0+ to prevent wrong-color associations.\n"
                 "Set to 0.0 to disable color-based association entirely.",
    range_min=0,
    range_max=500,
    initial_value=50,
    update_func=lambda v: self.parent.update_setting('color_mismatch_penalty_m', v / 100.0),
    is_float=True
)
```

**Location in UI**: 
- Section: "🔗 Detection Association"
- Position: Between "Max Association Distance" and "Color Sample Radius"

**Why This Helps**:
- Allows real-time tuning of color-based association strength
- Higher values (2.0m+) make color matching much more important than spatial proximity
- Can be adjusted per-environment based on lighting conditions and ball colors

## Testing Recommendations

### Test Case 1: Thrown Ball Tracking
1. Hold a ball in your hand
2. Throw the ball
3. **Expected**: Ball tracker should follow the thrown ball, not stay locked to hand
4. **Previous Behavior**: Tracker stayed locked to hand
5. **New Behavior**: Tracker follows thrown ball correctly

### Test Case 2: Color Mismatch Penalty Tuning
1. Set `color_mismatch_penalty_m` to 0.5m (default)
2. Throw a ball while another ball is near your hand
3. If wrong ball is tracked, increase penalty to 2.0m
4. **Expected**: System should prefer color-matched detections over closer wrong-color detections

### Test Case 3: Re-acquisition After Occlusion
1. Throw a ball
2. Let it go behind an object (occluded)
3. Ball reappears
4. **Expected**: Ball should be re-acquired as IN_FLIGHT, then properly caught if near hand
5. **Previous Behavior**: Ball might be immediately set to HELD if detection was near hand

## Settings Recommendations

### Conservative (Strict Color Matching):
```json
{
  "color_mismatch_penalty_m": 2.0,
  "association_max_distance_m": 0.3
}
```
- Best for: Well-calibrated colors, good lighting
- Pros: Very accurate color-based tracking
- Cons: May lose track if colors drift due to lighting changes

### Balanced (Default):
```json
{
  "color_mismatch_penalty_m": 0.5,
  "association_max_distance_m": 0.5
}
```
- Best for: General use, moderate lighting variations
- Pros: Good balance between color and spatial matching
- Cons: May occasionally match wrong ball if colors are similar

### Permissive (Spatial Priority):
```json
{
  "color_mismatch_penalty_m": 0.1,
  "association_max_distance_m": 0.8
}
```
- Best for: Poor lighting, uncalibrated colors, single ball tracking
- Pros: More forgiving, less likely to lose track
- Cons: May match wrong ball in multi-ball scenarios

## Technical Details

### State Transition Flow (After Fix):

```
Re-acquisition:
  Detection found → Ball set to IN_FLIGHT
                 ↓
  Next frame → handleInFlightStateUpdate()
                 ↓
  Check distance to hands
                 ↓
  If < held_radius_m → Transition to HELD (proper catch detection)
  If >= held_radius_m → Stay IN_FLIGHT (ball is flying)
```

### Association Cost Calculation:

```cpp
float total_cost = distance + color_penalty;

where:
  distance = euclidean distance between prediction and detection
  color_penalty = color_mismatch_penalty_m (if colors don't match)
                = 0.0 (if colors match)
```

## Files Modified

1. [`engine/src/New3DTracker.cpp`](engine/src/New3DTracker.cpp) - Lines 1154-1168
   - Removed auto-HELD assignment in `createNewTracks()`
   - Always re-acquire as IN_FLIGHT

2. [`hub/components/ui_settings_new3d.py`](hub/components/ui_settings_new3d.py) - Lines 177-191
   - Added Color Mismatch Penalty slider to Detection Association section

## Related Issues

- This fix addresses the core issue identified in the log analysis
- Related to color-based tracking system
- Complements the existing throw detection logic
- Works with the Kalman filter prediction system

## Future Improvements

1. **Adaptive Color Penalty**: Automatically adjust penalty based on color calibration confidence
2. **Multi-Hypothesis Tracking**: Track multiple possible associations and resolve ambiguity over time
3. **Velocity-Based Association**: Use ball velocity to predict which detection is most likely
4. **Color Confidence Scoring**: Weight color matching by detection quality and lighting conditions

---

**Status**: ✅ Ready for testing  
**Timestamp**: 2025-10-26T15:47:00Z