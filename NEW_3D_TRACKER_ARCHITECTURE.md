
# New 3D Tracker Architecture Plan
**Date:** 2025-10-17  
**Status:** Architecture Design Phase  
**Tracking System:** `new_3d` - Physics-Based Kalman Filter Tracker

---

## Executive Summary

This document provides a complete architectural plan for implementing a new 3D tracking system called `new_3d` based on the provided specification. The system will be added as a **third tracking option** alongside the existing `depth_based` and `simple_2d` trackers, giving users the choice between three distinct tracking approaches.

### Key Design Decisions

1. **Third Option**: `new_3d` is a separate tracker, not a replacement
2. **YOLO Integration**: Uses existing YOLO models and detection pipeline
3. **Color Tracking**: Includes color-based identification like current system
4. **Settings Migration**: Provides migration from `depth_based` settings
5. **Physics-Based**: Uses Kalman filtering with gravity model for trajectory prediction

---

## Table of Contents

1. [System Overview](#1-system-overview)
2. [Data Structure Mapping](#2-data-structure-mapping)
3. [C++ Implementation Design](#3-c-implementation-design)
4. [UI Integration Design](#4-ui-integration-design)
5. [Settings Architecture](#5-settings-architecture)
6. [Visualization Requirements](#6-visualization-requirements)
7. [Implementation Phases](#7-implementation-phases)
8. [Testing Strategy](#8-testing-strategy)

---

## 1. System Overview

### 1.1 Tracking System Comparison

| Feature | depth_based (Current) | simple_2d | new_3d (New) |
|---------|----------------------|-----------|--------------|
| **Approach** | Trajectory + Color | 2D Pixel-based | Kalman Filter + Physics |
| **State Machine** | HELD/IN_FLIGHT | Simple 2D tracking | HELD/IN_AIR |
| **Physics Model** | Trajectory prediction | None | Kalman with gravity |
| **Hand Detection** | Pose-based | Pose-based | Pose-based |
| **Color Tracking** | Yes | Yes | Yes |
| **Depth Required** | Yes | No | Yes |
| **Complexity** | High | Low | Medium |

### 1.2 Core Philosophy

The `new_3d` tracker implements a **clean state machine** with physics-based prediction:

- **HELD State**: Ball position locked to hand, tracks hand movement
- **IN_AIR State**: Ball follows Kalman-predicted trajectory with gravity
- **State Transitions**: Based on distance thresholds and hand velocity
- **Association**: Hungarian algorithm for detection-to-track matching
- **Robustness**: Multi-frame confirmation for state changes

---

## 2. Data Structure Mapping

### 2.1 Specification → Codebase Mapping

| Specification Name | Codebase Name | Type | Notes |
|-------------------|---------------|------|-------|
| `Vector3` | `cv::Point3f` | struct | OpenCV 3D point |
| `BallState::IN_AIR` | `BallState::IN_FLIGHT` | enum | Rename for consistency with existing code |
| `BallState::HELD` | `BallState::HELD` | enum | Keep existing name |
| `Hand::NONE` | `-1` | int | Use existing convention |
| `Hand::LEFT` | `0` | int | Use existing convention |
| `Hand::RIGHT` | `1` | int | Use existing convention |
| `Detection3D` | `Detection` | struct | Extend existing with 3D position |
| `Pose3D` | `SimpleHand` | struct | Use existing hand structure |
| `KalmanFilter` | `cv::KalmanFilter` | class | Use OpenCV implementation |
| `TrackedBall` | `New3DBall` | struct | New structure for new_3d tracker |
| `JugglingTracker` | `New3DTracker` | class | New tracker class implementing `IBallTracker` |

### 2.2 New Data Structures

#### 2.2.1 New3DBall Structure

```cpp
struct New3DBall {
    // Identity
    long long id;                    // Unique permanent ID
    std::string color_name;          // "red", "green", "blue", etc.
    ColorProfile color_profile;      // Color matching profile
    
    // State
    BallState state;                 // IN_FLIGHT or HELD
    int associated_hand_id;          // -1=none, 0=left, 1=right
    
    // Physics (Kalman Filter)
    cv::KalmanFilter kf;             // 6-state [x,y,z,vx,vy,vz]
    cv::Point3f last_known_position; // Official position from previous frame
    cv::Point3f predicted_position;  // Kalman prediction for this frame
    
    // Tracking Quality
    int frames_since_seen;           // Counter for deletion
    int consecutive_frames_seen;     // Counter for confirmation
    bool color_locked;               // True after min_frames_for_color_lock
    
    // Visualization
    cv::Point2f pixel_pos;           // 2D position for display
    cv::Rect_<float> bbox;           // Bounding box
    float yolo_confidence;           // Detection confidence
    float color_match_score;         // Color matching score
    std::string tracking_reason;     // Debug info
    
    // Position history (optional for pattern analysis)
    std::vector<cv::Point3f> position_history;
    std::vector<uint64_t> timestamp_history;
};
```

#### 2.2.2 New3DTracker Settings

```cpp
struct New3DTrackerSettings {
    // Geometry & Distance (meters)
    float held_radius_m = 0.12f;                    // 12cm radius for "held" detection
    float association_max_distance_m = 0.50f;       // Max distance for detection matching
    
    // Physics & Dynamics
    float throw_velocity_threshold_mps = 0.50f;     // Min relative velocity for throw
    cv::Point3f gravity_mps2 = {0.0f, -9.81f, 0.0f}; // Gravity vector
    
    // Tracking Logic (frames)
    int max_frames_unseen = 30;                     // Delete after 30 frames
    int min_frames_for_new_track = 3;               // Confirm new track after 3 frames
    int min_frames_for_color_lock = 5;              // Lock color after 5 frames
    
    // Color Tracking
    bool use_color_tracking = true;                 // Enable color-based identification
    float color_match_threshold = 0.50f;            // Min color match score
    int color_sample_radius = 1;                    // Pixel radius for color sampling
    
    // YOLO Integration
    float ball_confidence_threshold = 0.25f;        // Min confidence for 'ball' class
    float ball_held_confidence_threshold = 0.25f;   // Min confidence for 'ball_held' class
    bool ignore_class = false;                      // Treat ball/ball_held same
    
    // Hand Velocity (for throw prediction)
    bool hand_velocity_enabled = true;              // Enable velocity-based throw detection
    float hand_velocity_threshold = 1.0f;           // Min hand speed (m/s) for enhanced detection
    
    // Visualization
    bool show_kalman_prediction = true;             // Show predicted position
    bool show_held_radius = true;                   // Show held detection radius
    bool show_association_lines = true;             // Show detection-to-track associations
};
```

---

## 3. C++ Implementation Design

### 3.1 Class Hierarchy

```
IBallTracker (interface)
    ├── SimpleBallTracker (existing - depth_based)
    ├── Simple2DBallTracker (existing - simple_2d)
    └── New3DTracker (NEW - new_3d)
```

### 3.2 New3DTracker Class Structure

**File:** `engine/include/New3DTracker.hpp`

```cpp
#pragma once

#include <opencv2/opencv.hpp>
#include <openvino/openvino.hpp>
#include <vector>
#include <string>
#include <memory>
#include "json.hpp"
#include "GpuHsvConverter.hpp"
#include "IBallTracker.hpp"

using json = nlohmann::json;

// Forward declarations
struct CameraIntrinsics;
struct ColorProfile;
struct Detection;
struct SimpleHand;
struct BallEvent;

// Ball state enum
enum class BallState {
    HELD,       // Ball is in hand
    IN_FLIGHT   // Ball is airborne (renamed from IN_AIR for consistency)
};

// New3DBall structure
struct New3DBall {
    // Identity
    long long id;
    std::string color_name;
    ColorProfile color_profile;
    
    // State
    BallState state;
    int associated_hand_id;  // -1=none, 0=left, 1=right
    
    // Physics
    cv::KalmanFilter kf;
    cv::Point3f last_known_position;
    cv::Point3f predicted_position;
    
    // Tracking quality
    int frames_since_seen;
    int consecutive_frames_seen;
    bool color_locked;
    
    // Visualization
    cv::Point2f pixel_pos;
    cv::Rect_<float> bbox;
    float yolo_confidence;
    float color_match_score;
    std::string tracking_reason;
    
    // History (optional)
    std::vector<cv::Point3f> position_history;
    std::vector<uint64_t> timestamp_history;
    
    New3DBall() : id(-1), state(BallState::HELD), associated_hand_id(-1),
                  frames_since_seen(0), consecutive_frames_seen(0),
                  color_locked(false), yolo_confidence(0.0f),
                  color_match_score(0.0f) {}
};

// Settings structure
struct New3DTrackerSettings {
    // Geometry & Distance (meters)
    float held_radius_m = 0.12f;
    float association_max_distance_m = 0.50f;
    
    // Physics & Dynamics
    float throw_velocity_threshold_mps = 0.50f;
    cv::Point3f gravity_mps2 = {0.0f, -9.81f, 0.0f};
    
    // Tracking Logic (frames)
    int max_frames_unseen = 30;
    int min_frames_for_new_track = 3;
    int min_frames_for_color_lock = 5;
    
    // Color Tracking
    bool use_color_tracking = true;
    float color_match_threshold = 0.50f;
    int color_sample_radius = 1;
    
    // YOLO Integration
    float ball_confidence_threshold = 0.25f;
    float ball_held_confidence_threshold = 0.25f;
    bool ignore_class = false;
    
    // Hand Velocity
    bool hand_velocity_enabled = true;
    float hand_velocity_threshold = 1.0f;
    
    // Visualization
    bool show_kalman_prediction = true;
    bool show_held_radius = true;
    bool show_association_lines = true;
};

// Pose3D structure (for storing previous frame pose)
struct Pose3D {
    cv::Point3f left_wrist_pos;
    cv::Point3f right_wrist_pos;
    bool is_left_wrist_valid;
    bool is_right_wrist_valid;
};

class New3DTracker : public IBallTracker {
public:
    New3DTracker(const std::string& ball_model_path,
                 const std::string& pose_model_path,
                 const std::string& device_name,
                 const std::string& settings_file = "new_3d_settings.json");
    ~New3DTracker() = default;
    
    // Main update function
    std::pair<std::vector<New3DBall>, std::vector<BallEvent>> update(
        const cv::Mat& color_frame,
        const cv::Mat& depth_frame,
        const CameraIntrinsics& intrinsics
    );
    
    // Settings management
    bool loadSettings() override;
    void saveSettings() override;
    bool updateSetting(const std::string& key, const std::string& value) override;
    
    // Color calibration
    bool calibrateColor(const std::string& color_name,
                       cv::Point click_point,
                       std::string& error_message) override;
    
    // Getters
    const std::vector<New3DBall>& getBalls() const { return tracked_balls_; }
    const std::vector<SimpleHand>& getHands() const { return hands_; }
    const std::vector<Detection>& getLastRawDetections() const { return last_raw_detections_; }
    New3DTrackerSettings& getTrackingSettings() { return settings_; }
    const std::vector<ColorProfile>& getColorProfiles() const { return color_profiles_; }
    std::vector<ColorProfile>& getColorProfiles() { return color_profiles_; }

private:
    // === STEP 1: PREDICTION ===
    void predictAllBalls(float dt);
    void predictHeldBall(New3DBall& ball, const SimpleHand& hand, float dt);
    void predictInAirBall(New3DBall& ball, float dt);
    
    // === STEP 2: ASSOCIATION ===
    struct MatchPair {
        New3DBall* ball;
        const Detection* detection;
        float distance;
    };
    
    struct AssociationResult {
        std::vector<MatchPair> matched_pairs;
        std::vector<New3DBall*> unmatched_balls;
        std::vector<const Detection*> unmatched_detections;
    };
    
    AssociationResult associateDetections(
        std::vector<New3DBall>& balls,
        const std::vector<Detection>& detections,
        float max_distance
    );
    
    // === STEP 3: UPDATE MATCHED ===
    void updateMatchedBalls(
        const std::vector<MatchPair>& matches,
        const std::vector<SimpleHand>& hands,
        const Pose3D& previous_pose,
        float dt,
        std::vector<BallEvent>& events
    );
    
    void handleHeldStateUpdate(
        New3DBall& ball,
        const Detection& detection,
        const std::vector<SimpleHand>& current_hands,
        const Pose3D& previous_pose,
        float dt,
        std::vector<BallEvent>& events
    );
    
    void handleInAirStateUpdate(
        New3DBall& ball,
        const Detection& detection,
        const std::vector<SimpleHand>& current_hands,
        std::vector<BallEvent>& events
    );
    
    // === STEP 4: HANDLE UNMATCHED BALLS ===
    void handleUnmatchedBalls(
        const std::vector<New3DBall*>& unmatched_balls
    );
    
    // === STEP 5: HANDLE UNMATCHED DETECTIONS ===
    void createNewTracks(
        const std::vector<const Detection*>& unmatched_detections,
        const cv::Mat& color_frame
    );
    
    // === STEP 6: FINALIZE ===
    void finalizeBallPositions(const std::vector<SimpleHand>& hands);
    
    // === HELPER METHODS ===
    cv::KalmanFilter createKalmanFilter(const cv::Point3f& initial_pos);
    cv::Point3f calculateHandVelocity(const SimpleHand& hand, const Pose3D& previous_pose, float dt);
    bool isHandAvailable(int hand_id, const std::vector<New3DBall>& balls);
    float matchColor(const Detection& det, const ColorProfile& profile, const cv::Mat& color_frame);
    std::string determineColor(const Detection& det, const cv::Mat& color_frame);
    Pose3D createPose3D(const std::vector<SimpleHand>& hands);
    
    // === YOLO DETECTION ===
    cv::Mat preprocess(const cv::Mat& frame, float& scale_x, float& scale_y);
    std::vector<Detection> runBallDetection(
        const cv::Mat& preprocessed,
        float scale_x, float scale_y,
        const cv::Mat& color_frame,
        const cv::Mat& depth_frame,
        const CameraIntrinsics& intrinsics
    );
    
    std::vector<SimpleHand> runPoseEstimation(
        const cv::Mat& preprocessed,
        float scale_x, float scale_y,
        const cv::Mat& color_frame,
        const cv::Mat& depth_frame,
        const CameraIntrinsics& intrinsics
    );
    
    // === VISUALIZATION ===
    void drawBall(cv::Mat& frame, const New3DBall& ball, const CameraIntrinsics& intrinsics);
    void drawHandThresholds(cv::Mat& frame, const std::vector<SimpleHand>& hands, 
                           const CameraIntrinsics& intrinsics) override;
    void drawAssociations(cv::Mat& frame, const std::vector<MatchPair>& matches,
                         const CameraIntrinsics& intrinsics);
    
    // === UTILITY ===
    float getDepthAtPoint(const cv::Mat& depth_frame, const cv::Point2f& point);
    cv::Point3f deprojectToWorld(const cv::Point2f& pixel, float depth,
                                const CameraIntrinsics& intrinsics);
    static cv::Point2f project_3d_to_2d(const cv::Point3f& world_pos, 
                                       const CameraIntrinsics& intrinsics);
    
    // === STATE ===
    std::vector<New3DBall> tracked_balls_;
    std::vector<SimpleHand> hands_;
    Pose3D previous_frame_pose_;
    std::vector<Detection> last_raw_detections_;
    std::vector<ColorProfile> color_profiles_;
    long long next_track_id_ = 0;
    
    // === SETTINGS ===
    New3DTrackerSettings settings_;
    std::string settings_file_;
    
    // === OPENVINO ===
    ov::Core core_;
    ov::CompiledModel ball_model_;
    ov::InferRequest ball_infer_;
    ov::CompiledModel pose_model_;
    ov::InferRequest pose_infer_;
    
    // === GPU ACCELERATION ===
    std::unique_ptr<GpuHsvConverter> gpu_hsv_converter_;
    
    // === TIMING ===
    std::chrono::steady_clock::time_point last_update_time_;
    
    // === MODEL PARAMETERS ===
    int input_width_ = 640;
    int input_height_ = 640;
    float nms_threshold_ = 0.5f;
    const int num_classes_ = 2;  // ball, ball_held
};
```

### 3.3 Key Algorithm: Main Update Loop

```cpp
std::pair<std::vector<New3DBall>, std::vector<BallEvent>> 
New3DTracker::update(const cv::Mat& color_frame,
                     const cv::Mat& depth_frame,
                     const CameraIntrinsics& intrinsics) {
    
    std::vector<BallEvent> events;
    
    // Calculate time delta
    auto current_time = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(current_time - last_update_time_).count();
    last_update_time_ = current_time;
    
    // Run YOLO detection
    float scale_x, scale_y;
    cv::Mat preprocessed = preprocess(color_frame, scale_x, scale_y);
    std::vector<Detection> detections = runBallDetection(
        preprocessed, scale_x, scale_y, color_frame, depth_frame, intrinsics
    );
    std::vector<SimpleHand> current_hands = runPoseEstimation(
        preprocessed, scale_x, scale_y, color_frame, depth_frame, intrinsics
    );
    
    // STEP 1: PREDICTION
    predictAllBalls(dt);
    
    // STEP 2: ASSOCIATION
    auto association = associateDetections(tracked_balls_, detections, 
                                          settings_.association_max_distance_m);
    
    // STEP 3: UPDATE MATCHED
    updateMatchedBalls(association.matched_pairs, current_hands, 
                      previous_frame_pose_, dt, events);
    
    // STEP 4: HANDLE UNMATCHED BALLS
    handleUnmatchedBalls(association.unmatched_balls);
    
    // STEP 5: HANDLE UNMATCHED DETECTIONS
    createNewTracks(association.unmatched_detections, color_frame);
    
    // STEP 6: FINALIZE
    finalizeBallPositions(current_hands);
    
    // Store current pose for next frame
    previous_frame_pose_ = createPose3D(current_hands);
    hands_ = current_hands;
    last_raw_detections_ = detections;
    
    return {tracked_balls_, events};
}
```

### 3.4 Kalman Filter Implementation

```cpp
cv::KalmanFilter New3DTracker::createKalmanFilter(const cv::Point3f& initial_pos) {
    // 6-state Kalman filter: [x, y, z, vx, vy, vz]
    cv::KalmanFilter kf(6, 3, 0);
    
    // State transition matrix (constant acceleration model)
    // x_new = x + vx*dt
    // vx_new = vx + ax*dt (where ax = gravity_x)
    kf.transitionMatrix = (cv::Mat_<float>(6, 6) <<
        1, 0, 0, 1, 0, 0,  // x = x + vx*dt
        0, 1, 0, 0, 1, 0,  // y = y + vy*dt
        0, 0, 1, 0, 0, 1,  // z = z + vz*dt
        0, 0, 0, 1, 0, 0,  // vx = vx
        0, 0, 0, 0, 1, 0,  // vy = vy
        0, 0, 0, 0, 0, 1   // vz = vz
    );
    
    // Measurement matrix (we measure position only)
    kf.measurementMatrix = (cv::Mat_<float>(3, 6) <<
        1, 0, 0, 0, 0, 0,
        0, 1, 0, 0, 0, 0,
        0, 0, 1, 0, 0, 0
    );
    
    // Process noise covariance
    cv::setIdentity(kf.processNoiseCov, cv::Scalar::all(1e-2));
    
    // Measurement noise covariance
    cv::setIdentity(kf.measurementNoiseCov, cv::Scalar::all(1e-1));
    
    // Error covariance
    cv::setIdentity(kf.errorCovPost, cv::Scalar::all(1));
    
    // Initialize state
    kf.statePost.at<float>(0) = initial_pos.x;
    kf.statePost.at<float>(1) = initial_pos.y;
    kf.statePost.at<float>(2) = initial_pos.z;
    kf.statePost.at<float>(3) = 0;  // vx
    kf.statePost.at<float>(4) = 0;  // vy
    kf.statePost.at<float>(5) = 0;  // vz
    
    return kf;
}

void New3DTracker::predictInAirBall(New3DBall& ball, float dt) {
    // Update transition matrix with dt
    ball.kf.transitionMatrix.at<float>(0, 3) = dt;
    ball.kf.transitionMatrix.at<float>(1, 4) = dt;
    ball.kf.transitionMatrix.at<float>(2, 5) = dt;
    
    // Apply gravity to velocity
    cv::Mat control = (cv::Mat_<float>(6, 1) <<
        0,
        0,
        0,
        settings_.gravity_mps2.x * dt,
        settings_.gravity_mps2.y * dt,
        settings_.gravity_mps2.z * dt
    );
    
    // Predict
    cv::Mat prediction = ball.kf.predict();
    prediction += control;
    
    // Store predicted position
    ball.predicted_position = cv::Point3f(
        prediction.at<float>(0),
        prediction.at<float>(1),
        prediction.at<float>(2)
    );
}
```

---

## 4. UI Integration Design

### 4.1 Tracking System Dropdown

**File:** [`hub/components/ui_settings_common.py`](hub/components/ui_settings_common.py:130)

**Changes:**
```python
# Line 130-133
self.parent.tracking_system_combo = QComboBox()
self.parent.tracking_system_combo.addItem("Depth-Based 3D (Legacy)", "depth_based")
self.parent.tracking_system_combo.addItem("Simple 2D", "simple_2d")
self.parent.tracking_system_combo.addItem("New 3D Kalman ⭐", "new_3d")  # NEW
self.parent.tracking_system_combo.currentIndexChanged.connect(self.parent.on_tracking_system_changed)
```

### 4.2 New Settings File

**File:** `hub/components/ui_settings_new3d.py` (NEW)

```python
"""
New 3D Tracker Settings Sections for JuggleHub UI.
Contains settings sections that are ONLY visible when new_3d tracker is selected.
"""

from PyQt6.QtWidgets import (QLabel, QPushButton, QGridLayout, QVBoxLayout, 
                              QGroupBox, QHBoxLayout)
from .ui_widgets import CollapsibleGroupBox
import juggler_pb2


class New3DSettingsSections:
    """New 3D tracker-specific settings sections."""
    
    def __init__(self, parent_widget, udp_client, zmq_client):
        self.parent = parent_widget
        self.udp_client = udp_client
        self.zmq_client = zmq_client
    
    def create_physics_section(self):
        """Create the Physics & Kalman Filter settings section"""
        section = CollapsibleGroupBox("⚛️ Physics & Kalman Filter", collapsed=False)
        layout = QGridLayout()
        section.get_content_layout().addLayout(layout)
        
        row = 0
        
        # Info label
        info_label = QLabel("ℹ️ Configure physics parameters for Kalman filter prediction")
        info_label.setStyleSheet("color: #aaaaaa; font-size: 10px;")
        info_label.setWordWrap(True)
        layout.addWidget(info_label, row, 0, 1, 3)
        row += 1
        
        # Held Radius
        self.parent.new3d_held_radius_slider, self.parent.new3d_held_radius_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="Held Radius (cm)",
            tooltip_text="Radius around wrist where ball is considered 'held'.\n"
                         "Range: 5-30cm. Default: 12cm.\n"
                         "Smaller = stricter held detection, Larger = more forgiving.",
            range_min=5,
            range_max=30,
            initial_value=12,
            update_func=lambda v: self.parent.update_setting('held_radius_m', v / 100.0),
            is_float=False
        )
        row += 1
        
        # Throw Velocity Threshold
        self.parent.new3d_throw_velocity_slider, self.parent.new3d_throw_velocity_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="Throw Velocity Threshold (m/s)",
            tooltip_text="Minimum relative velocity between ball and hand to trigger throw.\n"
                         "Range: 0.1-3.0 m/s. Default: 0.5 m/s.\n"
                         "Lower = more sensitive, Higher = requires faster throws.",
            range_min=10,
            range_max=300,
            initial_value=50,
            update_func=lambda v: self.parent.update_setting('throw_velocity_threshold_mps', v / 100.0),
            is_float=True
        )
        row += 1
        
        # Gravity
        self.parent.new3d_gravity_slider, self.parent.new3d_gravity_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="Gravity (m/s²)",
            tooltip_text="Gravitational acceleration for trajectory prediction.\n"
                         "Range: 5.0-15.0 m/s². Default: 9.81 m/s² (Earth gravity).\n"
                         "Adjust if calibration seems off.",
            range_min=50,
            range_max=150,
            initial_value=98,
            update_func=lambda v: self.parent.update_setting('gravity_y', -v / 10.0),
            is_float=True
        )
        row += 1
        
        return section
    
    def create_tracking_logic_section(self):
        """Create the Tracking Logic settings section"""
        section = CollapsibleGroupBox("🎯 Tracking Logic", collapsed=False)
        layout = QGridLayout()
        section.get_content_layout().addLayout(layout)
        
        row = 0
        
        # Info label
        info_label = QLabel("ℹ️ Configure tracking behavior and confirmation thresholds")
        info_label.setStyleSheet("color: #aaaaaa; font-size: 10px;")
        info_label.setWordWrap(True)
        layout.addWidget(info_label, row, 0, 1, 3)
        row += 1
        
        # Max Frames Unseen
        self.parent.new3d_max_frames_unseen_slider, self.parent.new3d_max_frames_unseen_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,
            label_text="Max Frames Unseen",
            tooltip_text="Maximum frames without detection before deleting track.\n"
                         "Range: 10-60 frames. Default: 30 frames (~1 second at 30fps).\n"
                         "Higher = more persistent tracking, Lower = faster cleanup.",
            range_min=10,
            range_max=60,
            initial_value=30,
            update_func=lambda v: self.parent.update_setting('max_frames_unseen', v),
            is_float=False
        )
        row += 1
        
        # Min Frames for New Track
        self.parent.new3d_min_frames_new_track_slider, self.parent.new3d_min_frames_new_track_label = self.parent._create_slider_widget(
            parent_layout=layout,
            row=row,