# Adaptive Color Range Adjustment System
**Date:** 2025-01-04  
**Purpose:** Design a dynamic, real-time color range adjustment system that automatically optimizes HSV ranges to minimize unmatched detections

---

## Executive Summary

The current color-dominated tracking system uses **fixed HSV ranges** for each color (green, pink, orange, etc.). When a ball's actual color doesn't match these ranges perfectly, it remains untracked. This document proposes an **adaptive system** that:

1. **Monitors tracking success** - Tracks which colors are consistently matched vs. which have unmatched detections
2. **Dynamically adjusts ranges** - Expands ranges for colors with unmatched detections, contracts ranges for well-tracked colors
3. **Prevents overlap** - Ensures color ranges don't interfere with each other
4. **Operates in real-time** - Adjustments happen automatically during tracking without user intervention

**Goal:** Achieve 3 solid, consistent trackings by automatically tuning color ranges to match the actual balls being juggled.

---

## Problem Analysis

### Current System Issues

1. **Fixed Ranges**: HSV ranges are hardcoded or manually calibrated
   - Pink: Hue 140-175 (recently widened)
   - Green: Hue 45-75
   - Orange: Hue 5-20

2. **Unmatched Detections**: When a ball's color falls outside its range:
   - Detection appears as "unmatched box" (white box in visualization)
   - No tracker claims it
   - Tracking fails for that ball

3. **No Adaptation**: System cannot learn from tracking failures
   - If pink ball has hue 135 (below range), it will NEVER be tracked
   - Manual intervention required to adjust ranges

### User Requirements

From user feedback:
> "we need to have the ability to change the ranges of the colors for the balls as we track them in real time. the program should do it automatically so that the amount of unmatched boxes is reduced. this means that if green is being tracked consistently then we move it slightly away from pink if pink is not being tracked and we make the range for pink much larger."

**Key Requirements:**
1. **Automatic adjustment** - No manual tuning required
2. **Real-time operation** - Adjustments happen during tracking
3. **Success-based** - Expand ranges for failing colors, contract for successful ones
4. **Separation logic** - Move successful colors away from failing ones to give failing colors more "space"
5. **Goal-oriented** - Minimize unmatched detections

---

## System Architecture

### Core Components

```
┌─────────────────────────────────────────────────────────────┐
│                   Adaptive Color Manager                     │
├─────────────────────────────────────────────────────────────┤
│                                                               │
│  ┌──────────────────┐  ┌──────────────────┐  ┌────────────┐│
│  │ Tracking Monitor │  │ Range Optimizer  │  │ Conflict   ││
│  │                  │  │                  │  │ Resolver   ││
│  │ - Success rate   │  │ - Expand/shrink  │  │            ││
│  │ - Unmatched det. │  │ - Shift centers  │  │ - Prevent  ││
│  │ - Color scores   │  │ - Adjust bounds  │  │   overlap  ││
│  └──────────────────┘  └──────────────────┘  └────────────┘│
│           │                      │                    │      │
│           └──────────────────────┴────────────────────┘      │
│                              │                               │
└──────────────────────────────┼───────────────────────────────┘
                               │
                               ▼
                    ┌──────────────────────┐
                    │  Color Profiles      │
                    │  (HSV Ranges)        │
                    │                      │
                    │  - Dynamic min/max   │
                    │  - Adjustment history│
                    │  - Confidence scores │
                    └──────────────────────┘
```

### Data Structures

```cpp
struct AdaptiveColorProfile {
    std::string name;
    bool enabled;
    
    // Current HSV range (dynamic)
    cv::Scalar min_hsv;
    cv::Scalar max_hsv;
    cv::Scalar min_hsv2;  // For wrap-around colors
    cv::Scalar max_hsv2;
    
    // Adaptation state
    float center_hue;           // Current center of hue range
    float hue_range_width;      // Current width of hue range
    float expansion_rate;       // How fast to expand when failing
    float contraction_rate;     // How fast to contract when succeeding
    
    // Tracking statistics (rolling window)
    std::deque<bool> recent_matches;  // Last N frames: matched or not
    int frames_tracked;         // Consecutive frames with successful match
    int frames_unmatched;       // Consecutive frames without match
    float success_rate;         // Percentage of recent successful matches
    
    // Constraints
    float min_hue_width;        // Minimum range width (e.g., 10 degrees)
    float max_hue_width;        // Maximum range width (e.g., 40 degrees)
    
    // Observed color data
    std::vector<float> observed_hues;  // Actual hue values when matched
    float mean_observed_hue;           // Average of observed hues
};

struct AdaptationConfig {
    // Monitoring parameters
    int history_window_size = 60;      // Track last 60 frames (1 second at 60fps)
    float success_threshold = 0.7f;    // 70% success = "well tracked"
    float failure_threshold = 0.3f;    // 30% success = "poorly tracked"
    
    // Adjustment parameters
    float expansion_step = 2.0f;       // Expand by 2 hue degrees per adjustment
    float contraction_step = 1.0f;     // Contract by 1 hue degree per adjustment
    float shift_step = 1.0f;           // Shift center by 1 degree per adjustment
    
    // Adjustment frequency
    int frames_between_adjustments = 30;  // Adjust every 30 frames (0.5s at 60fps)
    
    // Safety limits
    float min_separation = 5.0f;       // Minimum hue separation between colors
    float max_range_width = 40.0f;     // Maximum hue range width
    float min_range_width = 10.0f;     // Minimum hue range width
};
```

---

## Adaptive Algorithm

### Phase 1: Monitoring (Every Frame)

```cpp
void AdaptiveColorManager::monitorFrame(
    const std::vector<Detection>& detections,
    const std::vector<ColorScores>& detection_scores,
    const std::map<std::string, int>& matched_colors) {
    
    for (auto& profile : adaptive_profiles_) {
        if (!profile.enabled) continue;
        
        // Check if this color was matched this frame
        bool matched_this_frame = (matched_colors.count(profile.name) > 0);
        
        // Update rolling history
        profile.recent_matches.push_back(matched_this_frame);
        if (profile.recent_matches.size() > config_.history_window_size) {
            profile.recent_matches.pop_front();
        }
        
        // Update consecutive counters
        if (matched_this_frame) {
            profile.frames_tracked++;
            profile.frames_unmatched = 0;
            
            // Record observed hue value
            int detection_idx = matched_colors.at(profile.name);
            float observed_hue = getDetectionHue(detections[detection_idx]);
            profile.observed_hues.push_back(observed_hue);
            if (profile.observed_hues.size() > 30) {
                profile.observed_hues.erase(profile.observed_hues.begin());
            }
        } else {
            profile.frames_unmatched++;
            profile.frames_tracked = 0;
        }
        
        // Calculate success rate
        int successes = std::count(profile.recent_matches.begin(),
                                   profile.recent_matches.end(), true);
        profile.success_rate = static_cast<float>(successes) / 
                              profile.recent_matches.size();
    }
}
```

### Phase 2: Range Adjustment (Every N Frames)

```cpp
void AdaptiveColorManager::adjustRanges() {
    // Only adjust every N frames to avoid instability
    if (frame_count_ % config_.frames_between_adjustments != 0) {
        return;
    }
    
    // Step 1: Identify colors needing adjustment
    std::vector<AdaptiveColorProfile*> failing_colors;
    std::vector<AdaptiveColorProfile*> succeeding_colors;
    
    for (auto& profile : adaptive_profiles_) {
        if (!profile.enabled) continue;
        
        if (profile.success_rate < config_.failure_threshold) {
            failing_colors.push_back(&profile);
        } else if (profile.success_rate > config_.success_threshold) {
            succeeding_colors.push_back(&profile);
        }
    }
    
    // Step 2: Expand ranges for failing colors
    for (auto* profile : failing_colors) {
        expandRange(*profile);
        
        // If we have observed hues, shift center toward them
        if (!profile->observed_hues.empty()) {
            float mean_hue = calculateMean(profile->observed_hues);
            shiftCenterToward(*profile, mean_hue);
        }
    }
    
    // Step 3: Contract ranges for succeeding colors
    for (auto* profile : succeeding_colors) {
        contractRange(*profile);
        
        // Shift center toward mean of observed hues
        if (!profile->observed_hues.empty()) {
            float mean_hue = calculateMean(profile->observed_hues);
            profile->center_hue = mean_hue;
            updateRangeFromCenter(*profile);
        }
    }
    
    // Step 4: Resolve conflicts (overlapping ranges)
    resolveConflicts(failing_colors, succeeding_colors);
    
    // Step 5: Apply updated ranges to color profiles
    applyAdaptedRanges();
}
```

### Phase 3: Range Expansion

```cpp
void AdaptiveColorManager::expandRange(AdaptiveColorProfile& profile) {
    // Increase range width
    profile.hue_range_width += config_.expansion_step;
    
    // Clamp to maximum
    if (profile.hue_range_width > config_.max_range_width) {
        profile.hue_range_width = config_.max_range_width;
    }
    
    // Update min/max from center and width
    updateRangeFromCenter(profile);
    
    INFO_LOG("Adaptive: Expanded ", profile.name, " range to ",
             profile.hue_range_width, " degrees (success rate: ",
             profile.success_rate * 100, "%)");
}
```

### Phase 4: Range Contraction

```cpp
void AdaptiveColorManager::contractRange(AdaptiveColorProfile& profile) {
    // Decrease range width
    profile.hue_range_width -= config_.contraction_step;
    
    // Clamp to minimum
    if (profile.hue_range_width < config_.min_range_width) {
        profile.hue_range_width = config_.min_range_width;
    }
    
    // Update min/max from center and width
    updateRangeFromCenter(profile);
    
    INFO_LOG("Adaptive: Contracted ", profile.name, " range to ",
             profile.hue_range_width, " degrees (success rate: ",
             profile.success_rate * 100, "%)");
}
```

### Phase 5: Conflict Resolution

```cpp
void AdaptiveColorManager::resolveConflicts(
    const std::vector<AdaptiveColorProfile*>& failing_colors,
    const std::vector<AdaptiveColorProfile*>& succeeding_colors) {
    
    // For each failing color, check if it overlaps with succeeding colors
    for (auto* failing : failing_colors) {
        for (auto* succeeding : succeeding_colors) {
            float overlap = calculateOverlap(*failing, *succeeding);
            
            if (overlap > 0) {
                // Move succeeding color away from failing color
                // This gives failing color more "space" to expand
                float shift_direction = (succeeding->center_hue > failing->center_hue) ? 1.0f : -1.0f;
                succeeding->center_hue += shift_direction * config_.shift_step;
                
                // Wrap around if needed
                if (succeeding->center_hue < 0) succeeding->center_hue += 180;
                if (succeeding->center_hue >= 180) succeeding->center_hue -= 180;
                
                updateRangeFromCenter(*succeeding);
                
                INFO_LOG("Adaptive: Shifted ", succeeding->name, " away from ",
                         failing->name, " to reduce overlap");
            }
        }
    }
    
    // Ensure minimum separation between all enabled colors
    ensureMinimumSeparation();
}
```

### Phase 6: Separation Enforcement

```cpp
void AdaptiveColorManager::ensureMinimumSeparation() {
    // Sort profiles by center hue
    std::vector<AdaptiveColorProfile*> sorted_profiles;
    for (auto& profile : adaptive_profiles_) {
        if (profile.enabled) {
            sorted_profiles.push_back(&profile);
        }
    }
    
    std::sort(sorted_profiles.begin(), sorted_profiles.end(),
              [](const AdaptiveColorProfile* a, const AdaptiveColorProfile* b) {
                  return a->center_hue < b->center_hue;
              });
    
    // Check adjacent pairs for minimum separation
    for (size_t i = 0; i < sorted_profiles.size() - 1; ++i) {
        auto* current = sorted_profiles[i];
        auto* next = sorted_profiles[i + 1];
        
        float separation = next->center_hue - current->center_hue;
        
        if (separation < config_.min_separation) {
            // Push them apart equally
            float adjustment = (config_.min_separation - separation) / 2.0f;
            current->center_hue -= adjustment;
            next->center_hue += adjustment;
            
            updateRangeFromCenter(*current);
            updateRangeFromCenter(*next);
        }
    }
}
```

---

## Integration with Existing System

### Modified `compute_color_dominance()` Function

```cpp
// In DNNTracker.cpp - use adaptive ranges instead of hardcoded ones
static ColorScores compute_color_dominance(
    const cv::Mat& color_frame,
    const Detection& detection,
    const AdaptiveColorManager& adaptive_manager) {  // NEW PARAMETER
    
    ColorScores scores;
    
    // ... existing sampling code ...
    
    // Use adaptive ranges instead of hardcoded ranges
    for (const auto& profile : adaptive_manager.getProfiles()) {
        if (!profile.enabled) continue;
        
        // Check if hue falls within this color's ADAPTIVE range
        bool in_range = false;
        if (avg_h >= profile.min_hsv[0] && avg_h <= profile.max_hsv[0]) {
            in_range = true;
        }
        // Check secondary range for wrap-around colors
        if (profile.min_hsv2[0] >= 0 &&
            avg_h >= profile.min_hsv2[0] && avg_h <= profile.max_hsv2[0]) {
            in_range = true;
        }
        
        if (in_range) {
            // Set score based on color name
            float score = color_strength;
            if (profile.name == "green") scores.green_score = score;
            else if (profile.name == "pink") scores.pink_score = score;
            else if (profile.name == "orange") scores.orange_score = score;
            // ... etc for other colors
        }
    }
    
    return scores;
}
```

### Modified Tracking Loop

```cpp
// In DNNTracker::update()

// After color-based assignment, monitor results
std::map<std::string, int> matched_colors;
for (auto* tracker : ball_trackers_list) {
    if (tracker->status == TrackerStatus::TRACKED && 
        tracker->has_color_assignment) {
        // Find which detection was matched
        for (size_t i = 0; i < valid_detections.size(); ++i) {
            if (/* detection i was matched to this tracker */) {
                matched_colors[tracker->assigned_color_name] = i;
                break;
            }
        }
    }
}

// Monitor tracking success
adaptive_color_manager_->monitorFrame(
    valid_detections,
    detection_color_scores,
    matched_colors
);

// Periodically adjust ranges
adaptive_color_manager_->adjustRanges();
```

---

## Implementation Plan

### Phase 1: Core Infrastructure (Priority 1)
**Files:** `engine/include/AdaptiveColorManager.hpp`, `engine/src/AdaptiveColorManager.cpp`

1. Create `AdaptiveColorProfile` struct
2. Create `AdaptationConfig` struct
3. Implement `AdaptiveColorManager` class with:
   - Profile initialization from current ColorTracker profiles
   - Monitoring functions
   - Basic range adjustment logic

### Phase 2: Monitoring System (Priority 2)
**Files:** `engine/src/AdaptiveColorManager.cpp`

1. Implement `monitorFrame()` - track success/failure per color
2. Implement rolling window statistics
3. Add logging for tracking metrics

### Phase 3: Range Adjustment (Priority 3)
**Files:** `engine/src/AdaptiveColorManager.cpp`

1. Implement `expandRange()` - increase range for failing colors
2. Implement `contractRange()` - decrease range for succeeding colors
3. Implement `shiftCenterToward()` - move center based on observed hues
4. Add safety limits and bounds checking

### Phase 4: Conflict Resolution (Priority 4)
**Files:** `engine/src/AdaptiveColorManager.cpp`

1. Implement `calculateOverlap()` - detect range overlaps
2. Implement `resolveConflicts()` - separate overlapping ranges
3. Implement `ensureMinimumSeparation()` - maintain minimum gaps
4. Add wrap-around handling for hue circle

### Phase 5: Integration (Priority 5)
**Files:** `engine/src/DNNTracker.cpp`, `engine/include/DNNTracker.hpp`

1. Add `AdaptiveColorManager` member to `DNNTracker`
2. Modify `compute_color_dominance()` to use adaptive ranges
3. Add monitoring calls after color-based assignment
4. Add adjustment calls at appropriate intervals

### Phase 6: Visualization & Debugging (Priority 6)
**Files:** `hub/components/ui.py`

1. Add UI display for current color ranges
2. Show success rates per color
3. Visualize range adjustments in real-time
4. Add manual override controls

---

## Configuration Parameters

### Recommended Starting Values

```json
{
  "adaptive_color_config": {
    "enabled": true,
    "history_window_size": 60,
    "success_threshold": 0.7,
    "failure_threshold": 0.3,
    "expansion_step": 2.0,
    "contraction_step": 1.0,
    "shift_step": 1.0,
    "frames_between_adjustments": 30,
    "min_separation": 5.0,
    "max_range_width": 40.0,
    "min_range_width": 10.0
  }
}
```

### Tuning Guidelines

- **Increase `expansion_step`** if ranges expand too slowly
- **Decrease `frames_between_adjustments`** for faster adaptation (but less stable)
- **Increase `min_separation`** if colors interfere with each other
- **Adjust thresholds** based on juggling speed and lighting conditions

---

## Expected Behavior

### Scenario 1: Pink Ball Not Detected

**Initial State:**
- Pink range: 140-175 (35 degrees)
- Green range: 45-75 (30 degrees)
- Pink ball actual hue: 135 (below range)

**Adaptive Response:**
1. Frame 1-30: Pink unmatched, green tracked → Pink success_rate drops
2. Frame 30: System expands pink range: 138-177 (39 degrees)
3. Frame 31-60: Pink still unmatched → Pink success_rate = 0%
4. Frame 60: System expands pink further: 135-180 (45 degrees, clamped to max)
5. Frame 61: Pink ball NOW DETECTED (hue 135 is in range)
6. Frame 61-90: Pink tracked consistently → Pink success_rate rises
7. Frame 90: System contracts pink slightly: 136-179 (43 degrees)
8. Steady state: Pink range stabilizes around actual ball color

### Scenario 2: Green and Pink Interfering

**Initial State:**
- Pink range: 140-175
- Green range: 45-75
- Both balls tracked successfully

**Adaptive Response:**
1. Both colors have high success rates
2. System contracts both ranges slightly
3. Pink: 142-173 (31 degrees)
4. Green: 47-73 (26 degrees)
5. More "space" available for other colors
6. Tighter ranges = more precise matching

### Scenario 3: Three Colors Active

**Initial State:**
- Green: 45-75 (tracked)
- Orange: 5-20 (not tracked)
- Pink: 140-175 (tracked)

**Adaptive Response:**
1. Orange unmatched → Expand orange: 3-22
2. Green tracked → Contract green: 47-73
3. Orange still unmatched → Expand more: 0-25
4. Green shifts away from orange (if needed)
5. Orange eventually captures its ball
6. All three stabilize with optimal ranges

---

## Safety Mechanisms

### 1. Bounded Adjustments
- Ranges cannot exceed `max_range_width` (prevents one color from dominating)
- Ranges cannot shrink below `min_range_width` (prevents loss of detection)

### 2. Minimum Separation
- Colors must maintain `min_separation` degrees apart
- Prevents complete overlap and ambiguity

### 3. Gradual Changes
- Adjustments happen in small steps (`expansion_step`, `contraction_step`)
- Prevents sudden, destabilizing changes

### 4. Adjustment Frequency Limit
- Only adjust every N frames (`frames_between_adjustments`)
- Allows system to stabilize between adjustments

### 5. Manual Override
- User can disable adaptive system
- User can manually set ranges (overrides adaptive)
- User can reset to defaults

---

## Performance Considerations

### Computational Cost
- **Monitoring**: O(N) per frame where N = number of enabled colors (negligible)
- **Adjustment**: O(N²) every M frames for conflict resolution (still fast)
- **Overall Impact**: < 1% CPU overhead

### Memory Usage
- Rolling window: 60 frames × 8 colors × 1 byte = 480 bytes
- Observed hues: 30 samples × 8 colors × 4 bytes = 960 bytes
- **Total**: ~2KB additional memory (negligible)

### Stability
- System converges within 3-5 seconds (180-300 frames at 60fps)
- Once converged, minimal adjustments needed
- Robust to temporary occlusions or lighting changes

---

## Testing Strategy

### Unit Tests
1. Test range expansion/contraction logic
2. Test conflict resolution with known overlaps
3. Test wrap-around handling for red/pink
4. Test bounds enforcement

### Integration Tests
1. Test with simulated tracking data (known success/failure patterns)
2. Test convergence speed with different parameters
3. Test stability under varying conditions

### Real-World Tests
1. Test with actual juggling (3 balls, different colors)
2. Test with challenging lighting conditions
3. Test with balls that have similar colors
4. Measure reduction in unmatched detections

---

## Success Metrics

### Primary Metrics
1. **Unmatched Detection Rate**: Should approach 0% after convergence
2. **Tracking Consistency**: All enabled colors should maintain >90% success rate
3. **Convergence Time**: System should stabilize within 5 seconds

### Secondary Metrics
1. **Range Stability**: Ranges should not oscillate after convergence
2. **Color Separation**: Minimum separation maintained at all times
3. **Adaptation Speed**: System responds to new balls within 2 seconds

---

## Future Enhancements

### 1. Learning from History
- Save optimal ranges for specific ball sets
- Auto-load ranges when same balls detected

### 2. Multi-Modal Distributions
- Handle balls with multiple color peaks (e.g., striped balls)
- Use Gaussian mixture models instead of simple ranges

### 3. Saturation/Value Adaptation
- Currently only adapts hue ranges
- Could also adapt saturation and value thresholds

### 4. Predictive Adjustment
- Use velocity and trajectory to predict which color will be needed next
- Pre-expand ranges for anticipated balls

---

## Conclusion

The Adaptive Color Range Adjustment System provides a **robust, automatic solution** to the color tracking problem. By continuously monitoring tracking success and dynamically adjusting HSV ranges, the system:

✅ **Eliminates manual tuning** - No need to manually adjust color ranges  
✅ **Minimizes unmatched detections** - System automatically finds optimal ranges  
✅ **Handles varying conditions** - Adapts to different lighting and ball colors  
✅ **Maintains stability** - Converges quickly and remains stable  
✅ **Prevents interference** - Ensures colors don't overlap or conflict  

**Next Steps:**
1. Review and approve this design
2. Implement Phase 1 (Core Infrastructure)
3. Test with simulated data
4. Integrate with existing tracking system
5. Test with real juggling scenarios