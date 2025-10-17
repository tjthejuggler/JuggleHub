# New 3D Tracker Implementation Plan
**Date:** 2025-10-17  
**Companion to:** NEW_3D_TRACKER_ARCHITECTURE.md

---

## Implementation Phases

### Phase 1: Core C++ Tracker (Week 1-2)

#### 1.1 Data Structures
- [ ] Create `New3DBall` struct in `engine/include/New3DTracker.hpp`
- [ ] Create `New3DTrackerSettings` struct
- [ ] Create `Pose3D` struct for hand state storage
- [ ] Add `BallState` enum (HELD/IN_FLIGHT)

#### 1.2 Kalman Filter Setup
- [ ] Implement `createKalmanFilter()` with 6-state model
- [ ] Implement `predictInAirBall()` with gravity
- [ ] Implement `predictHeldBall()` with hand velocity
- [ ] Test Kalman predictions with synthetic data

#### 1.3 Core Tracker Class
- [ ] Create `New3DTracker` class inheriting from `IBallTracker`
- [ ] Implement constructor with YOLO model loading
- [ ] Implement `update()` main loop skeleton
- [ ] Implement `predictAllBalls()` method

#### 1.4 Detection Association
- [ ] Implement Hungarian algorithm for detection matching
- [ ] Implement `associateDetections()` method
- [ ] Test association with various ball configurations

### Phase 2: State Machine Logic (Week 2-3)

#### 2.1 State Updates
- [ ] Implement `handleHeldStateUpdate()`
  - [ ] Hand-off detection logic
  - [ ] Throw detection logic
  - [ ] Velocity calculation
- [ ] Implement `handleInAirStateUpdate()`
  - [ ] Catch detection logic
  - [ ] Hand availability checking
- [ ] Implement state transition debouncing

#### 2.2 Track Management
- [ ] Implement `handleUnmatchedBalls()`
  - [ ] Increment frames_since_seen
  - [ ] Delete old tracks
- [ ] Implement `createNewTracks()`
  - [ ] Multi-frame confirmation
  - [ ] Color determination
  - [ ] Initial state detection
- [ ] Implement `finalizeBallPositions()`

#### 2.3 Event Generation
- [ ] Implement throw event generation
- [ ] Implement catch event generation
- [ ] Test event timing and accuracy

### Phase 3: Color Integration (Week 3)

#### 3.1 Color Matching
- [ ] Integrate existing `matchColor()` from SimpleBallTracker
- [ ] Implement `determineColor()` for new tracks
- [ ] Implement color locking after N frames
- [ ] Test with multi-colored balls

#### 3.2 Color Profiles
- [ ] Load color profiles from `ball_settings.json`
- [ ] Implement color calibration interface
- [ ] Test calibration workflow

### Phase 4: UI Integration (Week 4)

#### 4.1 Settings UI
- [ ] Create `hub/components/ui_settings_new3d.py`
- [ ] Implement physics settings section
- [ ] Implement tracking logic section
- [ ] Implement association settings section
- [ ] Implement hand velocity section
- [ ] Implement visualization section

#### 4.2 Settings Manager
- [ ] Update `SettingsManager` to handle `new_3d` tracker
- [ ] Create `calibration_settings_new3d.json` template
- [ ] Implement settings migration from `depth_based`
- [ ] Test settings save/load

#### 4.3 Dropdown Integration
- [ ] Add "New 3D Kalman" option to tracking system dropdown
- [ ] Implement tracker switching logic
- [ ] Test switching between all three trackers

### Phase 5: Visualization (Week 4-5)

#### 5.1 Ball Visualization
- [ ] Implement `drawBall()` with state-based coloring
- [ ] Draw Kalman prediction (magenta circle)
- [ ] Draw ball ID and color name
- [ ] Draw confidence scores

#### 5.2 Hand Visualization
- [ ] Implement `drawHandThresholds()` for held radius
- [ ] Draw yellow circles around wrists
- [ ] Scale circles based on depth

#### 5.3 Association Visualization
- [ ] Implement `drawAssociations()` for matched pairs
- [ ] Draw lines from balls to detections
- [ ] Draw distance labels

#### 5.4 Debug Visualization
- [ ] Implement debug info overlay
- [ ] Show ball count and states
- [ ] Show per-ball tracking info

### Phase 6: Testing & Validation (Week 5-6)

#### 6.1 Unit Tests
- [ ] Test Kalman filter predictions
- [ ] Test association algorithm
- [ ] Test state transitions
- [ ] Test color matching

#### 6.2 Integration Tests
- [ ] Test with single ball
- [ ] Test with 3 balls
- [ ] Test with 5+ balls
- [ ] Test hand-offs
- [ ] Test rapid throws/catches

#### 6.3 Performance Tests
- [ ] Measure FPS with new tracker
- [ ] Profile CPU usage
- [ ] Optimize hot paths if needed

#### 6.4 Comparison Tests
- [ ] Compare accuracy vs `depth_based`
- [ ] Compare robustness vs `depth_based`
- [ ] Document improvements and regressions

---

## Settings Migration Guide

### Mapping from depth_based to new_3d

| depth_based Setting | new_3d Setting | Conversion |
|---------------------|----------------|------------|
| `hand_distance_threshold` (0.25m) | `held_radius_m` (0.12m) | Divide by 2 |
| `min_throw_distance` (0.20m) | `throw_velocity_threshold_mps` (0.50 m/s) | Use default |
| `traj_gravity` (9.81) | `gravity_y` (-9.81) | Negate |
| `max_tracker_distance_per_frame` (0.50m) | `association_max_distance_m` (0.50m) | Direct copy |
| `min_frames_for_state_change` (3) | `min_frames_for_new_track` (3) | Direct copy |
| `color_sample_radius` (1) | `color_sample_radius` (1) | Direct copy |
| `ball_confidence_threshold` (0.25) | `ball_confidence_threshold` (0.25) | Direct copy |
| `ball_held_confidence_threshold` (0.25) | `ball_held_confidence_threshold` (0.25) | Direct copy |
| `ignore_class` (false) | `ignore_class` (false) | Direct copy |
| `hand_velocity_enabled` (true) | `hand_velocity_enabled` (true) | Direct copy |
| `hand_velocity_threshold` (1.0) | `hand_velocity_threshold` (1.0) | Direct copy |

### Migration Code

```python
def migrate_depth_based_to_new3d(source_settings: dict) -> dict:
    """Migrate settings from depth_based to new_3d tracker."""
    
    new3d_settings = {
        'tracker_type': 'new_3d',
        'migrated_from': 'depth_based',
        'migration_date': datetime.now().isoformat(),
    }
    
    # Direct mappings
    direct_mappings = {
        'camera_settings_profile': 'camera_settings_profile',
        'resolution': 'resolution',
        'fps': 'fps',
        'depth_sensor_enabled': 'depth_sensor_enabled',
        'enable_ball_detection': 'enable_ball_detection',
        'ball_confidence_threshold': 'ball_confidence_threshold',
        'ball_held_confidence_threshold': 'ball_held_confidence_threshold',
        'nms_threshold': 'nms_threshold',
        'ignore_class': 'ignore_class',
        'pose_model_enabled': 'pose_model_enabled',
        'color_sample_radius': 'color_sample_radius',
        'hand_velocity_enabled': 'hand_velocity_enabled',
        'hand_velocity_threshold': 'hand_velocity_threshold',
        'max_tracker_distance_per_frame': 'association_max_distance_m',
        'min_frames_for_state_change': 'min_frames_for_new_track',
    }
    
    for old_key, new_key in direct_mappings.items():
        if old_key in source_settings:
            new3d_settings[new_key] = source_settings[old_key]
    
    # Converted mappings
    if 'hand_distance_threshold' in source_settings:
        new3d_settings['held_radius_m'] = source_settings['hand_distance_threshold'] / 2.0
    
    if 'traj_gravity' in source_settings:
        new3d_settings['gravity_y'] = -abs(source_settings['traj_gravity'])
        new3d_settings['gravity_x'] = 0.0
        new3d_settings['gravity_z'] = 0.0
    
    # Use defaults for new settings
    defaults = {
        'throw_velocity_threshold_mps': 0.50,
        'max_frames_unseen': 30,
        'min_frames_for_color_lock': 5,
        'use_color_tracking': True,
        'color_match_threshold': 0.50,
        'show_kalman_prediction': True,
        'show_held_radius': True,
        'show_association_lines': True,
    }
    
    for key, value in defaults.items():
        if key not in new3d_settings:
            new3d_settings[key] = value
    
    return new3d_settings
```

---

## Testing Strategy

### Unit Tests

```cpp
// Test Kalman filter prediction
TEST(New3DTracker, KalmanPrediction) {
    New3DTracker tracker(...);
    New3DBall ball;
    ball.kf = tracker.createKalmanFilter(cv::Point3f(0, 1, 2));
    
    // Predict for 1 second with gravity
    for (int i = 0; i < 30; i++) {
        tracker.predictInAirBall(ball, 1.0f / 30.0f);
    }
    
    // Check that ball fell due to gravity
    EXPECT_LT(ball.predicted_position.y, 1.0f);
}

// Test association algorithm
TEST(New3DTracker, Association) {
    std::vector<New3DBall> balls = {/* ... */};
    std::vector<Detection> detections = {/* ... */};
    
    auto result = tracker.associateDetections(balls, detections, 0.5f);
    
    EXPECT_EQ(result.matched_pairs.size(), expected_matches);
    EXPECT_EQ(result.unmatched_balls.size(), expected_unmatched_balls);
    EXPECT_EQ(result.unmatched_detections.size(), expected_unmatched_dets);
}

// Test state transitions
TEST(New3DTracker, ThrowDetection) {
    New3DBall ball;
    ball.state = BallState::HELD;
    ball.associated_hand_id = 0;
    
    Detection detection;
    detection.world_pos = cv::Point3f(0.5, 1.0, 2.0);  // Far from hand
    
    SimpleHand hand;
    hand.wrist_pos_3d = cv::Point3f(0, 1.0, 2.0);
    
    // Should trigger throw
    tracker.handleHeldStateUpdate(ball, detection, {hand}, prev_pose, 0.033f, events);
    
    EXPECT_EQ(ball.state, BallState::IN_FLIGHT);
    EXPECT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].type, BallEvent::THROW);
}
```

### Integration Tests

```python
def test_single_ball_tracking():
    """Test tracking a single ball through throw and catch."""
    tracker = New3DTracker(...)
    
    # Simulate ball being held
    for i in range(10):
        balls, events = tracker.update(frame, depth, intrinsics)
        assert len(balls) == 1
        assert balls[0].state == BallState.HELD
    
    # Simulate throw
    # ... move hand and ball apart ...
    balls, events = tracker.update(frame, depth, intrinsics)
    assert len(events) == 1
    assert events[0].type == BallEvent.THROW
    assert balls[0].state == BallState.IN_FLIGHT
    
    # Simulate flight
    for i in range(20):
        balls, events = tracker.update(frame, depth, intrinsics)
        assert balls[0].state == BallState.IN_FLIGHT
    
    # Simulate catch
    # ... move ball near hand ...
    balls, events = tracker.update(frame, depth, intrinsics)
    assert len(events) == 1
    assert events[0].type == BallEvent.CATCH
    assert balls[0].state == BallState.HELD

def test_three_ball_cascade():
    """Test tracking 3-ball cascade pattern."""
    tracker = New3DTracker(...)
    
    # Run for 100 frames
    throw_count = 0
    catch_count = 0
    
    for i in range(100):
        balls, events = tracker.update(frame, depth, intrinsics)
        
        for event in events:
            if event.type == BallEvent.THROW:
                throw_count += 1
            elif event.type == BallEvent.CATCH:
                catch_count += 1
        
        # Should maintain 3 balls
        assert len(balls) == 3
    
    # Should have detected multiple throws and catches
    assert throw_count > 10
    assert catch_count > 10
    assert abs(throw_count - catch_count) <= 2  # Should be roughly equal
```

### Performance Benchmarks

```cpp
void benchmark_new3d_tracker() {
    New3DTracker tracker(...);
    
    // Load test video
    cv::VideoCapture cap("test_juggling.mp4");
    
    auto start = std::chrono::high_resolution_clock::now();
    int frame_count = 0;
    
    while (cap.isOpened()) {
        cv::Mat frame, depth;
        cap >> frame;
        if (frame.empty()) break;
        
        auto balls_events = tracker.update(frame, depth, intrinsics);
        frame_count++;
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    double fps = frame_count / (duration.count() / 1000.0);
    std::cout << "Average FPS: " << fps << std::endl;
    std::cout << "Frame time: " << (duration.count() / frame_count) << "ms" << std::endl;
}
```

---

## File Structure

### New Files to Create

```
engine/
├── include/
│   └── New3DTracker.hpp          # NEW: Main tracker header
└── src/
    └── New3DTracker.cpp          # NEW: Main tracker implementation

hub/
├── components/
│   └── ui_settings_new3d.py      # NEW: UI settings for new_3d
└── config/
    └── calibration_settings_new3d.json  # NEW: Default settings

docs/
└── NEW_3D_TRACKER_USER_GUIDE.md  # NEW: User documentation
```

### Files to Modify

```
engine/
└── src/
    └── Engine.cpp                # Add new_3d tracker instantiation

hub/
└── components/
    ├── ui_settings_common.py     # Add new_3d to dropdown
    ├── ui_settings_manager.py    # Add new_3d settings handling
    └── ui_settings.py            # Integrate new_3d sections
```

---

## Risk Assessment

### High Risk Items

1. **Kalman Filter Tuning**
   - Risk: Filter may be unstable or inaccurate
   - Mitigation: Start with conservative noise parameters, tune with real data
   
2. **State Transition Logic**
   - Risk: False throw/catch detections
   - Mitigation: Multi-frame confirmation, velocity thresholds
   
3. **Performance Impact**
   - Risk: Kalman filter may slow down tracking
   - Mitigation: Profile early, optimize if needed

### Medium Risk Items

1. **Color Integration**
   - Risk: Color matching may not work well with Kalman predictions
   - Mitigation: Use existing proven color matching code
   
2. **Settings Migration**
   - Risk: Migrated settings may not work well
   - Mitigation: Provide sensible defaults, allow manual tuning

### Low Risk Items

1. **UI Integration**
   - Risk: UI may be cluttered with too many settings
   - Mitigation: Use collapsible sections, group related settings
   
2. **Visualization**
   - Risk: Too much visual clutter
   - Mitigation: Make visualizations toggleable

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

## Timeline

| Week | Phase | Deliverables |
|------|-------|--------------|
| 1 | Core C++ Tracker | Data structures, Kalman filter, basic tracking |
| 2 | State Machine | State transitions, event generation |
| 3 | Color Integration | Color matching, calibration |
| 4 | UI Integration | Settings UI, dropdown, migration |
| 5 | Visualization | Ball/hand/association visualization |
| 6 | Testing | Unit tests, integration tests, benchmarks |

**Total Estimated Time:** 6 weeks

---

## Next Steps

1. **Review this plan** with the team
2. **Create feature branch** `feature/new-3d-tracker`
3. **Start Phase 1** with data structures
4. **Set up CI/CD** for automated testing
5. **Create tracking issue** in project management system

---

## Questions for Review

1. Should we support 2D mode (no depth) for new_3d?
2. What should be the default tracker for new users?
3. Should we deprecate depth_based after new_3d is stable?
4. Do we need GPU acceleration for Kalman filter?
5. Should color tracking be optional or always enabled?

---

**Document Version:** 1.0  
**Last Updated:** 2025-10-17  
**Author:** Architecture Team  
**Status:** Ready for Review