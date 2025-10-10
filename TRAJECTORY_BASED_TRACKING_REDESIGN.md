# Trajectory-Based Ball Tracking System - Architectural Redesign

**Date:** 2025-10-10  
**Status:** Architecture Planning Phase  
**Goal:** Simplify tracking by using physics-based trajectory prediction instead of complex fallback logic

---

## 🎯 **VISION: Simplified State Machine**

Replace the complex 5-stage fallback system with a clean 2-state model:

```
┌─────────────┐  THROW (ball leaves wrist)   ┌──────────────┐
│    HELD     │ ──────────────────────────> │  IN_FLIGHT   │
│             │                              │              │
│ Tracker =   │                              │ Tracker =    │
│ Wrist Pos   │                              │ Trajectory   │
└─────────────┘ <────────────────────────── └──────────────┘
                CATCH (ball reaches wrist)
```

### **Key Principles:**

1. **HELD State**: Ball tracker is ALWAYS at wrist position (no blob search, no ML ball_held)
2. **IN_FLIGHT State**: Ball tracker follows predicted trajectory path
3. **Trajectory Confidence**: More verified points = higher confidence = tighter search radius
4. **GPU Acceleration**: All trajectory calculations done on GPU
5. **Visual Feedback**: Trajectory path shown in live feed and recordings

---

## 📊 **NEW BALL STATE STRUCTURE**

```cpp
enum BallState {
    HELD,       // Ball is in hand, tracker at wrist
    IN_FLIGHT   // Ball is airborne, tracker on trajectory
};

struct TrajectoryPoint {
    cv::Point3f position;      // 3D world position
    uint64_t timestamp;        // When this point was verified
    float confidence;          // How confident we are (0.0-1.0)
    bool verified;             // True if YOLO or color blob confirmed
};

struct BallTrajectory {
    std::vector<TrajectoryPoint> points;  // All verified in-flight points
    
    // Physics parameters (calculated on GPU)
    cv::Point3f initial_velocity;    // v0 from throw
    cv::Point3f initial_position;    // p0 from throw
    float gravity;                   // g = 9.81 m/s²
    uint64_t throw_timestamp;        // When trajectory started
    
    // Confidence metrics
    int verified_point_count;        // Number of confirmed points
    float trajectory_confidence;     // Overall confidence (0.0-1.0)
    float search_radius_m;           // Current search radius (shrinks with confidence)
    
    // Prediction cache (GPU-computed)
    std::vector<cv::Point3f> predicted_path;  // Full predicted trajectory
    uint64_t prediction_timestamp;            // When prediction was computed
};

struct SimpleBall {
    int id;
    std::string color_name;
    
    // Current state
    BallState state;                 // HELD or IN_FLIGHT
    cv::Point3f position;            // Current tracker position
    cv::Point2f pixel_pos;           // Current 2D position
    cv::Rect_<float> bbox;           // Bounding box
    
    // State-specific data
    int held_by_hand_id;             // -1 if not held, 0=left, 1=right
    BallTrajectory trajectory;       // Only valid when IN_FLIGHT
    
    // Tracking metadata
    bool has_yolo_detection;         // YOLO sees it this frame
    float yolo_confidence;
    float color_match_score;
    std::string tracking_reason;     // Debug info
    
    // State transition tracking
    int state_frames;                // Frames in current state
    int min_frames_for_transition;   // Debouncing threshold
};
```

---

## 🚀 **GPU-ACCELERATED TRAJECTORY SYSTEM**

### **New GPU Component: `GpuTrajectoryPredictor`**

```cpp
class GpuTrajectoryPredictor {
public:
    /**
     * Calculate full trajectory path using physics equations
     * Runs on GPU for parallel computation of all points
     * 
     * @param initial_pos Starting position (throw point)
     * @param initial_vel Initial velocity vector
     * @param gravity Gravitational acceleration (default: 9.81 m/s²)
     * @param time_step Time between predicted points (default: 0.033s = 30fps)
     * @param max_time Maximum trajectory time (default: 3.0s)
     * @return Vector of predicted 3D positions along trajectory
     */
    std::vector<cv::Point3f> predictTrajectory(
        const cv::Point3f& initial_pos,
        const cv::Point3f& initial_vel,
        float gravity = 9.81f,
        float time_step = 0.033f,
        float max_time = 3.0f
    );
    
    /**
     * Find closest point on trajectory to a given position
     * Used for matching YOLO detections to trajectory
     * 
     * @param trajectory Predicted trajectory points
     * @param position Position to match
     * @return Index of closest point and distance
     */
    std::pair<int, float> findClosestPoint(
        const std::vector<cv::Point3f>& trajectory,
        const cv::Point3f& position
    );
    
    /**
     * Calculate initial velocity from first N trajectory points
     * Uses least-squares fitting on GPU
     * 
     * @param points Verified trajectory points
     * @return Estimated initial velocity vector
     */
    cv::Point3f estimateInitialVelocity(
        const std::vector<TrajectoryPoint>& points
    );
    
    /**
     * Refine trajectory prediction as more points are verified
     * Updates physics parameters to minimize error
     * 
     * @param trajectory Current trajectory with verified points
     * @return Updated trajectory with refined prediction
     */
    BallTrajectory refineTrajectory(const BallTrajectory& trajectory);
    
private:
    cv::UMat gpu_trajectory_cache_;  // Pre-allocated GPU memory
    std::mutex mutex_;
    bool gpu_enabled_;
};
```

### **Physics Equations (GPU Kernel)**

```cpp
// Ballistic trajectory with air resistance (optional)
// Position at time t:
//   x(t) = x0 + vx0 * t
//   y(t) = y0 + vy0 * t
//   z(t) = z0 + vz0 * t - 0.5 * g * t²

// GPU kernel computes all points in parallel:
__kernel void compute_trajectory(
    __global float3* output_points,
    float3 initial_pos,
    float3 initial_vel,
    float gravity,
    float time_step,
    int num_points
) {
    int i = get_global_id(0);
    if (i >= num_points) return;
    
    float t = i * time_step;
    output_points[i].x = initial_pos.x + initial_vel.x * t;
    output_points[i].y = initial_pos.y + initial_vel.y * t;
    output_points[i].z = initial_pos.z + initial_vel.z * t - 0.5f * gravity * t * t;
}
```

---

## 🎬 **SIMPLIFIED TRACKING LOGIC**

### **Main Update Loop (Pseudocode)**

```cpp
std::pair<std::vector<SimpleBall>, std::vector<BallEvent>> update(
    const cv::Mat& color_frame,
    const cv::Mat& depth_frame,
    const CameraIntrinsics& intrinsics
) {
    // 1. Run YOLO detection (unchanged)
    auto yolo_detections = runBallDetection(color_frame, depth_frame, intrinsics);
    
    // 2. Run pose estimation (unchanged)
    auto hands = runPoseEstimation(color_frame, depth_frame, intrinsics);
    
    // 3. Process each ball based on its state
    for (auto& ball : balls_) {
        if (ball.state == HELD) {
            updateHeldBall(ball, hands, yolo_detections);
        } else {  // IN_FLIGHT
            updateInFlightBall(ball, yolo_detections, color_frame, depth_frame, intrinsics);
        }
    }
    
    // 4. Detect state transitions and generate events
    auto events = detectStateTransitions(balls_, hands);
    
    return {balls_, events};
}
```

### **HELD State Logic (MASSIVELY SIMPLIFIED)**

```cpp
void updateHeldBall(
    SimpleBall& ball,
    const std::vector<SimpleHand>& hands,
    const std::vector<Detection>& detections
) {
    // Find the hand holding this ball
    const SimpleHand* hand = findHand(hands, ball.held_by_hand_id);
    
    if (!hand || !hand->is_visible) {
        // Hand not visible - keep last position
        ball.tracking_reason = "HELD_hand_offscreen";
        return;
    }
    
    // ALWAYS place tracker at wrist - NO blob search, NO ML ball_held check
    ball.position = hand->wrist_pos_3d;
    ball.pixel_pos = project_3d_to_2d(hand->wrist_pos_3d, intrinsics);
    ball.tracking_reason = "HELD@wrist";
    
    // Check for THROW: ball moves away from wrist
    float dist_from_wrist = cv::norm(ball.position - hand->wrist_pos_3d);
    
    // Look for YOLO detection moving away from hand
    for (const auto& det : detections) {
        float det_dist_from_wrist = cv::norm(det.world_pos - hand->wrist_pos_3d);
        
        // If detection is moving away AND matches ball color
        if (det_dist_from_wrist > THROW_DISTANCE_THRESHOLD) {  // e.g., 0.20m
            float color_score = matchColor(det, ball.color_profile, color_frame);
            
            if (color_score > MIN_COLOR_MATCH) {  // e.g., 0.5
                // THROW DETECTED - transition to IN_FLIGHT
                initiateThrow(ball, det, hand);
                break;
            }
        }
    }
}
```

### **IN_FLIGHT State Logic (TRAJECTORY-BASED)**

```cpp
void updateInFlightBall(
    SimpleBall& ball,
    const std::vector<Detection>& detections,
    const cv::Mat& color_frame,
    const cv::Mat& depth_frame,
    const CameraIntrinsics& intrinsics
) {
    // 1. Update trajectory prediction on GPU
    if (needsTrajectoryUpdate(ball)) {
        ball.trajectory = gpu_trajectory_predictor_->refineTrajectory(ball.trajectory);
    }
    
    // 2. Get current predicted position from trajectory
    float time_since_throw = getCurrentTime() - ball.trajectory.throw_timestamp;
    cv::Point3f predicted_pos = interpolateTrajectory(ball.trajectory, time_since_throw);
    
    // 3. Search for verification along trajectory
    bool verified = false;
    
    // 3a. Try YOLO detection near trajectory
    for (const auto& det : detections) {
        auto [closest_idx, distance] = gpu_trajectory_predictor_->findClosestPoint(
            ball.trajectory.predicted_path, det.world_pos
        );
        
        if (distance < ball.trajectory.search_radius_m) {
            float color_score = matchColor(det, ball.color_profile, color_frame);
            
            if (color_score > MIN_COLOR_MATCH) {
                // VERIFIED - add point to trajectory
                addVerifiedPoint(ball.trajectory, det.world_pos, getCurrentTime());
                ball.position = det.world_pos;
                verified = true;
                break;
            }
        }
    }
    
    // 3b. Fallback: Color blob search along trajectory
    if (!verified) {
        cv::Point2f predicted_2d = project_3d_to_2d(predicted_pos, intrinsics);
        cv::Point2f blob = gpu_hsv_converter_->findColorBlob(
            color_frame, 
            createSearchROI(predicted_2d, ball.trajectory.search_radius_m),
            ball.color_profile.min_hsv,
            ball.color_profile.max_hsv
        );
        
        if (blob.x > 0) {
            float depth = getDepthAtPoint(depth_frame, blob);
            cv::Point3f blob_3d = deprojectToWorld(blob, depth, intrinsics);
            
            // Verify blob is on trajectory
            auto [closest_idx, distance] = gpu_trajectory_predictor_->findClosestPoint(
                ball.trajectory.predicted_path, blob_3d
            );
            
            if (distance < ball.trajectory.search_radius_m) {
                addVerifiedPoint(ball.trajectory, blob_3d, getCurrentTime());
                ball.position = blob_3d;
                verified = true;
            }
        }
    }
    
    // 3c. No verification - use predicted position
    if (!verified) {
        ball.position = predicted_pos;
        ball.tracking_reason = "IN_FLIGHT_predicted";
    } else {
        ball.tracking_reason = "IN_FLIGHT_verified";
        
        // Update confidence and search radius
        updateTrajectoryConfidence(ball.trajectory);
    }
    
    // 4. Check for CATCH: ball reaches hand
    for (const auto& hand : hands) {
        if (!hand.is_visible) continue;
        
        float dist_to_hand = cv::norm(ball.position - hand.wrist_pos_3d);
        
        if (dist_to_hand < CATCH_DISTANCE_THRESHOLD) {  // e.g., 0.15m
            // CATCH DETECTED - transition to HELD
            initiateCatch(ball, hand);
            break;
        }
    }
}
```

### **Trajectory Confidence System**

```cpp
void updateTrajectoryConfidence(BallTrajectory& trajectory) {
    // More verified points = higher confidence = tighter search
    int verified_count = trajectory.verified_point_count;
    
    // Confidence curve: 0 points = 0.0, 5+ points = 1.0
    trajectory.trajectory_confidence = std::min(1.0f, verified_count / 5.0f);
    
    // Search radius shrinks with confidence
    // Start: 0.30m, End: 0.10m
    float min_radius = 0.10f;
    float max_radius = 0.30f;
    trajectory.search_radius_m = max_radius - 
        (trajectory.trajectory_confidence * (max_radius - min_radius));
}
```

---

## 🎨 **TRAJECTORY VISUALIZATION**

### **New Visualization Settings**

```cpp
struct TrajectoryVisualizationSettings {
    bool show_trajectory = true;           // Toggle trajectory display
    bool show_verified_points = true;      // Show confirmed points
    bool show_predicted_path = true;       // Show full predicted path
    bool show_search_radius = true;        // Show current search area
    
    // Colors
    cv::Scalar trajectory_color = cv::Scalar(0, 255, 255);      // Cyan
    cv::Scalar verified_point_color = cv::Scalar(0, 255, 0);    // Green
    cv::Scalar predicted_point_color = cv::Scalar(255, 255, 0); // Yellow
    cv::Scalar search_radius_color = cv::Scalar(255, 0, 255);   // Magenta
    
    // Sizes
    int trajectory_thickness = 2;
    int point_radius = 5;
    float trajectory_point_spacing = 0.05f;  // 5cm between drawn points
};
```

### **Visualization Rendering**

```cpp
void drawTrajectory(
    cv::Mat& frame,
    const SimpleBall& ball,
    const CameraIntrinsics& intrinsics,
    const TrajectoryVisualizationSettings& settings
) {
    if (ball.state != IN_FLIGHT || !settings.show_trajectory) return;
    
    // 1. Draw predicted path
    if (settings.show_predicted_path) {
        std::vector<cv::Point2f> path_2d;
        for (const auto& point_3d : ball.trajectory.predicted_path) {
            cv::Point2f point_2d = project_3d_to_2d(point_3d, intrinsics);
            if (isOnScreen(point_2d, frame)) {
                path_2d.push_back(point_2d);
            }
        }
        
        // Draw as polyline
        if (path_2d.size() > 1) {
            cv::polylines(frame, path_2d, false, 
                         settings.trajectory_color, 
                         settings.trajectory_thickness);
        }
    }
    
    // 2. Draw verified points
    if (settings.show_verified_points) {
        for (const auto& traj_point : ball.trajectory.points) {
            if (!traj_point.verified) continue;
            
            cv::Point2f point_2d = project_3d_to_2d(traj_point.position, intrinsics);
            if (isOnScreen(point_2d, frame)) {
                cv::circle(frame, point_2d, settings.point_radius,
                          settings.verified_point_color, -1);
            }
        }
    }
    
    // 3. Draw current search radius
    if (settings.show_search_radius) {
        cv::Point2f current_2d = ball.pixel_pos;
        float radius_pixels = ball.trajectory.search_radius_m * 
                             intrinsics.fx / ball.position.z;  // Approximate
        
        cv::circle(frame, current_2d, radius_pixels,
                  settings.search_radius_color, 2);
    }
    
    // 4. Draw confidence indicator
    std::string conf_text = cv::format("Conf: %.2f", 
                                       ball.trajectory.trajectory_confidence);
    cv::putText(frame, conf_text, 
                cv::Point(ball.pixel_pos.x + 10, ball.pixel_pos.y - 10),
                cv::FONT_HERSHEY_SIMPLEX, 0.5, 
                settings.trajectory_color, 1);
}
```

---

## 📝 **STATE TRANSITION LOGIC**

### **THROW Detection (HELD → IN_FLIGHT)**

```cpp
void initiateThrow(
    SimpleBall& ball,
    const Detection& first_detection,
    const SimpleHand* hand
) {
    // 1. Change state
    ball.state = IN_FLIGHT;
    ball.state_frames = 0;
    
    // 2. Initialize trajectory
    ball.trajectory = BallTrajectory();
    ball.trajectory.initial_position = hand->wrist_pos_3d;
    ball.trajectory.throw_timestamp = getCurrentTimestamp();
    
    // 3. Estimate initial velocity from first detection
    float dt = 0.033f;  // Assume 30fps
    ball.trajectory.initial_velocity = 
        (first_detection.world_pos - hand->wrist_pos_3d) / dt;
    
    // 4. Add first verified point
    TrajectoryPoint first_point;
    first_point.position = first_detection.world_pos;
    first_point.timestamp = getCurrentTimestamp();
    first_point.confidence = first_detection.confidence;
    first_point.verified = true;
    ball.trajectory.points.push_back(first_point);
    ball.trajectory.verified_point_count = 1;
    
    // 5. Compute initial trajectory prediction on GPU
    ball.trajectory.predicted_path = gpu_trajectory_predictor_->predictTrajectory(
        ball.trajectory.initial_position,
        ball.trajectory.initial_velocity
    );
    
    // 6. Set initial search radius (wide)
    ball.trajectory.search_radius_m = 0.30f;
    ball.trajectory.trajectory_confidence = 0.2f;  // Low confidence initially
    
    // 7. Generate THROW event
    events.push_back({BallEvent::THROW, ball.id, ball.held_by_hand_id, 
                     getCurrentTimestamp()});
    
    ball.tracking_reason = "THROW_initiated";
}
```

### **CATCH Detection (IN_FLIGHT → HELD)**

```cpp
void initiateCatch(SimpleBall& ball, const SimpleHand& hand) {
    // 1. Change state
    ball.state = HELD;
    ball.state_frames = 0;
    ball.held_by_hand_id = hand.id;
    
    // 2. Clear trajectory data
    ball.trajectory = BallTrajectory();  // Reset
    
    // 3. Snap to wrist
    ball.position = hand.wrist_pos_3d;
    ball.pixel_pos = project_3d_to_2d(hand.wrist_pos_3d, intrinsics);
    
    // 4. Generate CATCH event
    events.push_back({BallEvent::CATCH, ball.id, hand.id, 
                     getCurrentTimestamp()});
    
    ball.tracking_reason = "CATCH_completed";
}
```

---

## 🔧 **IMPLEMENTATION ROADMAP**

### **Phase 1: GPU Trajectory Predictor** (New Component)

**Files to Create:**
- `engine/include/GpuTrajectoryPredictor.hpp`
- `engine/src/GpuTrajectoryPredictor.cpp`

**Key Functions:**
1. `predictTrajectory()` - GPU kernel for ballistic motion
2. `findClosestPoint()` - GPU-accelerated distance calculation
3. `estimateInitialVelocity()` - Least-squares fitting on GPU
4. `refineTrajectory()` - Iterative trajectory refinement

**Dependencies:**
- OpenCV UMat (already used in `GpuHsvConverter`)
- OpenCL kernels for parallel computation

---

### **Phase 2: Simplify Ball State** (Refactor Existing)

**Files to Modify:**
- `engine/include/SimpleBallTracker.hpp`
- `engine/src/SimpleBallTracker.cpp`

**Changes:**

1. **Remove Complex Structures:**
   - Delete `ColorBasedPredictor` (replaced by trajectory)
   - Delete `KalmanFilter3D` (replaced by physics-based prediction)
   - Remove all fallback logic (Kalman glob, color blob near hand, etc.)

2. **Add New Structures:**
   - `BallState` enum
   - `TrajectoryPoint` struct
   - `BallTrajectory` struct
   - `TrajectoryVisualizationSettings` struct

3. **Simplify `SimpleBall`:**
   - Replace `kalman` and `color_predictor` with `trajectory`
   - Add `state` field
   - Remove `frames_without_yolo`, `state_change_counter`, etc.

---

### **Phase 3: Rewrite Tracking Logic** (Major Refactor)

**Files to Modify:**
- `engine/src/SimpleBallTracker.cpp`

**Functions to Rewrite:**

1. **`update()`** - Main loop
   - Remove euclidean matching system
   - Remove override detection
   - Remove fallback strategies
   - Replace with state-based dispatch

2. **New Functions:**
   - `updateHeldBall()` - Simple wrist tracking
   - `updateInFlightBall()` - Trajectory-based tracking
   - `initiateThrow()` - HELD → IN_FLIGHT transition
   - `initiateCatch()` - IN_FLIGHT → HELD transition
   - `addVerifiedPoint()` - Add point to trajectory
   - `updateTrajectoryConfidence()` - Confidence calculation

3. **Remove Functions:**
   - `isBallHeld()` - No longer needed (state-based)
   - `searchForColorBlob()` - Moved to GPU component
   - All Kalman-related logic

---

### **Phase 4: Visualization System** (New Feature)

**Files to Modify:**
- `engine/include/Engine.hpp`
- `engine/src/Engine.cpp` (or visualization module)

**New Functions:**
- `drawTrajectory()` - Render trajectory on frame
- `drawVerifiedPoints()` - Show confirmed points
- `drawSearchRadius()` - Show current search area
- `drawConfidenceIndicator()` - Show trajectory confidence

**Settings Integration:**
- Add trajectory visualization toggles to UI
- Save/load visualization settings

---

### **Phase 5: Settings & Configuration** (Update)

**Files to Modify:**
- `engine/include/SimpleBallTracker.hpp` (TrackingSettings)
- `ball_settings.json` (configuration file)

**New Settings:**
```cpp
struct TrackingSettings {
    // State transition thresholds
    float throw_distance_threshold = 0.20f;   // Min distance to detect throw
    float catch_distance_threshold = 0.15f;   // Max distance to detect catch
    int min_frames_for_transition = 2;        // Debouncing
    
    // Trajectory parameters
    float gravity = 9.81f;                    // m/s²
    float trajectory_time_step = 0.033f;      // 30fps
    float max_trajectory_time = 3.0f;         // Max 3 seconds
    
    // Search parameters
    float initial_search_radius = 0.30f;      // Wide search initially
    float min_search_radius = 0.10f;          // Tight search when confident
    float min_color_match_score = 0.50f;      // Color verification threshold
    
    // Confidence parameters
    int points_for_full_confidence = 5;       // 5 verified points = 100% confidence
    
    // Visualization
    TrajectoryVisualizationSettings viz_settings;
};
```

---

### **Phase 6: Testing & Validation** (Quality Assurance)

**Test Cases:**

1. **Single Ball Juggling:**
   - Verify THROW detection
   - Verify trajectory prediction accuracy
   - Verify CATCH detection
   - Verify smooth transitions

2. **Multi-Ball Juggling:**
   - Verify no identity swaps
   - Verify independent trajectories
   - Verify correct ball-hand assignments

3. **Edge Cases:**
   - Ball goes off-screen during flight
   - Hand occludes ball
   - Fast throws (high velocity)
   - Slow catches (low velocity)

4. **Performance:**
   - GPU utilization
   - Frame rate impact
   - Memory usage

---

## 📊 **PERFORMANCE EXPECTATIONS**

### **Computational Savings:**

**Removed Operations (per frame):**
- ❌ Euclidean color matching for all ball-detection pairs (~50-100 comparisons)
- ❌ Temporal consistency bonus calculations
- ❌ Kalman prediction bonus calculations
- ❌ Override detection search
- ❌ Multiple color blob searches (near hand, near Kalman, etc.)
- ❌ Kalman filter updates and predictions
- ❌ Color predictor velocity estimation

**New Operations (per frame):**
- ✅ GPU trajectory prediction (1 per ball, ~90 points)
- ✅ GPU closest-point search (1 per detection per ball)
- ✅ Simple distance checks for THROW/CATCH

**Expected Speedup:**
- **CPU load**: -40% (removed complex matching logic)
- **GPU load**: +20% (trajectory computation)
- **Overall FPS**: +15-25% (GPU is underutilized currently)

### **Tracking Quality:**

**Improvements:**
- ✅ No identity swaps (physics-based prediction)
- ✅ Smoother tracking (continuous trajectory)
- ✅ Better occlusion handling (trajectory continues)
- ✅ Clearer visual feedback (trajectory path)

**Trade-offs:**
- ⚠️ Requires good initial velocity estimate
- ⚠️ Sensitive to throw detection accuracy
- ⚠️ May struggle with very short throws

---

## 🎯 **SUCCESS CRITERIA**

1. **Functional:**
   - ✅ All balls tracked correctly in HELD state
   - ✅ All balls tracked correctly in IN_FLIGHT state
   - ✅ THROW events detected accurately
   - ✅ CATCH events detected accurately
   - ✅ No identity swaps between balls

2. **Performance:**
   - ✅ FPS improvement of 15%+
   - ✅ GPU utilization increased
   - ✅ CPU load decreased

3. **Usability:**
   - ✅ Trajectory visualization works
   - ✅ Settings are intuitive
   - ✅ Debugging is easier (clear state machine)

4. **Code Quality:**
   - ✅ Reduced code complexity (fewer lines)
   - ✅ Better separation of concerns
   - ✅ Easier to maintain and extend

---

## 📅 **ESTIMATED TIMELINE**

- **Phase 1** (GPU Trajectory Predictor): 2-3 days
- **Phase 2** (Simplify Ball State): 1 day
- **Phase 3** (Rewrite Tracking Logic): 3-4 days
- **Phase 4** (Visualization System): 1-2 days
- **Phase 5** (Settings & Configuration): 1 day
- **Phase 6** (Testing & Validation): 2-3 days

**Total: 10-14 days**

---

## 🔄 **MIGRATION STRATEGY**

1. **Keep old system running** during development
2. **Add feature flag** to switch between old/new systems
3. **Test new system** in parallel with old system
4. **Compare results** to validate accuracy
5. **Remove old system** once new system is proven

---

## 📚 **NEXT STEPS**

1. ✅ Review this architecture document
2. ⏳ Get approval for major changes
3. ⏳ Create detailed implementation plan for Phase 1
4. ⏳ Begin GPU trajectory predictor development
5. ⏳ Iteratively implement and test each phase

---

**End of Architecture Document**