# SimpleBallTracker Implementation Plan

**Created:** 2025-10-04  
**For:** Code Mode Implementation

## Overview

This document provides a step-by-step implementation plan for replacing the complex DNNTracker system with the simplified SimpleBallTracker. This plan is designed to be executed by Code mode.

---

## Implementation Phases

### Phase 1: Create SimpleBallTracker Header
**File:** `engine/include/SimpleBallTracker.hpp`

**Tasks:**
1. Create header file with include guards
2. Define `SimpleBall` struct with all required fields
3. Define `SimpleHand` struct
4. Define `BallEvent` struct for throw/catch events
5. Define `ColorProfile` struct (simplified from ColorTracker)
6. Declare `SimpleBallTracker` class with public interface
7. Declare private helper methods

**Key Methods:**
```cpp
class SimpleBallTracker {
public:
    SimpleBallTracker(const std::string& settings_file);
    
    // Main update function
    std::pair<std::vector<SimpleBall>, std::vector<BallEvent>> update(
        const cv::Mat& color_frame,
        const cv::Mat& depth_frame,
        const CameraIntrinsics& intrinsics,
        const std::vector<Detection>& yolo_detections,
        const std::vector<SimpleHand>& hands
    );
    
    // Settings management
    bool loadSettings();
    void saveSettings();
    bool updateSetting(const std::string& key, const std::string& value);
    
    // Color calibration
    bool calibrateColor(const std::string& color_name, const cv::Point& click_point,
                       const cv::Mat& color_frame, const std::vector<Detection>& detections,
                       std::string& error_message);

private:
    // Color matching
    float matchColor(const Detection& det, const ColorProfile& profile, const cv::Mat& hsv_frame);
    Detection* findBestColorMatch(const std::vector<Detection>& detections, 
                                  const ColorProfile& profile, const cv::Mat& hsv_frame);
    
    // State detection
    bool isBallHeld(SimpleBall& ball, const std::vector<SimpleHand>& hands);
    
    // Fallback tracking
    cv::Point2f searchForColorBlob(const cv::Mat& hsv_frame, const ColorProfile& profile,
                                   const cv::Point2f& search_center, int radius);
    
    // Utility
    float getDepthAtPoint(const cv::Mat& depth_frame, const cv::Point2f& point);
    cv::Point3f deprojectToWorld(const cv::Point2f& pixel, float depth, 
                                const CameraIntrinsics& intrinsics);
};
```

**Estimated Lines:** ~120 lines

---

### Phase 2: Implement SimpleBallTracker Core
**File:** `engine/src/SimpleBallTracker.cpp`

**Tasks:**
1. Implement constructor and destructor
2. Implement `loadSettings()` - read color profiles from JSON
3. Implement `saveSettings()` - write color profiles to JSON
4. Implement utility functions (depth, deprojection)

**Key Implementation Details:**
- Load color profiles from `ball_settings.json`
- Initialize Kalman filters for each ball
- Set up default parameters

**Estimated Lines:** ~100 lines

---

### Phase 3: Implement Color Matching
**File:** `engine/src/SimpleBallTracker.cpp`

**Tasks:**
1. Implement `matchColor()` - calculate color match score
2. Implement `findBestColorMatch()` - find best detection for a color
3. Implement `searchForColorBlob()` - fallback color blob search

**Algorithm Details:**

```cpp
float SimpleBallTracker::matchColor(const Detection& det, const ColorProfile& profile, 
                                   const cv::Mat& hsv_frame) {
    // Get detection center
    cv::Point2f center(det.box.x + det.box.width / 2.0f,
                      det.box.y + det.box.height / 2.0f);
    
    // Sample 7x7 region around center
    const int sample_radius = 7;
    int match_count = 0;
    int total_count = 0;
    
    for (int dy = -sample_radius; dy <= sample_radius; dy++) {
        for (int dx = -sample_radius; dx <= sample_radius; dx++) {
            int x = center.x + dx;
            int y = center.y + dy;
            
            if (x >= 0 && x < hsv_frame.cols && y >= 0 && y < hsv_frame.rows) {
                cv::Vec3b hsv = hsv_frame.at<cv::Vec3b>(y, x);
                
                // Check if within color range
                bool matches = (hsv[0] >= profile.min_hsv[0] && hsv[0] <= profile.max_hsv[0] &&
                               hsv[1] >= profile.min_hsv[1] && hsv[1] <= profile.max_hsv[1] &&
                               hsv[2] >= profile.min_hsv[2] && hsv[2] <= profile.max_hsv[2]);
                
                if (matches) match_count++;
                total_count++;
            }
        }
    }
    
    return total_count > 0 ? (float)match_count / total_count : 0.0f;
}
```

**Estimated Lines:** ~150 lines

---

### Phase 4: Implement Main Update Loop
**File:** `engine/src/SimpleBallTracker.cpp`

**Tasks:**
1. Implement main `update()` function
2. Process YOLO detections
3. Match detections to balls by color
4. Update ball positions
5. Handle balls without YOLO detections (Kalman/color blob fallback)

**Algorithm Flow:**
```cpp
std::pair<std::vector<SimpleBall>, std::vector<BallEvent>> 
SimpleBallTracker::update(...) {
    // Convert to HSV once
    cv::Mat hsv_frame;
    cv::cvtColor(color_frame, hsv_frame, cv::COLOR_BGR2HSV);
    
    // Track which detections are used
    std::set<int> used_detections;
    
    // For each enabled color profile
    for (size_t i = 0; i < color_profiles_.size(); i++) {
        if (!color_profiles_[i].enabled) continue;
        
        SimpleBall& ball = balls_[i];
        ball.color_name = color_profiles_[i].name;
        
        // Find best matching detection
        Detection* best_det = findBestColorMatch(yolo_detections, 
                                                color_profiles_[i], 
                                                hsv_frame);
        
        if (best_det && used_detections.find(best_det->index) == used_detections.end()) {
            // Update from YOLO detection
            ball.position = best_det->world_pos;
            ball.pixel_pos = cv::Point2f(best_det->box.x + best_det->box.width/2,
                                         best_det->box.y + best_det->box.height/2);
            ball.bbox = best_det->box;
            ball.has_yolo_detection = true;
            ball.frames_without_yolo = 0;
            ball.yolo_confidence = best_det->confidence;
            ball.yolo_class_id = best_det->class_id;
            
            // Update Kalman filter
            ball.kalman.update(KalmanFilter3D::MeasurementVector(
                ball.position.x, ball.position.y, ball.position.z));
            
            used_detections.insert(best_det->index);
        }
        else {
            // No YOLO detection - use fallback
            ball.has_yolo_detection = false;
            ball.frames_without_yolo++;
            
            if (ball.frames_without_yolo < 5) {
                // Use Kalman prediction
                ball.kalman.predict(dt);
                auto state = ball.kalman.get_state();
                ball.position = cv::Point3f(state(0), state(1), state(2));
            }
            else {
                // Check if near a hand
                bool near_hand = false;
                for (const auto& hand : hands) {
                    if (!hand.is_visible) continue;
                    float dist = cv::norm(ball.position - hand.wrist_pos);
                    if (dist < 0.15f) {
                        ball.position = hand.wrist_pos;
                        ball.held_by_hand_id = hand.id;
                        near_hand = true;
                        break;
                    }
                }
                
                if (!near_hand) {
                    // Search for color blob
                    cv::Point2f blob_pos = searchForColorBlob(hsv_frame, 
                                                              color_profiles_[i],
                                                              ball.pixel_pos, 
                                                              100);
                    if (blob_pos.x >= 0) {
                        float depth = getDepthAtPoint(depth_frame, blob_pos);
                        if (depth > 0.2f && depth < 2.0f) {
                            ball.position = deprojectToWorld(blob_pos, depth, intrinsics);
                            ball.pixel_pos = blob_pos;
                        }
                    }
                }
            }
        }
    }
    
    // Detect ball states and events
    std::vector<BallEvent> events = detectStatesAndEvents(balls_, hands);
    
    return {balls_, events};
}
```

**Estimated Lines:** ~200 lines

---

### Phase 5: Implement State and Event Detection
**File:** `engine/src/SimpleBallTracker.cpp`

**Tasks:**
1. Implement `isBallHeld()` - determine if ball is in hand
2. Implement `detectStatesAndEvents()` - detect state changes and generate events

**Algorithm:**
```cpp
bool SimpleBallTracker::isBallHeld(SimpleBall& ball, const std::vector<SimpleHand>& hands) {
    // Check ML model classification first
    if (ball.has_yolo_detection && ball.yolo_class_id == 1) {  // ball_held class
        // Find closest hand
        float min_dist = std::numeric_limits<float>::max();
        int closest_hand = -1;
        
        for (const auto& hand : hands) {
            if (!hand.is_visible) continue;
            float dist = cv::norm(ball.position - hand.wrist_pos);
            if (dist < min_dist) {
                min_dist = dist;
                closest_hand = hand.id;
            }
        }
        
        ball.held_by_hand_id = closest_hand;
        return true;
    }
    
    // Check proximity to wrists
    for (const auto& hand : hands) {
        if (!hand.is_visible) continue;
        
        float dist = cv::norm(ball.position - hand.wrist_pos);
        if (dist < 0.15f) {  // 15cm threshold
            ball.held_by_hand_id = hand.id;
            return true;
        }
    }
    
    ball.held_by_hand_id = -1;
    return false;
}

std::vector<BallEvent> SimpleBallTracker::detectStatesAndEvents(
    std::vector<SimpleBall>& balls, const std::vector<SimpleHand>& hands) {
    
    std::vector<BallEvent> events;
    
    for (auto& ball : balls) {
        bool was_held = ball.is_held;
        bool now_held = isBallHeld(ball, hands);
        
        // Debounce state changes (require 3 consecutive frames)
        if (now_held != ball.is_held) {
            ball.state_change_counter++;
            if (ball.state_change_counter >= 3) {
                ball.is_held = now_held;
                ball.state_change_counter = 0;
                
                // Generate event
                if (was_held && !now_held) {
                    events.push_back({
                        BallEvent::THROW,
                        ball.id,
                        ball.held_by_hand_id,
                        getCurrentTimestamp()
                    });
                }
                else if (!was_held && now_held) {
                    events.push_back({
                        BallEvent::CATCH,
                        ball.id,
                        ball.held_by_hand_id,
                        getCurrentTimestamp()
                    });
                }
            }
        }
        else {
            ball.state_change_counter = 0;
        }
    }
    
    return events;
}
```

**Estimated Lines:** ~100 lines

---

### Phase 6: Implement Color Calibration
**File:** `engine/src/SimpleBallTracker.cpp`

**Tasks:**
1. Implement `calibrateColor()` - calibrate color profile from clicked detection

**Algorithm:**
```cpp
bool SimpleBallTracker::calibrateColor(const std::string& color_name, 
                                      const cv::Point& click_point,
                                      const cv::Mat& color_frame,
                                      const std::vector<Detection>& detections,
                                      std::string& error_message) {
    // Find detection containing click point
    const Detection* clicked_det = nullptr;
    for (const auto& det : detections) {
        if (click_point.x >= det.box.x && 
            click_point.x <= (det.box.x + det.box.width) &&
            click_point.y >= det.box.y && 
            click_point.y <= (det.box.y + det.box.height)) {
            clicked_det = &det;
            break;
        }
    }
    
    if (!clicked_det) {
        error_message = "No detection found at click location";
        return false;
    }
    
    // Convert to HSV and sample detection box
    cv::Mat hsv_frame;
    cv::cvtColor(color_frame, hsv_frame, cv::COLOR_BGR2HSV);
    
    cv::Rect roi(clicked_det->box.x, clicked_det->box.y,
                 clicked_det->box.width, clicked_det->box.height);
    cv::Mat roi_hsv = hsv_frame(roi);
    
    // Calculate mean and stddev
    cv::Scalar mean, stddev;
    cv::meanStdDev(roi_hsv, mean, stddev);
    
    // Set range as mean ± 2*stddev
    float hue_tol = std::max(15.0f, (float)stddev[0] * 2.0f);
    float sat_tol = std::max(50.0f, (float)stddev[1] * 2.0f);
    float val_tol = std::max(50.0f, (float)stddev[2] * 2.0f);
    
    // Find and update color profile
    for (auto& profile : color_profiles_) {
        if (profile.name == color_name) {
            profile.min_hsv = cv::Scalar(
                std::max(0.0, mean[0] - hue_tol),
                std::max(0.0, mean[1] - sat_tol),
                std::max(0.0, mean[2] - val_tol)
            );
            profile.max_hsv = cv::Scalar(
                std::min(180.0, mean[0] + hue_tol),
                std::min(255.0, mean[1] + sat_tol),
                std::min(255.0, mean[2] + val_tol)
            );
            
            saveSettings();
            return true;
        }
    }
    
    error_message = "Color profile not found: " + color_name;
    return false;
}
```

**Estimated Lines:** ~80 lines

---

### Phase 7: Integrate with Engine
**File:** `engine/src/Engine.cpp`

**Tasks:**
1. Replace `#include "DNNTracker.hpp"` with `#include "SimpleBallTracker.hpp"`
2. Replace `dnn_tracker_` member with `simple_tracker_`
3. Update initialization in constructor
4. Update main loop to use SimpleBallTracker
5. Update command processing for calibration
6. Remove old visualization data population

**Key Changes:**
```cpp
// In Engine.hpp
std::shared_ptr<SimpleBallTracker> simple_tracker_;

// In Engine constructor
simple_tracker_ = std::make_shared<SimpleBallTracker>("ball_settings.json");

// In main loop
auto [tracked_balls, events] = simple_tracker_->update(
    color_image, depth_image, camera_intrinsics_,
    yolo_detections, tracked_hands);

// Populate protobuf
for (const auto& ball : tracked_balls) {
    auto* ball_pb = frame_data.add_balls();
    ball_pb->set_id(ball.id);
    ball_pb->set_logical_id(ball.id);
    
    auto* pos = ball_pb->mutable_position();
    pos->set_x(ball.position.x);
    pos->set_y(ball.position.y);
    pos->set_z(ball.position.z);
    
    auto* bbox = ball_pb->mutable_bounding_box_2d();
    bbox->set_x(ball.bbox.x);
    bbox->set_y(ball.bbox.y);
    bbox->set_width(ball.bbox.width);
    bbox->set_height(ball.bbox.height);
    
    ball_pb->set_class_name(ball.is_held ? "ball_held" : "ball");
}

// Populate events
for (const auto& event : events) {
    auto* event_pb = frame_data.add_ball_events();
    event_pb->set_type(event.type == BallEvent::THROW ? "throw" : "catch");
    event_pb->set_ball_id(event.ball_id);
    event_pb->set_hand_id(event.hand_id);
    event_pb->set_timestamp_us(event.timestamp);
}
```

**Estimated Lines:** ~150 lines changed

---

### Phase 8: Update CMakeLists.txt
**File:** `engine/CMakeLists.txt`

**Tasks:**
1. Add `SimpleBallTracker.cpp` to sources
2. Remove old tracker files (can be done later after testing)

**Changes:**
```cmake
# Add new file
set(SOURCES
    src/main.cpp
    src/Engine.cpp
    src/SimpleBallTracker.cpp  # NEW
    src/KalmanFilter3D.cpp
    # ... other files
)

# Later, remove these:
# src/DNNTracker.cpp
# src/ColorTracker.cpp
# src/AdaptiveColorManager.cpp
# src/ThrowCatchDetector.cpp
```

---

### Phase 9: Testing and Validation

**Tasks:**
1. Build the project
2. Test with single ball
3. Test with multiple balls
4. Test throw/catch detection
5. Test color calibration
6. Test fallback tracking (when YOLO fails)

**Test Cases:**
- Single green ball tracking
- Three balls (green, pink, orange) simultaneously
- Ball occlusion by hand
- Fast throws and catches
- Varying lighting conditions
- Color calibration workflow

---

### Phase 10: Cleanup (After Validation)

**Tasks:**
1. Remove old tracker files:
   - `engine/include/DNNTracker.hpp`
   - `engine/src/DNNTracker.cpp`
   - `engine/include/PersistentTracker.hpp`
   - `engine/include/ColorTracker.hpp`
   - `engine/src/ColorTracker.cpp`
   - `engine/include/AdaptiveColorManager.hpp`
   - `engine/src/AdaptiveColorManager.cpp`
   - `engine/include/ThrowCatchDetector.hpp`
   - `engine/src/ThrowCatchDetector.cpp`

2. Update CMakeLists.txt to remove old files
3. Update README.md with new architecture
4. Archive old documentation files

---

## Estimated Total Lines of Code

| Component | Lines |
|-----------|-------|
| SimpleBallTracker.hpp | ~120 |
| SimpleBallTracker.cpp | ~630 |
| Engine.cpp changes | ~150 |
| **Total New/Modified** | **~900** |
| **Old System (Removed)** | **~2500** |
| **Net Reduction** | **~1600 lines** |

---

## Implementation Order for Code Mode

1. ✅ Create `SimpleBallTracker.hpp` with all structs and class declaration
2. ✅ Create `SimpleBallTracker.cpp` with constructor and settings management
3. ✅ Implement color matching functions
4. ✅ Implement main update loop
5. ✅ Implement state and event detection
6. ✅ Implement color calibration
7. ✅ Update `Engine.cpp` to use SimpleBallTracker
8. ✅ Update `CMakeLists.txt`
9. ✅ Build and test
10. ✅ Remove old files after validation

---

## Key Design Decisions

1. **Color = Identity**: Each enabled color profile maps to exactly one ball ID
2. **YOLO First**: Always prefer YOLO detections when available
3. **Simple Fallback**: Use Kalman for short gaps, color blobs for longer gaps, hand position when near hands
4. **Debounced States**: Require 3 consecutive frames to change held/flight state
5. **Simple Events**: State transitions directly generate throw/catch events
6. **No ByteTrack**: Color matching provides stable identity without temporal tracking

---

## Success Metrics

- ✅ Code compiles without errors
- ✅ Balls are consistently tracked with correct IDs
- ✅ Held/flight state is accurately detected
- ✅ Throw/catch events are reliably generated
- ✅ System handles YOLO detection failures gracefully
- ✅ Color calibration works correctly
- ✅ Total code is under 1000 lines (vs 2500+ before)

---

## Notes for Code Mode

- Keep functions small and focused
- Add clear comments for complex logic
- Use const references where possible
- Handle edge cases (empty detections, no hands, etc.)
- Log important events for debugging
- Follow existing code style in the project
