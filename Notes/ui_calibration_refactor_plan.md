# UI Calibration Settings Refactor Plan

**Created:** 2025-10-01  
**Status:** Planning Phase  
**Goal:** Refactor CalibrationSettingsWidget to support collapsible sections, scrolling, and throw/catch detection settings

---

## 1. Overview

The current [`CalibrationSettingsWidget`](../hub/components/ui.py:114) needs to be refactored to:
1. Make all settings sections collapsible
2. Add scrollable area to fit all settings
3. Add new throw/catch detection settings section
4. Integrate new settings with UDP, save/load, and auto-save systems

---

## 2. Current Architecture Analysis

### 2.1 Existing Structure
- **Location:** [`hub/components/ui.py`](../hub/components/ui.py:114-641)
- **Current Sections:**
  - Camera Settings (lines 147-232)
  - YOLO Tracker Settings (lines 234-266)
  - ByteTrack Settings (lines 268-327)
  - Pose Model Settings (lines 329-340)

### 2.2 Current Features
- UDP communication via [`UdpClient`](../hub/components/ui.py:45-54)
- Auto-save on setting changes (line 523-524)
- Save/load to JSON files (lines 594-640)
- Settings stored in `hub/config/calibration_settings.json`

### 2.3 Limitations
- No scrolling support (all sections must fit in viewport)
- QGroupBox sections are not collapsible
- No throw/catch detection settings exposed to UI

---

## 3. Collapsible QGroupBox Solution

### 3.1 Design Approach: Custom CollapsibleGroupBox Widget

**Rationale:** QGroupBox doesn't natively support collapsing. We'll create a custom widget that mimics QGroupBox appearance but adds collapse functionality.

### 3.2 Implementation Strategy

```python
class CollapsibleGroupBox(QWidget):
    """
    A collapsible group box that mimics QGroupBox appearance.
    
    Features:
    - Clickable header with expand/collapse icon
    - Smooth animation (optional)
    - Maintains QGroupBox styling
    - Remembers collapsed state in settings
    """
    
    def __init__(self, title: str, parent=None, collapsed: bool = False):
        super().__init__(parent)
        self.title = title
        self.is_collapsed = collapsed
        
        # Main layout
        main_layout = QVBoxLayout(self)
        main_layout.setContentsMargins(0, 0, 0, 0)
        main_layout.setSpacing(0)
        
        # Header button (clickable title)
        self.header_button = QPushButton(f"▼ {title}")
        self.header_button.setCheckable(True)
        self.header_button.setChecked(not collapsed)
        self.header_button.clicked.connect(self.toggle_collapsed)
        self.header_button.setStyleSheet("""
            QPushButton {
                text-align: left;
                padding: 8px;
                border: 2px solid #555555;
                border-radius: 5px 5px 0 0;
                background-color: #3a3a3a;
                font-weight: bold;
            }
            QPushButton:hover {
                background-color: #4a4a4a;
            }
        """)
        main_layout.addWidget(self.header_button)
        
        # Content container
        self.content_widget = QWidget()
        self.content_layout = QVBoxLayout(self.content_widget)
        self.content_widget.setStyleSheet("""
            QWidget {
                border: 2px solid #555555;
                border-top: none;
                border-radius: 0 0 5px 5px;
                background-color: #2b2b2b;
            }
        """)
        main_layout.addWidget(self.content_widget)
        
        # Set initial state
        if collapsed:
            self.content_widget.hide()
            self.header_button.setText(f"▶ {title}")
    
    def toggle_collapsed(self):
        self.is_collapsed = not self.is_collapsed
        if self.is_collapsed:
            self.content_widget.hide()
            self.header_button.setText(f"▶ {self.title}")
        else:
            self.content_widget.show()
            self.header_button.setText(f"▼ {self.title}")
    
    def get_content_layout(self):
        """Returns the layout where child widgets should be added"""
        return self.content_layout
```

### 3.3 Alternative Approach: QToolBox

**Pros:**
- Built-in Qt widget
- Native collapse/expand behavior
- Less custom code

**Cons:**
- Different visual style (accordion-like)
- Less control over appearance
- May not match existing QGroupBox aesthetic

**Decision:** Use custom CollapsibleGroupBox for better control and consistency.

---

## 4. Scrollable Area Structure

### 4.1 Design

```python
def init_ui(self):
    # Main layout for the widget
    main_layout = QVBoxLayout(self)
    
    # Create scroll area
    scroll_area = QScrollArea()
    scroll_area.setWidgetResizable(True)
    scroll_area.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
    scroll_area.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAsNeeded)
    
    # Container widget for all collapsible sections
    container_widget = QWidget()
    container_layout = QVBoxLayout(container_widget)
    container_layout.setSpacing(10)
    
    # Add all collapsible sections to container
    container_layout.addWidget(self.create_camera_section())
    container_layout.addWidget(self.create_yolo_section())
    container_layout.addWidget(self.create_bytetrack_section())
    container_layout.addWidget(self.create_pose_section())
    container_layout.addWidget(self.create_throw_catch_section())
    
    # Add stretch to push sections to top
    container_layout.addStretch()
    
    # Set container as scroll area widget
    scroll_area.setWidget(container_widget)
    
    # Add scroll area to main layout
    main_layout.addWidget(scroll_area)
```

### 4.2 Styling Considerations

```python
scroll_area.setStyleSheet("""
    QScrollArea {
        border: none;
        background-color: #2b2b2b;
    }
    QScrollBar:vertical {
        border: none;
        background: #1e1e1e;
        width: 12px;
        margin: 0px;
    }
    QScrollBar::handle:vertical {
        background: #555555;
        min-height: 20px;
        border-radius: 6px;
    }
    QScrollBar::handle:vertical:hover {
        background: #666666;
    }
""")
```

---

## 5. Throw/Catch Detection Settings

### 5.1 Settings to Expose

Based on [`ThrowCatchDetector::Config`](../engine/include/ThrowCatchDetector.hpp:61-86):

| Setting | Type | Range | Default | UDP Key |
|---------|------|-------|---------|---------|
| ML Weight | Float | 0-100% | 35% | `tc_ml_weight` |
| Proximity Weight | Float | 0-100% | 25% | `tc_proximity_weight` |
| Kinematic Weight | Float | 0-100% | 25% | `tc_kinematic_weight` |
| Relative Velocity Weight | Float | 0-100% | 15% | `tc_relative_velocity_weight` |
| Catch Threshold | Float | 0-100% | 75% | `tc_catch_threshold` |
| Throw Threshold | Float | 0-100% | 75% | `tc_throw_threshold` |
| Catch Distance | Float | 0-50cm | 15cm | `tc_catch_distance` |
| Throw Distance | Float | 0-50cm | 20cm | `tc_throw_distance` |
| Min Frames for Event | Int | 1-10 | 2 | `tc_min_frames` |

### 5.2 UI Layout

```python
def create_throw_catch_section(self):
    """Create the Throw/Catch Detection settings section"""
    section = CollapsibleGroupBox("🎯 Throw/Catch Detection", collapsed=False)
    layout = QGridLayout()
    section.get_content_layout().addLayout(layout)
    
    row = 0
    
    # Weight sliders (must sum to 100%)
    layout.addWidget(QLabel("Evidence Weights:"), row, 0, 1, 3)
    row += 1
    
    self.tc_ml_weight_slider, self.tc_ml_weight_label = self._create_slider_widget(
        parent_layout=layout,
        row=row,
        label_text="ML Weight",
        tooltip_text="Weight given to ML model classification (ball vs ball_held).\n"
                     "Range: 0-100%. Default: 35%.\n"
                     "All weights should sum to 100%.",
        range_min=0,
        range_max=100,
        initial_value=35,
        update_func=lambda v: self.update_setting('tc_ml_weight', v / 100.0),
        is_float=True
    )
    row += 1
    
    self.tc_proximity_weight_slider, self.tc_proximity_weight_label = self._create_slider_widget(
        parent_layout=layout,
        row=row,
        label_text="Proximity Weight",
        tooltip_text="Weight given to distance between ball and hand.\n"
                     "Range: 0-100%. Default: 25%.",
        range_min=0,
        range_max=100,
        initial_value=25,
        update_func=lambda v: self.update_setting('tc_proximity_weight', v / 100.0),
        is_float=True
    )
    row += 1
    
    self.tc_kinematic_weight_slider, self.tc_kinematic_weight_label = self._create_slider_widget(
        parent_layout=layout,
        row=row,
        label_text="Kinematic Weight",
        tooltip_text="Weight given to velocity changes (acceleration/deceleration).\n"
                     "Range: 0-100%. Default: 25%.",
        range_min=0,
        range_max=100,
        initial_value=25,
        update_func=lambda v: self.update_setting('tc_kinematic_weight', v / 100.0),
        is_float=True
    )
    row += 1
    
    self.tc_rel_velocity_weight_slider, self.tc_rel_velocity_weight_label = self._create_slider_widget(
        parent_layout=layout,
        row=row,
        label_text="Relative Velocity Weight",
        tooltip_text="Weight given to velocity difference between ball and hand.\n"
                     "Range: 0-100%. Default: 15%.",
        range_min=0,
        range_max=100,
        initial_value=15,
        update_func=lambda v: self.update_setting('tc_relative_velocity_weight', v / 100.0),
        is_float=True
    )
    row += 1
    
    # Separator
    layout.addWidget(QLabel("Detection Thresholds:"), row, 0, 1, 3)
    row += 1
    
    self.tc_catch_threshold_slider, self.tc_catch_threshold_label = self._create_slider_widget(
        parent_layout=layout,
        row=row,
        label_text="Catch Threshold",
        tooltip_text="Minimum total score required to detect a catch event.\n"
                     "Range: 0-100%. Default: 75%.\n"
                     "Higher values = fewer false positives, may miss real catches.",
        range_min=0,
        range_max=100,
        initial_value=75,
        update_func=lambda v: self.update_setting('tc_catch_threshold', v / 100.0),
        is_float=True
    )
    row += 1
    
    self.tc_throw_threshold_slider, self.tc_throw_threshold_label = self._create_slider_widget(
        parent_layout=layout,
        row=row,
        label_text="Throw Threshold",
        tooltip_text="Minimum total score required to detect a throw event.\n"
                     "Range: 0-100%. Default: 75%.",
        range_min=0,
        range_max=100,
        initial_value=75,
        update_func=lambda v: self.update_setting('tc_throw_threshold', v / 100.0),
        is_float=True
    )
    row += 1
    
    # Separator
    layout.addWidget(QLabel("Distance Thresholds:"), row, 0, 1, 3)
    row += 1
    
    self.tc_catch_distance_slider, self.tc_catch_distance_label = self._create_slider_widget(
        parent_layout=layout,
        row=row,
        label_text="Catch Distance (cm)",
        tooltip_text="Maximum distance between ball and hand for catch detection.\n"
                     "Range: 0-50cm. Default: 15cm.",
        range_min=0,
        range_max=50,
        initial_value=15,
        update_func=lambda v: self.update_setting('tc_catch_distance', v / 100.0),  # Convert cm to m
        is_float=True
    )
    row += 1
    
    self.tc_throw_distance_slider, self.tc_throw_distance_label = self._create_slider_widget(
        parent_layout=layout,
        row=row,
        label_text="Throw Distance (cm)",
        tooltip_text="Minimum distance ball must travel from hand for throw detection.\n"
                     "Range: 0-50cm. Default: 20cm.",
        range_min=0,
        range_max=50,
        initial_value=20,
        update_func=lambda v: self.update_setting('tc_throw_distance', v / 100.0),  # Convert cm to m
        is_float=True
    )
    row += 1
    
    # Separator
    layout.addWidget(QLabel("Temporal Filtering:"), row, 0, 1, 3)
    row += 1
    
    self.tc_min_frames_slider, self.tc_min_frames_label = self._create_slider_widget(
        parent_layout=layout,
        row=row,
        label_text="Min Frames for Event",
        tooltip_text="Number of consecutive frames an event must persist to be confirmed.\n"
                     "Range: 1-10 frames. Default: 2.\n"
                     "Higher values = more stable detection, slower response.",
        range_min=1,
        range_max=10,
        initial_value=2,
        update_func=lambda v: self.update_setting('tc_min_frames', v),
        is_float=False
    )
    
    return section
```

---

## 6. UDP Communication Integration

### 6.1 Engine-Side UDP Handler

The engine needs to handle new UDP settings. This should be added to the existing UDP settings handler (likely in [`Engine.cpp`](../engine/src/Engine.cpp) or a dedicated UDP module).

```cpp
// In Engine.cpp or UdpSettingsModule.cpp
void handleUdpSetting(const std::string& key, const std::string& value) {
    // Existing settings...
    
    // Throw/Catch Detection settings
    if (key == "tc_ml_weight") {
        throw_catch_detector_.config_.ml_weight = std::stof(value);
    }
    else if (key == "tc_proximity_weight") {
        throw_catch_detector_.config_.proximity_weight = std::stof(value);
    }
    else if (key == "tc_kinematic_weight") {
        throw_catch_detector_.config_.kinematic_weight = std::stof(value);
    }
    else if (key == "tc_relative_velocity_weight") {
        throw_catch_detector_.config_.relative_velocity_weight = std::stof(value);
    }
    else if (key == "tc_catch_threshold") {
        throw_catch_detector_.config_.catch_threshold = std::stof(value);
    }
    else if (key == "tc_throw_threshold") {
        throw_catch_detector_.config_.throw_threshold = std::stof(value);
    }
    else if (key == "tc_catch_distance") {
        throw_catch_detector_.config_.catch_distance = std::stof(value);
    }
    else if (key == "tc_throw_distance") {
        throw_catch_detector_.config_.throw_distance = std::stof(value);
    }
    else if (key == "tc_min_frames") {
        throw_catch_detector_.config_.min_frames_for_event = std::stoi(value);
    }
}
```

### 6.2 Hub-Side UDP Client

The existing [`UdpClient`](../hub/components/ui.py:45-54) already handles sending settings. No changes needed here.

---

## 7. Save/Load System Integration

### 7.1 Update `get_current_settings()`

Add throw/catch settings to the settings dictionary:

```python
def get_current_settings(self) -> dict:
    """Get current calibration settings as a dictionary."""
    # Check if ALL UI elements exist before accessing them
    required_attrs = [
        'confidence_slider', 'nms_slider', 'track_buffer_slider',
        'track_thresh_slider', 'high_thresh_slider', 'match_thresh_slider',
        'pose_model_toggle', 'camera_settings_combo', 'resolution_combo', 'fps_combo',
        # Add throw/catch sliders
        'tc_ml_weight_slider', 'tc_proximity_weight_slider', 
        'tc_kinematic_weight_slider', 'tc_rel_velocity_weight_slider',
        'tc_catch_threshold_slider', 'tc_throw_threshold_slider',
        'tc_catch_distance_slider', 'tc_throw_distance_slider',
        'tc_min_frames_slider'
    ]
    
    for attr in required_attrs:
        if not hasattr(self, attr):
            return {}
    
    return {
        # Existing settings...
        'camera_settings_profile': self.camera_settings_combo.currentData(),
        'resolution': self.resolution_combo.currentText(),
        'fps': self.fps_combo.currentData(),
        'confidence_threshold': self.confidence_slider.value() / 100.0,
        'nms_threshold': self.nms_slider.value() / 100.0,
        'track_buffer': self.track_buffer_slider.value(),
        'track_thresh': self.track_thresh_slider.value() / 100.0,
        'high_thresh': self.high_thresh_slider.value() / 100.0,
        'match_thresh': self.match_thresh_slider.value() / 100.0,
        'pose_model_enabled': self.pose_model_toggle.isChecked(),
        
        # Throw/Catch Detection settings
        'tc_ml_weight': self.tc_ml_weight_slider.value() / 100.0,
        'tc_proximity_weight': self.tc_proximity_weight_slider.value() / 100.0,
        'tc_kinematic_weight': self.tc_kinematic_weight_slider.value() / 100.0,
        'tc_relative_velocity_weight': self.tc_rel_velocity_weight_slider.value() / 100.0,
        'tc_catch_threshold': self.tc_catch_threshold_slider.value() / 100.0,
        'tc_throw_threshold': self.tc_throw_threshold_slider.value() / 100.0,
        'tc_catch_distance': self.tc_catch_distance_slider.value() / 100.0,  # cm to m
        'tc_throw_distance': self.tc_throw_distance_slider.value() / 100.0,  # cm to m
        'tc_min_frames': self.tc_min_frames_slider.value(),
        
        # Collapsed states for UI persistence
        'collapsed_camera': self.camera_section.is_collapsed,
        'collapsed_yolo': self.yolo_section.is_collapsed,
        'collapsed_bytetrack': self.bytetrack_section.is_collapsed,
        'collapsed_pose': self.pose_section.is_collapsed,
        'collapsed_throw_catch': self.throw_catch_section.is_collapsed
    }
```

### 7.2 Update `apply_settings()`

Add throw/catch settings application:

```python
def apply_settings(self, settings: dict):
    """Apply settings from a dictionary to the UI controls."""
    # Existing settings application...
    
    # Throw/Catch Detection settings
    if 'tc_ml_weight' in settings:
        self.tc_ml_weight_slider.setValue(int(settings['tc_ml_weight'] * 100))
    
    if 'tc_proximity_weight' in settings:
        self.tc_proximity_weight_slider.setValue(int(settings['tc_proximity_weight'] * 100))
    
    if 'tc_kinematic_weight' in settings:
        self.tc_kinematic_weight_slider.setValue(int(settings['tc_kinematic_weight'] * 100))
    
    if 'tc_relative_velocity_weight' in settings:
        self.tc_rel_velocity_weight_slider.setValue(int(settings['tc_relative_velocity_weight'] * 100))
    
    if 'tc_catch_threshold' in settings:
        self.tc_catch_threshold_slider.setValue(int(settings['tc_catch_threshold'] * 100))
    
    if 'tc_throw_threshold' in settings:
        self.tc_throw_threshold_slider.setValue(int(settings['tc_throw_threshold'] * 100))
    
    if 'tc_catch_distance' in settings:
        self.tc_catch_distance_slider.setValue(int(settings['tc_catch_distance'] * 100))  # m to cm
    
    if 'tc_throw_distance' in settings:
        self.tc_throw_distance_slider.setValue(int(settings['tc_throw_distance'] * 100))  # m to cm
    
    if 'tc_min_frames' in settings:
        self.tc_min_frames_slider.setValue(settings['tc_min_frames'])
    
    # Restore collapsed states
    if 'collapsed_camera' in settings:
        if settings['collapsed_camera'] != self.camera_section.is_collapsed:
            self.camera_section.toggle_collapsed()
    
    if 'collapsed_yolo' in settings:
        if settings['collapsed_yolo'] != self.yolo_section.is_collapsed:
            self.yolo_section.toggle_collapsed()
    
    if 'collapsed_bytetrack' in settings:
        if settings['collapsed_bytetrack'] != self.bytetrack_section.is_collapsed:
            self.bytetrack_section.toggle_collapsed()
    
    if 'collapsed_pose' in settings:
        if settings['collapsed_pose'] != self.pose_section.is_collapsed:
            self.pose_section.toggle_collapsed()
    
    if 'collapsed_throw_catch' in settings:
        if settings['collapsed_throw_catch'] != self.throw_catch_section.is_collapsed:
            self.throw_catch_section.toggle_collapsed()
```

---

## 8. Implementation Steps

### Phase 1: Create CollapsibleGroupBox Widget
1. Create `CollapsibleGroupBox` class in [`ui.py`](../hub/components/ui.py)
2. Test with one existing section (e.g., Camera Settings)
3. Verify styling matches existing QGroupBox appearance

### Phase 2: Add Scroll Area
1. Wrap all sections in `QScrollArea`
2. Test scrolling behavior
3. Adjust styling for dark theme

### Phase 3: Convert Existing Sections
1. Convert Camera Settings to `CollapsibleGroupBox`
2. Convert YOLO Settings to `CollapsibleGroupBox`
3. Convert ByteTrack Settings to `CollapsibleGroupBox`
4. Convert Pose Model Settings to `CollapsibleGroupBox`
5. Test all sections expand/collapse correctly

### Phase 4: Add Throw/Catch Section
1. Create `create_throw_catch_section()` method
2. Add all 9 sliders with proper tooltips
3. Test slider value changes
4. Verify UDP messages are sent correctly

### Phase 5: Integrate with Save/Load
1. Update `get_current_settings()` to include new settings
2. Update `apply_settings()` to restore new settings
3. Add collapsed state persistence
4. Test save/load functionality

### Phase 6: Engine Integration
1. Add UDP handler for throw/catch settings in engine
2. Test settings are applied to `ThrowCatchDetector::Config`
3. Verify throw/catch detection responds to setting changes

### Phase 7: Testing & Polish
1. Test auto-save on setting changes
2. Test settings persistence across app restarts
3. Verify all tooltips are helpful and accurate
4. Check UI responsiveness with all sections expanded
5. Test with different window sizes

---

## 9. Code Organization

### 9.1 File Structure

```
hub/components/ui.py
├── CollapsibleGroupBox (new class, lines ~110-180)
├── CalibrationSettingsWidget (refactored, lines ~180-700)
│   ├── __init__()
│   ├── init_ui() (refactored with scroll area)
│   ├── create_camera_section() (new method)
│   ├── create_yolo_section() (new method)
│   ├── create_bytetrack_section() (new method)
│   ├── create_pose_section() (new method)
│   ├── create_throw_catch_section() (new method)
│   ├── _create_slider_widget() (existing helper)
│   ├── update_setting() (existing)
│   ├── get_current_settings() (updated)
│   ├── apply_settings() (updated)
│   ├── save_settings() (existing)
│   └── load_settings() (existing)
```

### 9.2 Estimated Line Count Changes

- **CollapsibleGroupBox class:** ~70 lines
- **Refactored init_ui():** ~50 lines (simplified)
- **create_camera_section():** ~90 lines
- **create_yolo_section():** ~40 lines
- **create_bytetrack_section():** ~90 lines
- **create_pose_section():** ~20 lines
- **create_throw_catch_section():** ~150 lines
- **Updated get_current_settings():** +30 lines
- **Updated apply_settings():** +40 lines

**Total new/changed lines:** ~580 lines

---

## 10. Potential Issues & Solutions

### 10.1 Weight Sliders Must Sum to 100%

**Issue:** The four weight sliders (ML, Proximity, Kinematic, Relative Velocity) should ideally sum to 100%.

**Solutions:**
1. **Option A (Simple):** Add a warning label that shows current sum
2. **Option B (Complex):** Auto-adjust other sliders when one changes
3. **Option C (Recommended):** Show sum in UI, let user manage manually

**Recommendation:** Option C - Add a label showing "Total Weight: XX%" that updates as sliders change. Color it red if not 100%, green if exactly 100%.

### 10.2 Scroll Area Performance

**Issue:** Many widgets in scroll area might impact performance.

**Solution:** 
- Use `setWidgetResizable(True)` for efficient resizing
- Collapsed sections hide their content, reducing render load
- Test with all sections expanded

### 10.3 Settings File Compatibility

**Issue:** Existing settings files won't have new throw/catch settings.

**Solution:**
- Use `.get()` with defaults when loading settings
- Document default values clearly
- Consider migration script if needed

### 10.4 UDP Message Ordering

**Issue:** Multiple UDP messages sent rapidly might arrive out of order.

**Solution:**
- Current implementation sends one message per setting change
- Engine should handle settings in any order
- No changes needed

---

## 11. Testing Checklist

- [ ] CollapsibleGroupBox expands/collapses correctly
- [ ] All sections can be collapsed independently
- [ ] Scroll area scrolls smoothly
- [ ] Scroll bar appears only when needed
- [ ] All sliders send UDP messages correctly
- [ ] Settings are saved to JSON correctly
- [ ] Settings are loaded from JSON correctly
- [ ] Auto-save triggers on setting changes
- [ ] Collapsed states persist across app restarts
- [ ] Engine receives and applies throw/catch settings
- [ ] Throw/catch detection responds to setting changes
- [ ] UI remains responsive with all sections expanded
- [ ] Dark theme styling is consistent
- [ ] Tooltips are helpful and accurate
- [ ] Weight sum indicator works correctly

---

## 12. Future Enhancements

1. **Weight Normalization:** Auto-normalize weights to sum to 100%
2. **Preset Profiles:** Save/load different throw/catch detection profiles
3. **Real-time Feedback:** Show detection confidence scores in UI
4. **Advanced Mode:** Hide/show advanced settings
5. **Validation:** Prevent invalid setting combinations
6. **Undo/Redo:** Allow reverting setting changes

---

## 13. References

- [`CalibrationSettingsWidget`](../hub/components/ui.py:114-641)
- [`ThrowCatchDetector::Config`](../engine/include/ThrowCatchDetector.hpp:61-86)
- [`ThrowCatchDetector` implementation](../engine/src/ThrowCatchDetector.cpp)
- [Qt QScrollArea Documentation](https://doc.qt.io/qt-6/qscrollarea.html)
- [Qt Layouts Documentation](https://doc.qt.io/qt-6/layout.html)

---

**End of Plan**