# Tracking Lab Results — Multi-Ball Validation (2026-11-10)

**Data:** 6 new recordings via the guided wizard (`python3 -m tracking_lab.wizard`),
~1800 frames each (RealSense RGB+depth) / ~1190 (webcam), recorded by the user.

## Final metrics (tracker: YOLO+MOG2 fusion, Hungarian, debounced events)

| session | camera | coverage | throws | catches | tracks | avg |
|---|---|---|---|---|---|---|
| cascade3_mixed | RealSense | 99.6% | 98 | 100 | 4 | 2.99 |
| cascade3_same (identical) | RealSense | 98.9% | 101 | 102 | 6 | 2.99 |
| cascade3_glow_mixed | RealSense | 99.7% | 84 | 86 | 3 | 2.99 |
| cascade3_mixed_webcam | webcam | 97.1% | 71 | 72 | 25 | 3.03 |
| cascade3_same_webcam | webcam | 94.6% | 51 | 51 | 47 | 3.02 |
| columns_shower | RealSense | 99.6% | 133 | 135 | 3 | 2.99 |

RealSense sessions: stable identity, coverage ≥98.9%, throws≈catches (a healthy
cascade), no ceiling-light or clutter false positives. Review renders:
`.tmp/final_<tag>.mp4` (annotated: track IDs, states, event banners, trails).

## What made it work (all in tracking_lab/)

1. **YOLO+MOG2 detection fusion** ([detectors.py](tracking_lab/detectors.py),
   [__main__.py](tracking_lab/__main__.py)): yolo11n misses motion-blurred
   white-on-white balls; MOG2 background subtraction catches them. YOLO vets
   blobs; union feeds the tracker.
2. **Static light mask** (`_light_mask` in [__main__.py](tracking_lab/__main__.py)):
   ceiling spotlights read as 0.93-confidence balls. Mask = bright + low-saturation
   pixels stable across the whole session (median over uniformly-sampled frames),
   top 40% of frame, dilated. Kills light FPs everywhere, robust to scene drift.
3. **HELD tracks stick to the wrist** ([tracker.py](tracking_lab/tracker.py)):
   a held ball occluded by its hand stays alive following the wrist instead of
   coasting and dying — this was the webcam track-churn driver.
4. **Real capture rate** ([recording.py](tracking_lab/recording.py)): the webcam
   recorded 19.8 fps, not 30; velocity/prediction math was 1.5× off. Now read
   from meta.json stamps.
5. **State machine fixes**: THROW only after genuine flight (`flight_frames ≥
   min_flight_frames`), CATCH requires real flight since last hold; pending-tracks
   need `min_hits=3` and die on first miss; ID stitching reserves last-seen
   positions of dead tracks (kept for future use).

## Remaining issue

Webcam + identical balls: identity still churns at cascade crossings
(25–47 tracks over a session; avg concurrent = 3.0 so coverage is fine, but IDs
swap). Cause: two balls merge into one detection at the crossing apex; on split,
association can assign the wrong one and the loser re-spawns. Next steps if
webcam-only same-ball identity matters: parabolic trajectory prediction through
the merge window, or prefer MOG2 blob split association over re-spawn.

## Recommendation

Use the RealSense path in the engine (port `tracking_lab/tracker.py` semantics
into New3DTracker or as its replacement): it is validated at ~99%+ coverage with
stable IDs and balanced events on all ball types tested — mixed, identical,
glowing. Webcam works for presence/position tracking; identity on identical
balls needs the crossing fix above.
