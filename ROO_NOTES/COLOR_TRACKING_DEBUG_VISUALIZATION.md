# Color Tracking Debug Visualization Implementation

**Date:** 2025-10-05  
**Issue:** Color tracker flickering between ball and hand during juggling (frames 171-175)

## Problem Analysis

The color tracker was incorrectly jumping from the ball in free flight to the user's hands, even when:
1. YOLO had valid ball detections with high confidence
2. The ball was clearly in free flight (not near hands)
3. The trajectory indicated the ball should not be near any hand

## Root Causes Identified

1. **Insufficient YOLO prioritization**: Color matching was weighted equally with YOLO detections
2. **Class ID confusion**: Free-flight balls (class_id=0) and held balls (class_id=1) were treated equally
3. **Lack of trajectory validation**: System allowed impossible teleportation to hands
4. **No debug visibility**: Couldn't see WHY tracking decisions were made

## Solutions Implemented

### 1. Enhanced YOLO Prioritization (SimpleBallTracker.cpp:277-315)

**Priority Scoring System:**
```cpp
float class_weight = (det.class_id == 0) ? 3.0f : 1.0f;  // 3x weight for free-flight balls
float combined_score = (class_weight * det.confidence * 2.0f) + color_score;
```

- Free-flight balls (class_id=0) get **3x weight** over held balls (class_id=1)
- YOLO confidence is doubled in the scoring
- Color match is still considered but secondary

### 2. Class ID State Management (SimpleBallTracker.cpp:680-710)

**Kalman Prediction with Class Updates:**
- During Kalman prediction, class_id is updated based on proximity to hands
- Prevents stale class_id from persisting and causing wrong state detection
- Tracks which hand is nearest and updates `held_by_hand_id` accordingly

### 3. Trajectory Validation (SimpleBallTracker.cpp:700-745)

**Physics-Based Hand Association:**
- Only allows hand snapping if predicted trajectory leads toward that hand
- Prevents impossible teleportation jumps
- Uses velocity and position to validate hand proximity

### 4. Debug Visualization System

**Added tracking_reason field to SimpleBall:**
```cpp
std::string tracking_reason;  // Debug info: why this position was chosen
```

**Tracking Reasons Populated:**
- `"YOLO: cls=0 conf=0.85 col=0.6"` - YOLO detection used
- `"Kalman pred"` - Kalman prediction for free flight
- `"Kalman+Near[L] d=0.15m"` - Kalman prediction near left hand
- `"Traj→[R] d=0.12m"` - Trajectory leads to right hand
- `"Traj→Flight"` - Trajectory indicates free flight

**Visualization in Engine.cpp:**
- Tracking reason displayed below ball label in yellow text
- Only shown when color tracker visualization is enabled
- Helps diagnose why tracker makes each decision

## Usage

1. **Enable color tracker visualization** in the UI
2. **Record frames** with visualization enabled
3. **Review recorded frames** in `with_visualizations` folder
4. **Check tracking_reason text** below each ball to see decision logic

## Expected Behavior

With these changes, the tracker should:
1. ✅ Prioritize YOLO detections, especially for free-flight balls
2. ✅ Use Kalman prediction with gravity physics during free flight
3. ✅ Only snap to hands when trajectory validates the association
4. ✅ Show clear debug information about each tracking decision

## Testing Recommendations

1. Test with the problematic frames 171-175 sequence
2. Verify tracking reason shows correct logic at each frame
3. Confirm no flickering between ball and hand during free flight
4. Check that held balls are correctly associated with hands
5. Validate that throws and catches are detected properly

## Files Modified

1. **engine/include/SimpleBallTracker.hpp** - Added `tracking_reason` field
2. **engine/src/SimpleBallTracker.cpp** - Implemented priority scoring and tracking reason population
3. **engine/src/Engine.cpp** - Added tracking reason visualization to recorded frames

## Next Steps

If flickering persists:
1. Review tracking_reason text in recorded frames to see exact decision logic
2. Adjust class_weight multiplier if needed (currently 3.0x for free-flight)
3. Tune trajectory validation thresholds if hand snapping is too aggressive/conservative
4. Consider adding projectile motion prediction for better free-flight tracking