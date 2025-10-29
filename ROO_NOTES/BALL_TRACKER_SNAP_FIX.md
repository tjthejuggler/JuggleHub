# Ball Tracker Snap Fix

**Date:** 2025-10-21  
**Issue:** Pink ball tracker incorrectly snapping to left hand when losing YOLO detection

## Problems Identified

### 1. Recording Frame Numbers Not Displayed
The debug logs showed only runtime frame numbers (FRAME 630, 631, etc.) but not the recording frame numbers, making it difficult to correlate with recording.log output.

### 2. Ball Tracker Snapping to Wrong Hand
When a ball was thrown from the right hand and YOLO lost detection for a few frames, the ball tracker incorrectly:
- Changed from IN_FLIGHT to HELD state
- Snapped to the left hand's wrist position
- Did this WITHOUT any catch detection being logged

## Root Cause

The issue was in the `createNewTracks()` function's re-acquisition logic. When a ball lost YOLO detection and was then re-acquired:

```cpp
// OLD BUGGY CODE:
if (near_hand) {
    ball->state = HELD;
    ball->associated_hand_id = closest_hand_id;
    ball->tracking_reason = "Re-acquired (HELD)";
}
```

This would set ANY ball near a hand to HELD state, even if:
- The ball was IN_FLIGHT (just thrown)
- No catch detection occurred
- The ball was near a DIFFERENT hand than the one that threw it

## Fixes Applied

### Fix 1: Display Recording Frame Numbers
Changed the frame header logging to show both frame numbers on the same line:

```cpp
// NEW CODE:
if (recording_frame_number_ >= 0) {
    logDebug("FRAME ", frame_counter_, " (Recording Frame: ", recording_frame_number_, ")");
} else {
    logDebug("FRAME ", frame_counter_);
}
```

**Result:** Logs now show:
```
FRAME 630 (Recording Frame: 45)
```

### Fix 2: Preserve Ball State During Re-acquisition
Modified the re-acquisition logic to respect the ball's previous state:

```cpp
// NEW FIXED CODE:
if (near_hand && ball->state == HELD) {
    // Ball was held, lost detection briefly, now re-acquired near same/different hand
    ball->state = HELD;
    ball->associated_hand_id = closest_hand_id;
    ball->tracking_reason = "Re-acquired (HELD)";
    logDebug("  Re-acquired ball ", ball->id, " as HELD by hand ", closest_hand_id);
} else {
    // Ball was IN_FLIGHT or not near any hand - keep/set as IN_FLIGHT
    ball->state = IN_FLIGHT;
    ball->associated_hand_id = -1;
    ball->tracking_reason = "Re-acquired (IN_FLIGHT)";
    logDebug("  Re-acquired ball ", ball->id, " as IN_FLIGHT");
}
```

**Key Change:** Added `&& ball->state == HELD` condition

This ensures:
1. **If ball was HELD** before losing detection → can be re-acquired as HELD (hand-off or brief occlusion)
2. **If ball was IN_FLIGHT** → stays IN_FLIGHT until proper catch detection occurs
3. **Catch detection** must happen through `handleInFlightStateUpdate()` with proper distance checks

### Fix 3: Added Finalize Position Logging
Added debug logging in `finalizeBallPositions()` to track when and why ball positions are set to hand wrists:

```cpp
if (holding_hand) {
    ball.last_known_position = holding_hand->wrist_pos_3d;
    logDebug("  Ball ", ball.id, " finalized at hand ", ball.associated_hand_id, " wrist");
}
```

## Expected Behavior After Fix

### Scenario: Ball Thrown, YOLO Lost, Then Re-acquired

**Before Fix:**
1. Frame 630: Ball HELD by right hand
2. Frame 631: Ball thrown → IN_FLIGHT
3. Frames 631-633: YOLO loses detection
4. Frame 634: Ball re-acquired near left hand → **WRONGLY** set to HELD by left hand ❌

**After Fix:**
1. Frame 630: Ball HELD by right hand
2. Frame 631: Ball thrown → IN_FLIGHT
3. Frames 631-633: YOLO loses detection, ball stays IN_FLIGHT
4. Frame 634: Ball re-acquired → **CORRECTLY** stays IN_FLIGHT ✅
5. Frame 635+: Ball approaches left hand → Proper catch detection → HELD by left hand ✅

## State Transition Rules

### Valid Transitions

1. **HELD → IN_FLIGHT**
   - Via throw detection (distance + velocity thresholds exceeded)
   - Via hand lost (holding hand no longer detected)

2. **IN_FLIGHT → HELD**
   - **ONLY** via catch detection in `handleInFlightStateUpdate()`
   - Requires ball to come within `held_radius_m` of a hand
   - Properly logged as "CATCH DETECTED"

3. **HELD → HELD** (hand-off)
   - Ball moves from one hand to another while staying within `held_radius_m`
   - Logged as "Hand-off detected"

### Invalid Transitions (Now Prevented)

1. **IN_FLIGHT → HELD** during re-acquisition ❌
   - This was the bug - now fixed
   - Re-acquisition preserves IN_FLIGHT state

## Testing

To verify the fix works:

1. Run with debug logging:
   ```bash
   ./scripts/run_hub.sh --use-venv --device GPU --engine-log
   ```

2. Perform juggling with brief YOLO detection losses

3. Check `engine_debug.log` for:
   - Recording frame numbers displayed correctly
   - No unexpected state transitions from IN_FLIGHT to HELD
   - All HELD transitions have corresponding "CATCH DETECTED" logs
   - Re-acquisition preserves ball state correctly

## Files Modified

- [`engine/src/New3DTracker.cpp`](engine/src/New3DTracker.cpp)
  - Fixed frame number logging
  - Fixed re-acquisition state logic
  - Added finalize position logging

## Related Documentation

- [`ENGINE_DEBUG_LOGGING_GUIDE.md`](ENGINE_DEBUG_LOGGING_GUIDE.md) - How to use debug logging
- [`NEW_3D_TRACKER_ARCHITECTURE.md`](NEW_3D_TRACKER_ARCHITECTURE.md) - Tracker architecture