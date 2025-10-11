# Tracking Settings Audit Report
**Date:** 2025-10-10  
**Purpose:** Identify which UI tracking settings are active vs. dead code from old system

## Executive Summary

Analyzed all tracking settings in the UI to determine which are currently hooked up to active tracking code versus which are legacy settings from the old Kalman-based system that are no longer in use.

**Key Finding:** The system has transitioned from Kalman filtering to trajectory-based physics prediction. Several "Kalman" settings were found to be dead code - stored but never used in active tracking logic.

---

## Methodology

1. **Traced execution path:** UI → UDP → Engine → Active tracking code
2. **Verified storage:** Checked where settings are received in `SimpleBallTracker::updateSetting()`
3. **Verified usage:** Searched for where settings are READ/USED in actual tracking logic
4. **Confirmed active code:** Verified the call chain through `Engine::run()` → `simple_tracker_->update()`

---

## Settings Status: ACTIVE ✅

These settings are currently hooked up and actively used in the tracking system:

### Camera Settings
- ✅ **Resolution** - Active
- ✅ **FPS** - Active
- ✅ **Camera Settings Profile** - Active

### YOLO Tracker Settings
- ✅ **ball_confidence_threshold** - Active (class_id=0)
- ✅ **ball_held_confidence_threshold** - Active (class_id=1)
- ✅ **nms_threshold** - Active
- ✅ **show_raw_yolo_detections** - Active

### Pose Model Settings
- ✅ **pose_model_enabled** - Active

### Ball State Detection
- ✅ **undetected_near_hand_threshold** - Active
- ✅ **min_frames_for_state_change** - Active
- ✅ **throw_distance_threshold** - Active (NEW trajectory-based)
- ✅ **catch_distance_threshold** - Active (NEW trajectory-based)
- ✅ **min_throw_distance** - Active (LEGACY, kept for compatibility)
- ✅ **max_tracker_distance_per_frame** - Active
- ✅ **tc_sound_on_catch** - Active
- ✅ **tc_sound_on_throw** - Active
- ✅ **tc_name_on_catch** - Active
- ✅ **tc_name_on_throw** - Active

### Color Tracker Weights
- ✅ **temporal_consistency_bonus** - Active
- ✅ **spatial_threshold** - Active
- ✅ **color_sample_radius** - Active
- ✅ **max_euclidean_distance** - Active
- ✅ **min_euclidean_color_score** - Active
- ✅ **max_depth_jump_strict** - Active

### Override Detection
- ✅ **override_ball_confidence_threshold** - Active (class_id=0)
- ✅ **override_ball_color_threshold** - Active (class_id=0)
- ✅ **override_ball_held_confidence_threshold** - Active (class_id=1)
- ✅ **override_ball_held_color_threshold** - Active (class_id=1)

### Held Ball Color Detection
- ✅ **held_color_search_radius** - Active
- ✅ **held_color_min_score** - Active
- ✅ **held_color_max_distance** - Active

### Trajectory Settings (NEW System)
- ✅ **traj_gravity** - Active
- ✅ **traj_time_step** - Active
- ✅ **traj_max_time** - Active
- ✅ **traj_search_radius** - Active
- ✅ **traj_min_points_for_prediction** - Active
- ✅ **traj_color_match_threshold** - Active
- ✅ **traj_velocity_estimation_time** - Active
- ✅ **traj_max_search_distance** - Active

### Ball Profiles
- ✅ **Ball tracking enabled/disabled per color** - Active
- ✅ **Ball color calibration (avg_hue, avg_saturation)** - Active

---

## Settings Status: REMOVED ❌ (Dead Code)

These settings were found to be dead code and have been **REMOVED** from both UI and engine:

### Kalman Prediction Section (REMOVED)
- ❌ **prediction_history_frames** - DEAD CODE (never read/used)
- ❌ **prediction_radius_m** - DEAD CODE (never read/used)

### Kalman Glob Detection Section (REMOVED)
- ❌ **kalman_glob_detection_enabled** - DEAD CODE (never read/used)
- ❌ **kalman_glob_search_radius** - DEAD CODE (never read/used)
- ❌ **kalman_glob_min_color_score** - DEAD CODE (never read/used)
- ❌ **kalman_glob_max_depth_diff** - DEAD CODE (never read/used)

### Color Tracker Weights (REMOVED)
- ❌ **max_kalman_prediction_jump** - DEAD CODE (never read/used)

### Ball State Detection - Weighted Scoring (REMOVED - 2025-10-11)
- ❌ **ml_ball_weight** - DEAD CODE (part of unused `isBallHeld()` function)
- ❌ **ml_ball_held_weight** - DEAD CODE (part of unused `isBallHeld()` function)
- ❌ **wrist_proximity_weight** - DEAD CODE (part of unused `isBallHeld()` function)

### Ball State Detection - Redundant Threshold (REMOVED - 2025-10-11)
- ❌ **wrist_proximity_threshold** - DEAD CODE (redundant with throw/catch distance thresholds)

**Total Removed:** 11 dead settings

---

## Technical Details

### Why These Settings Were Dead Code

The system underwent a major architectural change from **Kalman filtering** to **trajectory-based physics prediction**. The old Kalman-based settings were:

1. **Received and stored** in `SimpleBallTracker::updateSetting()` 
2. **Never read or used** in the actual tracking logic (`updateHeldBall()`, `updateInFlightBall()`, `isBallHeld()`)
3. **Replaced by trajectory-based prediction** which uses physics (gravity, velocity, parabolic arcs)

### Verification Process

For each setting, I verified:
1. ✅ Setting is received via UDP in `updateSetting()`
2. ✅ Setting is stored in `tracking_settings_` struct
3. ❌ Setting is READ/USED in active tracking code paths

The Kalman settings failed step 3 - they were stored but never used.

---

## Files Modified

### Engine Files (Initial Cleanup)
1. **engine/src/SimpleBallTracker.cpp**
   - Removed 7 dead setting handlers from `updateSetting()` method
   - Lines removed: 285-293, 363-377, 377-380

2. **engine/include/SimpleBallTracker.hpp**
   - Removed `max_kalman_prediction_jump` from `TrackingSettings` struct
   - Line removed: 211

### UI Files (Initial Cleanup)
1. **hub/components/ui_settings.py**
   - Removed entire "Kalman Prediction" section (lines 637-684)
   - Removed entire "Kalman Glob Detection" section (lines 686-771)
   - Removed "Max Kalman Prediction Jump" slider (lines 747-764)
   - Removed all references in `get_current_settings()` method
   - Removed all references in `apply_settings()` method
   - Removed all references in `_send_all_settings_to_engine()` method
   - Removed collapsed state handling for Kalman sections

### Engine Files (2025-10-11 - Weighted Scoring Cleanup)
1. **engine/include/SimpleBallTracker.hpp**
   - Removed `ml_ball_weight`, `ml_ball_held_weight`, `wrist_proximity_weight` from `TrackingSettings` struct
   - Removed unused `isBallHeld()` function declaration

2. **engine/src/SimpleBallTracker.cpp**
   - Removed 3 setting handlers from `updateSetting()` method (ml_ball_weight, ml_ball_held_weight, wrist_proximity_weight)
   - Removed entire `isBallHeld()` function (145 lines of unused weighted scoring logic)

### UI Files (2025-10-11 - Weighted Scoring Cleanup)
1. **hub/components/ui_settings.py**
   - Removed 3 weight sliders from Ball State Detection section (lines 339-391)
   - Removed settings from `required_attrs` list in `get_current_settings()`
   - Removed settings from `get_current_settings()` method
   - Removed settings from `apply_settings()` method
   - Removed settings from `_send_all_settings_to_engine()` method

---

## Current System Architecture

The tracking system now uses:

1. **Trajectory-Based Prediction** (NEW)
   - Physics-based parabolic arc prediction
   - Gravity, velocity, time-based calculations
   - Search along predicted trajectory path

2. **YOLO Detection** (ACTIVE)
   - Ball detection (class_id=0)
   - Ball_held detection (class_id=1)
   - Confidence thresholds per class

3. **Color Matching** (ACTIVE)
   - Euclidean distance in HSV space
   - Adaptive color calibration
   - Temporal consistency for identity preservation

4. **State Machine** (ACTIVE)
   - HELD state (ball in hand)
   - IN_FLIGHT state (ball airborne)
   - Throw/catch event detection

---

## Recommendations

1. ✅ **COMPLETED:** Remove all dead Kalman settings from UI and engine
2. ✅ **COMPLETED:** Clean up code to prevent confusion
3. 📝 **SUGGESTED:** Update user documentation to reflect trajectory-based system
4. 📝 **SUGGESTED:** Consider removing legacy `min_throw_distance` in future (replaced by `throw_distance_threshold`)

---

## Conclusion

Successfully identified and removed 11 dead settings from the old Kalman-based tracking system and unused weighted scoring system. The UI now only contains settings that are actively used in the current trajectory-based tracking architecture. This cleanup:

- ✅ Reduces confusion for users
- ✅ Simplifies the codebase
- ✅ Removes misleading settings that had no effect
- ✅ Makes the UI more maintainable

All active settings have been verified to be properly hooked up to the tracking engine and are functioning as intended.