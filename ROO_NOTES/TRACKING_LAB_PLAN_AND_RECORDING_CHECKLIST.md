# Tracking Lab — Status, Findings, and Recording Checklist

**Date:** 2026-11-10
**Goal:** Robust real-time juggling tracking — per-ball identity, throw/catch events, works with same-color and glowing/non-glowing balls, webcam preferred (depth optional), pose for hand logic.

## Key findings from the existing corpus

1. **All 8 recordings in `engine/data/1_raw_recordings/` are single-ball sessions.**
   Raw pre-NMS predictions from yolo11n (V8) contain literally one object per frame
   (`conf ~0.93`, cls `ball_held`/`ball`). Lowering conf 0.25 → 0.15 changes nothing —
   there is nothing to recover. This fully explains the engine logs: only "Ball 0 (pink)"
   ever produces THROW/CATCH events. **We have zero multi-ball recordings to iterate on.**
2. **Engine event spam was an architecture problem** (no debouncing): the log alternates
   THROW/CATCH every 1–2 frames. The new tracker's debounced events (held_dwell=5,
   throw_dwell=3, min_flight=8) produce 3 throws / 5 catches over the same 356-frame
   session — sane for single-ball.
3. **Pose detector bug fixed**: yolo11n-pose had no NMS in our wrapper → 10 duplicate
   "persons" per frame. Now returns exactly 1 person with clean wrists/elbows/nose.
4. **Offline pipeline speed**: ~15 fps including JPEG I/O + both detectors on CPU
   (det ~17 ms + pose ~25 ms + I/O). Real-time budget at 30 fps is feasible.

## New architecture (tracking_lab/)

- **Detector:** yolo11n V8 (ball / ball_held) — champion from the 12-model bake-off.
- **Identity:** motion-first Hungarian assignment (distance + soft color-histogram cost).
  Color helps but never hard-binds a ball to a track (fixes the "only pink Ball 0" failure).
- **States:** HELD / IN_FLIGHT / COAST (coast through occlusions, gravity prior).
- **Events:** wrist-proximity HELD detection → debounced CATCH/THROW.
- **Metrics:** full_coverage_pct, id_resets, throws, catches; renders annotated mp4 for review.
- **Recording:** `python3 -m tracking_lab.record` produces the same layout as engine
  recordings (`no_boxes/`, `depth/`, `meta.json`) from realsense or plain webcam.

## What I need from you (recording checklist)

Use: `python3 -m tracking_lab.record --source realsense --seconds 60 --label <label>`
(or `--source webcam` for webcam-only). Sit/stand as for normal juggling, whole body +
full ball arc in frame, ~2–3 m from camera. Record:

| # | Label | Balls | Camera | Notes |
|---|-------|-------|--------|-------|
| 1 | `cascade3_mixed` | 3 distinct colors | realsense | baseline: identity via color |
| 2 | `cascade3_same` | 3 identical (non-glowing) | realsense | hardest: identity via motion only |
| 3 | `cascade3_glow_same` | 3 identical glowing | realsense | LED-on same color |
| 4 | `cascade3_glow_mixed` | 3 glowing distinct colors | realsense | LED-on mixed |
| 5 | `cascade3_mixed_webcam` | 3 distinct colors | webcam | webcam-only path |
| 6 | `cascade3_same_webcam` | 3 identical | webcam | webcam-only, no depth |
| 7 | `drops3_mixed` | 3 distinct colors | realsense | include a few deliberate drops + picks |
| 8 | `columns_or_shower` | 3 distinct colors | realsense | a second pattern (asynchronous timing) |

60–90 s each is plenty. Priority order if short on time: 1, 2, 6, 5, then the rest.

## Iteration plan once recordings exist

1. Baseline all recordings with current tracker (`python3 -m tracking_lab --rec ... --render ...`).
2. Tune assoc gate / coast / debounce from renders + metrics.
3. If same-color identity swaps: add trajectory-based re-ID (parabolic fit over COAST window).
4. If detection recall drops on glowing balls: mix glowing frames into V11 dataset
   (SAM 3 can pre-label — this is why we keep it).
5. Depth: evaluate as gate/verification only; drop if webcam-only works.
