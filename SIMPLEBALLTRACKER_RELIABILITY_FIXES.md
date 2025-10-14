# SimpleBallTracker Reliability Fixes

**Date:** 2025-10-14  
**Issue:** Ball identity confusion and false throw/catch events during tracking

## Problems Identified

Based on log analysis, four main problems were causing unreliable tracking:

1. **Duplicate Detection Assignment**: Multiple balls were being assigned to the same YOLO detection, causing identity swaps
2. **Weak Color Matching**: Detections with poor color confidence were overriding correct ball positions
3. **Ball Position Overlap**: Balls at identical positions (except when held by same hand) indicated tracking corruption
4. **False Hand Change Events**: Small position noise was triggering spurious throw/catch events when balls were stationary

## Fixes Implemented

### Fix #1: Override Detection Assignment
**Location:** [`SimpleBallTracker.cpp:1024-1207`](engine/src/SimpleBallTracker.cpp:1024)

**What was broken:**
- Multiple balls could be assigned to the same YOLO detection
- No tracking of which detections were already used
- Led to ball identity swaps and position confusion

**How it was fixed:**
```cpp
// Track which detections have been assigned to prevent duplicates
std::set<int> used_detections;

// For each ball, find BEST UNUSED detection
// Use combined scoring: color_score * 0.6 + distance_score * 0.4
// Mark detection as used after assignment
used_detections.insert(best_detection_index);
```

**Expected improvement:**
- Each detection can only be assigned to ONE ball
- Balls maintain unique identities throughout tracking
- No more position swaps between balls

**Log indicators:**
```
[OVERRIDE] Ball 0 assigned to detection 0
[OVERRIDE] Ball 1 assigned to detection 1  // Different detection!
```

---

### Fix #2: Minimum Color Confidence Threshold
**Location:** [`SimpleBallTracker.cpp:1072-1082`](engine/src/SimpleBallTracker.cpp:1072)

**What was broken:**
- Weak color matches (< 0.35) were accepted for override
- Wrong-colored detections could override correct ball positions
- Led to balls jumping to incorrect detections

**How it was fixed:**
```cpp
// Reject detection if color confidence is too weak
if (color_score < tracking_settings_.min_color_confidence_override) {
    continue;  // Skip this detection
}
```

**Expected improvement:**
- Only strong color matches (≥ 0.35) trigger override
- Reduces false positives from similar-colored objects
- More stable ball identity tracking

**Log indicators:**
```
[OVERRIDE] Detection 0 REJECTED for ball 0
  Reason: color_score 0.28 < threshold 0.35
```

---

### Fix #3: Ball Separation Validation
**Location:** [`SimpleBallTracker.cpp:62-91`](engine/src/SimpleBallTracker.cpp:62)

**What was broken:**
- Balls could end up at identical 3D positions
- Indicated tracking corruption or identity confusion
- No validation to catch this error condition

**How it was fixed:**
```cpp
// Validate minimum separation between balls
// EXCEPTION: If both balls HELD by SAME hand, allow same position
if (balls[i].state == HELD && balls[j].state == HELD &&
    balls[i].held_by_hand_id == balls[j].held_by_hand_id) {
    continue;  // Same hand holding both - valid
}

// Otherwise enforce minimum separation (default: 15cm)
float dist = cv::norm(balls[i].position - balls[j].position);
if (dist < tracking_settings_.min_ball_separation) {
    return false;  // Validation failed
}
```

**Expected improvement:**
- Catches tracking bugs where balls overlap
- Allows legitimate case of multiple balls in same hand
- Provides clear error logging for debugging

**Log indicators:**
```
[BALL_SEPARATION_ERROR] Ball separation violation: balls 0 and 1 are 0.12m apart (min: 0.15m)
  Ball 0 state=1, Ball 1 state=1
```

---

### Fix #4: Hand Change Detection with Movement Threshold
**Location:** [`SimpleBallTracker.cpp:1380-1415`](engine/src/SimpleBallTracker.cpp:1380)

**What was broken:**
- Small position noise (< 25cm) triggered false hand changes
- Generated spurious throw/catch events for stationary balls
- Caused event spam in logs

**How it was fixed:**
```cpp
// Calculate movement since last held position
float movement = glm::distance(ball.position, ball.last_held_position);

// Reject hand change if ball hasn't moved enough
if (movement < tracking_settings_.min_hand_change_distance) {
    // Keep ball with current hand, don't generate events
    continue;
}
```

**Expected improvement:**
- Requires 25cm+ movement for hand change
- Eliminates false events from tracking noise
- More stable held ball tracking

**Log indicators:**
```
[OVERRIDE HAND CHANGE] Hand change rejected: ball moved only 0.08m (need 0.25m+)
  Keeping ball with hand 0
```

---

### Fix #5: Configurable Settings
**Location:** [`SimpleBallTracker.cpp:405-416`](engine/src/SimpleBallTracker.cpp:405)

**What was added:**
Three new configurable thresholds in UI under "Color Tracker Weights":

1. **`min_color_confidence_override`** (default: 0.35)
   - Minimum color score for override detection
   - Higher = stricter color matching

2. **`min_ball_separation`** (default: 0.15m / 15cm)
   - Minimum distance between balls
   - Higher = more separation required

3. **`min_hand_change_distance`** (default: 0.25m / 25cm)
   - Minimum movement for hand change
   - Higher = less sensitive to noise

**Expected improvement:**
- Tunable thresholds for different juggling styles
- Can adjust sensitivity vs stability tradeoff
- Easy experimentation without recompilation

---

## Testing Procedures

### Test 1: Static Hold Test
**Purpose:** Verify balls maintain identity when held still

**Procedure:**
1. Hold yellow ball in left hand, pink ball in right hand
2. Keep both hands still for 10 seconds
3. Check logs for identity swaps

**Expected Result:**
- No ball identity changes
- No false throw/catch events
- Each ball stays assigned to correct hand

**Log Indicators:**
```
✓ Look for: [OVERRIDE] messages showing correct ball-to-detection assignments
✓ No: Ball separation violation errors
✓ No: Spurious [THROW] or [CATCH] events
```

---

### Test 2: Single Ball Throw Test
**Purpose:** Verify one ball can be thrown while other stays held

**Procedure:**
1. Hold yellow ball in left hand, pink ball in right hand
2. Throw pink ball straight up with right hand
3. Keep yellow ball stationary in left hand
4. Catch pink ball with right hand

**Expected Result:**
- Yellow ball maintains identity and stays in left hand
- Pink ball correctly tracked during flight
- Only pink ball generates throw/catch events
- No identity confusion

**Log Indicators:**
```
✓ Yellow ball: Continuous state=HELD, held_by_hand=0 (left hand)
✓ Pink ball: state=HELD → state=IN_FLIGHT → state=HELD
✓ No [OVERRIDE HAND CHANGE] for yellow ball
✓ Movement validation: "Hand change rejected: ball moved only Xm" if noise occurs
```

---

### Test 3: Ball Crossing Test
**Purpose:** Verify balls maintain identity when passing near each other

**Procedure:**
1. Hold both balls
2. Pass them close to each other (within 20cm)
3. Separate them again

**Expected Result:**
- Both balls maintain correct identity
- No position swaps
- Separation validation may trigger if too close (< 15cm)

**Log Indicators:**
```
✓ Each ball assigned to unique detection (different indices)
✓ Color scores above 0.35 threshold
⚠ Possible: Ball separation violation if balls get too close
```

---

### Test 4: Hand Change Test
**Purpose:** Verify legitimate hand changes are detected

**Procedure:**
1. Hold ball in right hand
2. Throw to left hand (25cm+ movement)
3. Catch with left hand

**Expected Result:**
- Throw event generated from right hand
- Ball tracked during flight
- Catch event generated for left hand
- Movement threshold met (≥25cm)

**Log Indicators:**
```
✓ [THROW] Ball X thrown from hand Y
✓ [CATCH] Ball X caught by hand Z
✓ Movement validation passed
✓ No rejection messages
```

---

## New Settings to Tune

Located in UI under "Color Tracker Weights":

### 1. Min Color Confidence Override (default: 0.35)
**Purpose:** Minimum color match score for override detection

**When to adjust:**
- **Increase** (0.40-0.50) if getting false color matches
- **Decrease** (0.25-0.30) if override not triggering when it should
- **Symptoms of too low:** Balls jumping to wrong detections
- **Symptoms of too high:** Override not working, balls lost

---

### 2. Min Ball Separation (default: 15cm)
**Purpose:** Minimum allowed distance between balls

**When to adjust:**
- **Increase** (20-25cm) if balls getting confused when close
- **Decrease** (10-12cm) if false separation violations
- **Symptoms of too low:** Ball identity swaps when close
- **Symptoms of too high:** False errors when juggling tight patterns

---

### 3. Min Hand Change Distance (default: 25cm)
**Purpose:** Minimum movement required for hand change detection

**When to adjust:**
- **Increase** (30-35cm) if getting false hand changes from noise
- **Decrease** (15-20cm) if missing legitimate hand changes
- **Symptoms of too low:** Spurious throw/catch events
- **Symptoms of too high:** Missing actual hand-to-hand passes

---

## Debugging Tips

### If balls still swap identities:
1. Check color confidence scores in logs - should be > 0.35
2. Verify each ball assigned to different detection index
3. Check for separation violations
4. **Action:** Increase `min_color_confidence_override` setting

### If missing throw/catch events:
1. Check movement distance in logs
2. Verify movement > 25cm threshold
3. **Action:** Decrease `min_hand_change_distance` if needed

### If false throw/catch events:
1. Check movement distance - should be < 25cm for rejection
2. **Action:** Increase `min_hand_change_distance` setting
3. Look for `Hand change rejected` messages

---

## Log Analysis

### Key log patterns to look for:

#### Good Override Assignment:
```
[OVERRIDE] Evaluating ball 0 (pink):
  Detection 0: color=0.65, distance=0.12, combined=0.44 ✓ BEST
  Detection 1: color=0.15, distance=0.08, combined=0.12 ✗ (already used)
[OVERRIDE] Ball 0 assigned to detection 0
```

#### Rejected Weak Color Match:
```
[OVERRIDE] Detection 0 REJECTED for ball 0
  Reason: color_score 0.28 < threshold 0.35
```

#### Rejected False Hand Change:
```
[OVERRIDE HAND CHANGE] Hand change rejected: ball moved only 0.08m (need 0.25m+)
  Keeping ball with hand 0
```

#### Separation Violation:
```
[BALL_SEPARATION_ERROR] Ball separation violation: balls 0 and 1 are 0.12m apart (min: 0.15m)
  Ball 0 state=HELD, Ball 1 state=HELD
```

---

## Files Modified

- [`engine/src/SimpleBallTracker.cpp`](engine/src/SimpleBallTracker.cpp) - Core tracking logic
- [`engine/include/SimpleBallTracker.hpp`](engine/include/SimpleBallTracker.hpp) - Settings structure
- [`hub/components/ui_settings.py`](hub/components/ui_settings.py) - UI controls
- [`engine/src/modules/UdpBallSettingsModule.cpp`](engine/src/modules/UdpBallSettingsModule.cpp) - Settings handling

---

## Compilation

After pulling these changes:
```bash
cd engine
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

---

## Summary

These fixes address the root causes of ball identity confusion and false events:

1. **Prevents duplicate detection assignments** - Each detection used only once
2. **Enforces minimum color confidence** - Rejects weak color matches (< 0.35)
3. **Validates ball separation** - Catches position overlap bugs (with same-hand exception)
4. **Requires significant movement for hand changes** - Eliminates noise-induced false events (≥ 25cm)
5. **Makes all thresholds configurable** - Easy tuning without recompilation

The system should now provide reliable tracking with balls never being confused with each other, and throw/catch events only generated for actual juggling actions.