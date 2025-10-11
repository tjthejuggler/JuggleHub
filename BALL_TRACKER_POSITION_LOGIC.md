# Ball Tracker Position Logic Documentation

**Last Updated:** 2025-10-11

This document explains the complete ball tracking position logic in the SimpleBallTracker system, including state transitions, override detection, and the unified hand distance threshold.

## Table of Contents

1. [Overview](#overview)
2. [Ball States](#ball-states)
3. [Tracking Flow](#tracking-flow)
4. [Override Detection Logic](#override-detection-logic)
5. [State Transitions](#state-transitions)
6. [Key Variables Reference](#key-variables-reference)

---

## Overview

The SimpleBallTracker uses a two-state system (HELD/IN_FLIGHT) with trajectory-based prediction and override detection to accurately track juggling balls. The system prioritizes high-confidence, color-matched YOLO detections while maintaining smooth tracking through prediction when detections are unavailable.

### Core Principles

1. **Override Priority**: High-confidence, color-matched detections immediately override current tracking
2. **Distance-Based State Verification**: Ball state (HELD/IN_FLIGHT) is determined by distance from hands, not YOLO class
3. **Unified Threshold**: Single [`hand_distance_threshold`](engine/include/SimpleBallTracker.hpp:149) for all hand-ball proximity checks
4. **Trajectory Prediction**: Physics-based prediction maintains tracking between detections

---

## Ball States

### HELD State
- Ball is in a hand
- Tracker position follows wrist position
- Waiting for throw detection

### IN_FLIGHT State
- Ball is airborne
- Tracker uses trajectory prediction
- Searching for catches

---

## Tracking Flow

The tracking system follows this sequence each frame:

```
1. Run YOLO Detection (balls + hands)
2. Check Override Logic (Step 6)
3. Update Ball States:
   - If HELD: Check for throw (Step 7)
   - If IN_FLIGHT: Check for catch (Step 8)
4. Generate Events (throws/catches)
```

---

## Override Detection Logic

**Location:** [`SimpleBallTracker::update()`](engine/src/SimpleBallTracker.cpp:833)

### Step 6: Override Detection

Override detection allows high-confidence, color-matched YOLO detections to immediately take control of ball tracking, regardless of current state. This ensures the tracker doesn't "lose" a ball when YOLO has a clear view of it.

#### Override Criteria

A detection overrides current tracking when ALL conditions are met:

1. **Confidence Threshold**: Detection confidence ≥ threshold
   - For `ball` class (class_id=0): [`override_ball_confidence_threshold`](engine/include/SimpleBallTracker.hpp:194) (default: 0.7)
   - For `ball_held` class (class_id=1): [`override_ball_held_confidence_threshold`](engine/include/SimpleBallTracker.hpp:196) (default: 0.7)

2. **Color Match Threshold**: Color match score ≥ threshold
   - For `ball` class: [`override_ball_color_threshold`](engine/include/SimpleBallTracker.hpp:195) (default: 0.8)
   - For `ball_held` class: [`override_ball_held_color_threshold`](engine/include/SimpleBallTracker.hpp:197) (default: 0.8)

3. **Class Requirement** (optional): Detection must be `ball` class (class_id=0)
   - Controlled by [`override_require_ball_class`](engine/include/SimpleBallTracker.hpp:199) (default: true)

#### State Verification After Override

**CRITICAL:** After an override positions a ball, the system verifies the ball's state based on **distance from hands**, NOT the YOLO class_id:

```cpp
// Calculate distance from override detection position to each hand
float min_dist = distance_to_closest_hand(ball.position, hands);
int closest_hand_id = find_closest_hand_id(ball.position, hands);

// Distance-based state determination (ignore YOLO class_id)
if (closest_hand_id >= 0 && min_dist < hand_distance_threshold) {
    // Ball is near a hand - set to HELD state
    ball.state = HELD;
    ball.held_by_hand_id = closest_hand_id;
} else {
    // Ball is far from hands - set to IN_FLIGHT state
    ball.state = IN_FLIGHT;
    ball.held_by_hand_id = -1;
}
```

**Why Distance-Based?**
- YOLO class_id can be unreliable (ball vs ball_held confusion)
- Distance provides objective, consistent state determination
- Prevents state oscillation from class prediction noise
- Single source of truth: [`hand_distance_threshold`](engine/include/SimpleBallTracker.hpp:149)

**Implementation:** [`SimpleBallTracker::update()`](engine/src/SimpleBallTracker.cpp:1021-1091)

---

## State Transitions

### Step 7: Throw Detection (HELD → IN_FLIGHT)

**Location:** [`updateHeldBall()`](engine/src/SimpleBallTracker.cpp:2693)

A throw is detected when:

1. **Distance Check**: Detection is > [`hand_distance_threshold`](engine/include/SimpleBallTracker.hpp:149) from hand
2. **Movement Check**: Ball has moved ≥ 50% of [`hand_distance_threshold`](engine/include/SimpleBallTracker.hpp:149) from previous position
3. **Proximity Check**: Detection is < [`max_tracker_distance_per_frame`](engine/include/SimpleBallTracker.hpp:200) from ball
4. **Color Match**: Detection matches ball color (score > [`min_color_match_score`](engine/include/SimpleBallTracker.hpp:414))

**Why Movement Check?**
Prevents false throws from:
- Small hand jitter
- Hand movement while holding ball
- Tracking noise

**Implementation:** [`SimpleBallTracker::updateHeldBall()`](engine/src/SimpleBallTracker.cpp:2856-2920)

### Step 8: Catch Detection (IN_FLIGHT → HELD)

**Location:** [`updateInFlightBall()`](engine/src/SimpleBallTracker.cpp:2018)

A catch is detected when:

1. **Distance Check**: Ball position < [`hand_distance_threshold`](engine/include/SimpleBallTracker.hpp:149) from hand
2. **Movement Check**: Ball has moved ≥ [`hand_distance_threshold`](engine/include/SimpleBallTracker.hpp:149) from throw position
3. **Debouncing**: Not the throwing hand OR ball has been in flight for ≥10 frames

**Why Movement Check?**
Prevents immediate re-catch by throwing hand:
- Ball must travel away from throw position
- Ensures ball has actually left the hand
- Prevents catch-throw-catch oscillation

**Implementation:** [`SimpleBallTracker::updateInFlightBall()`](engine/src/SimpleBallTracker.cpp:2101-2206)

---

## Key Variables Reference

### Unified Hand Distance Threshold

**Primary Variable:**
- [`hand_distance_threshold`](engine/include/SimpleBallTracker.hpp:149) (default: 0.30m)
  - **Purpose**: Distance threshold for all hand-ball proximity checks
  - **Used For**:
    - Override state verification (is ball near a hand?)
    - Throw detection (is ball far enough from hand?)
    - Catch detection (is ball close enough to hand?)
    - Movement verification (has ball moved away from throw position?)

**Deprecated Variables (Backward Compatibility):**
- [`throw_distance_threshold`](engine/include/SimpleBallTracker.hpp:152) - **DEPRECATED**: Use `hand_distance_threshold` instead
- [`catch_distance_threshold`](engine/include/SimpleBallTracker.hpp:153) - **DEPRECATED**: Use `hand_distance_threshold` instead

**Note:** When old threshold names are used in settings, they automatically update `hand_distance_threshold` to maintain backward compatibility. See [`SimpleBallTracker::updateSetting()`](engine/src/SimpleBallTracker.cpp:415-422).

### Override Detection Thresholds

**Ball Class (class_id=0):**
- [`override_ball_confidence_threshold`](engine/include/SimpleBallTracker.hpp:194) (default: 0.7)
- [`override_ball_color_threshold`](engine/include/SimpleBallTracker.hpp:195) (default: 0.8)

**Ball Held Class (class_id=1):**
- [`override_ball_held_confidence_threshold`](engine/include/SimpleBallTracker.hpp:196) (default: 0.7)
- [`override_ball_held_color_threshold`](engine/include/SimpleBallTracker.hpp:197) (default: 0.8)

**Class Requirement:**
- [`override_require_ball_class`](engine/include/SimpleBallTracker.hpp:199) (default: true)

### Other Tracking Parameters

- [`min_frames_for_transition`](engine/include/SimpleBallTracker.hpp:155) (default: 2) - Debouncing for state changes
- [`max_tracker_distance_per_frame`](engine/include/SimpleBallTracker.hpp:200) (default: 0.50m) - Maximum ball movement per frame
- [`min_color_match_score`](engine/include/SimpleBallTracker.hpp:414) (default: 0.5) - Minimum color match for detection acceptance

### Trajectory Parameters

- [`traj_search_radius`](engine/include/SimpleBallTracker.hpp:163) (default: 0.15m) - Search radius along predicted trajectory
- [`traj_gravity`](engine/include/SimpleBallTracker.hpp:158) (default: 9.81 m/s²) - Gravitational acceleration
- [`traj_time_step`](engine/include/SimpleBallTracker.hpp:159) (default: 0.033s) - Time between predicted points

### Visualization Settings

- [`show_hand_distance_threshold`](engine/include/SimpleBallTracker.hpp:228) (default: true) - Show threshold circles around hands
- [`show_throw_distance_threshold`](engine/include/SimpleBallTracker.hpp:230) - **DEPRECATED**: Use `show_hand_distance_threshold` instead
- [`show_catch_distance_threshold`](engine/include/SimpleBallTracker.hpp:231) - **DEPRECATED**: Use `show_hand_distance_threshold` instead

**Note:** Old visualization setting names automatically map to `show_hand_distance_threshold` for backward compatibility. See [`SimpleBallTracker::updateSetting()`](engine/src/SimpleBallTracker.cpp:397-403).

---

## Backward Compatibility

The system maintains backward compatibility with old threshold names:

### Settings File Compatibility

Old settings automatically convert to new unified threshold:
```json
{
  "catch_distance_threshold": 0.30,  // Automatically sets hand_distance_threshold
  "throw_distance_threshold": 0.20   // Automatically sets hand_distance_threshold
}
```

New settings format:
```json
{
  "hand_distance_threshold": 0.30    // Single unified threshold
}
```

### UI Compatibility

Old UI setting names are automatically mapped:
- `show_catch_distance_threshold` → `show_hand_distance_threshold`
- `show_throw_distance_threshold` → `show_hand_distance_threshold`

### Code Compatibility

Legacy threshold variables are kept in [`TrackingSettings`](engine/include/SimpleBallTracker.hpp:146) but marked as deprecated. They are automatically synchronized with `hand_distance_threshold` when settings are updated.

---

## Summary

The tracking system uses a **unified distance threshold** approach:

1. **Single Threshold**: [`hand_distance_threshold`](engine/include/SimpleBallTracker.hpp:149) defines "how close is close enough to a hand"
2. **Distance-Based States**: Ball state determined by distance from hands, not YOLO class
3. **Override Priority**: High-confidence detections immediately take control
4. **Movement Verification**: State transitions require actual ball movement to prevent false detections
5. **Backward Compatible**: Old threshold names automatically convert to new unified system

This design provides:
- ✅ Simpler configuration (one threshold instead of two)
- ✅ More accurate state detection (distance-based, not class-based)
- ✅ Consistent behavior (single source of truth)
- ✅ Easier tuning (adjust one value for all hand-ball proximity checks)
- ✅ Seamless migration (automatic conversion from old settings)