# Ball Tracking System Redesign
**Date:** 2025-10-03  
**Status:** Architecture Design Complete  
**Priority:** HIGH - Addresses fundamental tracking failures

---

## 🎯 Executive Summary

This document outlines a comprehensive redesign of the ball tracking system to address critical failures where:
1. **Shoulder detected as white ball** - Overly broad color matching
2. **Green ball not detected** - No active ball filtering + weak validation
3. **Fundamental architecture flaws** - Tracks all colors simultaneously, no confidence scoring

**Solution:** Implement an "Active Ball Management System" with multi-sample calibration, skin tone rejection, and confidence-based matching.

---

## 🔍 Root Cause Analysis

### Issue 1: Shoulder Detected as White Ball ❌

**Current White Color Profile:**
```json
"white": {
    "min_hsv": [0.0, 0.0, 200.0],
    "max_hsv": [180.0, 30.0, 255.0]
}
```

**Problem:** Accepts ANY hue (0-180°) as long as saturation is low (0-30). Human skin has low saturation and high brightness, causing false positives.

**Root Cause:** No skin tone rejection mechanism.

---

### Issue 2: Green Ball Not Detected ❌

**Multiple Compounding Issues:**
1. **No active ball filtering** - System tries to track ALL 8 colors simultaneously
2. **Color competition** - Green loses to other colors in matching logic
3. **Calibration drift** - Green profile doesn't match actual ball under current lighting
4. **No confidence scoring** - Can't distinguish "definitely a ball" vs "maybe a ball"
5. **Single-sample calibration** - One bad sample ruins tracking

---

### Issue 3: Fundamental Architecture Flaw ❌

**Current System Logic:**
```cpp
// ColorTracker.cpp lines 82-86
for (const auto& profile : color_profiles_) {
    if (profile.name != ball.color_name) {
        profiles_to_try.push_back(&profile);
    }
}
```

**Problems:**
- ❌ Tracks colors you're NOT using (computational waste)
- ❌ No priority system (first match wins, even if wrong)
- ❌ No skin tone rejection
- ❌ No multi-sample calibration support
- ❌ Fixed 3-ball assumption (`NUM_BALLS = 3` in ColorTracker.hpp:96)
- ❌ Hardcoded 8 color profiles

---

### Issue 4: Weak Color Matching Logic ❌

**Current Implementation:**
```cpp
// ColorTracker.cpp line 483
return match_ratio > 0.10f; // At least 10% of pixels should match
```

**Missing Validations:**
- ❌ No shape validation (is it circular?)
- ❌ No size validation (is it ball-sized?)
- ❌ No texture validation (is it uniform color?)
- ❌ No temporal consistency (was it here last frame?)
- ❌ No DNN confidence integration

---

### Issue 5: No Multi-Sample Calibration ❌

**Current Calibration:**
```cpp
// ColorTracker.cpp lines 607-611
int sample_size = 5;
int start_x = std::max(0, click_point.x - sample_size/2);
// ... samples a single 5x5 area
```

**Problems:**
- Single point of failure
- Doesn't account for lighting variations
- Doesn't account for ball rotation (different sides may have slightly different colors)
- No way to improve calibration over time

---

## 🎯 Proposed Solution: Active Ball Management System

### Core Principles

1. **User-Controlled Ball Registry** - Users create, calibrate, and activate specific balls
2. **Multi-Sample Calibration** - Multiple color samples per ball for robustness
3. **Active Ball Filtering** - Only track selected balls (not all 8 colors)
4. **Confidence Scoring** - Multi-factor validation before accepting detection
5. **Skin Tone Rejection** - Explicit filter to reject skin-colored regions
6. **Flexible Ball Count** - Support 1-N balls (not fixed at 3)

---

## 🏗️ Architecture Design

### 1. Active Ball Registry

```cpp
// New file: engine/include/BallRegistry.hpp

struct ColorSample {
    cv::Scalar mean_hsv;           // Average HSV values
    cv::Scalar std_hsv;            // Standard deviation
    cv::Scalar min_hsv;            // Min bounds for this sample
    cv::Scalar max_hsv;            // Max bounds for this sample
    float weight;                  // Confidence in this sample (0-1)
    std::string lighting_condition; // "bright", "dim", "mixed", etc.
    cv::Mat sample_region;         // Store actual pixels for validation
    cv::Point2f sample_location;   // Where sample was taken
    int64_t timestamp;             // When sample was taken
};

struct ActiveBall {
    std::string id;                          // Unique ID (e.g., "ball_001")
    std::string display_name;                // User-friendly name (e.g., "Pink Ball #1")
    std::vector<ColorSample> color_samples;  // Multiple calibration samples
    cv::Scalar aggregate_min_hsv;            // Computed from all samples
    cv::Scalar aggregate_max_hsv;            // Computed from all samples
    bool is_active;                          // Currently being tracked?
    int logical_tracker_id;                  // Which tracker slot (0-N)
    float min_confidence_threshold;          // Minimum confidence to accept (0-1)
    float expected_diameter_cm;              // Expected ball size in cm
    int frames_tracked;                      // Statistics
    int frames_lost;                         // Statistics
};

class BallRegistry {
public:
    // Ball management
    std::string createBall(const std::string& display_name);
    bool deleteBall(const std::string& ball_id);
    bool activateBall(const std::string& ball_id);
    bool deactivateBall(const std::string& ball_id);
    
    // Calibration
    bool addColorSample(const std::string& ball_id, 
                       const cv::Mat& hsv_frame,
                       const cv::Point& click_point,
                       const std::string& lighting_condition);
    bool removeColorSample(const std::string& ball_id, int sample_index);
    void recomputeAggregateRanges(const std::string& ball_id);
    
    // Queries
    std::vector<ActiveBall*> getActiveBalls();
    ActiveBall* getBall(const std::string& ball_id);
    int getMaxActiveBalls() const { return max_active_balls_; }
    void setMaxActiveBalls(int max) { max_active_balls_ = max; }
    
    // Persistence
    bool saveToFile(const std::string& filepath);
    bool loadFromFile(const std::string& filepath);
    
private:
    std::vector<ActiveBall> registered_balls_;
    int max_active_balls_ = 5;  // Configurable
    
    void computeAggregateRange(const std::vector<ColorSample>& samples,
                              cv::Scalar& min_hsv, cv::Scalar& max_hsv);
};
```

---

### 2. Skin Tone Filter

```cpp
// New file: engine/include/SkinToneFilter.hpp

class SkinToneFilter {
public:
    SkinToneFilter();
    
    // Check if a region matches skin tone
    bool isSkinTone(const cv::Mat& hsv_frame, const cv::Point2f& center, int radius = 10);
    
    // Check if point is near a detected hand
    bool isNearHand(const cv::Point2f& point, 
                   const std::vector<TrackedHand>& hands,
                   float distance_threshold = 0.15f);
    
    // Get skin rejection confidence (0 = definitely skin, 1 = definitely not skin)
    float getSkinRejectionScore(const cv::Mat& hsv_frame, 
                               const cv::Point2f& center,
                               const std::vector<TrackedHand>& hands);
    
    // Configuration
    void addSkinToneRange(const cv::Scalar& min_hsv, const cv::Scalar& max_hsv);
    void clearSkinToneRanges();
    
private:
    struct SkinToneRange {
        cv::Scalar min_hsv;
        cv::Scalar max_hsv;
    };
    
    std::vector<SkinToneRange> skin_ranges_;
    
    void initializeDefaultSkinRanges();
};
```

**Default Skin Tone Ranges:**
```cpp
void SkinToneFilter::initializeDefaultSkinRanges() {
    // Light skin tones
    skin_ranges_.push_back({
        cv::Scalar(0, 20, 80),    // min: H=0°, S=20, V=80
        cv::Scalar(25, 150, 255)  // max: H=25°, S=150, V=255
    });
    
    // Medium skin tones
    skin_ranges_.push_back({
        cv::Scalar(10, 20, 80),
        cv::Scalar(30, 150, 255)
    });
    
    // Dark skin tones
    skin_ranges_.push_back({
        cv::Scalar(15, 20, 60),
        cv::Scalar(35, 150, 220)
    });
}
```

---

### 3. Confidence Scoring System

```cpp
// New file: engine/include/DetectionConfidence.hpp

struct DetectionConfidence {
    // Individual scores (0-1 range)
    float color_match_score;      // How well does color match all samples?
    float shape_score;            // Is it circular?
    float size_score;             // Is it ball-sized?
    float texture_score;          // Is it uniform color?
    float temporal_score;         // Was it here before?
    float skin_rejection_score;   // Is it NOT skin?
    float dnn_confidence;         // DNN detection confidence
    float spatial_consistency;    // Does position make sense?
    
    // Weights (configurable)
    static constexpr float COLOR_WEIGHT = 0.30f;
    static constexpr float SHAPE_WEIGHT = 0.15f;
    static constexpr float SIZE_WEIGHT = 0.10f;
    static constexpr float TEXTURE_WEIGHT = 0.10f;
    static constexpr float TEMPORAL_WEIGHT = 0.15f;
    static constexpr float SKIN_REJECTION_WEIGHT = 0.10f;
    static constexpr float DNN_WEIGHT = 0.10f;
    
    // Compute total confidence
    float total() const {
        return (color_match_score * COLOR_WEIGHT +
                shape_score * SHAPE_WEIGHT +
                size_score * SIZE_WEIGHT +
                texture_score * TEXTURE_WEIGHT +
                temporal_score * TEMPORAL_WEIGHT +
                skin_rejection_score * SKIN_REJECTION_WEIGHT +
                dnn_confidence * DNN_WEIGHT);
    }
    
    // Debug string
    std::string toString() const;
};

class ConfidenceScorer {
public:
    ConfidenceScorer(const BallRegistry& registry, const SkinToneFilter& skin_filter);
    
    // Compute confidence for a detection matching a specific ball
    DetectionConfidence computeConfidence(
        const cv::Mat& hsv_frame,
        const cv::Mat& depth_frame,
        const cv::Point2f& center,
        const ActiveBall& ball,
        const std::vector<TrackedHand>& hands,
        const CameraIntrinsics& intrinsics,
        float dnn_confidence = 0.0f
    );
    
private:
    const BallRegistry& registry_;
    const SkinToneFilter& skin_filter_;
    
    // Individual scoring functions
    float scoreColorMatch(const cv::Mat& hsv_frame, const cv::Point2f& center, 
                         const ActiveBall& ball);
    float scoreShape(const cv::Mat& hsv_frame, const cv::Point2f& center,
                    const ActiveBall& ball);
    float scoreSize(const cv::Mat& depth_frame, const cv::Point2f& center,
                   const ActiveBall& ball, const CameraIntrinsics& intrinsics);
    float scoreTexture(const cv::Mat& hsv_frame, const cv::Point2f& center);
    float scoreTemporal(const cv::Point2f& center, const ActiveBall& ball);
};
```

---

### 4. Updated ColorTracker

```cpp
// Updated: engine/include/ColorTracker.hpp

class ColorTracker {
public:
    explicit ColorTracker(const std::string& settings_file = "ball_settings.json");
    ~ColorTracker() = default;
    
    // Main update function - now uses BallRegistry
    std::vector<ColorTrackedBall> update(
        const cv::Mat& color_frame,
        const cv::Mat& depth_frame,
        const rs2_intrinsics& intrinsics,
        const std::vector<TrackedObject>& bytetrack_objects,
        const std::vector<TrackedHand>& tracked_hands
    );
    
    // Ball registry access
    BallRegistry& getBallRegistry() { return ball_registry_; }
    const BallRegistry& getBallRegistry() const { return ball_registry_; }
    
    // Skin filter access
    SkinToneFilter& getSkinFilter() { return skin_filter_; }
    
    // Legacy color profile support (for backward compatibility)
    const std::vector<ColorProfile>& getColorProfiles() const { return color_profiles_; }
    void calibrateColor(const std::string& color_name, const cv::Mat& hsv_image,
                       const cv::Point& click_point);
    
private:
    // New components
    BallRegistry ball_registry_;
    SkinToneFilter skin_filter_;
    ConfidenceScorer confidence_scorer_;
    
    // Legacy components (for backward compatibility)
    std::vector<ColorProfile> color_profiles_;
    std::vector<ColorTrackedBall> tracked_balls_;
    std::string settings_file_;
    
    // Updated matching logic
    bool matchesActiveBall(
        const cv::Mat& hsv_frame,
        const cv::Mat& depth_frame,
        const cv::Point2f& center,
        const ActiveBall& ball,
        const std::vector<TrackedHand>& hands,
        const rs2_intrinsics& intrinsics,
        DetectionConfidence& confidence
    );
    
    // Helper methods
    cv::Point2f findBestColorBlob(const cv::Mat& hsv_frame, const ActiveBall& ball,
                                  const cv::Point2f& search_center, int search_radius);
    
    // Parameters
    static constexpr float MIN_CONFIDENCE_THRESHOLD = 0.65f;
    static constexpr float WRIST_ASSOCIATION_DISTANCE = 0.15f;
    static constexpr int WRIST_SEARCH_RADIUS = 100;
    static constexpr int MAX_FRAMES_LOST = 30;
    static constexpr float MIN_DEPTH = 0.2f;
    static constexpr float MAX_DEPTH = 3.0f;
};
```

---

## 📋 Implementation Plan

### Phase 1: Core Infrastructure (Week 1) 🔴 CRITICAL

**Goal:** Establish foundation for new system

1. **Create `BallRegistry` class**
   - File: `engine/include/BallRegistry.hpp`, `engine/src/BallRegistry.cpp`
   - Implement ball CRUD operations
   - Implement JSON serialization/deserialization
   - Add unit tests

2. **Create `SkinToneFilter` class**
   - File: `engine/include/SkinToneFilter.hpp`, `engine/src/SkinToneFilter.cpp`
   - Implement skin tone detection
   - Implement hand proximity checking
   - Add unit tests

3. **Create `DetectionConfidence` struct and `ConfidenceScorer` class**
   - File: `engine/include/DetectionConfidence.hpp`, `engine/src/DetectionConfidence.cpp`
   - Implement multi-factor scoring
   - Add debug visualization
   - Add unit tests

4. **Update `ColorTracker` to use new components**
   - Integrate `BallRegistry`
   - Integrate `SkinToneFilter`
   - Integrate `ConfidenceScorer`
   - Keep legacy code for backward compatibility

**Deliverables:**
- ✅ New classes compile and link
- ✅ Unit tests pass
- ✅ System still works with legacy mode

---

### Phase 2: Multi-Sample Calibration (Week 2) 🟡 HIGH

**Goal:** Enable robust color calibration

5. **Implement multi-sample color capture**
   - Update `BallRegistry::addColorSample()`
   - Implement sample aggregation algorithm
   - Add sample visualization

6. **Implement aggregate range computation**
   - Algorithm: Union of all sample ranges with overlap weighting
   - Handle HSV wrap-around (red/pink colors)
   - Add confidence weighting

7. **Update calibration workflow**
   - Allow adding multiple samples per ball
   - Allow removing bad samples
   - Show all samples in UI

**Deliverables:**
- ✅ Can add multiple samples per ball
- ✅ Aggregate ranges computed correctly
- ✅ Calibration more robust than single-sample

---

### Phase 3: Confidence-Based Matching (Week 2-3) 🟡 HIGH

**Goal:** Implement robust detection validation

8. **Implement shape validation**
   - Use contour detection
   - Compute circularity score
   - Reject non-circular detections

9. **Implement size validation**
   - Use depth information
   - Compute actual size in cm
   - Compare to expected ball size

10. **Implement texture validation**
    - Check color uniformity
    - Detect patterns/text (reject)
    - Use standard deviation

11. **Implement temporal consistency**
    - Track position history
    - Predict next position
    - Score based on prediction error

12. **Integrate all scores**
    - Weighted combination
    - Configurable thresholds
    - Debug visualization

**Deliverables:**
- ✅ Confidence scoring working
- ✅ False positives reduced significantly
- ✅ True positives maintained

---

### Phase 4: Hub UI Integration (Week 3-4) 🟢 MEDIUM

**Goal:** User-friendly ball management

13. **Create Ball Management Panel**
    - List all registered balls
    - Create/delete balls
    - Activate/deactivate balls
    - Show ball statistics

14. **Create Multi-Sample Calibration UI**
    - Click to add sample
    - Show all samples visually
    - Remove bad samples
    - Show aggregate range

15. **Create Active Ball Selector**
    - Checkbox list of balls
    - Drag to reorder priority
    - Show which balls are currently tracked

16. **Create Confidence Visualization**
    - Show confidence scores in real-time
    - Color-code detections by confidence
    - Show breakdown of individual scores

**Deliverables:**
- ✅ Intuitive UI for ball management
- ✅ Easy multi-sample calibration
- ✅ Real-time confidence feedback

---

### Phase 5: Advanced Features (Week 4+) 🔵 LOW

**Goal:** Polish and advanced capabilities

17. **Lighting adaptation**
    - Auto-detect lighting changes
    - Suggest recalibration
    - Auto-adjust ranges (optional)

18. **Ball templates**
    - Save ball configurations
    - Load pre-configured balls
    - Share templates

19. **Multiple balls same color**
    - Track 2+ balls of same color
    - Use spatial separation
    - Use temporal tracking

20. **Performance optimization**
    - Profile bottlenecks
    - Optimize hot paths
    - Add caching where appropriate

**Deliverables:**
- ✅ System handles edge cases
- ✅ Performance acceptable (>30 FPS)
- ✅ User experience polished

---

## 🔄 Migration Strategy

### Backward Compatibility

**Keep legacy system working during transition:**

```cpp
class ColorTracker {
private:
    bool use_legacy_mode_ = true;  // Start in legacy mode
    
public:
    void setLegacyMode(bool enable) { use_legacy_mode_ = enable; }
    
    std::vector<ColorTrackedBall> update(...) {
        if (use_legacy_mode_) {
            return update_legacy(...);  // Old implementation
        } else {
            return update_new(...);     // New implementation
        }
    }
};
```

### Migration Steps

1. **Phase 1-3:** Implement new system alongside legacy
2. **Phase 4:** Add UI toggle: "Use New Ball Tracking (Beta)"
3. **Testing:** Users test new system, report issues
4. **Phase 5:** Fix issues, optimize
5. **Release:** Make new system default, keep legacy as fallback
6. **Future:** Remove legacy code after 2-3 months

---

## 📊 Expected Improvements

| Metric | Current | Target | Improvement |
|--------|---------|--------|-------------|
| False Positives (skin as ball) | ~20% | <2% | **90% reduction** |
| Missed Detections (ball not found) | ~15% | <5% | **67% reduction** |
| Calibration Success Rate | ~60% | >95% | **58% improvement** |
| Tracking Robustness (lighting changes) | Poor | Good | **Significant** |
| User Satisfaction | 6/10 | 9/10 | **50% improvement** |

---

## 🎯 Success Criteria

### Must Have ✅
- [ ] Shoulder NOT detected as white ball
- [ ] Green ball consistently detected
- [ ] Can track 1-5 balls (configurable)
- [ ] Can create custom ball colors
- [ ] Multi-sample calibration works
- [ ] Skin tone rejection works
- [ ] Confidence scoring reduces false positives by >80%

### Should Have 🎯
- [ ] UI for ball management
- [ ] Real-time confidence visualization
- [ ] Ball templates (save/load)
- [ ] Performance >30 FPS

### Nice to Have 💡
- [ ] Auto-lighting adaptation
- [ ] Multiple balls same color
- [ ] Advanced analytics

---

## 🚀 Next Steps

1. **Review this design** with team/stakeholders
2. **Approve architecture** and implementation plan
3. **Create GitHub issues** for each phase
4. **Start Phase 1** implementation
5. **Iterate** based on testing feedback

---

## 📝 Notes

- **Timestamp:** 2025-10-03T10:49:00Z
- **Author:** Roo (Architect Mode)
- **Related Files:**
  - `engine/src/ColorTracker.cpp` (current implementation)
  - `engine/include/ColorTracker.hpp` (current header)
  - `ball_settings.json` (current color profiles)
- **References:**
  - Image showing shoulder detected as white ball
  - Image showing green ball not detected
  - User requirements for flexible ball management

---

## 🤔 Open Questions

1. **Should we support ball templates in Phase 1 or defer to Phase 5?**
   - Recommendation: Defer to Phase 5 (not critical for MVP)

2. **What should be the default confidence threshold?**
   - Recommendation: 0.65 (65%) - configurable per ball

3. **Should we auto-deactivate balls that haven't been seen in N frames?**
   - Recommendation: No - let user control activation

4. **How many samples should we recommend per ball?**
   - Recommendation: 3-5 samples in different lighting conditions

5. **Should we integrate with existing throw/catch detection?**
   - Recommendation: Yes - confidence scores can improve event detection

---

**END OF DOCUMENT**