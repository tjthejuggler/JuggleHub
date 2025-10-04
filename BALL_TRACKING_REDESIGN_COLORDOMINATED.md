# Color-Dominated Ball Tracking System Redesign
**Date:** 2025-01-04  
**Last Updated:** 2025-01-04 14:28 UTC  
**Purpose:** Complete redesign of ball tracking to use color dominance instead of Kalman predictions for identity matching

---

## Executive Summary

This document outlines a complete redesign of the ball tracking system to leverage:
1. **High-speed camera with depth** - Motion blur is minimal
2. **Excellent YOLO ball detection** - Reliable ball detection
3. **Different colored balls** - Primary identity mechanism
4. **Enabled color profiles** - User controls which balls to track

### Key Philosophy Changes

**OLD APPROACH:**
- Use Kalman filter predictions to match detections to trackers
- Complex cost matrices with spatial distance
- Color as secondary validation
- Auto-initialization from any detection

**NEW APPROACH:**
- Use color dominance as PRIMARY identity mechanism
- Kalman filters ONLY for smoothing and velocity estimation
- Spatial distance as secondary validation
- Initialize trackers based on enabled color profiles

---

## Core Design Principles

### 1. Color Dominance Matching

Instead of asking "which detection is closest to this tracker's prediction?", we ask:
- **"Which detection is the MOST green?"**
- **"Which detection is the MOST orange?"**
- **"Which detection is the MOST pink?"**

Each enabled color profile gets exactly ONE tracker. The tracker claims the detection that scores highest for its color.

### 2. Simplified Tracker Management

```
Enabled Colors = Active Trackers
- If green, orange, pink are enabled → 3 trackers
- If only green and orange are enabled → 2 trackers
- If user disables pink → pink tracker deactivates immediately
```

### 3. No Prediction-Based Matching

Kalman filters are used AFTER matching for:
- Position smoothing
- Velocity estimation
- Trajectory prediction (for visualization only)
- Gravity application during freefall

But NOT for determining which detection belongs to which tracker.

---

## Detailed Algorithm Design

### Frame Processing Pipeline

```
FRAME N:
├─ 1. YOLO Detection (unchanged)
│   └─ Get all ball detections with bounding boxes
│
├─ 2. Depth Filtering (unchanged)
│   └─ Filter by depth range (0.2m - 2.0m)
│
├─ 3. Color Scoring (NEW)
│   ├─ For each detection:
│   │   ├─ Sample 5x5 region at center
│   │   ├─ Convert to HSV
│   │   ├─ Compute color dominance scores:
│   │   │   ├─ green_score = how "green" is this detection?
│   │   │   ├─ orange_score = how "orange" is this detection?
│   │   │   ├─ pink_score = how "pink" is this detection?
│   │   │   └─ yellow_score = how "yellow" is this detection?
│   │   └─ Store scores with detection
│   └─ Result: Each detection has color dominance profile
│
├─ 4. Color-Based Assignment (NEW - REPLACES 3D MATCHING)
│   ├─ Get list of enabled color profiles from settings
│   ├─ For each enabled color:
│   │   ├─ Find detection with HIGHEST score for that color
│   │   ├─ Assign to tracker for that color
│   │   └─ Remove detection from pool
│   └─ Result: Each enabled color matched to best detection
│
├─ 5. Kalman Update (SIMPLIFIED)
│   ├─ For each matched tracker:
│   │   ├─ Update Kalman filter with detection position
│   │   ├─ Extract smoothed position and velocity
│   │   └─ Update tracker state
│   └─ For unmatched trackers:
│       └─ Mark as LOST (no prediction-based tracking)
│
├─ 6. Throw/Catch Detection (unchanged)
│   └─ Detect state transitions using ML + kinematics
│
└─ 7. Output Results
    └─ Send tracked balls to Hub
```

---

## Color Dominance Scoring Algorithm

### HSV-Based Scoring

```cpp
struct ColorScores {
    float green_score = 0.0f;
    float pink_score = 0.0f;
    float orange_score = 0.0f;
    float yellow_score = 0.0f;
    float red_score = 0.0f;
    float blue_score = 0.0f;
};

ColorScores compute_color_dominance(const cv::Mat& hsv_frame, 
                                     const cv::Rect& bbox) {
    ColorScores scores;
    
    // Sample 5x5 region at bbox center
    cv::Point2f center(bbox.x + bbox.width/2, bbox.y + bbox.height/2);
    std::vector<cv::Vec3b> samples = sample_region(hsv_frame, center, 5);
    
    // Compute average HSV
    float avg_h = 0, avg_s = 0, avg_v = 0;
    for (const auto& sample : samples) {
        avg_h += sample[0];
        avg_s += sample[1];
        avg_v += sample[2];
    }
    avg_h /= samples.size();
    avg_s /= samples.size();
    avg_v /= samples.size();
    
    // Score based on HSV characteristics
    // Higher saturation and value = stronger color signal
    float color_strength = (avg_s / 255.0f) * (avg_v / 255.0f);
    
    // Green: Hue 60-90
    if (avg_h >= 60 && avg_h <= 90) {
        scores.green_score = color_strength;
    }
    
    // Orange: Hue 5-20
    if (avg_h >= 5 && avg_h <= 20) {
        scores.orange_score = color_strength;
    }
    
    // Pink: Hue 160-180 or 0-10 (wraps)
    if ((avg_h >= 160 && avg_h <= 180) || (avg_h >= 0 && avg_h <= 10)) {
        scores.pink_score = color_strength;
    }
    
    // Yellow: Hue 20-40
    if (avg_h >= 20 && avg_h <= 40) {
        scores.yellow_score = color_strength;
    }
    
    // Red: Hue 0-10 or 170-180
    if ((avg_h >= 0 && avg_h <= 10) || (avg_h >= 170 && avg_h <= 180)) {
        scores.red_score = color_strength;
    }
    
    // Blue: Hue 100-130
    if (avg_h >= 100 && avg_h <= 130) {
        scores.blue_score = color_strength;
    }
    
    return scores;
}
```

### Assignment Algorithm

```cpp
void assign_detections_by_color(
    std::vector<Detection>& detections,
    std::vector<PersistentTracker>& trackers,
    const std::vector<ColorProfile>& enabled_profiles) {
    
    // Compute color scores for all detections
    std::vector<ColorScores> detection_scores;
    for (const auto& det : detections) {
        detection_scores.push_back(compute_color_dominance(hsv_frame, det.box));
    }
    
    // For each enabled color profile
    for (const auto& profile : enabled_profiles) {
        if (!profile.enabled) continue;
        
        // Find detection with HIGHEST score for this color
        int best_detection_idx = -1;
        float best_score = 0.1f; // Minimum threshold
        
        for (size_t i = 0; i < detections.size(); ++i) {
            if (detections[i].already_assigned) continue;
            
            float score = get_score_for_color(detection_scores[i], profile.name);
            
            if (score > best_score) {
                best_score = score;
                best_detection_idx = i;
            }
        }
        
        // Assign if found
        if (best_detection_idx >= 0) {
            // Find tracker for this color
            auto* tracker = find_tracker_for_color(trackers, profile.name);
            
            if (tracker) {
                // Update tracker with detection
                tracker->kf.update(detections[best_detection_idx].world_pos);
                tracker->status = TRACKED;
                tracker->frames_since_seen = 0;
                
                // Mark detection as assigned
                detections[best_detection_idx].already_assigned = true;
            }
        }
    }
}
```

---

## Tracker Lifecycle Management

### Initialization

```cpp
void initialize_trackers_from_enabled_colors() {
    // Get enabled color profiles
    std::vector<std::string> enabled_colors;
    for (const auto& profile : color_profiles) {
        if (profile.enabled) {
            enabled_colors.push_back(profile.name);
        }
    }
    
    // Create one tracker per enabled color
    trackers.clear();
    for (size_t i = 0; i < enabled_colors.size(); ++i) {
        PersistentTracker tracker;
        tracker.logical_id = i;
        tracker.assigned_color_name = enabled_colors[i];
        tracker.has_color_assignment = true;
        tracker.status = LOST; // Will activate when detection found
        trackers.push_back(tracker);
    }
}
```

### Dynamic Enable/Disable

```cpp
void handle_color_profile_change(const std::string& color_name, bool enabled) {
    if (enabled) {
        // Add tracker for this color if not exists
        bool exists = false;
        for (const auto& tracker : trackers) {
            if (tracker.assigned_color_name == color_name) {
                exists = true;
                break;
            }
        }
        
        if (!exists) {
            PersistentTracker new_tracker;
            new_tracker.logical_id = trackers.size();
            new_tracker.assigned_color_name = color_name;
            new_tracker.has_color_assignment = true;
            new_tracker.status = LOST;
            trackers.push_back(new_tracker);
        }
    } else {
        // Deactivate tracker for this color
        for (auto& tracker : trackers) {
            if (tracker.assigned_color_name == color_name) {
                tracker.status = LOST;
                // Optionally remove from list
            }
        }
    }
}
```

---

## Advantages of New System

### 1. Simplicity
- No complex cost matrices
- No Hungarian algorithm needed
- No prediction-based matching logic
- Clear 1:1 mapping: color → tracker

### 2. Robustness
- Color is stable across frames
- No ID switching when balls cross paths
- Works even with fast motion (high-speed camera)
- Leverages depth camera advantages

### 3. User Control
- User explicitly controls which balls to track
- Toggle colors on/off in real-time
- Predictable behavior

### 4. Performance
- Simpler algorithm = faster execution
- No need for complex optimization
- Scales linearly with number of colors

### 5. Reliability
- Color dominance is unambiguous
- No "closest match" ambiguity
- Works even when balls are close together

---

## Edge Cases and Solutions

### Case 1: Two Balls Same Color
**Problem:** User has two green balls  
**Solution:** Only track one. System assumes different colored balls.  
**Mitigation:** Document requirement for different colors

### Case 2: Poor Lighting
**Problem:** Color scores are low  
**Solution:** Use minimum threshold (0.1). If no detection meets threshold, tracker goes LOST.  
**Mitigation:** Provide lighting guidelines to users

### Case 3: Ball Temporarily Occluded
**Problem:** No detection for a frame  
**Solution:** Tracker goes LOST immediately (no prediction-based tracking).  
**Benefit:** When ball reappears, color matching will correctly re-identify it

### Case 4: Color Ambiguity (Orange vs Yellow)
**Problem:** Detection scores high for both orange and yellow  
**Solution:** Whichever color is checked first in enabled list gets priority.  
**Mitigation:** Use well-separated colors (green, orange, pink work well)

### Case 5: Ball Changes Apparent Color (Rotation)
**Problem:** Ball surface has multiple colors  
**Solution:** Use larger sample region (5x5 → 7x7) to average out variations.  
**Mitigation:** Use solid-colored balls

---

## Implementation Roadmap

### Phase 1: Core Algorithm (Priority 1)
1. Implement `compute_color_dominance()` function
2. Implement `assign_detections_by_color()` function
3. Remove prediction-based cost matrix code
4. Update tracker initialization to use enabled colors

### Phase 2: Integration (Priority 2)
1. Connect color profile enable/disable to tracker lifecycle
2. Update visualization to show color assignments
3. Remove ByteTrack dependencies for balls
4. Simplify Kalman filter usage (update only, no prediction matching)

### Phase 3: Testing & Refinement (Priority 3)
1. Test with 2-ball juggling (green + orange)
2. Test with 3-ball juggling (green + orange + pink)
3. Tune color dominance thresholds
4. Optimize HSV ranges for each color

### Phase 4: Documentation (Priority 4)
1. Update user guide with color requirements
2. Document color calibration process
3. Create troubleshooting guide
4. Update API documentation

---

## Files to Modify

### Core Tracking Logic
- [`engine/src/DNNTracker.cpp`](engine/src/DNNTracker.cpp) - Main tracking loop
  - Remove lines 504-681 (3D distance-based matching)
  - Replace with color-based assignment
  - Simplify auto-initialization (lines 734-883)

### Color Scoring
- [`engine/src/DNNTracker.cpp`](engine/src/DNNTracker.cpp) - Add color dominance functions
  - Enhance `compute_color_scores()` (lines 90-157)
  - Make it return dominance scores, not just match confidence

### Tracker Management
- [`engine/include/PersistentTracker.hpp`](engine/include/PersistentTracker.hpp)
  - Ensure color assignment fields are present
  - Add color dominance score field

### Color Profiles
- [`engine/src/ColorTracker.cpp`](engine/src/ColorTracker.cpp)
  - Keep color profile management
  - Remove redundant Kalman filters (lines 28-33)
  - Simplify to pure color detection

### Settings Integration
- [`engine/src/DNNTracker.cpp`](engine/src/DNNTracker.cpp)
  - Update `update_setting()` to handle color enable/disable
  - Trigger tracker reinitialization on color changes

---

## Migration Strategy

### Step 1: Preserve Current System
- Keep current code in separate branch
- Create feature branch for new system

### Step 2: Implement New Algorithm
- Add new functions alongside old code
- Use feature flag to switch between systems

### Step 3: Parallel Testing
- Run both systems simultaneously
- Compare results
- Tune new system parameters

### Step 4: Cutover
- Remove old prediction-based matching
- Remove ByteTrack for balls
- Clean up dead code

### Step 5: Optimization
- Profile performance
- Optimize color scoring
- Reduce memory allocations

---

## Testing Plan

### Unit Tests
1. Color dominance scoring with known HSV values
2. Assignment algorithm with mock detections
3. Tracker lifecycle (enable/disable colors)

### Integration Tests
1. Single ball tracking (green only)
2. Two ball tracking (green + orange)
3. Three ball tracking (green + orange + pink)
4. Dynamic color enable/disable during tracking

### Performance Tests
1. Frame processing time
2. Memory usage
3. CPU utilization
4. Comparison with old system

### Real-World Tests
1. Cascade pattern (3 balls)
2. Fountain pattern (3 balls)
3. Fast throws
4. Balls crossing paths
5. Temporary occlusions

---

## Success Criteria

### Functional Requirements
- ✅ Track N balls where N = number of enabled colors
- ✅ Maintain correct identity across entire juggling session
- ✅ No ID switching when balls cross paths
- ✅ Recover correctly after temporary occlusions
- ✅ Real-time enable/disable of color tracking

### Performance Requirements
- ✅ Process frames at 60 FPS
- ✅ < 5ms per frame for tracking logic
- ✅ < 100 MB memory usage

### Quality Requirements
- ✅ 95%+ correct identity maintenance
- ✅ < 1% false positive rate
- ✅ < 5% false negative rate (missed detections)

---

## Future Enhancements

### Phase 2 Features
1. **Multi-color balls** - Support balls with multiple color regions
2. **Color learning** - Automatically learn color profiles from juggling session
3. **Adaptive thresholds** - Adjust color thresholds based on lighting
4. **Color confidence** - Report confidence in color identification

### Phase 3 Features
1. **Pattern detection** - Use color sequences to detect juggling patterns
2. **Ball counting** - Automatically detect number of balls in use
3. **Color-based events** - Trigger events based on color interactions

---

## Conclusion

This redesign simplifies the tracking system by leveraging the key advantages of the hardware setup:
- High-speed camera eliminates motion blur concerns
- Depth camera provides reliable 3D positions
- Different colored balls provide unambiguous identity

By using color dominance as the PRIMARY identity mechanism, we eliminate the complexity of prediction-based matching while improving robustness and user control.

The new system is:
- **Simpler** - Less code, clearer logic
- **More robust** - No ID switching
- **More predictable** - User controls which balls to track
- **Faster** - Linear complexity instead of quadratic

**Next Steps:** Review this design with the team, then proceed to implementation Phase 1.