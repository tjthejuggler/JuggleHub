# Tracking System Settings Architecture

**Created:** 2025-10-16  
**Status:** Design Document  
**Purpose:** Define architecture for tracking-system-specific settings in JuggleHub UI

---

## Overview

This document describes the architecture for implementing dynamic, tracking-system-specific settings in the JuggleHub UI. The system will show/hide settings sections based on which tracking system is selected (3D depth-based vs 2D simple), with separate settings files per tracker.

## Current State Analysis

### Existing Tracking Systems

1. **SimpleBallTracker (3D Depth-Based)**
   - Location: [`engine/include/SimpleBallTracker.hpp`](engine/include/SimpleBallTracker.hpp)
   - Uses RealSense depth data for 3D tracking
   - Complex trajectory prediction with physics simulation
   - State machine (HELD/IN_FLIGHT)
   - Color matching and calibration
   - Hand velocity tracking
   - ~50+ configurable settings

2. **Simple2DBallTracker (2D Simple)**
   - Location: [`engine/include/Simple2DBallTracker.hpp`](engine/include/Simple2DBallTracker.hpp)
   - 2D-only tracking without depth processing
   - No trajectory prediction or physics
   - No state machine
   - No color matching
   - Minimal settings (~5 basic YOLO parameters)

### Current UI Structure

The settings UI is in [`hub/components/ui_settings.py`](hub/components/ui_settings.py) with these sections:

**Common Settings (Both Trackers):**
- 📷 Camera Settings (resolution, FPS, depth sensor toggle, tracking system selector)
- 🎯 YOLO Tracker Settings (confidence thresholds, NMS)
- 🧍 Pose Model Settings (enable/disable)

**3D-Only Settings (SimpleBallTracker):**
- 🎯 Ball State Detection (hand distance thresholds, state transitions)
- 🎯 Color Tracker Weights (temporal consistency, spatial thresholds)
- ⚡ Override Detection (confidence/color thresholds per class)
- 🤲 Held Ball Color Detection (search radius, color matching)
- 🎯 Trajectory Settings (gravity, time step, search parameters)
- 🤚 Hand Velocity Tracking (velocity thresholds, detection zones)
- 🎨 Ball Profiles (color calibration, HSV ranges)

---

## Design Requirements

### Functional Requirements

1. **Dynamic UI**: Show/hide settings sections based on selected tracking system
2. **Separate Persistence**: Each tracker has its own settings file
3. **Minimal 2D Settings**: Simple2DBallTracker only needs basic YOLO parameters
4. **Backward Compatibility**: Existing 3D settings files should continue to work
5. **Clean Organization**: Settings grouped logically by functionality
6. **No File Bloat**: Keep files modular and under 500 lines each

### User Experience Requirements

1. Switching trackers should be instant and clear
2. Settings should auto-save per tracker
3. Last-used settings should persist per tracker
4. UI should clearly indicate which tracker is active
5. Irrelevant settings should be completely hidden (not just disabled)

---

## Architecture Design

### 1. Settings File Structure

```
hub/config/
├── calibration_settings_3d.json      # 3D tracker settings
├── calibration_settings_2d.json      # 2D tracker settings
└── calibration_settings.json         # Legacy (backward compatibility)
```

**File Naming Convention:**
- `calibration_settings_3d.json` - SimpleBallTracker (depth-based 3D)
- `calibration_settings_2d.json` - Simple2DBallTracker (simple 2D)
- `calibration_settings.json` - Legacy file (maps to 3D for backward compatibility)

### 2. Settings Categories

#### Common Settings (Always Visible)

```python
COMMON_SETTINGS = [
    "camera_settings",      # Camera profile, resolution, FPS
    "depth_sensor",         # Enable/disable depth sensor
    "tracking_system",      # Tracker selection dropdown
    "yolo_settings",        # Ball/ball_held confidence, NMS
    "pose_settings",        # Enable/disable pose model
]
```

#### 3D-Specific Settings (SimpleBallTracker Only)

```python
TRACKER_3D_SETTINGS = [
    "ball_state_detection",      # Hand distance, state transitions
    "color_tracker_weights",     # Temporal consistency, spatial thresholds
    "override_detection",        # Override thresholds per class
    "held_color_blob",          # Color blob search for held balls
    "trajectory_settings",       # Physics, gravity, search parameters
    "hand_velocity_tracking",    # Velocity-based throw prediction
    "ball_profiles",            # Color calibration, HSV ranges
]
```

#### 2D-Specific Settings (Simple2DBallTracker Only)

```python
TRACKER_2D_SETTINGS = [
    # Currently minimal - just uses common YOLO settings
    # Future: Could add 2D-specific visualization or tracking parameters
]
```

### 3. UI Component Architecture

#### File Organization

```
hub/components/
├── ui_settings.py                    # Main settings widget (< 500 lines)
├── ui_settings_common.py             # Common settings sections
├── ui_settings_3d.py                 # 3D tracker-specific sections
├── ui_settings_2d.py                 # 2D tracker-specific sections (minimal)
└── ui_settings_manager.py            # Settings persistence & loading
```

#### Class Structure

```python
# ui_settings.py - Main container
class CalibrationSettingsWidget(QWidget):
    def __init__(self, ...):
        self.settings_manager = SettingsManager()
        self.common_sections = CommonSettingsSections(...)
        self.tracker_3d_sections = Tracker3DSettingsSections(...)
        self.tracker_2d_sections = Tracker2DSettingsSections(...)
        self.current_tracker = "depth_based"  # or "simple_2d"
        
    def on_tracking_system_changed(self):
        """Handle tracker selection change"""
        self.hide_all_tracker_sections()
        self.show_tracker_sections(self.current_tracker)
        self.settings_manager.switch_tracker(self.current_tracker)

# ui_settings_common.py - Common sections
class CommonSettingsSections:
    def create_camera_section(self) -> CollapsibleGroupBox: ...
    def create_yolo_section(self) -> CollapsibleGroupBox: ...
    def create_pose_section(self) -> CollapsibleGroupBox: ...

# ui_settings_3d.py - 3D-specific sections
class Tracker3DSettingsSections:
    def create_ball_state_section(self) -> CollapsibleGroupBox: ...
    def create_color_tracker_section(self) -> CollapsibleGroupBox: ...
    def create_override_detection_section(self) -> CollapsibleGroupBox: ...
    def create_held_color_blob_section(self) -> CollapsibleGroupBox: ...
    def create_trajectory_section(self) -> CollapsibleGroupBox: ...
    def create_hand_velocity_section(self) -> CollapsibleGroupBox: ...
    def create_ball_profiles_section(self) -> CollapsibleGroupBox: ...

# ui_settings_2d.py - 2D-specific sections (minimal)
class Tracker2DSettingsSections:
    # Currently empty - 2D tracker only uses common settings
    # Future: Add 2D-specific visualization or tracking parameters
    pass

# ui_settings_manager.py - Settings persistence
class SettingsManager:
    def __init__(self):
        self.settings_dir = "hub/config"
        self.tracker_files = {
            "depth_based": "calibration_settings_3d.json",
            "simple_2d": "calibration_settings_2d.json"
        }
        
    def load_settings(self, tracker_type: str) -> dict: ...
    def save_settings(self, tracker_type: str, settings: dict): ...
    def switch_tracker(self, new_tracker: str): ...
    def migrate_legacy_settings(self): ...
```

### 4. Dynamic Section Management

```python
class CalibrationSettingsWidget(QWidget):
    def init_ui(self):
        # Create scroll area
        self.scroll_area = QScrollArea()
        self.container = QWidget()
        self.layout = QVBoxLayout(self.container)
        
        # Add common sections (always visible)
        self.common_sections = self.create_common_sections()
        for section in self.common_sections:
            self.layout.addWidget(section)
        
        # Create tracker-specific sections (hidden by default)
        self.tracker_3d_sections = self.create_3d_sections()
        self.tracker_2d_sections = self.create_2d_sections()
        
        # Initially hide all tracker sections
        self.hide_all_tracker_sections()
        
        # Show sections for current tracker
        self.show_tracker_sections(self.current_tracker)
        
    def hide_all_tracker_sections(self):
        """Hide all tracker-specific sections"""
        for section in self.tracker_3d_sections:
            section.setVisible(False)
        for section in self.tracker_2d_sections:
            section.setVisible(False)
    
    def show_tracker_sections(self, tracker_type: str):
        """Show sections for specified tracker"""
        if tracker_type == "depth_based":
            for section in self.tracker_3d_sections:
                section.setVisible(True)
        elif tracker_type == "simple_2d":
            for section in self.tracker_2d_sections:
                section.setVisible(True)
    
    def on_tracking_system_changed(self):
        """Handle tracker selection change"""
        new_tracker = self.tracking_system_combo.currentData()
        
        # Save current tracker settings before switching
        self.settings_manager.save_settings(
            self.current_tracker,
            self.get_current_settings()
        )
        
        # Switch tracker
        self.current_tracker = new_tracker
        
        # Hide all sections
        self.hide_all_tracker_sections()
        
        # Show new tracker sections
        self.show_tracker_sections(new_tracker)
        
        # Load new tracker settings
        settings = self.settings_manager.load_settings(new_tracker)
        self.apply_settings(settings)
        
        # Send tracker switch command to engine
        self.send_tracker_switch_command(new_tracker)
```

### 5. Settings Persistence Strategy

#### Settings File Format

**3D Tracker Settings (`calibration_settings_3d.json`):**
```json
{
  "tracker_type": "depth_based",
  "saved_at": "2025-10-16T14:00:00.000Z",
  
  "common": {
    "camera_settings_profile": "default",
    "resolution": "640 x 480",
    "fps": 60,
    "depth_sensor_enabled": true,
    "ball_confidence_threshold": 0.25,
    "ball_held_confidence_threshold": 0.25,
    "nms_threshold": 0.5,
    "pose_model_enabled": true
  },
  
  "tracker_3d": {
    "hand_distance_threshold": 0.25,
    "min_frames_for_state_change": 2,
    "temporal_consistency_bonus": 0.25,
    "spatial_threshold": 0.40,
    "override_ball_confidence_threshold": 0.70,
    "override_ball_color_threshold": 0.80,
    "traj_gravity": 9.81,
    "traj_search_radius": 0.15,
    "hand_velocity_enabled": true,
    "hand_velocity_threshold": 1.0,
    "ball_profiles": {
      "red": {"enabled": true, "min_hsv": [0, 100, 100], "max_hsv": [10, 255, 255]},
      "green": {"enabled": true, "min_hsv": [40, 100, 100], "max_hsv": [80, 255, 255]}
    }
  }
}
```

**2D Tracker Settings (`calibration_settings_2d.json`):**
```json
{
  "tracker_type": "simple_2d",
  "saved_at": "2025-10-16T14:00:00.000Z",
  
  "common": {
    "camera_settings_profile": "default",
    "resolution": "640 x 480",
    "fps": 60,
    "depth_sensor_enabled": false,
    "ball_confidence_threshold": 0.25,
    "ball_held_confidence_threshold": 0.25,
    "nms_threshold": 0.5,
    "pose_model_enabled": true
  },
  
  "tracker_2d": {
    // Currently minimal - just uses common settings
    // Future: Add 2D-specific parameters here
  }
}
```

#### Settings Manager Implementation

```python
class SettingsManager:
    def __init__(self):
        self.settings_dir = Path("hub/config")
        self.settings_dir.mkdir(parents=True, exist_ok=True)
        
        self.tracker_files = {
            "depth_based": self.settings_dir / "calibration_settings_3d.json",
            "simple_2d": self.settings_dir / "calibration_settings_2d.json"
        }
        
        self.legacy_file = self.settings_dir / "calibration_settings.json"
        
        # Migrate legacy settings on first run
        self.migrate_legacy_settings()
    
    def load_settings(self, tracker_type: str) -> dict:
        """Load settings for specified tracker"""
        filepath = self.tracker_files.get(tracker_type)
        if not filepath or not filepath.exists():
            return self.get_default_settings(tracker_type)
        
        try:
            with open(filepath, 'r') as f:
                settings = json.load(f)
            print(f"✅ Loaded {tracker_type} settings from {filepath}")
            return settings
        except Exception as e:
            print(f"❌ Error loading {tracker_type} settings: {e}")
            return self.get_default_settings(tracker_type)
    
    def save_settings(self, tracker_type: str, settings: dict):
        """Save settings for specified tracker"""
        filepath = self.tracker_files.get(tracker_type)
        if not filepath:
            print(f"❌ Unknown tracker type: {tracker_type}")
            return False
        
        settings['tracker_type'] = tracker_type
        settings['saved_at'] = datetime.now().isoformat()
        
        try:
            with open(filepath, 'w') as f:
                json.dump(settings, f, indent=2)
            print(f"✅ Saved {tracker_type} settings to {filepath}")
            return True
        except Exception as e:
            print(f"❌ Error saving {tracker_type} settings: {e}")
            return False
    
    def migrate_legacy_settings(self):
        """Migrate old calibration_settings.json to 3D tracker settings"""
        if not self.legacy_file.exists():
            return
        
        # Check if 3D settings already exist
        if self.tracker_files["depth_based"].exists():
            print("ℹ️ 3D settings already exist, skipping migration")
            return
        
        try:
            with open(self.legacy_file, 'r') as f:
                legacy_settings = json.load(f)
            
            # Copy to 3D settings
            with open(self.tracker_files["depth_based"], 'w') as f:
                json.dump(legacy_settings, f, indent=2)
            
            print(f"✅ Migrated legacy settings to 3D tracker settings")
            
            # Optionally rename legacy file
            backup_file = self.legacy_file.with_suffix('.json.backup')
            self.legacy_file.rename(backup_file)
            print(f"✅ Backed up legacy file to {backup_file}")
            
        except Exception as e:
            print(f"❌ Error migrating legacy settings: {e}")
    
    def get_default_settings(self, tracker_type: str) -> dict:
        """Get default settings for specified tracker"""
        common_defaults = {
            "camera_settings_profile": "default",
            "resolution": "640 x 480",
            "fps": 60,
            "depth_sensor_enabled": tracker_type == "depth_based",
            "ball_confidence_threshold": 0.25,
            "ball_held_confidence_threshold": 0.25,
            "nms_threshold": 0.5,
            "pose_model_enabled": True
        }
        
        if tracker_type == "depth_based":
            return {
                "tracker_type": "depth_based",
                "common": common_defaults,
                "tracker_3d": {
                    "hand_distance_threshold": 0.25,
                    "min_frames_for_state_change": 2,
                    # ... all 3D defaults
                }
            }
        elif tracker_type == "simple_2d":
            return {
                "tracker_type": "simple_2d",
                "common": common_defaults,
                "tracker_2d": {}
            }
        else:
            return {"common": common_defaults}
```

---

## Implementation Plan

### Phase 1: Refactor Settings UI (File Organization)

**Goal:** Split monolithic `ui_settings.py` into modular files

**Files to Create:**
1. `hub/components/ui_settings_common.py` - Common settings sections
2. `hub/components/ui_settings_3d.py` - 3D tracker sections
3. `hub/components/ui_settings_2d.py` - 2D tracker sections (minimal)
4. `hub/components/ui_settings_manager.py` - Settings persistence

**Steps:**
1. Extract common sections from `ui_settings.py` to `ui_settings_common.py`
2. Extract 3D-specific sections to `ui_settings_3d.py`
3. Create minimal `ui_settings_2d.py` (currently empty)
4. Create `ui_settings_manager.py` with SettingsManager class
5. Update `ui_settings.py` to use new modular structure
6. Test that existing functionality still works

**Estimated Lines:**
- `ui_settings.py`: ~300 lines (main container + coordination)
- `ui_settings_common.py`: ~200 lines (camera, YOLO, pose sections)
- `ui_settings_3d.py`: ~800 lines (all 3D-specific sections)
- `ui_settings_2d.py`: ~50 lines (placeholder for future)
- `ui_settings_manager.py`: ~200 lines (persistence logic)

### Phase 2: Implement Dynamic Section Visibility

**Goal:** Show/hide sections based on selected tracker

**Steps:**
1. Add section visibility management to `CalibrationSettingsWidget`
2. Implement `hide_all_tracker_sections()` method
3. Implement `show_tracker_sections(tracker_type)` method
4. Update `on_tracking_system_changed()` to manage visibility
5. Test switching between trackers

**Testing:**
- Switch from 3D to 2D: verify 3D sections disappear
- Switch from 2D to 3D: verify 3D sections appear
- Verify common sections always visible
- Verify UI doesn't flicker or jump

### Phase 3: Implement Separate Settings Files

**Goal:** Each tracker has its own settings file

**Steps:**
1. Implement `SettingsManager` class in `ui_settings_manager.py`
2. Create `load_settings(tracker_type)` method
3. Create `save_settings(tracker_type, settings)` method
4. Implement `migrate_legacy_settings()` for backward compatibility
5. Update `CalibrationSettingsWidget` to use `SettingsManager`
6. Test settings persistence per tracker

**Testing:**
- Configure 3D tracker, switch to 2D, switch back: verify 3D settings preserved
- Configure 2D tracker, switch to 3D, switch back: verify 2D settings preserved
- Start with legacy `calibration_settings.json`: verify migration to 3D file
- Delete settings files: verify defaults load correctly

### Phase 4: Update Settings Application Logic

**Goal:** Apply correct settings when switching trackers

**Steps:**
1. Update `get_current_settings()` to separate common/tracker-specific
2. Update `apply_settings()` to handle new structure
3. Update `_send_all_settings_to_engine()` to send correct settings
4. Test settings application on tracker switch

**Testing:**
- Switch trackers: verify engine receives correct settings
- Modify 3D settings, switch to 2D: verify 3D settings not sent to engine
- Modify 2D settings, switch to 3D: verify 2D settings not sent to engine

### Phase 5: Testing & Documentation

**Goal:** Comprehensive testing and user documentation

**Steps:**
1. Test all tracker switching scenarios
2. Test settings persistence across app restarts
3. Test backward compatibility with legacy settings
4. Update user documentation
5. Create migration guide for existing users

**Test Cases:**
- [ ] Fresh install: both trackers have default settings
- [ ] Legacy settings exist: migrated to 3D tracker
- [ ] Switch 3D→2D→3D: settings preserved
- [ ] Switch 2D→3D→2D: settings preserved
- [ ] Modify settings, restart app: settings loaded correctly
- [ ] Delete settings files: defaults restored
- [ ] Camera settings shared between trackers
- [ ] YOLO settings shared between trackers
- [ ] 3D-specific settings only visible in 3D mode
- [ ] 2D tracker works with minimal settings

---

## Backward Compatibility

### Legacy Settings Migration

**Scenario:** User has existing `calibration_settings.json`

**Solution:**
1. On first run with new system, detect legacy file
2. Copy contents to `calibration_settings_3d.json`
3. Rename legacy file to `calibration_settings.json.backup`
4. Log migration success

**Code:**
```python
def migrate_legacy_settings(self):
    legacy_file = Path("hub/config/calibration_settings.json")
    new_3d_file = Path("hub/config/calibration_settings_3d.json")
    
    if legacy_file.exists() and not new_3d_file.exists():
        # Copy to 3D settings
        shutil.copy(legacy_file, new_3d_file)
        
        # Backup legacy file
        backup_file = legacy_file.with_suffix('.json.backup')
        legacy_file.rename(backup_file)
        
        print(f"✅ Migrated legacy settings to 3D tracker")
        print(f"✅ Backed up legacy file to {backup_file}")
```

### Settings Structure Compatibility

**Old Structure (Flat):**
```json
{
  "camera_settings_profile": "default",
  "ball_confidence_threshold": 0.25,
  "hand_distance_threshold": 0.25,
  "traj_gravity": 9.81
}
```

**New Structure (Nested):**
```json
{
  "common": {
    "camera_settings_profile": "default",
    "ball_confidence_threshold": 0.25
  },
  "tracker_3d": {
    "hand_distance_threshold": 0.25,
    "traj_gravity": 9.81
  }
}
```

**Migration Strategy:**
- `SettingsManager.load_settings()` detects flat structure
- Automatically converts to nested structure
- Saves in new format on next save

---

## File Size Management

### Current File Sizes
- `ui_settings.py`: ~2818 lines ❌ (too large)

### Target File Sizes
- `ui_settings.py`: ~300 lines ✅
- `ui_settings_common.py`: ~200 lines ✅
- `ui_settings_3d.py`: ~800 lines ✅
- `ui_settings_2d.py`: ~50 lines ✅
- `ui_settings_manager.py`: ~200 lines ✅

### Refactoring Strategy
1. Extract common sections (camera, YOLO, pose) → `ui_settings_common.py`
2. Extract 3D sections (7 sections) → `ui_settings_3d.py`
3. Extract settings persistence → `ui_settings_manager.py`
4. Keep main coordination logic in `ui_settings.py`

---

## Future Extensibility

### Adding New Trackers

**Example: Adding a "Hybrid" tracker**

1. **Create settings file:**
   ```python
   self.tracker_files["hybrid"] = "calibration_settings_hybrid.json"
   ```

2. **Create sections file:**
   ```python
   # hub/components/ui_settings_hybrid.py
   class TrackerHybridSettingsSections:
       def create_hybrid_section(self) -> CollapsibleGroupBox: ...
   ```

3. **Register in main widget:**
   ```python
   self.tracker_hybrid_sections = TrackerHybridSettingsSections(...)
   ```

4. **Add to dropdown:**
   ```python
   self.tracking_system_combo.addItem("Hybrid Tracker", "hybrid")
   ```

5. **Update visibility logic:**
   ```python
   elif tracker_type == "hybrid":
       for section in self.tracker_hybrid_sections:
           section.setVisible(True)
   ```

### Adding Settings to Existing Tracker

**Example: Adding a new 2D-specific setting**

1. **Add to `ui_settings_2d.py`:**
   ```python
   def create_2d_visualization_section(self):
       section = CollapsibleGroupBox("2D Visualization")
       # Add sliders, toggles, etc.
       return section
   ```

2. **Add to settings structure:**
   ```python
   "tracker_2d": {
       "show_bounding_boxes": true,
       "show_confidence_scores": true
   }
   ```

3. **Update `get_current_settings()` and `apply_settings()`**

---

## Summary

### Key Design Decisions

1. ✅ **Dynamic Sections**: Hide/show based on tracker selection
2. ✅ **Separate Files**: Each tracker has its own settings file
3. ✅ **Modular Code**: Split into multiple files (< 500 lines each)
4. ✅ **Backward Compatible**: Migrate legacy settings automatically
5. ✅ **Minimal 2D**: Simple2DBallTracker only needs basic YOLO settings
6. ✅ **Extensible**: Easy to add new trackers or settings

### Benefits

- **Cleaner UI**: Only relevant settings visible
- **Better Organization**: Settings grouped by tracker
- **Easier Maintenance**: Modular files easier to update
- **User-Friendly**: Clear which tracker is active
- **Future-Proof**: Easy to add new trackers

### Implementation Effort

- **Phase 1** (Refactor): ~4-6 hours
- **Phase 2** (Visibility): ~2-3 hours
- **Phase 3** (Persistence): ~3-4 hours
- **Phase 4** (Application): ~2-3 hours
- **Phase 5** (Testing): ~3-4 hours

**Total Estimated Time:** 14-20 hours

---

## Next Steps

1. Review this design document
2. Get approval on architecture decisions
3. Begin Phase 1: Refactor settings UI into modular files
4. Implement remaining phases sequentially
5. Test thoroughly with both trackers
6. Update user documentation

---

**Document Version:** 1.0  
**Last Updated:** 2025-10-16  
**Author:** Roo (Architect Mode)