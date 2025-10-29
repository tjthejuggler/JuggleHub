# Ball Tracking Tuning Guide
**Date:** 2025-10-04  
**Purpose:** Guide for tuning the adaptive color tracking system for consistent ball tracking

---

## Problem Identified

The tracking system was inconsistent because:
1. **Adaptive system was running but invisible** - No UI controls to monitor or tune it
2. **No feedback on what's happening** - Couldn't see success rates or current HSV ranges
3. **No way to adjust parameters** - Fixed parameters that might not work for all lighting conditions

## Solution Implemented

Added a new **"🎨 Adaptive Color Tracking"** section to the calibration settings with full control over the adaptive system.

---

## New UI Controls

### Location
**Settings Tab** → **🎨 Adaptive Color Tracking** section

### Available Controls

#### 1. Enable/Disable Toggle
- **Control:** "Enable Adaptive Ranges" button
- **Purpose:** Turn the entire adaptive system on/off
- **Default:** Enabled
- **When to disable:** If you want completely manual control via the hue sliders

#### 2. Success Threshold (50-95%, default 70%)
- **What it does:** When a color's success rate exceeds this, its range contracts
- **Higher value:** More aggressive contraction, tighter ranges
- **Lower value:** Less aggressive, keeps ranges wider
- **Tune this if:** Colors are tracking too loosely or picking up wrong objects

#### 3. Failure Threshold (5-50%, default 30%)
- **What it does:** When a color's success rate falls below this, its range expands
- **Higher value:** Expands sooner, more forgiving
- **Lower value:** Only expands when really struggling
- **Tune this if:** System loses tracking too easily

#### 4. Expansion Step (1-10 degrees, default 2)
- **What it does:** How much to widen the hue range when failing
- **Higher value:** Faster adaptation but less stable
- **Lower value:** Slower adaptation but more stable
- **Tune this if:** System takes too long to find balls or is too jittery

#### 5. Contraction Step (0.5-5 degrees, default 1)
- **What it does:** How much to narrow the hue range when succeeding
- **Higher value:** Faster convergence but may lose tracking
- **Lower value:** Slower convergence but more stable
- **Tune this if:** System keeps losing balls after finding them

#### 6. History Window (30-120 frames, default 60)
- **What it does:** How many frames to track for calculating success rate
- **Higher value:** More stable metrics but slower to adapt
- **Lower value:** Faster adaptation but more reactive to noise
- **Tune this if:** System is too slow or too reactive to changes

#### 7. Min Color Confidence (5-50%, default 10%)
- **What it does:** Minimum color match score to count as successful tracking
- **Higher value:** Stricter matching, may miss balls
- **Lower value:** More lenient, may track wrong objects
- **Tune this if:** Getting false positives or missing valid balls

---

## Tuning Strategy

### For Inconsistent Tracking

**Symptoms:** Balls flicker in and out, IDs switch frequently

**Try this:**
1. **Increase History Window** to 90 frames (more stable)
2. **Decrease Expansion Step** to 1 degree (less jittery)
3. **Increase Failure Threshold** to 40% (expand sooner)

### For Lost Tracking

**Symptoms:** System loses balls and doesn't recover

**Try this:**
1. **Increase Expansion Step** to 3-4 degrees (adapt faster)
2. **Decrease Failure Threshold** to 20% (expand more aggressively)
3. **Decrease Min Color Confidence** to 5% (more lenient matching)

### For False Positives

**Symptoms:** Tracking wrong objects, multiple balls on one object

**Try this:**
1. **Increase Min Color Confidence** to 20-30% (stricter matching)
2. **Decrease Success Threshold** to 60% (contract ranges sooner)
3. **Increase Contraction Step** to 1.5-2 degrees (tighten ranges faster)

### For Slow Adaptation

**Symptoms:** Takes too long to find balls after lighting changes

**Try this:**
1. **Decrease History Window** to 40 frames (react faster)
2. **Increase Expansion Step** to 3-4 degrees (adapt faster)
3. **Increase Failure Threshold** to 40% (trigger expansion sooner)

---

## Monitoring

### Status Display
The text box at the bottom of the Adaptive Color Tracking section will show:
- Current success rates for each enabled color
- Current HSV ranges being used
- Recent adaptation events (expansions/contractions)

### Debug Logs
For detailed monitoring, check the engine debug log:
```bash
tail -f engine_debug.log | grep -E "(ADAPTIVE|COLOR-DOMINATED)"
```

This will show:
- Frame-by-frame color matching results
- Range adjustments as they happen
- Success rate calculations
- Conflict resolution between colors

---

## Recommended Starting Points

### Good Lighting (Bright, Even)
- Success Threshold: 75%
- Failure Threshold: 25%
- Expansion Step: 2 degrees
- Contraction Step: 1 degree
- History Window: 60 frames
- Min Confidence: 15%

### Poor Lighting (Dim, Uneven)
- Success Threshold: 65%
- Failure Threshold: 35%
- Expansion Step: 3 degrees
- Contraction Step: 0.8 degrees
- History Window: 75 frames
- Min Confidence: 8%

### Fast Juggling (High Speed)
- Success Threshold: 70%
- Failure Threshold: 30%
- Expansion Step: 3 degrees
- Contraction Step: 1.2 degrees
- History Window: 45 frames
- Min Confidence: 10%

---

## Reset to Defaults

If you get lost in tuning, click the **"Reset to Defaults"** button at the bottom of the Adaptive Color Tracking section to restore all values to their optimal starting points.

---

## Next Steps

1. **Run the system:** `./scripts/run_hub.sh --use-venv --device GPU`
2. **Open Settings Tab** in the UI
3. **Expand "🎨 Adaptive Color Tracking"** section
4. **Start with defaults** and observe the status display
5. **Adjust one parameter at a time** based on what you observe
6. **Save settings** (Ctrl+S) when you find good values

The system should converge to optimal ranges within 3-5 seconds of starting. Watch the status display to see the adaptation happening in real-time.