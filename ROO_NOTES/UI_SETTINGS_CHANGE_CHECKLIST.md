# UI Settings Change Checklist

This checklist ensures all necessary code changes are made when adding or removing settings from the JuggleHub UI.

**Last Updated:** 2025-01-09 (ByteTrack settings removed)

---

## ✅ Adding a New Setting

### 1. **Engine C++ Header** (`engine/include/SimpleBallTracker.hpp`)

- [ ] Add the setting variable to the `TrackingSettings` struct (around line 140-180)
- [ ] Include a default value in the declaration
- [ ] Add a brief comment explaining what the setting does

**Example:**
```cpp
int color_sample_radius = 1;  // Radius for color sampling (1=3x3, 2=5x5, etc.)
```

---

### 2. **Engine C++ Implementation** (`engine/src/SimpleBallTracker.cpp`)

#### 2.1 Add Save/Load Handler (around line 240-330)
- [ ] Add an `else if` block in the `updateSetting()` method
- [ ] Parse the value correctly (use `std::stoi()` for int, `std::stof()` for float)
- [ ] Return `true` on success

**Example:**
```cpp
else if (key == "color_sample_radius") {
    tracking_settings_.color_sample_radius = std::stoi(value);
    return true;
}
```

#### 2.2 Use the Setting in Code
- [ ] Replace any hardcoded values with `tracking_settings_.your_setting_name`
- [ ] Update comments to reference the setting

**Example:**
```cpp
const int sample_radius = tracking_settings_.color_sample_radius;
```

---

### 3. **Python UI Settings** (`hub/components/ui_settings.py`)

#### 3.1 Create UI Control (in appropriate `create_*_section()` method)
- [ ] Use `_create_slider_widget()` helper for sliders
- [ ] Store slider and label references as instance variables (e.g., `self.ct_setting_name_slider`)
- [ ] Set appropriate range, default value, and update function
- [ ] Write a **detailed, multi-line tooltip** explaining:
  - What the setting does
  - Valid range and default value
  - How it affects behavior
  - Any performance implications
  - Warnings if applicable

**Example:**
```python
self.ct_color_sample_radius_slider, self.ct_color_sample_radius_label = self._create_slider_widget(
    parent_layout=layout,
    row=row,
    label_text="Color Sample Radius (pixels)",
    tooltip_text="Radius for color sampling from detection center.\n"
                 "Range: 1-5 pixels. Default: 1 (3x3 sample).\n"
                 "Lower = faster, Higher = more robust but slower.\n"
                 "⚠️ Increasing this will reduce FPS!",
    range_min=1,
    range_max=5,
    initial_value=1,
    update_func=lambda v: self.update_setting('color_sample_radius', v),
    is_float=False
)
row += 1
```

#### 3.2 Add to `get_current_settings()` (around line 1370-1450)
- [ ] Add the setting to the returned dictionary
- [ ] Use `_safe_get_slider_value()` with appropriate default
- [ ] Handle the case where the widget doesn't exist yet (`hasattr` check)
- [ ] Apply correct scaling (e.g., `/100.0` for percentages, `/10.0` for decimals)

**Example:**
```python
'color_sample_radius': self._safe_get_slider_value(self.ct_color_sample_radius_slider, 1) if hasattr(self, 'ct_color_sample_radius_slider') else 1,
```

#### 3.3 Add to `apply_settings()` (around line 1468-1625)
- [ ] Add an `if` block to restore the setting from loaded config
- [ ] Check if the widget exists with `hasattr()`
- [ ] Apply correct scaling (reverse of what was done in `get_current_settings()`)

**Example:**
```python
if 'color_sample_radius' in settings and hasattr(self, 'ct_color_sample_radius_slider'):
    self.ct_color_sample_radius_slider.setValue(settings['color_sample_radius'])
```

#### 3.4 Add to `_send_all_settings_to_engine()` (around line 1685-1797)
- [ ] Add an `if` block to send the setting to the engine
- [ ] Use `self.udp_client.send_setting(key, value)`
- [ ] Apply correct value transformation if needed

**Example:**
```python
if 'color_sample_radius' in settings:
    self.udp_client.send_setting('color_sample_radius', settings['color_sample_radius'])
```

---

### 4. **Testing**

- [ ] Start the application and verify the UI control appears
- [ ] Change the setting and verify it's sent to the engine (check console logs)
- [ ] Restart the application and verify the setting is loaded correctly
- [ ] Test that the setting actually affects behavior as expected
- [ ] Verify auto-save works (change setting, restart, check it persists)

---

## ❌ Removing a Setting

### 1. **Engine C++ Header** (`engine/include/SimpleBallTracker.hpp`)
- [ ] Remove the variable from `TrackingSettings` struct
- [ ] Check if any other code references this variable

### 2. **Engine C++ Implementation** (`engine/src/SimpleBallTracker.cpp`)
- [ ] Remove the `else if` block from `updateSetting()`
- [ ] Remove any code that uses the setting
- [ ] Replace with hardcoded value or alternative logic

### 3. **Python UI Settings** (`hub/components/ui_settings.py`)
- [ ] Remove the UI control creation code
- [ ] Remove from `get_current_settings()`
- [ ] Remove from `apply_settings()`
- [ ] Remove from `_send_all_settings_to_engine()`
- [ ] Remove any instance variable references (e.g., `self.ct_setting_name_slider`)

### 4. **Configuration Files**
- [ ] Consider if old config files need migration
- [ ] Document the removal in release notes if applicable

---

## 📝 Common Patterns

### Slider Value Scaling

| Type | Storage | Display | get_current_settings() | apply_settings() |
|------|---------|---------|----------------------|------------------|
| Percentage (0.0-1.0) | Float | 0-100 | `value / 100.0` | `int(value * 100)` |
| Decimal (0.0-5.0) | Float | 0-50 | `value / 10.0` | `int(value * 10)` |
| Distance (meters) | Float | cm | `value / 100.0` | `int(value * 100)` |
| Integer | Int | Direct | `value` | `value` |
| Boolean | Bool | Checkbox | `checkbox.isChecked()` | `checkbox.setChecked(value)` |

### Naming Conventions

- **C++ variable**: `snake_case` (e.g., `color_sample_radius`)
- **Python slider**: `self.section_setting_name_slider` (e.g., `self.ct_color_sample_radius_slider`)
- **Python label**: `self.section_setting_name_label` (e.g., `self.ct_color_sample_radius_label`)
- **Settings key**: Same as C++ variable (e.g., `"color_sample_radius"`)

### Section Prefixes

- `ct_` = Color Tracker Weights
- `tc_` = Throw/Catch (Ball State Detection)
- `kp_` = Kalman Prediction
- No prefix = Camera, YOLO, or Pose settings

---

## 🔍 Quick Reference: File Locations

| Component | File Path | Approximate Line Range |
|-----------|-----------|----------------------|
| TrackingSettings struct | `engine/include/SimpleBallTracker.hpp` | 140-180 |
| updateSetting() handler | `engine/src/SimpleBallTracker.cpp` | 240-330 |
| UI control creation | `hub/components/ui_settings.py` | Varies by section |
| get_current_settings() | `hub/components/ui_settings.py` | 1370-1450 |
| apply_settings() | `hub/components/ui_settings.py` | 1468-1625 |
| _send_all_settings_to_engine() | `hub/components/ui_settings.py` | 1685-1797 |

---

## 💡 Tips

1. **Always use descriptive tooltips** - Users need to understand what each setting does
2. **Include performance warnings** - If a setting affects FPS, mention it
3. **Use appropriate ranges** - Don't allow values that could break the system
4. **Test thoroughly** - Verify save/load, auto-save, and actual behavior
5. **Keep defaults sensible** - The default should work well for most users
6. **Document in code** - Add comments explaining non-obvious settings
7. **Consider backwards compatibility** - Old config files should still load

---

## 📚 Related Documentation

- [`SimpleBallTracker.hpp`](engine/include/SimpleBallTracker.hpp) - All tracking settings
- [`ui_settings.py`](hub/components/ui_settings.py) - UI implementation
- [`TRACKING_TUNING_GUIDE.md`](TRACKING_TUNING_GUIDE.md) - User-facing tuning guide

---

**Note:** This checklist is based on the implementation as of January 2025. If the codebase structure changes significantly, this document should be updated accordingly.