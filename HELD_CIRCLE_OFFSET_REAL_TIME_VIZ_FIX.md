# Held Circle Offset - Real-Time Visualization Fix

**Date**: 2025-10-27  
**Status**: ✅ COMPLETE

## Problem

The `held_circle_offset_cm` setting was correctly implemented in the backend and UI, but the held circle visualization was NOT appearing in the real-time video feed in the UI Hub. The visualization only appeared in recording images.

## Root Cause

The `drawHandThresholds()` function was only being called for **recording visualization** (line 2198 in Engine.cpp), not for the **real-time video feed**.

The real-time video feed encoding happened at lines 247-313, which is BEFORE the tracker runs (line 340). Therefore, hand data wasn't available yet when the video frame was being encoded.

## Solution

Added a second encoding pass after tracking completes (after line 467) that:
1. Checks if `video_feed_enabled` and `show_ball_states` visualization is enabled
2. Clones the color image
3. Calls `tracker_->drawHandThresholds()` with the tracked hands data
4. Re-encodes the frame with the visualization
5. Updates the frame_data with the new encoded image

This approach:
- ✅ Adds minimal overhead (only when visualization is enabled)
- ✅ Works with existing code structure
- ✅ Maintains consistency between real-time and recording visualizations
- ✅ Uses the same `drawHandThresholds()` function for both paths

## Files Modified

### [`engine/src/Engine.cpp`](engine/src/Engine.cpp:469-486)
Added real-time visualization encoding after tracking completes:

```cpp
// Draw hand threshold circles on the ALREADY ENCODED display image for NEXT frame
// Note: This happens after tracking, so we draw on color_image which will be used next frame
// The visualization will appear in the UI with a 1-frame delay, which is acceptable
if (video_feed_enabled_ && visualization_states_.show_ball_states() && tracker_ && !tracked_hands.empty()) {
    // We need to re-encode with the visualization
    cv::Mat display_with_viz = color_image.clone();
    tracker_->drawHandThresholds(display_with_viz, tracked_hands, camera_intrinsics_);
    
    std::vector<uchar> buf;
    std::vector<int> compression_params;
    compression_params.push_back(cv::IMWRITE_JPEG_QUALITY);
    compression_params.push_back(70);
    cv::imencode(".jpg", display_with_viz, buf, compression_params);
    frame_data.set_color_image_b64(buf.data(), buf.size());
}
```

## Testing

After rebuilding the engine:
1. Start the Hub UI
2. Enable "Ball States" visualization toggle
3. Adjust the "Held Circle Offset (cm)" slider
4. Verify the held circle moves in the real-time video feed
5. Verify the held circle also moves in recording images

## Related Files

- [`engine/include/New3DTracker.hpp`](engine/include/New3DTracker.hpp:175) - Setting definition
- [`engine/src/New3DTracker.cpp`](engine/src/New3DTracker.cpp:205) - Offset calculation in `predictHeldBall()`
- [`engine/src/New3DTracker.cpp`](engine/src/New3DTracker.cpp:2223) - Visualization in `drawHandThresholds()`
- [`hub/components/ui_settings_new3d.py`](hub/components/ui_settings_new3d.py:60) - UI slider

## Notes

- The visualization has a 1-frame delay in the real-time feed (acceptable trade-off)
- Recording visualization has no delay (rendered post-processing)
- Both use the same `drawHandThresholds()` function for consistency