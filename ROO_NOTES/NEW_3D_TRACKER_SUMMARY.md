# New 3D Tracker - Executive Summary
**Date:** 2025-10-17  
**Status:** Architecture Complete - Ready for Implementation

---

## Overview

This document summarizes the architectural design for implementing a new 3D tracking system called **`new_3d`** based on Kalman filtering and physics-based prediction. The system will be added as a **third tracking option** in JuggleHub, alongside the existing `depth_based` and `simple_2d` trackers.

---

## Key Decisions

### 1. System Architecture

✅ **Decision:** Add `new_3d` as a third independent tracking option  
**Rationale:** Allows users to choose the best tracker for their needs without breaking existing functionality  
**Impact:** Requires UI dropdown update, new settings file, and tracker instantiation logic

### 2. Core Technology

✅ **Decision:** Use OpenCV's Kalman Filter with 6-state model [x, y, z, vx, vy, vz]  
**Rationale:** Proven, efficient, and well-suited for ballistic motion with gravity  
**Impact:** Requires careful tuning of process/measurement noise covariance

### 3. State Machine

✅ **Decision:** Two-state system (HELD / IN_FLIGHT) with multi-frame confirmation  
**Rationale:** Simpler than current system, more robust to noise  
**Impact:** Cleaner code, easier to debug and maintain

### 4. YOLO Integration

✅ **Decision:** Reuse existing YOLO models and detection pipeline  
**Rationale:** No need for model retraining, faster implementation  
**Impact:** Must handle both 'ball' and 'ball_held' classes appropriately

### 5. Color Tracking

✅ **Decision:** Include color-based identification using existing color matching code  
**Rationale:** Essential for multi-ball tracking and ball identification  
**Impact:** Requires color profile management and calibration UI

### 6. Settings Migration

✅ **Decision:** Provide automatic migration from `depth_based` settings  
**Rationale:** Smooth user experience when switching trackers  
**Impact:** Requires mapping logic and sensible default conversions

---

## Variable Name Mapping

### Specification → Codebase

| Specification | Codebase | Notes |
|--------------|----------|-------|
| `Vector3` | `cv::Point3f` | Use OpenCV types |
| `BallState::IN_AIR` | `BallState::IN_FLIGHT` | Match existing convention |
| `Hand::LEFT/RIGHT/NONE` | `0/1/-1` (int) | Match existing convention |
| `Detection3D` | `Detection` | Extend existing struct |
| `Pose3D` | Custom struct | Store previous frame hands |
| `TrackedBall` | `New3DBall` | New structure |
| `JugglingTracker` | `New3DTracker` | New class |
| `held_radius_m` | `held_radius_m` | Keep as-is |
| `throw_velocity_threshold_mps` | `throw_velocity_threshold_mps` | Keep as-is |
| `gravity_mps2` | `gravity_mps2` (cv::Point3f) | Keep as-is |

---

## Settings Architecture

### New Settings File

**Location:** `hub/config/calibration_settings_new3d.json`

### Key Settings (with defaults)

| Setting | Default | Range | Description |
|---------|---------|-------|-------------|
| `held_radius_m` | 0.12 | 0.05-0.30 | Radius for "held" detection (meters) |
| `association_max_distance_m` | 0.50 | 0.10-2.00 | Max distance for detection matching |
| `throw_velocity_threshold_mps` | 0.50 | 0.10-3.00 | Min velocity for throw detection |
| `gravity_y` | -9.81 | -15.0 to -5.0 | Gravity acceleration (m/s²) |
| `max_frames_unseen` | 30 | 10-60 | Frames before deleting track |
| `min_frames_for_new_track` | 3 | 1-10 | Frames to confirm new track |
| `min_frames_for_color_lock` | 5 | 1-20 | Frames to lock color |
| `use_color_tracking` | true | bool | Enable color identification |
| `color_match_threshold` | 0.50 | 0.0-1.0 | Min color match score |
| `hand_velocity_enabled` | true | bool | Enable velocity-based throw detection |
| `hand_velocity_threshold` | 1.0 | 0.1-5.0 | Min hand speed (m/s) |

### Settings Migration

**From `depth_based` to `new_3d`:**

- `hand_distance_threshold` (0.25m) → `held_radius_m` (0.12m) [÷2]
- `traj_gravity` (9.81) → `gravity_y` (-9.81) [negate]
- `max_tracker_distance_per_frame` → `association_max_distance_m` [direct]
- `min_frames_for_state_change` → `min_frames_for_new_track` [direct]

---

## Implementation Phases

### Phase 1: Core C++ Tracker (Weeks 1-2)
- Data structures (New3DBall, New3DTrackerSettings, Pose3D)
- Kalman filter setup and prediction
- Core tracker class skeleton
- Detection association (Hungarian algorithm)

### Phase 2: State Machine Logic (Weeks 2-3)
- HELD state update logic (hand-offs, throws)
- IN_FLIGHT state update logic (catches)
- Track management (creation, deletion)
- Event generation (throw/catch)

### Phase 3: Color Integration (Week 3)
- Color matching integration
- Color determination for new tracks
- Color locking mechanism
- Calibration interface

### Phase 4: UI Integration (Week 4)
- Create `ui_settings_new3d.py`
- Add to tracking system dropdown
- Settings manager updates
- Migration logic

### Phase 5: Visualization (Weeks 4-5)
- Ball visualization (state-based coloring)
- Hand threshold circles
- Association lines
- Debug overlays

### Phase 6: Testing & Validation (Weeks 5-6)
- Unit tests (Kalman, association, states)
- Integration tests (single ball, 3-ball cascade)
- Performance benchmarks
- Comparison with `depth_based`

**Total Timeline:** 6 weeks

---

## File Structure

### New Files

```
engine/include/New3DTracker.hpp          # Main tracker header
engine/src/New3DTracker.cpp              # Main tracker implementation
hub/components/ui_settings_new3d.py      # UI settings sections
hub/config/calibration_settings_new3d.json  # Default settings
```

### Modified Files

```
engine/src/Engine.cpp                    # Add new_3d instantiation
hub/components/ui_settings_common.py     # Add to dropdown
hub/components/ui_settings_manager.py    # Handle new_3d settings
hub/components/ui_settings.py            # Integrate new_3d sections
```

---

## UI Changes

### Tracking System Dropdown

**Before:**
```
- Depth-Based 3D (Current)
- Simple 2D (New)
```

**After:**
```
- Depth-Based 3D (Legacy)
- Simple 2D
- New 3D Kalman ⭐ (Recommended)
```

### Settings Sections (new_3d only)

1. **⚛️ Physics & Kalman Filter**
   - Held Radius, Throw Velocity Threshold, Gravity

2. **🎯 Tracking Logic**
   - Max Frames Unseen, Min Frames for New Track, Min Frames for Color Lock

3. **🔗 Detection Association**
   - Max Association Distance

4. **🤚 Hand Velocity Tracking**
   - Enable toggle, Velocity Threshold

5. **👁️ Visualization**
   - Show Kalman Prediction, Show Held Radius, Show Association Lines

---

## Visualization Features

### Ball Visualization
- **Bounding box**: Green (HELD) / Cyan (IN_FLIGHT)
- **Kalman prediction**: Magenta circle with line to current position
- **Labels**: Ball ID, color name, confidence scores
- **State indicator**: Visual distinction between HELD/IN_FLIGHT

### Hand Visualization
- **Held radius**: Yellow circles around wrists
- **Depth-scaled**: Circle size adjusts based on hand depth

### Association Visualization
- **Match lines**: Green lines from balls to matched detections
- **Distance labels**: Show matching distance in meters

### Debug Overlay
- Tracker name and version
- Ball count by state (Held: X, Air: Y)
- Per-ball tracking info (frames seen, color lock status)

---

## Testing Strategy

### Unit Tests
- ✅ Kalman filter prediction accuracy
- ✅ Association algorithm correctness
- ✅ State transition logic
- ✅ Color matching integration

### Integration Tests
- ✅ Single ball tracking (throw → flight → catch)
- ✅ 3-ball cascade pattern
- ✅ 5+ ball tracking
- ✅ Hand-off detection
- ✅ Rapid throw/catch sequences

### Performance Tests
- ✅ FPS measurement (target: >30 FPS)
- ✅ CPU profiling
- ✅ Memory usage
- ✅ Comparison with `depth_based`

---

## Success Criteria

### Minimum Viable Product (MVP)
- [ ] Tracks 3 balls reliably in cascade pattern
- [ ] Detects throws and catches with >90% accuracy
- [ ] Maintains stable IDs (no swapping)
- [ ] Runs at >30 FPS on target hardware
- [ ] UI allows switching to new_3d tracker
- [ ] Settings can be saved and loaded

### Full Release
- [ ] Tracks 5+ balls reliably
- [ ] Handles occlusions gracefully
- [ ] Color-based identification works
- [ ] Hand-offs detected correctly
- [ ] Migration from depth_based works
- [ ] Comprehensive documentation
- [ ] User guide with examples
- [ ] Performance benchmarks documented

---

## Risk Assessment

### High Risk ⚠️
1. **Kalman Filter Tuning** - May require extensive real-world testing
2. **State Transition Logic** - False positives/negatives in throw/catch detection
3. **Performance Impact** - Kalman filter overhead

### Medium Risk ⚡
1. **Color Integration** - Color matching with Kalman predictions
2. **Settings Migration** - Converted settings may not work optimally

### Low Risk ✅
1. **UI Integration** - Well-established patterns
2. **Visualization** - Straightforward implementation

---

## Next Steps

1. ✅ **Review architecture documents** with team
2. ⏭️ **Create feature branch** `feature/new-3d-tracker`
3. ⏭️ **Start Phase 1** implementation
4. ⏭️ **Set up CI/CD** for automated testing
5. ⏭️ **Create tracking issue** in project management

---

## Related Documents

- [`NEW_3D_TRACKER_ARCHITECTURE.md`](NEW_3D_TRACKER_ARCHITECTURE.md) - Detailed technical architecture
- [`NEW_3D_TRACKER_IMPLEMENTATION_PLAN.md`](NEW_3D_TRACKER_IMPLEMENTATION_PLAN.md) - Phase-by-phase implementation guide
- [`TRACKING_SYSTEM_SETTINGS_ARCHITECTURE.md`](TRACKING_SYSTEM_SETTINGS_ARCHITECTURE.md) - Existing settings system
- [`SimpleBallTracker.hpp`](engine/include/SimpleBallTracker.hpp) - Current depth_based tracker

---

## Questions for Team Review

1. ❓ Should we support 2D mode (no depth) for new_3d?
2. ❓ What should be the default tracker for new users?
3. ❓ Should we deprecate depth_based after new_3d is stable?
4. ❓ Do we need GPU acceleration for Kalman filter?
5. ❓ Should color tracking be optional or always enabled?

---

## Approval Checklist

- [ ] Architecture reviewed by lead developer
- [ ] UI/UX design approved
- [ ] Performance requirements validated
- [ ] Testing strategy approved
- [ ] Timeline agreed upon
- [ ] Resource allocation confirmed

---

**Document Version:** 1.0  
**Last Updated:** 2025-10-17  
**Status:** ✅ Ready for Implementation  
**Estimated Effort:** 6 weeks (1 developer)

---

## Appendix: Quick Reference

### Kalman Filter States
```
State Vector: [x, y, z, vx, vy, vz]
- Position: (x, y, z) in meters
- Velocity: (vx, vy, vz) in m/s
- Acceleration: gravity applied to vy
```

### State Transitions
```
HELD → IN_FLIGHT:
  - Distance from hand > held_radius_m
  - Relative velocity > throw_velocity_threshold_mps
  - Multi-frame confirmation

IN_FLIGHT → HELD:
  - Distance to available hand < held_radius_m
  - Multi-frame confirmation
```

### Association Algorithm
```
1. Calculate distance matrix (balls × detections)
2. Apply Hungarian algorithm for optimal matching
3. Filter matches by max_distance threshold
4. Return matched pairs + unmatched balls/detections
```

---

**End of Summary**