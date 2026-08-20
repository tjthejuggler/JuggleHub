# ADR: ColorOnlyTracker — identity-free color-class tracking

**Date:** 2026-08-20
**Status:** Accepted & Implemented

## Context
Every previous tracker attempt (SimpleBallTracker, New3DTracker, Simple2DBallTracker) tried to maintain per-ball identity across frames via IDs, Kalman filters, and data association. This repeatedly proved unreliable, especially with multiple same-colored balls. The user explicitly lowered the success bar: the system only needs to report **which color ball is where** — no individual identity required.

## Decision
1. **New tracker type `"color_only"`**: `ColorOnlyTracker : public IBallTracker` (engine/include/ColorOnlyTracker.hpp, engine/src/ColorOnlyTracker.cpp), registered in `Engine::setTrackerType()`. Instantiated unconditionally in the Engine constructor (lightweight — no AI models, no OpenVINO).
2. **Stateless per-frame pipeline** (mirrors the proven New3D color-first depth-blob algorithm): depth-range mask → per-enabled-color HSV mask (with red hue wrap-around handling on OpenCV's 0–180 scale) → mask AND → morphological close → contour filtering with depth-aware physical area (`pixel_area * depth² / (fx*fy)`) and circularity (`4πA/P²`) → deproject to 3D. Each surviving blob becomes one `SimpleBall` with `color_name` pre-identified, `state=IN_FLIGHT`, `has_yolo_detection=true` (so the hub renders it).
3. **Shared color calibration**: reads the same `hub/calibration_settings_new3d.json` that the existing hub calibration UI writes. `RELOAD_COLOR_PROFILES` in CommandProcessor was extended to also reload ColorOnlyTracker, so click-calibration works unchanged.
4. **Own tuning file**: `hub/config/calibration_settings_color_only.json` (via SettingsManager). `updateSetting()` accepts New3D-style aliases (`depth_blob_hue_tolerance`, `depth_blob_sat_minimum`, etc.) so the existing New3D depth-blob UI sliders — reused for color_only via `show_tracker_sections()` — control the new tracker.
5. **Hub UI entry**: third item in the visible Ball Tracking Mode dropdown — "🎨 Color-Only (No Identity)" (`mode == "color"`) — maps to the hidden `tracking_system_combo` entry `color_only` and sends `SET_TRACKER_TYPE`.

## Consequences
- No throw/catch events, no trajectories, no persistent IDs — by design. Two blue balls are simply "two blue balls".
- Existing trackers are untouched; the reliable-LED preset only auto-applies to `new_3d`, so it cannot conflict.
- Future identity-preserving work can layer on top of these per-frame color-class detections instead of re-implementing detection.