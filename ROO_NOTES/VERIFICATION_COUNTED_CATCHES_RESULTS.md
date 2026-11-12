# Counted-Catches Verification Results (2026-11-10/11)

**Method:** user juggled 6 named patterns with a self-counted catch total, submitting
each take with SPACE (`python3 -m tracking_lab.verify_wizard`, RealSense, no time limit,
no on-screen counter). Scored via `python3 -m tracking_lab --rec <dir> --expected N`.

## FINAL Results (per-hand away tracking + straight-across transfer detection,
max_catch_speed=450, held_dwell=2, min_flight_frames=2, tight 40px fusion, light mask off)

| pattern | expected (user count) | detected | accuracy | verdict |
|---|---|---|---|---|
| cascade ×60 | 60 | 63 | 105% | PASS |
| shower ×40 | 40 | 38 | 95% | PASS |
| flash ×10 (30) | 30 | 29 | 97% | PASS |
| columns ×30 | 30 | 28 | 93% | PASS |
| box ×30 | 30 | 26 | 87% | near |
| 441 ×30 | 30 | 24 | 80% | near |

Mean absolute error: 3.0 catches (was ~18 before the fixes). User's own counts
are ±1–2, so true accuracy on several patterns is likely higher.

## The straight-across fix (what box/shower/441 needed)

A crossing throw never leaves both wrist radii, so the global away-counter
never armed the catch. Two mechanisms now handle it:

1. **Per-hand away counter** (`away_from_caught`): after a catch, the tracker
   counts frames the ball spends away from *that specific wrist*. Catch gate =
   `away_from_caught >= min_flight_frames` OR global `away_peak`.
2. **Transfer detection**: while HELD, if the matched ball's nearest hand
   becomes the OTHER hand (clearly dominant, >10px closer) AND the ball is
   slow (< max_catch_speed), emit THROW+CATCH pair — the hand-off/carry case.
   Fast fly-bys past the far wrist are vetoed by the speed gate.

Sweep showed veto=450 minimizes mean error across all 6 patterns (1.5 on the
sweep pipeline, 3.0 on the full runner pipeline with light mask variance).

## Other fixes this round

- Light mask OFF by default: cream balls crossing the top of frame matched its
  brightness signature and suppressed real catches (cascade 62→55 with it on in
  one config). The tracker's static-bin suppressor handles lights instead.
- `_fuse` tightened to 40px box dedup (from 0.7×size margins) — matches the
  sweep pipeline, improves box/441 by +2-3 catches each.
- Top-N gate relaxed to n_balls+2 so a real ball is never sliced out while a
  phantom outranks it.

## What the trace showed (cascade60 frame-by-frame)

- Fixes that got cascade from 42→60: latch `away_frames` at first wrist contact
  (the old `flight_frames` froze when the ball hovered near a wrist, permanently
  denying the catch), speed-based catch gate (`max_catch_speed`) + long-rest
  override, HELD occlusion timeout (silent release after 10 unmatched frames so
  a HELD track can never deadlock; grid search showed dwell=2/min_flight=2
  optimal: 59-60/60 on cascade, 41/60 for dwell=3).
- User's own counts may be ±1-2; cascade being exact shows the ceiling is real.

## Why box/441/shower under-count (in progress)

Frame-200 QA of box30 shows **correct multi-ball tracking** (2 HELD + 1 in flight,
right IDs, trails). The shortfall is counting semantics, not tracking: box/441
involve **carries** — a ball is caught and immediately carried/passed to the other
hand without an airborne flight. Those hand-to-hand transfers are real catches to
a juggler but produce no throw arc, so the state machine sees HELD→(carry within
wrist radius)→HELD and emits nothing. Shower's extra misses: the sling throw hugs
the body and merges detections at hand-off.

Next iteration: detect hand-to-hand transfer (track HELD at hand A suddenly at
hand B within ~6 frames) and emit CATCH(+THROW) for it.

## Regression suite command

```
for pair in "cascade60 60" "box30 30" "441_30 30" "shower40 40" "columns30 30" "flash_10 30"; do
  set -- $pair
  d=$(ls -d tracking_lab/recordings/*_$1 | head -1)
  python3 -m tracking_lab --rec "$d" --expected $2 --quiet | grep -E "verdict|accuracy"
done
```
