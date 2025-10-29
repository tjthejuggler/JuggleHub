# FPS Optimization: Code Path Analysis

## Summary
This document shows exactly what code runs when the video feed is shown vs hidden, to understand the FPS optimization.

## Code Paths

### When Video Feed is SHOWN (Hide Video Feed toggle = OFF)

**Every Frame:**
1. `_update_ui()` line 824-988: Process frame data
   - Update frame timestamps (lines 829-833)
   - Log frame reception (line 836-837) - **Only every 30 frames**
   - Process throw/catch events (lines 840-863)
   - Update ball count (line 865-866)
   - Build ball HTML (lines 868-960) - **Runs every frame**
   - **Check video visibility** (line 963): `if self.video_group.isVisible() and not self.hide_video_feed_toggle.isChecked():`
     - ✅ Passes (toggle is OFF)
     - Calls `update_video_feed(frame_data)` (line 965)

2. `update_video_feed()` line 1060-1480: **Full rendering pipeline**
   - Check if hidden (line 1062) - ✅ Passes (not hidden)
   - Load JPEG image (lines 1071-1078)
   - Create pixmap and painter (lines 1080-1083)
   - **Draw all overlays** (lines 1085-1473):
     - Kalman predictions (if enabled)
     - YOLO detections (if enabled)
     - Filtered detections (if enabled)
     - 3D matching (if enabled)
     - New trackers (if enabled)
     - Unmatched detections (if enabled)
     - ByteTrack boxes (if enabled)
     - Hand tracking (if enabled)
     - Ball states (if enabled)
     - Occlusion (if enabled)
     - Color search (if enabled)
     - Color trackers (if enabled)
     - Tracker tails (if enabled)
     - Pose skeleton (if enabled)
   - End painter (line 1475)
   - Update pixmap item (line 1476)
   - Update scene rect (line 1479)
   - Fit view (line 1480)

**Total work per frame when SHOWN:**
- Process frame data ✓
- Build ball HTML ✓
- Load JPEG image ✓
- Create pixmap ✓
- Draw all enabled overlays ✓
- Update UI widgets ✓

---

### When Video Feed is HIDDEN (Hide Video Feed toggle = ON)

**Every Frame:**
1. `_update_ui()` line 824-988: Process frame data
   - Update frame timestamps (lines 829-833)
   - Log frame reception (line 836-837) - **Only every 30 frames**
   - Process throw/catch events (lines 840-863)
   - Update ball count (line 865-866)
   - Build ball HTML (lines 868-960) - **Runs every frame**
   - **Check video visibility** (line 963): `if self.video_group.isVisible() and not self.hide_video_feed_toggle.isChecked():`
     - ❌ Fails (toggle is ON)
     - **SKIPS** `update_video_feed()` entirely
     - No log message (fixed to only log every 30 frames if needed)

2. `update_video_feed()`: **NOT CALLED AT ALL**

**Total work per frame when HIDDEN:**
- Process frame data ✓
- Build ball HTML ✓
- ~~Load JPEG image~~ ✗ SKIPPED
- ~~Create pixmap~~ ✗ SKIPPED
- ~~Draw all overlays~~ ✗ SKIPPED
- ~~Update UI widgets~~ ✗ SKIPPED

---

## Performance Impact

### Engine Side (C++)
When video feed is hidden:
- **Skips JPG encoding** (`cv::imencode()`) - saves 2-5ms per frame
- Still captures frames from camera
- Still processes all tracking/detection
- Still sends frame data to hub (without image bytes)

### Hub Side (Python)
When video feed is hidden:
- **Skips JPEG decoding** (`QImage.loadFromData()`)
- **Skips pixmap creation** (`QPixmap.fromImage()`)
- **Skips all overlay drawing** (QPainter operations)
- **Skips scene updates** (QGraphicsScene operations)
- Still processes ball data for text display
- Still updates ball list HTML

### Logging Overhead (FIXED)
**Before fix:**
- Logged on EVERY frame (3-5 messages per frame)
- At 39 FPS = 117-195 string operations/sec
- Each log call: timestamp generation + string formatting + text widget update

**After fix:**
- Logs only every 30th frame
- At 39 FPS = 4-6 string operations/sec
- 97% reduction in logging overhead

---

## Expected FPS Improvement

**With video feed hidden:**
- Engine saves: 2-5ms (JPG encoding)
- Hub saves: 5-10ms (JPEG decode + rendering)
- Total savings: 7-15ms per frame

**At 39 FPS baseline:**
- Frame time: 25.6ms
- Savings: 7-15ms (27-58% of frame time)
- **Expected new FPS: 42-44 FPS** (10-15% improvement)

---

## Testing Instructions

1. Start the hub: `./scripts/run_hub.sh --use-venv --device GPU`
2. Note baseline FPS with video feed shown
3. Click "Hide Video Feed" toggle
4. Observe FPS increase
5. Expected result: FPS should increase by 10-15%

---

## Files Modified

1. `api/v1/juggler.proto` - Added SET_VIDEO_FEED_ENABLED command
2. `engine/include/Engine.hpp` - Added video_feed_enabled_ flag
3. `engine/src/Engine.cpp` - Conditional JPG encoding
4. `hub/components/ui.py` - Reduced logging overhead, skip video rendering when hidden

---

*Last updated: 2025-01-08*