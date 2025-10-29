# Simplified Ball Tracking System Architecture

**Created:** 2025-10-04  
**Status:** Design Phase

## Overview

This document outlines a major simplification of the JuggleHub ball tracking system. The current system is over-engineered with ByteTrack, complex Kalman filtering, adaptive color management, and throw/catch detection. We're replacing it with a simple, robust color-based tracking system.

---

## Current System Complexity (TO BE REMOVED)

### What We're Removing:
1. **ByteTrack** - Unnecessary for color-based ID assignment
2. **PersistentTracker** - Overly complex state management
3. **AdaptiveColorManager** - Over-engineered color range adjustment
4. **ThrowCatchDetector** - Complex multi-signal fusion system
5. **ColorTracker** - Separate color tracking system (will be integrated)
6. **Complex Kalman filtering** - Only use when YOLO fails

### Current File Structure (1667 lines in DNNTracker.cpp alone):
- `DNNTracker.hpp/cpp` - 1667 lines of complex tracking logic
- `PersistentTracker.hpp` - 70 lines of state management
- `ColorTracker.hpp/cpp` - Separate color tracking system
- `AdaptiveColorManager.hpp/cpp` - Adaptive color adjustment
- `ThrowCatchDetector.hpp/cpp` - Event detection system
- `KalmanFilter3D.hpp/cpp` - 3D Kalman filtering

---

## Simplified System Design

### Core Principle
**"Use YOLO first, color for identity, Kalman only as fallback"**

### Key Simplifications:
1. **No ByteTrack** - Ball ID is determined by color, not temporal tracking
2. **Color = Identity** - Each enabled color profile maps to one ball (Ball 0, Ball 1, Ball 2)
3. **Simple State Detection** - Use ML model class (ball vs ball_held) + wrist proximity
4. **Simple Event Detection** - State transitions = events (held→flight = throw, flight→held = catch)

---

## New Architecture

### 1. Data Structures

```cpp
// Simple ball state
struct SimpleBall {
    int id;                          // 0, 1, 2 (based on color order)
    std::string color_name;          // "green", "pink", "orange", etc.
    cv::Point3f position;            // Current 3D position
    cv::Point2f pixel_pos;           // Current 2D position
    cv::Rect_<float> bbox;           // Bounding box
    
    // State
    bool is_held;                    // In hand or in flight
    int held_by_hand_id;             // -1 if not held, 0=left, 1=right
    
    // Tracking
    bool has_yolo_detection;         // True if YOLO sees it this frame
    int frames_without_yolo;         // Counter for fallback logic
    KalmanFilter3D kalman;           // Only used when YOLO fails
    
    // Confidence
    float yolo_confidence;           // YOLO detection confidence
    float color_match_score;         // How well it matches assigned color
};

// Simple hand state
struct SimpleHand {
    int id;                          // 0=left, 1=right
    cv::Point3f wrist_pos;           // Wrist position from pose model
    bool is_visible;                 // Detected this frame
};
```

### 2. Tracking Algorithm

```
FOR EACH FRAME:
    1. Run YOLO ball detection
    2. Run pose estimation for hands
    
    3. FOR EACH enabled color profile:
        - Find YOLO detection with best color match
        - Assign to corresponding ball ID
        - Update ball position from YOLO
        - Mark has_yolo_detection = true
    
    4. FOR EACH ball without YOLO detection:
        - Increment frames_without_yolo
        - IF frames_without_yolo < 5:
            - Use Kalman prediction
        - ELSE IF near a hand:
            - Snap to hand position
            - Mark as held
        - ELSE:
            - Search for color blob near last position
            - IF found: update from color blob
            - ELSE: continue Kalman prediction
    
    5. Detect ball state (held vs flight):
        - IF YOLO class = "ball_held": is_held = true
        - ELSE IF distance to wrist < 0.15m: is_held = true
        - ELSE: is_held = false
    
    6. Detect events:
        - IF previous_state = held AND current_state = flight: THROW
        - IF previous_state = flight AND current_state = held: CATCH
```

### 3. Color Matching Algorithm

```cpp
float match_color(Detection det, ColorProfile profile) {
    // Sample pixels in detection bbox
    // Calculate average HSV
    // Check if within profile range
    // Return match percentage (0.0 - 1.0)
}

Detection* find_best_color_match(vector<Detection> detections, ColorProfile profile) {
    Detection* best = nullptr;
    float best_score = 0.0f;
    
    for (auto& det : detections) {
        float score = match_color(det, profile);
        if (score > best_score && score > 0.5) {  // Minimum 50% match
            best_score = score;
            best = &det;
        }
    }
    
    return best;
}
```

### 4. State Detection Logic

```cpp
bool is_ball_held(SimpleBall& ball, vector<SimpleHand>& hands) {
    // Check ML model classification
    if (ball.yolo_class_id == 1) {  // ball_held class
        return true;
    }
    
    // Check proximity to wrists
    for (auto& hand : hands) {
        if (!hand.is_visible) continue;
        
        float dist = distance_3d(ball.position, hand.wrist_pos);
        if (dist < 0.15f) {  // 15cm threshold
            ball.held_by_hand_id = hand.id;
            return true;
        }
    }
    
    return false;
}
```

### 5. Event Detection Logic

```cpp
struct BallEvent {
    enum Type { THROW, CATCH };
    Type type;
    int ball_id;
    int hand_id;
    uint64_t timestamp;
};

vector<BallEvent> detect_events(vector<SimpleBall>& balls) {
    vector<BallEvent> events;
    
    for (auto& ball : balls) {
        bool was_held = ball.previous_is_held;
        bool now_held = ball.is_held;
        
        // State transition detection
        if (was_held && !now_held) {
            // Ball went from held to flight = THROW
            events.push_back({
                BallEvent::THROW,
                ball.id,
                ball.held_by_hand_id,
                current_timestamp
            });
        }
        else if (!was_held && now_held) {
            // Ball went from flight to held = CATCH
            events.push_back({
                BallEvent::CATCH,
                ball.id,
                ball.held_by_hand_id,
                current_timestamp
            });
        }
        
        // Update previous state
        ball.previous_is_held = ball.is_held;
    }
    
    return events;
}
```

---

## Implementation Plan

### Phase 1: Create SimpleBallTracker Class
- New file: `SimpleBallTracker.hpp/cpp`
- Implement basic data structures
- Implement YOLO detection processing
- Implement color matching algorithm
- Target: ~300-400 lines total

### Phase 2: Integrate Color Profiles
- Load color profiles from `ball_settings.json`
- Implement color-to-ball ID mapping
- Add color blob search for fallback tracking

### Phase 3: Add State Detection
- Implement held/flight detection
- Add wrist proximity checking
- Integrate with pose estimation

### Phase 4: Add Event Detection
- Implement simple state transition detection
- Add throw/catch event generation

### Phase 5: Replace DNNTracker in Engine
- Update `Engine.cpp` to use `SimpleBallTracker`
- Remove old tracking system files
- Update protobuf messages if needed

---

## File Changes Required

### New Files:
- `engine/include/SimpleBallTracker.hpp` (~100 lines)
- `engine/src/SimpleBallTracker.cpp` (~300 lines)

### Modified Files:
- `engine/src/Engine.cpp` - Replace DNNTracker with SimpleBallTracker
- `engine/CMakeLists.txt` - Update build configuration

### Files to Remove (after migration):
- `engine/include/DNNTracker.hpp`
- `engine/src/DNNTracker.cpp`
- `engine/include/PersistentTracker.hpp`
- `engine/include/ColorTracker.hpp`
- `engine/src/ColorTracker.cpp`
- `engine/include/AdaptiveColorManager.hpp`
- `engine/src/AdaptiveColorManager.cpp`
- `engine/include/ThrowCatchDetector.hpp`
- `engine/src/ThrowCatchDetector.cpp`

### Files to Keep:
- `engine/include/KalmanFilter3D.hpp` - Used for fallback tracking
- `engine/src/KalmanFilter3D.cpp`
- `ball_settings.json` - Color profile configuration

---

## Benefits of Simplified System

1. **Drastically Reduced Complexity**
   - From ~2500 lines to ~400 lines
   - Single tracking class instead of 5+ classes
   - Clear, understandable logic flow

2. **More Robust Tracking**
   - Color-based identity is more stable than temporal tracking
   - No ID switching issues
   - Simpler fallback logic

3. **Easier to Maintain**
   - Less code to debug
   - Clear separation of concerns
   - Easy to understand and modify

4. **Better Performance**
   - No ByteTrack overhead
   - No complex cost matrix calculations
   - Simpler color matching

5. **Easier to Tune**
   - Fewer parameters to adjust
   - Clear thresholds (distance, color match, etc.)
   - Direct cause-and-effect relationships

---

## Configuration

### Color Profiles (ball_settings.json)
```json
{
  "colors": [
    {
      "name": "green",
      "enabled": true,
      "min_hsv": [45, 100, 100],
      "max_hsv": [75, 255, 255]
    },
    {
      "name": "pink",
      "enabled": true,
      "min_hsv": [140, 100, 100],
      "max_hsv": [175, 255, 255]
    },
    {
      "name": "orange",
      "enabled": true,
      "min_hsv": [5, 100, 100],
      "max_hsv": [20, 255, 255]
    }
  ]
}
```

### Tracking Parameters
```cpp
// Distance thresholds
const float WRIST_PROXIMITY_THRESHOLD = 0.15f;  // 15cm
const float COLOR_SEARCH_RADIUS = 100;          // pixels

// Timing thresholds
const int MAX_FRAMES_WITHOUT_YOLO = 30;         // ~1 second at 30fps
const int MIN_FRAMES_FOR_STATE_CHANGE = 3;      // Debounce state changes

// Color matching
const float MIN_COLOR_MATCH_SCORE = 0.5f;       // 50% match required
```

---

## Migration Strategy

1. **Parallel Implementation**
   - Create SimpleBallTracker alongside existing system
   - Test thoroughly before switching

2. **Gradual Cutover**
   - Add feature flag to switch between systems
   - Compare results side-by-side
   - Fix any issues in new system

3. **Remove Old System**
   - Once new system is proven stable
   - Remove old files and dependencies
   - Update documentation

---

## Testing Plan

1. **Unit Tests**
   - Color matching algorithm
   - State detection logic
   - Event detection logic

2. **Integration Tests**
   - Full tracking pipeline
   - Multiple balls simultaneously
   - Occlusion handling

3. **Real-World Tests**
   - 3-ball cascade pattern
   - Fast throws and catches
   - Varying lighting conditions

---

## Success Criteria

- ✅ Consistent ball ID assignment based on color
- ✅ Accurate held/flight state detection
- ✅ Reliable throw/catch event detection
- ✅ Smooth tracking with occasional YOLO failures
- ✅ Code reduced from ~2500 to ~400 lines
- ✅ Easier to understand and maintain

---

## Next Steps

1. Review and approve this architecture
2. Create SimpleBallTracker class skeleton
3. Implement core tracking algorithm
4. Test with real juggling footage
5. Integrate into Engine
6. Remove old system
