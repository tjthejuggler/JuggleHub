# How to Use Time-Based Color Calibration

## ⚠️ IMPORTANT: Two Different Calibration Systems

There are **TWO** calibration buttons in the UI:

### ❌ OLD System (Don't Use This)
- **Location**: Main UI → "Calibration & Visualization" section (left side)
- **Button**: "Set Color Profile"
- **Method**: Single-click on ball (OLD, not robust)
- **Status**: Deprecated, will be removed

### ✅ NEW System (Use This!)
- **Location**: Tracking Settings panel → "🎨 Color Calibration" section (right side)
- **Button**: "🎯 Calibrate Color (10s)"
- **Method**: Time-based multi-frame calibration (NEW, robust)
- **Status**: Active and recommended

---

## Step-by-Step Guide

### 1. Open Tracking Settings Panel
- Look at the **RIGHT SIDE** of the UI
- Find the "Tracking Settings" panel
- If it's collapsed, expand it

### 2. Scroll to Color Calibration Section
- In the Tracking Settings panel, scroll down
- Find the section titled **"🎨 Color Calibration"**
- This section should be expanded by default

### 3. Select Your Ball Color
- You'll see a group box for each color (Pink, Red, Blue, etc.)
- Each group shows:
  - "Track [Color]" toggle button
  - Current calibration values (Average Hue, Average Saturation)
  - HSV ranges (Min/Max)
  - **"🎯 Calibrate Color (10s)"** button ← THIS IS THE ONE!
  - Info text: "ℹ️ Juggle the ball for 10 seconds to calibrate"

### 4. Start Calibration
1. Click the **"🎯 Calibrate Color (10s)"** button for your chosen color
2. You'll see: "Get ready to juggle! Starting in 5... 4... 3... 2... 1..."
3. Get into position with the ball you want to calibrate

### 5. Recording Phase
1. After the 5-second countdown, you'll see: "Juggle the ball! Recording: 10... 9... 8..."
2. **Throw the ball back and forth** (or juggle it)
3. The system collects color samples from ALL depth blobs detected
4. The whiteness filter automatically removes overly bright pixels

### 6. Processing & Completion
1. After 10 seconds, you'll see: "Processing... Please wait."
2. The system calculates the median hue and saturation
3. Success message: "✅ Calibration complete! Captured [N] samples."
4. The calibration values update automatically
5. The engine reloads the new color profile

---

## Visual Guide

```
┌─────────────────────────────────────────────────────────────┐
│                    JuggleHub Main Window                     │
├──────────────────────────────┬──────────────────────────────┤
│                              │  TRACKING SETTINGS PANEL     │
│  Video Feed                  │  (RIGHT SIDE - LOOK HERE!)   │
│  (Main visualization area)   │                              │
│                              │  ▼ 🎨 Color Calibration      │
│  ┌────────────────────────┐  │                              │
│  │ Calibration &          │  │  ┌─────────────────────────┐│
│  │ Visualization          │  │  │ Pink                    ││
│  │                        │  │  │ ☑ Track Pink            ││
│  │ ❌ Set Color Profile   │  │  │ Average Hue: 170.5°     ││
│  │    (OLD - DON'T USE!)  │  │  │ Average Sat: 180.2      ││
│  └────────────────────────┘  │  │ Min HSV: H:0 S:0 V:0    ││
│                              │  │ Max HSV: H:180 S:255... ││
│                              │  │                         ││
│                              │  │ ✅ 🎯 Calibrate Color   ││
│                              │  │    (10s)                ││
│                              │  │    ← USE THIS BUTTON!   ││
│                              │  │                         ││
│                              │  │ ℹ️ Juggle the ball for  ││
│                              │  │    10 seconds to        ││
│                              │  │    calibrate            ││
│                              │  └─────────────────────────┘│
│                              │                              │
│                              │  (Scroll down for more       │
│                              │   color profiles...)         │
└──────────────────────────────┴──────────────────────────────┘
```

---

## Troubleshooting

### "I don't see the Tracking Settings panel"
- Make sure you're in the main JuggleHub window
- The panel should be on the right side
- Try resizing the window if it's too narrow

### "I don't see the Color Calibration section"
- Make sure you have the **New 3D tracker** selected
- Scroll down in the Tracking Settings panel
- The section should be expanded by default

### "The countdown didn't start"
- Make sure you clicked the button in the **Tracking Settings panel**, not the main visualization area
- Check that depth blob detection is enabled
- Look for the status label below the button for error messages

### "No samples were collected"
- Make sure depth blob detection is enabled
- Check that the whiteness threshold is set appropriately
- Verify that balls are being detected (you should see depth globs in the visualization)

---

## Requirements

For time-based calibration to work, you need:

1. **New 3D Tracker** selected as the active tracker
2. **Depth Blob Detection** enabled (in the "🔍 Depth-Based Blob Detection" section)
3. **Depth camera** connected and working
4. **Balls visible** in the depth camera's field of view

---

## Tips for Best Results

1. **Use good lighting** - helps with color detection
2. **Juggle continuously** - more frames = better calibration
3. **Vary the ball position** - different angles and distances
4. **Keep the ball in frame** - don't throw it out of view
5. **Use the whiteness filter** - set it appropriately for LED balls (e.g., 200)

---

## Technical Details

- **Preparation time**: 5 seconds
- **Recording time**: 10 seconds
- **Sample collection**: Every frame during recording
- **Averaging method**: Median (robust against outliers)
- **Whiteness filter**: Applied automatically during color sampling
- **Storage**: `hub/calibration_settings_new3d.json`

---

## Next Steps After Calibration

1. The new color profile is **immediately active**
2. The engine automatically reloads the profiles
3. You can verify the calibration by checking the updated values
4. Start juggling and the tracker should now correctly identify your ball!

---

*Last updated: 2025-11-07*