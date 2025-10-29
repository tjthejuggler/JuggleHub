# Raw Detections Fix - Unfiltered YOLO Detections

**Date:** 2025-10-11  
**Issue:** The `raw_detections` toggle was showing filtered detections (after confidence threshold), not truly raw/unfiltered detections.

## Problem

The user reported that when changing the min confidence setting, more detections would appear even with the `raw_detections` toggle enabled. This indicated that `raw_detections` was actually showing **filtered** detections (after confidence threshold), not truly raw detections.

### Root Cause

In `SimpleBallTracker.cpp`, the `runBallDetection()` method was filtering detections by confidence threshold **before** storing them:

```cpp
// Apply class-specific confidence thresholds
float threshold = (class_id == 0) ? ball_confidence_threshold_ : ball_held_confidence_threshold_;

if (confidence > threshold) {  // <-- FILTERING HAPPENS HERE
    // ... create detection ...
    raw_detections.push_back(det);
}
```

So `last_raw_detections_` contained filtered detections, not truly unfiltered ones.

## Solution

Created a new data flow for truly unfiltered detections:

### 1. Added Unfiltered Storage in SimpleBallTracker

**File:** `engine/include/SimpleBallTracker.hpp`
- Added `std::vector<Detection> last_unfiltered_detections_` member variable
- Added `getUnfilteredDetections()` method to retrieve them

### 2. Updated Detection Collection

**File:** `engine/src/SimpleBallTracker.cpp`
- Created `unfiltered_detections` vector to store ALL detections before filtering
- Store every detection in `unfiltered_detections` regardless of confidence
- Then apply confidence threshold to create `raw_detections` (filtered list)
- Store unfiltered list in `last_unfiltered_detections_`

### 3. Added Protobuf Field

**File:** `api/v1/juggler.proto`
- Added `repeated BoundingBox2D unfiltered_detections = 24;` field
- Updated comment for `raw_detections` to clarify it's filtered
- Regenerated protobuf files with `./scripts/generate_protos.sh`

### 4. Updated Engine to Populate Field

**File:** `engine/src/Engine.cpp`
- Get unfiltered detections: `auto unfiltered_detections = simple_tracker_->getUnfilteredDetections();`
- Populate `frame_data.unfiltered_detections` with ALL detections
- Keep `frame_data.raw_detections` for filtered detections (backward compatibility)

### 5. Updated UI Toggle

**File:** `hub/components/ui.py`
- Changed toggle button label from `"raw_detections"` to `"unfiltered_detections"`
- Updated tooltip: `"frame_data.unfiltered_detections - ALL YOLO detections (before confidence threshold)"`
- Changed rendering to use `frame_data.unfiltered_detections` instead of `frame_data.raw_detections`

## Data Flow Clarification

Now we have three levels of detection data:

1. **`unfiltered_detections`** - ALL YOLO detections before any filtering
   - Shown by: `unfiltered_detections` toggle
   - Not affected by confidence threshold settings
   - This is what the user wanted

2. **`raw_detections`** - Detections after confidence threshold, before NMS
   - Still available for backward compatibility
   - Affected by confidence threshold settings

3. **`filtered_detections`** - Detections that were rejected (for debugging)
   - Shown by: `filtered_detections` toggle
   - Shows why detections were rejected

## Testing

To verify the fix works:
1. Enable the `unfiltered_detections` toggle
2. Change the min confidence threshold setting
3. The number of detections shown should NOT change (all detections visible)
4. Compare with `filtered_detections` toggle to see which ones pass/fail threshold

## Benefits

- **True raw detections**: Users can now see ALL YOLO detections regardless of settings
- **Better debugging**: Can see exactly what YOLO is detecting before any filtering
- **Backward compatibility**: Kept `raw_detections` field for existing code
- **Clear naming**: Toggle now shows exact variable name `unfiltered_detections`