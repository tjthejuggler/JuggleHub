# Tracker Corruption and Missing Catch Event Fixes

**Date:** 2025-10-09  
**Issue:** Frame 174 tracker vanishing and Kalman prediction corruption + Missing catch events

## Problems Identified

### 1. Color Predictor Corruption (Frame 174 Issue)

**Symptom:** Trackers vanished and Kalman predictions went haywire with velocities of 179+ m/s

**Root Cause:** Bad depth readings from RealSense sensor caused position teleportation, which was added to the color predictor without validation. This corrupted velocity calculations:

- **Frame 173→174:** Ball 2 jumped from `(-0.17, -0.07, 1.67)m` to `(-1.32, 1.44, 3.35)m` (~2.3m jump)
- **Velocity corruption:** Calculated as 25.77 m/s (physically impossible for juggling)
- **Kalman chaos:** Subsequent frames had velocities of 179.65 m/s, 142.35 m/s, 228.31 m/s, etc.

**Code Locations:**
- Line 2836: Held ball wrist snapping added position without validation
- Line 2178: Swipe catch added position without validation  
- Line 2240: Proximity catch added position without validation
- Line 2054: Visible swipe catch added position without validation
- Line 2881: No-profile wrist snap added position without validation

### 2. Missing Catch Events

**Symptom:** Balls transitioning from IN_FLIGHT to HELD state without triggering catch events

**Root Cause:** The catch event generation code (line 1017-1070) was checking `min_throw_distance` and suppressing catch events if the ball was "too close to wrist". This was incorrect because:
- The state change debouncing (`min_frames_for_state_change`) already validates real transitions
- Distance checks were preventing legitimate catches from being detected

## Fixes Applied

### Fix 1: Position Jump Validation for Color Predictor

Added validation before ALL color predictor updates to detect and handle teleportation:

```cpp
// Validate position jump before adding to color predictor
bool should_add_to_predictor = true;
if (ball.color_predictor.getHistorySize() > 0) {
    auto history = ball.color_predictor.getHistory();
    cv::Point3f last_pos = history.back().position;
    float distance = cv::norm(ball.position - last_pos);
    const float MAX_POSITION_JUMP = 0.15f;  // 15cm max for held balls
    
    if (distance > MAX_POSITION_JUMP) {
        should_add_to_predictor = false;
        // Reset color predictor to prevent velocity corruption
        ball.color_predictor = ColorBasedPredictor();
        // Re-initialize with settings
        ColorBasedPredictor::PredictionSettings pred_settings;
        pred_settings.history_frames = tracking_settings_.prediction_history_frames;
        pred_settings.prediction_radius_m = tracking_settings_.prediction_radius_m;
        ball.color_predictor.setSettings(pred_settings);
    }
}

if (should_add_to_predictor) {
    ball.color_predictor.addDetection(ball.position);
}
```

**Applied to:**
1. **Line 2829:** Held ball wrist snapping (fallback tracking)
2. **Line 2174:** Swipe-through catch detection
3. **Line 2236:** Proximity-based catch detection
4. **Line 2050:** Visible ball swipe-through catch
5. **Line 2878:** No-profile wrist snap fallback

**Strategy:** When a large position jump is detected (>15cm for held balls), the color predictor is completely reset instead of adding the corrupted position. This prevents velocity corruption while maintaining tracker visibility.

### Fix 2: Simplified Catch Event Detection

Removed the distance-based suppression logic and simplified to always generate catch events for IN_FLIGHT → HELD transitions:

```cpp
else if (!old_state_was_held && now_held) {
    // Was in air, now held = CATCH
    // CRITICAL FIX: Always generate catch event when transitioning from IN_FLIGHT to HELD
    // The state change debouncing already ensures this is a real transition
    events.push_back({
        BallEvent::CATCH,
        ball.id,
        ball.held_by_hand_id,
        getCurrentTimestamp()
    });
}
```

**Rationale:**
- State change debouncing (`min_frames_for_state_change` = 2 frames) already validates real transitions
- Distance checks were causing false negatives (missing legitimate catches)
- Simpler logic = more reliable catch detection

## Expected Results

### Tracker Stability
- **No more wild Kalman predictions:** Velocities will stay within realistic juggling ranges (<8 m/s)
- **No more tracker vanishing:** Position jumps from sensor errors will be detected and handled gracefully
- **Smooth tracking:** Color predictor history remains clean, providing accurate velocity estimates

### Catch Detection
- **100% catch detection:** Every IN_FLIGHT → HELD transition will generate a catch event
- **No false negatives:** Distance-based suppression removed
- **Reliable event stream:** Apps can trust the catch events for pattern detection

## Testing Recommendations

1. **Verify no velocity corruption:**
   - Monitor color predictor velocities in debug logs
   - Check that velocities stay < 8 m/s during normal juggling
   - Verify Kalman predictions remain on-screen

2. **Verify catch detection:**
   - Count catch events vs visual catches
   - Check that every visible catch generates an event
   - Verify hand ID is correct for each catch

3. **Stress test with bad sensor data:**
   - Test in challenging lighting conditions
   - Test with fast hand movements
   - Verify system recovers gracefully from sensor glitches

## Related Files

- [`engine/src/SimpleBallTracker.cpp`](engine/src/SimpleBallTracker.cpp) - Main tracking implementation
- [`engine/include/ColorBasedPredictor.hpp`](engine/include/ColorBasedPredictor.hpp) - Color predictor interface

## Timestamp

All fixes applied: 2025-10-09 23:04 UTC