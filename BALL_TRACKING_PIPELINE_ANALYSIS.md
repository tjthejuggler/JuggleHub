# Complete Ball Tracking Pipeline Analysis
**Date:** 2025-10-03  
**Purpose:** Deep analysis of the entire ball tracking workflow to identify flickering and tracking issues

---

## Executive Summary

This document provides a comprehensive frame-by-frame analysis of the ball tracking pipeline, from YOLO detection through color tracking. The system uses a multi-layered approach with several potential points of failure that could cause flickering and poor tracking.

---

## Complete Frame Processing Workflow

### Frame N Processing Steps (60 FPS target)

#### **STEP 1: Frame Acquisition** (`Engine.cpp:97-120`)
```
1. Wait for RealSense frames (1 second timeout)
2. Align depth to color frame
3. Extract color_frame (CV_8UC3) and depth_frame (CV_16UC1)
4. Clone frames for processing: last_color_frame_, last_depth_frame_
```

**Potential Issues:**
- Frame timeout could cause dropped frames
- Alignment issues between color and depth

---

#### **STEP 2: Kalman Prediction** (`DNNTracker.cpp:91-103`)
```
For each logical_ball_tracker (NUM_BALLS = 3):
  If status != LOST:
    If is_in_freefall:
      kf.predict_ball(dt)  // Applies gravity: y += vy*dt + 0.5*g*dt²
    Else:
      kf.predict(dt)       // Constant velocity: pos += vel*dt

For each logical_hand_tracker (NUM_HANDS = 2):
  If status != LOST:
    kf.predict(dt)         // Constant velocity
```

**Why Predict Before Detection?**
This is the **standard Kalman filter predict-update cycle**:
1. **PREDICT**: Use Frame N-1 state to estimate where objects SHOULD be at Frame N
2. **DETECT**: Get noisy measurements from YOLO (Step 3)
3. **UPDATE**: Fuse prediction with measurement for optimal estimate (Step 5)

**Benefits:**
- Prediction provides search guidance for association (Steps 5-6)
- Handles missing detections gracefully
- Incorporates velocity for temporal consistency
- Enables tracking through brief occlusions

**Kalman Filter Details:**
- State vector: [x, y, z, vx, vy, vz]
- Process noise Q: Identity * 0.01, position uncertainty * 10
- Measurement noise R: Identity * 5.0
- Gravity: 9.81 m/s² applied to Y-axis when is_in_freefall=true

**Potential Issues:**
- ⚠️ **CRITICAL**: Gravity is ALWAYS applied when `is_in_freefall=true`, even during brief occlusions
- Prediction diverges quickly if ball is held but still marked as in_freefall
- High measurement noise (R=5.0) may cause lag in position updates
- ⚠️ **ByteTrack doesn't use predictions** - it only uses IoU on raw detections, wasting this valuable information

---

#### **STEP 3: YOLO Ball Detection** (`DNNTracker.cpp:106-113`)
```
1. Preprocess frame:
   - Resize to 640x640
   - Convert to float32 [0-1]
   - Create blob (NCHW format)

2. Run OpenVINO inference on ball model:
   - Input: [1, 3, 640, 640]
   - Output: [1, num_channels, num_detections]
   - num_channels = 4 (bbox) + num_classes

3. Postprocess detections:
   - Parse YOLO output (cx, cy, w, h, class_scores)
   - Apply confidence_threshold (default: varies)
   - Apply NMS with nms_threshold
   - Deproject 2D center to 3D using depth
   - Store as raw_detections_ (Detection struct)
```

**Detection Thresholds:**
- `confidence_threshold_`: Minimum confidence for detection
- `nms_threshold_`: IoU threshold for Non-Maximum Suppression (NMS)
- `raw_detection_threshold`: 0.1 (for storing all detections)

**What is NMS (Non-Maximum Suppression)?**
NMS is a post-processing step that removes duplicate detections:
1. YOLO often detects the same object multiple times with overlapping bounding boxes
2. NMS keeps only the detection with highest confidence and suppresses overlapping ones
3. "Overlap" is measured using IoU (Intersection over Union)
4. If two boxes have IoU > `nms_threshold`, the lower-confidence one is removed

**Example:**
```
Ball detected 3 times:
  Box A: confidence=0.9, position=(100,100)
  Box B: confidence=0.7, position=(102,98)  <- overlaps with A
  Box C: confidence=0.8, position=(200,200)

After NMS with threshold=0.5:
  Box A: kept (highest confidence in cluster)
  Box B: suppressed (overlaps with A)
  Box C: kept (no overlap with others)
```

**Potential Issues:**
- ⚠️ **FLICKERING SOURCE**: NMS can suppress valid detections when balls are close together
  - If two balls are juggled close to each other, NMS may suppress one as a "duplicate"
  - This causes intermittent detection loss, leading to flickering
- Depth lookup at single pixel (center) - noisy depth can cause 3D jitter
- No temporal filtering on raw detections

---

#### **STEP 4: ByteTrack Association** (`DNNTracker.cpp:116-131`)
```
1. ByteTrack.update(detections_for_bytetrack):
   - Matches detections to existing tracks using IoU
   - Creates new tracks for unmatched detections
   - Maintains track IDs across frames
   - Returns active tracks with track_id

2. Separate tracks by confidence:
   - High-conf tracks: score >= high_thresh_ (default varies)
   - Low-conf tracks: score < high_thresh_
```

**ByteTrack Parameters:**
- `track_buffer_`: Frames to keep lost tracks
- `track_thresh_`: Minimum score to start tracking
- `high_thresh_`: Threshold for high-confidence tracks
- `match_thresh_`: IoU threshold for matching

**Potential Issues:**
- ⚠️ **MAJOR FLICKERING SOURCE**: ByteTrack uses IoU matching, which FAILS for fast-moving balls
- IoU between consecutive frames can be near-zero for fast motion
- Track IDs can change frequently, causing logical tracker confusion
- No 3D distance consideration in ByteTrack matching

---

#### **STEP 5: Persistent Tracker Association** (`DNNTracker.cpp:133-205`)
```
1. Mark all TRACKED trackers as PREDICTED
2. Increment frames_since_seen for all non-LOST trackers

3. For each ByteTrack track:
   a. Find best matching raw detection (by IoU)
   b. Validate depth (0.2m < z < 2.0m)
   
   c. Try to match to persistent tracker:
      - First: Match by last_seen_bytetrack_id (ID continuity)
      - Second: Match by 3D distance to PREDICTED trackers (< 0.5m)
      - Third: Assign to first LOST tracker
   
   d. If matched:
      - kf.update(measurement) with 3D position
      - status = TRACKED
      - frames_since_seen = 0
      - last_seen_bytetrack_id = current_track_id
```

**Potential Issues:**
- ⚠️ **CRITICAL FLICKERING SOURCE**: ByteTrack ID changes cause tracker re-association
- 0.5m distance threshold may be too large, causing wrong associations
- Sequential matching (not global optimization) can cause "first ball claims all" problem
- No color information used in association

---

#### **STEP 6: 3D Distance Fallback Matching** (`DNNTracker.cpp:207-271`)
```
This is a CRITICAL recovery mechanism added to handle ByteTrack failures:

1. Find unmatched detections (not matched to any ByteTrack track)
2. For each unmatched detection:
   - Try to match to PREDICTED ball trackers using 3D distance
   - Threshold: MAX_3D_DISTANCE = 0.30m (30cm)
   
3. If match found:
   - Manually update Kalman filter
   - Set status = TRACKED
   - Reset frames_since_seen
```

**Potential Issues:**
- ⚠️ This is a BAND-AID for ByteTrack's IoU matching failure
- 30cm threshold may still miss fast-moving balls
- Runs AFTER ByteTrack, so already-matched detections are excluded

---

#### **STEP 7: Throw/Catch Detection** (`DNNTracker.cpp:273-275`, `ThrowCatchDetector.cpp`)
```
For each ball-hand pair:
  1. Update velocity history (last 5 frames)
  2. Find matching detection for ML classification
  3. Evaluate catch evidence if ball is IN_FLIGHT:
     - ML confidence (class_id == 1 for ball_held)
     - Proximity score (distance < catch_distance)
     - Kinematic score (velocity drop)
     - Relative velocity score
     - Weighted total: ml_weight + proximity_weight + kinematic_weight + relative_velocity_weight
  
  4. Evaluate throw evidence if ball is HELD:
     - ML confidence (class_id == 0 for ball in flight)
     - Proximity score (distance > throw_distance)
     - Kinematic score (velocity increase)
     - Relative velocity score
  
  5. State transitions:
     - IN_FLIGHT → TRANSITIONING → HELD (catch)
     - HELD → TRANSITIONING → IN_FLIGHT (throw)
     - Requires min_frames_for_event in TRANSITIONING
```

**Default Config:**
- `catch_threshold`: 0.6
- `throw_threshold`: 0.6
- `catch_distance`: 0.15m
- `throw_distance`: 0.20m
- `min_frames_for_event`: 3 frames

**Potential Issues:**
- State transitions require multiple frames, causing lag
- ML classification may be unreliable during fast motion
- Transitioning state can cause is_in_freefall confusion

---

#### **STEP 8: Legacy Occlusion Management** (`DNNTracker.cpp:278-283`)
```
manage_hand_tracks():
  - Assigns left/right based on x-coordinate
  
manage_ball_occlusion():
  - CATCH: If PREDICTED ball within 0.15m of TRACKED hand → OCCLUDED
  - THROW: If TRACKED ball > 0.20m from parent hand → IN_FLIGHT
  - Snaps ball position/velocity to hand when caught
```

**Potential Issues:**
- ⚠️ **REDUNDANT**: Overlaps with ThrowCatchDetector
- May conflict with throw/catch detector state changes
- Simple distance thresholds without temporal filtering

---

#### **STEP 9: Pose Estimation (Optional)** (`DNNTracker.cpp:307-309`)
```
If pose_model_enabled:
  1. Run YOLO-Pose inference
  2. Extract 17 keypoints per person (COCO format)
  3. Deproject wrist keypoints (9=left, 10=right) to 3D
  4. Create TrackedHand objects with all keypoints
```

**Potential Issues:**
- Pose model may not run every frame (performance)
- Wrist position may differ from hand detection position
- No fusion between pose-based and detection-based hand tracking

---

#### **STEP 10: Color Tracking** (`DNNTracker.cpp:311-324`, `ColorTracker.cpp`)
```
ColorTracker.update():
  
  STEP 10.1: Kalman Prediction for Color Trackers
    For each active color ball:
      kf.predict_ball(dt)  // With gravity
      Store predicted_world_pos for search guidance
  
  STEP 10.2: Global Assignment (Inactive → Active)
    a. Build list of inactive balls (frames_since_deactivated >= 10)
    b. Build list of ByteTrack ball detections
    c. Create scoring matrix [ball][detection]:
       - Try previous color first (if enabled)
       - Try all other enabled colors
       - Apply 1.5x bonus for color consistency
    d. Greedy assignment:
       - Find best (ball, detection) pair
       - Check for location deduplication (< 50 pixels)
       - Activate ball with matched color
       - Initialize Kalman filter
  
  STEP 10.3: Update Active Trackers
    For each active ball:
      a. Check if color profile is disabled → deactivate
      
      b. Check wrist association (< WRIST_ASSOCIATION_DISTANCE):
         - If near wrist: findClosestColorBlob(wrist_pos, WRIST_SEARCH_RADIUS)
         - If blob found: track blob
         - Else: track wrist position directly
      
      c. If not near wrist, check ByteTrack detections:
         - Find detection near last position (< 100 pixels)
         - Must match color profile (confidence > 0.10)
      
      d. If still not found, use color blob search:
         - Project predicted 3D position to 2D
         - findLargestColorBlob(predicted_pos, WRIST_SEARCH_RADIUS)
      
      e. Update Kalman filter with measurement
      f. Use Kalman-filtered position as final position
      
      g. If not found: frames_since_seen++
      h. If frames_since_seen > MAX_FRAMES_LOST (30): deactivate
  
  STEP 10.4: Deduplication
    For each pair of active balls:
      If distance < 0.10m (3D) OR < 50 pixels (2D):
        Deactivate ball with lower color_match_confidence
```

**Color Tracking Parameters:**
- `WRIST_ASSOCIATION_DISTANCE`: 0.15m
- `WRIST_SEARCH_RADIUS`: 100 pixels
- `MAX_FRAMES_LOST`: 30 frames
- `MIN_BLOB_AREA`: varies per profile

**Potential Issues:**
- ⚠️ **MAJOR COMPLEXITY**: Color tracking runs AFTER ByteTrack, creating dual tracking system
- Color Kalman filters are SEPARATE from persistent tracker Kalman filters
- Wrist association can "snap" ball position to wrist, causing jumps
- Color blob detection is sensitive to lighting changes
- Deduplication happens AFTER tracking, wasting computation
- 10-frame cooldown prevents rapid reactivation

---

#### **STEP 11: Compile Final Results** (`DNNTracker.cpp:286-303`)
```
For each persistent tracker (balls + hands):
  If status != LOST:
    tracker.update_from_kf()  // Extract position from Kalman state
    Create TrackedObject with:
      - box_2d (last known bounding box)
      - world_pos (from Kalman filter)
      - bytetrack_id
      - class_name
      - status (TRACKED/PREDICTED/OCCLUDED)
      - logical_id
```

---

#### **STEP 12: Protobuf Serialization** (`Engine.cpp:249-308`)
```
For each TrackedObject:
  If world_pos.z > 0 AND status != OCCLUDED:
    Add to frame_data.balls or frame_data.hands
    Include:
      - 3D position
      - 2D bounding box
      - Projected 2D position (from 3D)
      - Status
      - Logical ID

For each ColorTrackedBall:
  If is_active:
    Add to frame_data.color_tracked_balls
    Include:
      - logical_id
      - color_name
      - pixel_pos (2D)
      - world_pos (3D)
      - associated_wrist_id
      - frames_since_seen
```

---

## Critical Flickering Sources Identified

### 🔴 **CRITICAL ISSUE #1: ByteTrack IoU Matching Failure**
**Location:** `DNNTracker.cpp:116`  
**Problem:** ByteTrack uses IoU (Intersection over Union) to match detections across frames. For fast-moving balls, IoU between consecutive frames approaches ZERO, causing:
- Track ID changes every few frames
- Persistent trackers lose association
- Balls flip between TRACKED and PREDICTED states

**Evidence:**
```cpp
// ByteTrack internally uses IoU matching
std::vector<std::shared_ptr<byte_track::STrack>> byte_tracks = tracker->update(detections_for_bytetrack);
```

**Impact:** HIGH - This is likely the PRIMARY cause of flickering

---

### 🔴 **CRITICAL ISSUE #2: Dual Kalman Filter System**
**Location:** `ColorTracker.cpp:48-59`, `DNNTracker.cpp:91-103`  
**Problem:** There are TWO separate Kalman filter systems:
1. Persistent tracker Kalman filters (in DNNTracker)
2. Color tracker Kalman filters (in ColorTracker)

These can diverge and cause position jumps when switching between systems.

**Impact:** HIGH - Causes position discontinuities

---

### 🔴 **CRITICAL ISSUE #3: Wrist Position Snapping**
**Location:** `ColorTracker.cpp:342-355`  
**Problem:** When a ball is associated with a wrist but no color blob is visible, the system snaps the ball position directly to the wrist:
```cpp
// No color visible - fall back to wrist position
ball.pixel_pos = wrist_2d;
ball.kf.update(measurement); // Updates with wrist position
```

**Impact:** MEDIUM-HIGH - Causes sudden position jumps

---

### 🟡 **MAJOR ISSUE #4: Sequential Association (Not Global)**
**Location:** `DNNTracker.cpp:139-204`  
**Problem:** Persistent tracker association is sequential, not globally optimized:
```cpp
for (const auto& b_track : byte_tracks) {
    // First tracker to match claims this detection
    if (best_match_tracker) {
        // Assign and continue
    }
}
```

**Impact:** MEDIUM - Can cause wrong associations when multiple balls are close

---

### 🟡 **MAJOR ISSUE #5: Gravity Always Applied During Freefall**
**Location:** `KalmanFilter3D.cpp:53-79`  
**Problem:** When `is_in_freefall=true`, gravity is ALWAYS applied, even during brief occlusions or tracking losses. This causes predicted positions to diverge rapidly.

**Impact:** MEDIUM - Causes prediction errors during occlusions

---

### 🟡 **MAJOR ISSUE #6: Single-Pixel Depth Lookup**
**Location:** `DNNTracker.cpp:598-601`  
**Problem:** 3D position is calculated from depth at a single pixel (bbox center):
```cpp
uint16_t depth_value_mm = depth_frame.at<uint16_t>(center_pixel.y, center_pixel.x);
```
Depth sensors are noisy, causing 3D position jitter.

**Impact:** MEDIUM - Causes 3D position noise

---

### 🟡 **MAJOR ISSUE #7: Color Tracking Runs After ByteTrack**
**Location:** `DNNTracker.cpp:311-324`  
**Problem:** Color tracking is a separate system that runs AFTER ByteTrack association. This creates:
- Redundant tracking logic
- Potential conflicts between systems
- Wasted computation on duplicate tracking

**Impact:** MEDIUM - Architectural issue causing complexity

---

### 🟠 **MODERATE ISSUE #8: NMS Suppression**
**Location:** `DNNTracker.cpp:607`  
**Problem:** Non-Maximum Suppression can suppress valid detections when balls are close together:
```cpp
cv::dnn::NMSBoxes(boxes, confidences, confidence_threshold_, nms_threshold_, nms_indices);
```

**Impact:** MEDIUM - Can cause missed detections

---

### 🟠 **MODERATE ISSUE #9: State Transition Lag**
**Location:** `ThrowCatchDetector.cpp:67-98`  
**Problem:** State transitions require multiple frames in TRANSITIONING state:
```cpp
if (meetsTemporalRequirement(ball, config_.min_frames_for_event)) {
    // Confirm state change
}
```
This causes 3-frame lag (at 60 FPS = 50ms delay).

**Impact:** LOW-MEDIUM - Causes delayed state changes

---

### 🟠 **MODERATE ISSUE #10: Redundant Occlusion Logic**
**Location:** `DNNTracker.cpp:352-421`  
**Problem:** `manage_ball_occlusion()` overlaps with `ThrowCatchDetector`, potentially causing conflicting state changes.

**Impact:** LOW-MEDIUM - Can cause state confusion

---

## Recommended Fixes (Priority Order)

### 🔥 **PRIORITY 1: Replace ByteTrack IoU with 3D Distance Matching**
**Impact:** Will eliminate primary flickering source

**Implementation:**
1. Replace ByteTrack's IoU matching with 3D distance-based matching
2. Use Kalman predictions to guide association
3. Implement Hungarian algorithm for global optimization
4. Consider velocity matching in addition to position

---

### 🔥 **PRIORITY 2: Unify Kalman Filter Systems**
**Impact:** Will eliminate position discontinuities

**Implementation:**
1. Remove separate Kalman filters from ColorTracker
2. Use persistent tracker Kalman filters as single source of truth
3. Color tracking should only provide measurements, not maintain state

---

### 🔥 **PRIORITY 3: Implement Depth Filtering**
**Impact:** Will reduce 3D position noise

**Implementation:**
1. Sample depth in a small region (e.g., 5x5 pixels) around center
2. Use median or weighted average
3. Apply temporal filtering (e.g., exponential moving average)

---

### 🔥 **PRIORITY 4: Add Temporal Smoothing to Wrist Association**
**Impact:** Will prevent position snapping

**Implementation:**
1. Use gradual transition when associating with wrist
2. Blend between color blob position and wrist position
3. Add hysteresis to wrist association/disassociation

---

### ⚡ **PRIORITY 5: Implement Global Association**
**Impact:** Will prevent wrong associations

**Implementation:**
1. Build cost matrix for all (tracker, detection) pairs
2. Use Hungarian algorithm for optimal assignment
3. Include color information in cost function

---

## Performance Metrics to Monitor

1. **Track ID Stability**: How often ByteTrack IDs change
2. **Association Success Rate**: % of frames where trackers are TRACKED vs PREDICTED
3. **Position Jitter**: Standard deviation of position changes between frames
4. **State Transition Frequency**: How often balls change state
5. **3D Distance Matching Success**: % of balls recovered by fallback matching

---

## Conclusion

The ball tracking pipeline has **multiple layers of complexity** with several **redundant systems** (ByteTrack + Color Tracking, ThrowCatchDetector + manage_ball_occlusion). The primary flickering source is **ByteTrack's IoU matching failure** for fast-moving balls, compounded by **dual Kalman filter systems** and **wrist position snapping**.

The 3D distance fallback matching (Step 6) is already attempting to compensate for ByteTrack's failures, which indicates the core issue is well-understood but not properly addressed at the root cause.

**Recommended approach:** Focus on Priority 1-3 fixes first, as these address the root causes rather than symptoms.