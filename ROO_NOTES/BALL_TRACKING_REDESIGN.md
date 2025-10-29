# Ball Tracking System - Legacy Color Tracking

**Status:** ARCHIVED - New system removed, using legacy color tracking only
**Last Updated:** 2025-10-03

## Overview

This document describes the legacy color-based ball tracking system used in JuggleHub. The new multi-sample calibration system has been removed in favor of the simpler, more reliable legacy approach.

## Legacy Color Tracking System

The legacy system uses simple HSV color range matching for ball detection:

### Features
- Single HSV color range per ball
- Direct color calibration from clicked pixels
- Simple blob detection and matching
- Reliable performance in controlled lighting

### Implementation
- **File:** `engine/src/ColorTracker.cpp`
- **Method:** HSV color space filtering with morphological operations
- **Calibration:** Single-click color sampling

### Usage
1. Click on a ball in the video feed
2. System captures HSV values at that point
3. Creates a color range around those values
4. Uses this range for detection in subsequent frames

## Removed Components

The following components were part of the new tracking system and have been removed:

- `BallRegistry` - Multi-ball management system
- `SkinToneFilter` - Hand proximity detection
- `DetectionConfidence` - Multi-factor confidence scoring
- Ball Management UI tab
- REST API for ball management
- Multi-sample color calibration

## Current Architecture

```
ColorTracker (Legacy Mode Only)
├── HSV Color Filtering
├── Blob Detection
├── Closest Match Selection
└── Settings Persistence
```

## Configuration

Ball colors are configured in `ball_settings.json`:

```json
{
  "ball_0": {
    "h_min": 0, "h_max": 10,
    "s_min": 100, "s_max": 255,
    "v_min": 100, "v_max": 255
  }
}
```

## Future Considerations

If color tracking needs improvement, consider:
- Better lighting conditions
- More distinctive ball colors
- Improved HSV range tuning
- Alternative color spaces (LAB, YCrCb)