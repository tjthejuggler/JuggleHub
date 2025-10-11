# Catch/Throw State Transition Fix

**Date**: 2025-10-11  
**Issue**: Multiple catch/throw events registered for single physical catches/throws

## Problem Description

The ball tracking system was registering multiple CATCH and THROW events when a ball was caught or thrown once. This occurred in two scenarios:

### Scenario 1: Multiple Catches During Single Catch
When a ball fell into a hand, it would register multiple CATCH events as it passed through the catch threshold zone:
- Frame 361: Ball IN_FLIGHT approaching hand
- Frame 362: Ball enters catch zone → **CATCH event #1** → State: HELD
- Frame 363: Ball still in catch zone → State: HELD  
- Frame 364: Ball moves slightly → **Spurious THROW** → State: IN_FLIGHT
- Frame 365: Ball back in catch zone → **Spurious CATCH #2** → State: HELD

### Scenario 2: Extra Throw/Catch During Single Throw
When throwing a ball, spurious CATCH/THROW events occurred mid-flight:
- Frame 169: Ball HELD in hand
- Frame 170: Hand moves → **THROW event** → State: IN_FLIGHT
- Frame 171-173: Ball IN_FLIGHT
- Frame 174: Near-hand fallback snaps ball to hand position (still IN_FLIGHT)
- Frame 175: Ball now within catch threshold → **Spurious CATCH** → State: HELD
- Frame 176: Ball moves away → **Spurious THROW** → State: IN_FLIGHT

## Root Causes

### Cause 1: No State Transition Guard
The catch detection logic only checked if the ball was not currently held by a hand (`ball.previous_held_by_hand_id == -1`), but didn't verify the ball was actually IN_FLIGHT in the previous frame. This allowed catches to be registered even when the ball was already HELD.

**Location**: [`SimpleBallTracker.cpp:2033`](engine/src/SimpleBallTracker.cpp:2033)

### Cause 2: Near-Hand Fallback State Inconsistency
When a ball lost YOLO detection near a hand, the fallback logic would snap the ball to the hand's wrist position but keep it in IN_FLIGHT state. On the next frame, this would trigger a catch detection, creating spurious events.

**Location**: [`SimpleBallTracker.cpp:1939-1951`](engine/src/SimpleBallTracker.cpp:1939-1951)

## Solution

### Change 1: Added Previous State Tracking
Added a `previous_state` field to track the ball's state from the previous frame, enabling proper state transition detection.

**Files Modified**:
- [`SimpleBallTracker.hpp:97`](engine/include/SimpleBallTracker.hpp:97) - Added `BallState previous_state` field
- [`SimpleBallTracker.cpp:94`](engine/src/SimpleBallTracker.cpp:94) - Initialize `previous_state = HELD`
- [`SimpleBallTracker.cpp:751`](engine/src/SimpleBallTracker.cpp:751) - Update `previous_state` at end of each frame

### Change 2: State Transition Guard for Catches
Modified catch detection to require the ball was IN_FLIGHT in the previous frame:

**Before**:
```cpp
if (ball.previous_held_by_hand_id == -1) {
    // Register catch
}
```

**After**:
```cpp
if (ball.previous_state == IN_FLIGHT) {
    // Register catch - only if transitioning from IN_FLIGHT
}
```

**Location**: [`SimpleBallTracker.cpp:2033`](engine/src/SimpleBallTracker.cpp:2033)

### Change 3: Immediate State Transition in Near-Hand Fallback
When a ball loses detection near a hand and is snapped to the hand position, immediately transition to HELD state and generate a proper CATCH event:

**Before**:
```cpp
// Snap to hand position but keep IN_FLIGHT state
ball.position_3d = hand.wrist_3d;
ball.state = IN_FLIGHT;  // Inconsistent!
```

**After**:
```cpp
// Snap to hand and immediately transition to HELD
ball.position_3d = hand.wrist_3d;
initiateCatch(ball, hand_id, frame_number);  // Proper state transition
```

**Location**: [`SimpleBallTracker.cpp:1939-1951`](engine/src/SimpleBallTracker.cpp:1939-1951)

## State Machine Logic

The ball state machine now properly enforces these transitions:

```
HELD → (distance > throw_threshold) → IN_FLIGHT + THROW event
IN_FLIGHT → (distance < catch_threshold AND previous_state == IN_FLIGHT) → HELD + CATCH event
```

Key principles:
1. **State Hysteresis**: A ball must be IN_FLIGHT before it can be caught
2. **Single Event per Transition**: Each physical catch/throw generates exactly one event
3. **Consistent State**: Ball position and state must always be consistent

## Testing

The fix was validated against the provided frame data showing:
- Frames 356-368: Ball falling into right hand
- Expected: Single CATCH event when ball enters hand
- Result: ✅ Single CATCH event registered

## Files Changed

1. [`engine/include/SimpleBallTracker.hpp`](engine/include/SimpleBallTracker.hpp:97)
   - Added `BallState previous_state` field to `Ball` struct

2. [`engine/src/SimpleBallTracker.cpp`](engine/src/SimpleBallTracker.cpp)
   - Line 94: Initialize `previous_state = HELD` in `createBall()`
   - Line 751: Update `previous_state` in `detectStatesAndEvents()`
   - Line 1939-1951: Modified near-hand fallback to call `initiateCatch()`
   - Line 2033: Changed catch condition to check `previous_state == IN_FLIGHT`

## Build Status

✅ Engine rebuilt successfully  
✅ All compilation warnings are pre-existing (OpenVINO deprecation warnings)  
✅ Engine starts and runs correctly