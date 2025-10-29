# Tracking System Settings - User Guide

**Last Updated:** 2025-10-16  
**Status:** Complete

## Table of Contents

1. [Introduction](#introduction)
2. [Getting Started](#getting-started)
3. [Switching Between Trackers](#switching-between-trackers)
4. [3D Tracker Settings](#3d-tracker-settings)
5. [2D Tracker Settings](#2d-tracker-settings)
6. [Common Settings](#common-settings)
7. [Saving and Loading Settings](#saving-and-loading-settings)
8. [Troubleshooting](#troubleshooting)
9. [Best Practices](#best-practices)

---

## Introduction

JuggleHub's tracking system settings provide fine-grained control over how juggling balls are detected and tracked. The system supports two distinct tracker types, each with its own independent configuration:

- **3D Depth-Based Tracker**: Uses depth information and trajectory prediction for robust tracking
- **2D Simple Tracker**: Lightweight tracking using only RGB detections

This guide will help you understand and configure these settings for optimal tracking performance.

### Key Concepts

**Tracker Independence**: Each tracker maintains its own settings file. When you switch trackers, your previous settings are preserved and automatically restored when you switch back.

**Persistent Settings**: All settings are automatically saved when changed and loaded when the application starts or when you switch trackers.

**Modular Design**: Settings are organized into logical groups (trajectory, detection, visualization, etc.) for easy navigation.

---

## Getting Started

### Accessing Settings

1. **Launch JuggleHub**:
   ```bash
   ./scripts/run_hub.sh --use-venv
   ```

2. **Open Calibration Mode**:
   - Click the "Calibration Mode" button in the main window
   - The settings panel will appear on the right side

3. **Locate Tracking Settings**:
   - Scroll to the "Tracking System" section
   - You'll see a dropdown to select the tracker type
   - Below are all settings for the currently selected tracker

### First-Time Setup

On first launch, the system uses default settings optimized for general juggling scenarios. You can start using the system immediately or customize settings based on your needs.

**Default Tracker**: 3D Depth-Based Tracker (SimpleBallTracker)

---

## Switching Between Trackers

### Via UI Dropdown

1. **Open Calibration Mode**
2. **Find "Tracking System" dropdown** at the top of the settings panel
3. **Select desired tracker**:
   - "3D Depth-Based (SimpleBallTracker)" - Full-featured tracking
   - "2D Simple (Simple2DBallTracker)" - Lightweight testing

4. **Settings automatically update** to show the selected tracker's configuration

### When to Use Each Tracker

**Use 3D Tracker When**:
- You need accurate trajectory prediction
- Throw/catch detection is important
- You want robust occlusion handling
- You're doing serious juggling practice or performance

**Use 2D Tracker When**:
- Testing raw YOLO detection performance
- Debugging detection issues
- You don't have depth camera available
- You want minimal processing overhead

### Switching Behavior

- **No Restart Required**: Switch trackers instantly
- **Settings Preserved**: Previous tracker settings remain unchanged
- **Automatic Loading**: Settings for new tracker load immediately
- **Engine Updates**: Tracker change is sent to engine automatically

---

## 3D Tracker Settings

The 3D tracker provides comprehensive settings for advanced tracking scenarios.

### Trajectory Prediction Settings

These settings control how the tracker predicts ball trajectories during flight.

#### Search Radius Base
- **Range**: 0.05 - 0.50 meters
- **Default**: 0.15 meters
- **Description**: Base search radius around predicted position
- **When to Adjust**:
  - **Increase** if balls are frequently lost during flight
  - **Decrease** if tracker picks up false positives

#### Search Radius Velocity Factor
- **Range**: 0.0 - 2.0
- **Default**: 0.5
- **Description**: How much ball velocity expands search radius
- **Formula**: `actual_radius = base + (velocity * factor)`
- **When to Adjust**:
  - **Increase** for fast juggling patterns
  - **Decrease** for slow, controlled patterns

#### Minimum Confidence Threshold
- **Range**: 0.0 - 1.0
- **Default**: 0.7
- **Description**: Minimum confidence to accept trajectory prediction
- **When to Adjust**:
  - **Increase** to be more selective (fewer false positives)
  - **Decrease** to be more permissive (catch more balls)

### Throw/Catch Detection Settings

Controls when throw and catch events are detected.

#### Throw Velocity Threshold
- **Range**: 0.1 - 2.0 m/s
- **Default**: 0.5 m/s
- **Description**: Minimum velocity change to detect a throw
- **When to Adjust**:
  - **Increase** for hard throws only
  - **Decrease** to catch gentle tosses

#### Catch Distance Threshold
- **Range**: 0.05 - 0.30 meters
- **Default**: 0.15 meters
- **Description**: Maximum distance from hand to detect catch
- **When to Adjust**:
  - **Increase** for loose catching style
  - **Decrease** for precise catching detection

#### Minimum Frames Before Catch
- **Range**: 1 - 10 frames
- **Default**: 3 frames
- **Description**: Minimum frames ball must be in flight before catch
- **Purpose**: Prevents immediate re-catch after throw (3-frame rule)
- **When to Adjust**:
  - **Increase** for very fast patterns
  - **Decrease** for slow patterns with quick catches

### Detection Settings

Controls YOLO model detection parameters.

#### Confidence Threshold
- **Range**: 0.0 - 1.0
- **Default**: 0.45
- **Description**: Minimum YOLO confidence to accept detection
- **When to Adjust**:
  - **Increase** if too many false detections
  - **Decrease** if balls are missed

#### NMS Threshold
- **Range**: 0.0 - 1.0
- **Default**: 0.5
- **Description**: Non-Maximum Suppression threshold for overlapping boxes
- **When to Adjust**:
  - **Increase** to allow more overlapping detections
  - **Decrease** to be stricter about overlaps

### Visualization Settings

Control what debug information is displayed.

#### Show Trajectories
- **Type**: Checkbox
- **Default**: Enabled
- **Description**: Display predicted trajectory paths

#### Show Predictions
- **Type**: Checkbox
- **Default**: Enabled
- **Description**: Show prediction circles and search radii

#### Show Raw Detections
- **Type**: Checkbox
- **Default**: Disabled
- **Description**: Display raw YOLO detection boxes

#### Show NMS Detections
- **Type**: Checkbox
- **Default**: Disabled
- **Description**: Display detections after NMS filtering

---

## 2D Tracker Settings

The 2D tracker has simplified settings focused on detection.

### Detection Settings

#### Confidence Threshold
- **Range**: 0.0 - 1.0
- **Default**: 0.45
- **Description**: Minimum YOLO confidence for detection
- **Usage**: Same as 3D tracker confidence threshold

#### NMS Threshold
- **Range**: 0.0 - 1.0
- **Default**: 0.5
- **Description**: Non-Maximum Suppression threshold
- **Usage**: Same as 3D tracker NMS threshold

### Visualization Settings

#### Show Raw Detections
- **Type**: Checkbox
- **Default**: Enabled
- **Description**: Display all YOLO detections

---

## Common Settings

These settings apply to all tracker types.

### Camera Settings

#### Enable Depth Sensor
- **Type**: Checkbox
- **Default**: Enabled (for 3D tracker)
- **Description**: Enable/disable RealSense depth sensor
- **Note**: Required for 3D tracker, optional for 2D

#### Enable YOLO Detection
- **Type**: Checkbox
- **Default**: Enabled
- **Description**: Enable/disable YOLO ball detection
- **Note**: Disabling helps isolate performance issues

### Ball Profile Settings

#### Track Ball Colors
- **Type**: Checkboxes for each color
- **Colors**: Blue, Green, Orange, Pink, Purple, Red, White, Yellow
- **Description**: Enable/disable tracking for specific ball colors
- **Usage**: Disable unused colors to reduce false positives

#### Hue Range Sliders
- **Type**: Min/Max sliders per color
- **Range**: 0-180 (HSV hue values)
- **Description**: Fine-tune color detection ranges
- **Auto-Calibration**: Click "Auto-Calibrate" to set from samples

---

## Saving and Loading Settings

### Automatic Saving

Settings are automatically saved in these situations:

1. **When Changed**: Any slider or checkbox change triggers save
2. **On Tracker Switch**: Settings saved before switching
3. **On Application Exit**: All settings saved on clean exit

### Manual Save/Load

You can also manually manage settings:

**Save Settings**:
- File → Save Settings (Ctrl+S)
- Choose location and filename
- Saves current tracker settings to JSON file

**Load Settings**:
- File → Load Settings (Ctrl+O)
- Select previously saved settings file
- Settings applied to current tracker

### Settings File Locations

Settings are stored in the project root:

```
JuggleHub/
├── ball_settings.json          # 3D tracker settings
├── ball_settings_2d.json       # 2D tracker settings
└── ball_settings_legacy.json   # Backup (if migrated)
```

### Settings File Format

Settings files are human-readable JSON:

```json
{
  "tracker_type": "3d",
  "trajectory": {
    "search_radius_base": 0.15,
    "search_radius_velocity_factor": 0.5
  },
  "throw_catch": {
    "throw_velocity_threshold": 0.5
  }
}
```

You can edit these files directly if needed, but use the UI for safety.

---

## Troubleshooting

### Common Issues

#### "Balls Not Being Detected"

**Symptoms**: No balls appear in tracking visualization

**Solutions**:
1. Check "Enable YOLO Detection" is enabled
2. Lower confidence threshold (try 0.3)
3. Verify ball colors are enabled in Ball Profiles
4. Check lighting conditions
5. Ensure camera is working (check video feed)

#### "Tracker Keeps Losing Balls"

**Symptoms**: Balls disappear during flight

**Solutions**:
1. Increase search radius base (try 0.20)
2. Increase search radius velocity factor (try 0.8)
3. Lower minimum confidence threshold (try 0.6)
4. Check for occlusions or poor lighting
5. Verify depth sensor is enabled (3D tracker)

#### "Too Many False Detections"

**Symptoms**: Tracker detects non-ball objects

**Solutions**:
1. Increase confidence threshold (try 0.6)
2. Disable unused ball colors
3. Narrow hue ranges for enabled colors
4. Increase NMS threshold (try 0.6)
5. Improve lighting to reduce shadows

#### "Throw/Catch Events Not Detected"

**Symptoms**: No throw/catch events logged

**Solutions**:
1. Lower throw velocity threshold (try 0.3)
2. Increase catch distance threshold (try 0.20)
3. Reduce minimum frames before catch (try 2)
4. Verify hands are being tracked (check pose detection)
5. Check ball is transitioning between states

#### "Settings Not Persisting"

**Symptoms**: Settings reset after restart

**Solutions**:
1. Check file permissions on settings files
2. Verify settings files exist in project root
3. Check for errors in console output
4. Try manual save (Ctrl+S) to test
5. Ensure clean application exit (not force-killed)

#### "Tracker Switch Not Working"

**Symptoms**: Tracker doesn't change when selected

**Solutions**:
1. Check console for error messages
2. Verify both settings files exist
3. Restart application
4. Check engine is running and connected
5. Try manual tracker selection via dropdown again

### Debug Mode

Enable debug output for detailed troubleshooting:

```bash
JUGGLEHUB_DEBUG=1 ./scripts/run_hub.sh --use-venv
```

This shows:
- Settings load/save operations
- Tracker switch events
- Setting value changes
- Engine communication
- Error details

---

## Best Practices

### General Guidelines

1. **Start with Defaults**: Use default settings first, only adjust if needed
2. **Change One at a Time**: Adjust one setting, test, then adjust another
3. **Document Changes**: Keep notes on what works for your setup
4. **Save Configurations**: Save working configurations for different scenarios
5. **Use Appropriate Tracker**: 3D for serious use, 2D for testing

### Optimization Tips

**For Best Tracking Performance**:
- Use 3D tracker with depth sensor enabled
- Enable only ball colors you're actually using
- Set confidence threshold based on your lighting
- Adjust search radius for your juggling speed
- Enable trajectory visualization during tuning

**For Best Detection Performance**:
- Ensure good, even lighting
- Use distinct ball colors
- Calibrate color ranges carefully
- Keep background uncluttered
- Position camera at appropriate distance

**For Throw/Catch Detection**:
- Tune velocity threshold to your throwing style
- Adjust catch distance for your catching style
- Use 3-frame rule to prevent false catches
- Verify hand tracking is working well
- Test with different juggling patterns

### Recommended Settings by Use Case

#### Casual Practice
```
Tracker: 3D
Confidence: 0.45
Search Radius: 0.15
Throw Velocity: 0.5
Catch Distance: 0.15
```

#### Performance/Recording
```
Tracker: 3D
Confidence: 0.55
Search Radius: 0.12
Throw Velocity: 0.6
Catch Distance: 0.12
```

#### Fast Patterns
```
Tracker: 3D
Confidence: 0.40
Search Radius: 0.20
Velocity Factor: 0.8
Throw Velocity: 0.7
```

#### Slow/Technical Patterns
```
Tracker: 3D
Confidence: 0.50
Search Radius: 0.10
Velocity Factor: 0.3
Throw Velocity: 0.3
```

#### Testing/Debugging
```
Tracker: 2D
Confidence: 0.45
Show Raw Detections: Enabled
Show NMS Detections: Enabled
```

### Calibration Workflow

1. **Initial Setup**:
   - Start with default settings
   - Enable only colors you're using
   - Test basic detection

2. **Detection Tuning**:
   - Adjust confidence threshold
   - Fine-tune hue ranges
   - Verify all balls detected

3. **Tracking Tuning**:
   - Adjust search radius
   - Test with actual juggling
   - Verify no ball loss

4. **Event Detection Tuning**:
   - Test throw detection
   - Test catch detection
   - Adjust thresholds as needed

5. **Save Configuration**:
   - Save working settings
   - Document your setup
   - Create backups

---

## Additional Resources

### Related Documentation

- **Architecture**: [`TRACKING_SYSTEM_SETTINGS_ARCHITECTURE.md`](TRACKING_SYSTEM_SETTINGS_ARCHITECTURE.md) - System design details
- **Implementation**: `TRACKING_SYSTEM_SETTINGS_IMPLEMENTATION_SUMMARY.md` - Technical implementation
- **Migration**: `TRACKING_SYSTEM_SETTINGS_MIGRATION_GUIDE.md` - Upgrading from old system
- **Main README**: [`README.md`](README.md) - Complete system documentation

### Support

For issues or questions:
1. Check this guide's troubleshooting section
2. Review console output with debug mode enabled
3. Check GitHub issues for similar problems
4. Create new issue with detailed description

### Contributing

To improve this guide:
1. Fork the repository
2. Update documentation
3. Test your changes
4. Submit pull request

---

**Last Updated:** 2025-10-16  
**Version:** 1.0  
**Status:** Complete