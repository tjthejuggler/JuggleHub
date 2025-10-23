# Recording Playback Feature

**Implementation Date:** October 23, 2025  
**Version:** 1.0

## Overview

The Recording Playback feature allows JuggleHub to replay previously recorded juggling sessions instead of using live camera feed. This enables debugging, analysis, testing, and demonstration without requiring a physical camera setup.

## Features

✅ **Dual Input Modes**: Seamlessly switch between live camera and recorded playback  
✅ **Directory Browser**: Easy selection of recordings from `engine/data/1_raw_recordings/`  
✅ **Playback Controls**: Play, pause, stop, and frame-by-frame stepping  
✅ **Variable Speed**: Adjust playback speed from 0.1x to 2.0x  
✅ **Frame Counter**: Real-time display of current frame / total frames  
✅ **Automatic Looping**: Recordings loop automatically when reaching the end  
✅ **Full Tracking Support**: All tracking systems work identically with playback  
✅ **RGB + Depth Sync**: Synchronized loading of color and depth frames  

---

## User Guide

### Accessing Playback Mode

1. Open JuggleHub and navigate to the **Calibration Settings** panel
2. In the **📷 Camera Settings** section, find the **Input Source** dropdown
3. Select **📁 Recording Playback** from the dropdown

### Selecting a Recording

1. Click the **Browse...** button next to "Recording Directory"
2. Navigate to a recording folder in the file dialog
3. Select a valid recording directory (must contain `no_boxes/` and `depth/` subdirectories)
4. The frame count will be displayed once loaded

### Playback Controls

| Control | Function |
|---------|----------|
| **▶ Play** | Start or resume automatic playback |
| **⏸ Pause** | Pause automatic playback |
| **◀ Step Back** | Move backward one frame (pauses playback) |
| **Step Forward ▶** | Move forward one frame (pauses playback) |
| **⏹ Stop** | Stop playback and return to frame 0 |
| **Speed Slider** | Adjust playback speed (0.1x to 2.0x) |

### Playback Behavior

- **Automatic Looping**: When playback reaches the end, it automatically loops back to the beginning
- **Frame Stepping**: Using step controls automatically pauses playback
- **Speed Control**: Can be adjusted during playback without stopping
- **Tracking**: All tracking systems (3D, New3D, 2D) work normally with playback frames

### Returning to Live Camera

1. Change **Input Source** back to **🎥 Live Camera**
2. Live camera controls will be re-enabled
3. Click **Start Camera** to resume live feed

---

## Technical Architecture

### Recording Directory Structure

Recordings are stored in `engine/data/1_raw_recordings/` with this structure:

```
<session_name>/
├── recording.log              # Frame-by-frame tracking data
├── no_boxes/                  # RGB frames without overlays
│   └── <session>_frame_<N>.jpg
├── depth/                     # Depth frames (16-bit PNG)
│   └── <session>_frame_<N>_depth.png
└── with_visualizations/       # RGB with tracking overlays (optional)
    └── <session>_frame_<N>_viz.jpg
```

Session names follow these patterns:
- `continuous_YYYY-MM-DD_HH-MM-SS` - Continuous recordings
- `rs455_YYYY-MM-DD_HH-MM-SS` - 5-second clips

### Components

#### C++ Engine Components

1. **PlaybackManager** ([`engine/include/PlaybackManager.hpp`](engine/include/PlaybackManager.hpp))
   - Manages loading and sequencing of recorded frames
   - Handles frame file scanning and sorting
   - Provides frame-by-frame navigation
   - Supports speed control and pause/resume

2. **Engine Integration** ([`engine/src/Engine.cpp`](engine/src/Engine.cpp))
   - Dual-mode frame acquisition (live camera or playback)
   - Frame timing based on playback speed
   - Automatic looping at end of recording
   - SystemStatus reporting for UI updates

3. **Command Handlers** ([`engine/src/Engine.cpp`](engine/src/Engine.cpp:703-756))
   - `PLAYBACK_START` - Start playback from directory
   - `PLAYBACK_STOP` - Stop playback
   - `PLAYBACK_STEP_FORWARD` - Step one frame forward
   - `PLAYBACK_STEP_BACKWARD` - Step one frame backward
   - `PLAYBACK_SET_SPEED` - Set playback speed
   - `PLAYBACK_PAUSE` - Pause automatic playback
   - `PLAYBACK_RESUME` - Resume automatic playback

#### Python UI Components

1. **UI Controls** ([`hub/components/ui_settings_common.py`](hub/components/ui_settings_common.py:140-273))
   - Input source selector (Live/Playback)
   - Directory browser with validation
   - Playback control buttons
   - Speed slider (0.1x to 2.0x)
   - Frame counter display

2. **Control Methods** ([`hub/components/ui_settings.py`](hub/components/ui_settings.py:1401-1600))
   - `on_input_source_changed()` - Toggle between modes
   - `browse_playback_directory()` - Directory selection
   - `playback_start()` - Start playback
   - `playback_stop()` - Stop playback
   - `playback_toggle_play_pause()` - Play/pause control
   - `playback_step_forward()` - Frame stepping
   - `playback_step_backward()` - Frame stepping
   - `on_playback_speed_changed()` - Speed control

#### Protobuf Definitions

New command types and fields added to [`api/v1/juggler.proto`](api/v1/juggler.proto):

**CommandType enum:**
- `PLAYBACK_START = 23`
- `PLAYBACK_STOP = 24`
- `PLAYBACK_STEP_FORWARD = 25`
- `PLAYBACK_STEP_BACKWARD = 26`
- `PLAYBACK_SET_SPEED = 27`
- `PLAYBACK_PAUSE = 28`
- `PLAYBACK_RESUME = 29`

**CommandRequest fields:**
- `playback_directory` - Path to recording directory
- `playback_speed` - Speed multiplier (0.1 to 2.0)
- `playback_frame_number` - Specific frame to jump to

**SystemStatus fields:**
- `playback_mode` - Whether in playback mode
- `playback_directory` - Current recording path
- `playback_current_frame` - Current frame number
- `playback_total_frames` - Total frames in recording
- `playback_paused` - Pause state
- `playback_speed` - Current speed multiplier

---

## Frame Timing

Playback frame timing is calculated as:

```
target_frame_time = (1000ms / camera_fps) / playback_speed
```

Examples:
- At 60 FPS, 1.0x speed: 16.67ms per frame
- At 60 FPS, 0.5x speed: 33.33ms per frame (slow motion)
- At 60 FPS, 2.0x speed: 8.33ms per frame (fast forward)

---

## Use Cases

### 1. Debugging Tracking Issues
- Record a session where tracking fails
- Play back frame-by-frame to identify the exact failure point
- Adjust tracking parameters and replay to test fixes

### 2. Algorithm Development
- Record test sequences with known ground truth
- Develop and test new tracking algorithms offline
- Compare results across different algorithm versions

### 3. Performance Testing
- Test tracking performance without camera hardware
- Benchmark different tracking systems on identical data
- Profile code with consistent input data

### 4. Demonstrations
- Show tracking capabilities without live juggling
- Present specific scenarios or edge cases
- Create reproducible demos for documentation

### 5. Training Data Analysis
- Review recorded sessions for training data quality
- Identify frames that need annotation
- Verify tracking accuracy on recorded data

---

## Limitations

1. **No Recording Modification**: Playback is read-only; recordings cannot be edited
2. **Fixed Resolution**: Playback uses the resolution of the recorded frames
3. **No Real-time Constraints**: Playback timing may not match exact real-time if system is slow
4. **Memory Usage**: Large recordings may consume significant memory
5. **File I/O**: Frame loading speed depends on disk performance

---

## Troubleshooting

### "Invalid Recording" Error
**Cause**: Selected directory doesn't contain required subdirectories  
**Solution**: Ensure directory has both `no_boxes/` and `depth/` folders with frame files

### Playback Won't Start
**Cause**: No recording directory selected  
**Solution**: Click "Browse..." and select a valid recording directory first

### Jerky Playback
**Cause**: Disk I/O bottleneck or slow system  
**Solution**: Try reducing playback speed or use an SSD for recordings

### Frame Counter Not Updating
**Cause**: UI not receiving SystemStatus updates  
**Solution**: Check ZMQ connection between Engine and Hub

### Tracking Not Working in Playback
**Cause**: Depth frames may be missing or corrupted  
**Solution**: Verify depth frames exist and are valid 16-bit PNG files

---

## Future Enhancements

Potential improvements for future versions:

- [ ] Frame scrubbing with slider
- [ ] Jump to specific frame number
- [ ] Playback speed presets (0.25x, 0.5x, 1x, 2x)
- [ ] Recording metadata display (date, duration, FPS)
- [ ] Thumbnail preview of recordings
- [ ] Playlist support for multiple recordings
- [ ] Export selected frame range
- [ ] Reverse playback
- [ ] Frame caching for smoother playback
- [ ] Recording comparison mode (side-by-side)

---

## Implementation Notes

### Design Decisions

1. **Automatic Looping**: Chosen to enable continuous testing without manual intervention
2. **Frame-by-Frame Control**: Essential for debugging and analysis
3. **Speed Range (0.1x-2.0x)**: Balances slow-motion analysis with fast review
4. **Separate RGB/Depth Loading**: Maintains flexibility for depth-optional tracking
5. **UI in Camera Settings**: Logical grouping with other input source controls

### Performance Considerations

- Frame files are loaded on-demand (not preloaded)
- Regex-based frame number extraction for flexible naming
- Sorted frame lists for sequential access
- Minimal memory footprint (only current frame in memory)

### Compatibility

- Works with all tracking systems (3D, New3D, 2D)
- Compatible with existing recording format
- No changes required to tracking algorithms
- Backward compatible with existing recordings

---

## Testing Checklist

- [ ] Select recording directory
- [ ] Start playback
- [ ] Pause playback
- [ ] Resume playback
- [ ] Step forward through frames
- [ ] Step backward through frames
- [ ] Adjust playback speed
- [ ] Verify frame counter updates
- [ ] Test automatic looping
- [ ] Stop playback
- [ ] Switch back to live camera
- [ ] Verify tracking works in playback mode
- [ ] Test with different recording formats
- [ ] Verify RGB/depth synchronization

---

## Related Documentation

- [Recording System Documentation](RECORDING_LOG_DOCUMENTATION.md)
- [Engine Debug Logging Guide](ENGINE_DEBUG_LOGGING_GUIDE.md)
- [Tracking System Settings](TRACKING_SYSTEM_SETTINGS_ARCHITECTURE.md)

---

## Changelog

### Version 1.0 (October 23, 2025)
- Initial implementation
- Dual-mode input (live/playback)
- Playback controls (play, pause, step, stop)
- Variable speed control (0.1x to 2.0x)
- Directory browser
- Frame counter display
- Automatic looping
- Full tracking system integration