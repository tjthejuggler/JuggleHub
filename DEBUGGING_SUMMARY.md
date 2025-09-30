# Video Freeze Debugging Summary

**Date:** 2025-09-30  
**Issue:** Video stream freezes when a ball enters the frame

## Root Cause Analysis

The application was crashing with an `IndexError: list index out of range` in [`hub/components/juggling_system_manager.py`](hub/components/juggling_system_manager.py:44).

### The Problem

The original code attempted to identify new ball tracks using this logic:

```python
unique_id = self.ball_identifier.identify_balls([raw_ball_detections[0]], frame_image)[0]
```

This had multiple critical issues:

1. **Unsafe list access**: Assumed `raw_ball_detections[0]` always exists
2. **Wrong association logic**: No guarantee the first raw detection corresponds to the new track
3. **Incorrect return value handling**: `identify_balls()` returns a dict `{index: unique_id}`, not a list

### Why It Failed

When the Kalman tracker detected a new track:
- It might be based on a prediction, not a raw detection
- The `raw_ball_detections` list could be empty
- Even if not empty, `[0]` might not be the correct ball
- Accessing the result with `[0]` treated a dict as a list

## Fixes Implemented

### 1. Fixed Ball Identification Logic ([`juggling_system_manager.py`](hub/components/juggling_system_manager.py))

**Changes:**
- Added proper association between tracked balls and raw detections
- Uses distance-based matching to find the closest raw detection
- Handles cases where no raw detection exists (assigns temporary ID)
- Correctly accesses the dictionary returned by `identify_balls()`

**New Logic:**
```python
# Find the closest raw detection to this tracked ball
tracked_pos = ball['smoothed_position_3d']
min_distance = float('inf')
closest_detection_idx = None

for idx, detection in enumerate(raw_ball_detections):
    det_pos = np.array([detection.position.x, detection.position.y, detection.position.z])
    distance = np.linalg.norm(tracked_pos - det_pos)
    if distance < min_distance:
        min_distance = distance
        closest_detection_idx = idx

# Use the identified unique_id for the closest detection
if closest_detection_idx is not None and closest_detection_idx in identified_detections:
    unique_id = identified_detections[closest_detection_idx]
```

### 2. Added Comprehensive Logging

Added detailed logging to all critical components:

#### [`hub/main.py`](hub/main.py)
- Configured Python logging with DEBUG level
- Added try-catch around `JugglingSystemManager.process_frame()`
- Logs frame reception, processing, and errors with full stack traces

#### [`hub/components/juggling_system_manager.py`](hub/components/juggling_system_manager.py)
- Logs number of raw detections received
- Logs Kalman tracker output
- Logs ball identification results
- Logs new track creation with matching details
- Logs state estimation completion

#### [`hub/components/kalman_tracker.py`](hub/components/kalman_tracker.py)
- Logs number of raw detections
- Logs track creation and updates
- Logs data association results
- Logs prediction-only frames

#### [`hub/components/ball_identifier.py`](hub/components/ball_identifier.py)
- Logs each detection being processed
- Logs bounding box coordinates
- Logs color matching results
- Logs new profile creation
- Handles errors gracefully with logging

### 3. Improved Error Handling

- All critical sections wrapped in try-catch blocks
- Errors logged with full stack traces (`exc_info=True`)
- Graceful degradation (temporary IDs when identification fails)
- Null checks for frame images

## Testing Instructions

To test the fixes:

1. Run the hub using the standard script:
   ```bash
   ./scripts/run_hub.sh
   ```

2. Introduce a ball into the camera frame

3. Monitor the console output for:
   - `DEBUG` level logs showing data flow
   - `INFO` logs for new track creation
   - `ERROR` logs if any issues occur

4. Verify:
   - Video stream remains stable
   - Ball tracking works correctly
   - No crashes or freezes

## Log Output to Watch For

**Successful ball detection:**
```
DEBUG - Kalman tracker update called with 1 raw detections
INFO - New track detected: 0
DEBUG - Identified 1 balls by color: {0: 'ball_1'}
INFO - Track 0 matched to detection 0 with unique_id ball_1 (distance: 0.023)
```

**Potential issues:**
```
WARNING - Track X could not be matched to an identified detection
WARNING - Track X assigned temporary unique_id: unknown_X
ERROR - Error identifying balls: ...
```

## Files Modified

1. [`hub/components/juggling_system_manager.py`](hub/components/juggling_system_manager.py) - Fixed core logic
2. [`hub/main.py`](hub/main.py) - Added logging infrastructure
3. [`hub/components/kalman_tracker.py`](hub/components/kalman_tracker.py) - Added debug logging
4. [`hub/components/ball_identifier.py`](hub/components/ball_identifier.py) - Added error handling and logging

## Next Steps

If issues persist:

1. Check the log output for specific error messages
2. Verify the C++ engine is sending valid ball detections
3. Ensure the camera feed is providing valid frame images
4. Check that color profiles are being saved/loaded correctly

## Technical Notes

- The fix maintains backward compatibility with the existing system
- Temporary IDs (`unknown_X`) allow the system to continue even if color identification fails
- Distance-based matching is a simple but effective association method
- Future improvements could use IoU (Intersection over Union) for 2D bounding box matching