# Held Ball Wrist Tracking Fix

**Date:** 2025-10-22
**Component:** New 3D Kalman Tracking System
**Issue:** When a hand holding a ball is temporarily lost and then redetected, the ball tracker fails to re-associate the ball with the hand, causing it to be stuck in mid-air.

## Problem Description

When a ball is held, its state is marked as `HELD`, and its position is locked to the detected wrist. If hand detection is temporarily lost, the ball correctly transitions to an `IN_FLIGHT` state. However, upon re-detection of the hand, the system fails to transition the ball back to the `HELD` state because there is no new visual detection to trigger a "catch" event. As a result, the ball remains `IN_FLIGHT` and its trajectory is predicted by the Kalman filter, making it appear stuck in the air.

## Root Cause

The root cause of this issue is the absence of a mechanism to re-establish the `HELD` state based on proximity when a hand is re-detected. The system previously relied on visual detections to catch a ball, which is not possible when the ball is occluded by the hand.

## Solution

To resolve this, a new function, `reacquireHeldBallsByProximity`, has been introduced. This function is called after the main association step and is responsible for checking `IN_FLIGHT` balls against current hand positions. If a ball's predicted position is within the defined `held_radius` of a hand, its state is transitioned back to `HELD`.

### 1. `reacquireHeldBallsByProximity()` Implementation

This new function iterates through unmatched balls that are in the `IN_FLIGHT` state and have been unseen for a few frames. It calculates the distance between the ball's predicted position and each hand's wrist. If the distance is within the `held_radius`, the ball is "re-caught," its state is updated to `HELD`, and a `CATCH` event is generated.

```cpp
void New3DTracker::reacquireHeldBallsByProximity(
    std::vector<New3DBall*>& unmatched_balls,
    const std::vector<SimpleHand>& hands,
    std::vector<BallEvent>& events) {
    
    logDebug("  reacquireHeldBallsByProximity: Checking ", unmatched_balls.size(),
             " unmatched balls against ", hands.size(), " hands");
    
    // This list will hold balls that remain unmatched after this check
    std::vector<New3DBall*> still_unmatched_balls;
    
    for (auto* ball : unmatched_balls) {
        // Only consider balls that are IN_FLIGHT. A HELD ball without its hand
        // is handled in handleUnmatchedBalls.
        if (ball->state != IN_FLIGHT) {
            still_unmatched_balls.push_back(ball);
            continue;
        }
        
        // Don't re-acquire a ball that was just seen. This prevents a ball that was
        // just thrown from being immediately re-caught by the same hand.
        // Allow re-acquisition if it has been unseen for a few frames.
        if (ball->frames_since_seen < 5) {
            still_unmatched_balls.push_back(ball);
            continue;
        }
        
        bool reacquired = false;
        for (const auto& hand : hands) {
            // Check distance from ball's predicted position to hand's wrist
            const cv::Point3f& pred_pos = ball->predicted_position;
            const cv::Point3f& hand_pos = hand.wrist_pos_3d;
            
            float dx = pred_pos.x - hand_pos.x;
            float dy = pred_pos.y - hand_pos.y;
            float dz = pred_pos.z - hand_pos.z;
            float distance = std::sqrt(dx*dx + dy*dy + dz*dz);
            
            // If ball is within held_radius, re-acquire it as HELD
            if (distance < settings_.held_radius_m) {
                logDebug("    >>> PROXIMITY RE-ACQUIRE! Ball ", ball->id, " (", ball->color_name,
                         ") re-acquired by Hand ", hand.id, " (distance: ", distance, "m)");
                
                // Transition to HELD state
                ball->state = HELD;
                ball->associated_hand_id = hand.id;
                ball->frames_since_seen = 0; // It is now "seen" via proximity
                ball->tracking_reason = "Re-acquired by proximity";
                
                // Generate CATCH event
                BallEvent catch_event;
                catch_event.type = BallEvent::CATCH;
                catch_event.ball_id = static_cast<int>(ball->id);
                catch_event.hand_id = hand.id;
                catch_event.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()
                ).count();
                events.push_back(catch_event);
                
                reacquired = true;
                break; // Ball is re-acquired, no need to check other hand
            }
        }
        
        // If not re-acquired, add it to the list of balls that are still unmatched
        if (!reacquired) {
            still_unmatched_balls.push_back(ball);
        }
    }
    
    // The original unmatched_balls list is updated to only contain balls that
    // were not re-acquired by proximity.
    unmatched_balls = still_unmatched_balls;
}
```

### 2. Integration into `updateNew3D()`

The `reacquireHeldBallsByProximity` function is now called in the main `updateNew3D` loop between the re-acquisition of lost balls and the handling of unmatched balls. This ensures that any `IN_FLIGHT` balls are checked for proximity to a hand before being marked as lost.

**`updateNew3D()` call order:**
1. `predictAllBalls()`
2. `associateDetections()`
3. `updateMatchedBalls()`
4. `createNewTracks()`
5. **`reacquireHeldBallsByProximity()` (NEW STEP)**
6. `handleUnmatchedBalls()`
7. `finalizeBallPositions()`

## Behavior After Fix

- **Hand Re-detection:** When a hand is re-detected after being temporarily lost, any nearby `IN_FLIGHT` balls are immediately transitioned back to `HELD`.
- **Seamless Tracking:** The tracker no longer gets stuck in mid-air, and the ball's position remains locked to the wrist as intended.
- **Improved Robustness:** The system is now more resilient to brief interruptions in hand tracking, which is a common scenario in juggling.

## Files Modified

1. [`engine/src/New3DTracker.cpp`](engine/src/New3DTracker.cpp)
   - Added `reacquireHeldBallsByProximity()` implementation.
   - Updated `updateNew3D()` to include the new re-acquisition step and corrected debug log numbering.

2. [`engine/include/New3DTracker.hpp`](engine/include/New3DTracker.hpp)
   - Added the function declaration for `reacquireHeldBallsByProximity()`.

## Impact

- **Improved User Experience:** The tracker now behaves as expected, with held balls remaining locked to the wrist even after temporary hand loss.
- **Enhanced Juggling Support:** The fix addresses a common occlusion scenario, making the tracker more reliable for juggling analysis.
- **No Breaking Changes:** The change is an enhancement to the existing logic and does not introduce any breaking changes.

## Related Documentation

- [New 3D Tracker Architecture](NEW_3D_TRACKER_ARCHITECTURE.md)
- [New 3D Tracker Implementation](NEW_3D_TRACKER_IMPLEMENTATION_COMPLETE.md)
- [Persistent Ball Architecture](PERSISTENT_BALL_ARCHITECTURE_IMPLEMENTATION.md)