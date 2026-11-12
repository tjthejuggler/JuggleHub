# Tracking Improvements Backlog + Change Log

Living document. Every change to the tracker gets an entry here with its before/after
scores on the regression suite. If a change makes things worse, revert it and note why.

**AUDITED 2026-11-12.** Entries marked **[CORRECTED 2026-11-12]** were corrected by the audit; the original text is kept where it is history. Findings and the current plan: [`ROO_NOTES/AUDIT_2026-11-12.md`](ROO_NOTES/AUDIT_2026-11-12.md). Short version: the two LED recordings are invalid (5 real fps), the exposure-lock work is withdrawn, the six videos below are a tuning set, and there is no valid held-out recording yet.

## Regression suite (run after every change)

```bash
for pair in "cascade60 60" "box30 30" "441_30 30" "shower40 40" "columns30 30" "flash_10 30"; do
  set -- $pair
  d=$(ls -d tracking_lab/recordings/*_$1 | head -1)
  python3 -m tracking_lab --rec "$d" --expected $2 --quiet | grep -E "verdict|accuracy"
done
```

**[CORRECTED 2026-11-12]** Preferred command. It replays cached detections (under a second once the cache exists), prints the structural diagnostics, rejects recordings with repeated colour frames, and scores held-out takes separately:

```bash
python3 -m tracking_lab.evaluate            # add --sweep for constant sensitivity
TRACKING_BALL_MODEL=/path/to/model.xml python3 -m tracking_lab.evaluate
```

Rules from the audit: a change is judged on held-out takes (record them with `python3 -m tracking_lab.verify_wizard --heldout <tag>`) and on the diagnostics, not on the six-video total alone. The six videos were used to pick every constant.

Ground truth = user's hand counts (±1–2 tolerance acknowledged).
Review renders: `.tmp/final_*.mp4`, `.tmp/verify_*.mp4`.

## Scorecard (mean abs catch error across the 6 counted videos)

| date | change | cascade | box | 441 | shower | columns | flash | mean err |
|---|---|---|---|---|---|---|---|---|
| 11-10 | baseline (before tuning) | 25/60 | 15/30 | 10/30 | 13/40 | 19/30 | 17/30 | ~12.8 |
| 11-11 | face-region suppression 55px | 57/60 | 13/30 | 18/30 | 19/40 | 17/30 | 26/30 | ~13.7 **REVERTED** — balls thrown across the face were suppressed; face-phantom needs a track-level fix instead |
| 11-11 | track-level face-phantom kill (stall 20f on face + slow) | 63/60 | 26/30 | 24/30 | 38/40 | 28/30 | 29/30 | ~2.9 — no regression; face tracks now die automatically |
| 11-11 | same-hand catch dedup (min_catch_gap=4, THROW resets) | 62/60 | 26/30 | 24/30 | 38/40 | 28/30 | 28/30 | ~2.9 — no change; close-pair trace showed pairs were distinct-track real catches. box/441 gap = catches invisible to BOTH yolo and MOG2 during hand-occlusion, not dedup |
| 11-11 | **V11 detector** (retrained on our recordings, 559 GLM-labeled frames) | 59/60 | 24/30 | 27/30 | 29/40 | 39/30 | 30/30 | ~3.3 — mixed: flash 100% EXACT, cascade 98.3%, 441 90%, but box down (26→24) and columns over (+9→130%). LED session (216 count, auto exp): 138 vs V8's 137 — no big LED gain yet. KEEP BOTH: V11 promising on flash/441/shower-class frames; needs more LED-streak + hand-occluded training frames. V8 remains default until V11 wins the full suite. |
| 11-10 | dwell/flight tuning + fusion | 35/60 | 11/30 | 19/30 | 22/40 | 27/30 | 23/30 | ~8.0 |
| 11-11 | per-hand away + transfers (veto 450) | 62-63/60 | 26-29/30 | 24-28/30 | 38-39/40 | 28-30/30 | 29-32/30 | **~2.2** |
| 11-11 | carry detection v1 (silent-release → other-hand catch) | 63/60 | 26/30 | 24/30 | 38/40 | 28/30 | 29/30 | ~2.2 (no change — carries never fired; see note) |
| 11-11 | dominance margin 10→3 (equidistant swap rule) | 57-63/60 | 26/30 | 28/30 | 40/40 | 26/30 | 34/30 | ~2.8 (helps shower→40/40, 441→28; cascade/flash now over-count; margin vs speed veto trade-off at plateau) |

(±1–3 variance depending on light mask / fusion margins; final config: veto=450,
dwell=2, min_flight=2, tight 40px fusion, light mask OFF, static-bin suppressor ON.)

**[CORRECTED 2026-11-12]** **Recomputed scorecard.** In five of the nine rows above, the "mean err" value does not match the counts written in the same row (how those values were calculated is not recorded). Recalculated from each row's own counts (for a range, best case to worst case):

| date | change | counts as written | stated | recomputed |
|---|---|---|---|---|
| 11-10 | baseline (before tuning) | 25/60 15/30 10/30 13/40 19/30 17/30 | ~12.8 | 20.17 |
| 11-11 | face-region suppression 55px | 57/60 13/30 18/30 19/40 17/30 26/30 | ~13.7 | 11.67 |
| 11-11 | track-level face-phantom kill (stall 20f on face + slow) | 63/60 26/30 24/30 38/40 28/30 29/30 | ~2.9 | 3.00 |
| 11-11 | same-hand catch dedup (min_catch_gap=4, THROW resets) | 62/60 26/30 24/30 38/40 28/30 28/30 | ~2.9 | 3.00 |
| 11-11 | V11 detector (retrained on our recordings, 559 GLM-labeled f | 59/60 24/30 27/30 29/40 39/30 30/30 | ~3.3 | 5.00 |
| 11-10 | dwell/flight tuning + fusion | 35/60 11/30 19/30 22/40 27/30 23/30 | ~8.0 | 13.83 |
| 11-11 | per-hand away + transfers (veto 450) | 62-63/60 26-29/30 24-28/30 38-39/40 28-30/30 29-32/30 | ~2.2 | 1.00 to 3.17 |
| 11-11 | carry detection v1 (silent-release → other-hand catch) | 63/60 26/30 24/30 38/40 28/30 29/30 | ~2.2 | 3.00 |
| 11-11 | dominance margin 10→3 (equidistant swap rule) | 57-63/60 26/30 28/30 40/40 26/30 34/30 | ~2.8 | 2.33 to 2.83 |
| 11-12 | audit re-run, V8, code as it stands | 63/60 26/30 24/30 38/40 28/30 28/30 | — | **3.17** |
| 11-12 | audit re-run, V11 | 59/60 24/30 27/30 29/40 39/30 30/30 | — | **5.00** |

The audit re-runs are deterministic (same numbers on every run). Diagnostics for the V8 row: mean |throw err| 4.67, mean |catches − throws| 1.83, 68% of catches backed by an observed flight, two balls HELD in one hand in 14–44% of frames. The V11 row's "LED session: 138 vs V8's 137" compares two models on a recording with 5 real fps; it carries no information. V11 was also trained on 240 frames from these six videos.

## Backlog (priority order)

### 1. Exposure lock on RealSense — WITHDRAWN (2026-11-12 audit; was marked DONE)
**[CORRECTED 2026-11-12]** **Everything in the original entry below is invalid. Do not use any setting from it.**
- The values 8000 / 4000 / 2000 were passed to the RGB exposure option and labelled "8 ms / 4 ms / 2 ms". Measured on the saved clips, those settings delivered 1.3 / 2.6 / 5.1 unique colour frames per second (each image repeated 24 / 12 / 6 times). Only "auto" was a real 30 fps stream.
- "2ms_g16 ← WINNER" was chosen by counting detections on repeated, motion-smeared frames.
- The "400us/gain64 optimum" and the "detection cliff at 400us" came from the same mis-scaled values, and so does "glowing balls paint 10x more overexposed pixels under the lock".
- The likely cause is that one exposure unit is 100 µs, not 1 µs. That is inferred from the frame rates and is **not confirmed on the camera**. The check is described in [`tracking_lab/camera.py`](tracking_lab/camera.py).
- `TRACKING_LED_LOCK` and `TRACKING_EXPOSURE_LOCK` are retired. Manual exposure is `TRACKING_EXPOSURE_MS=<ms>` with optional `TRACKING_GAIN=<g>`. All scripts default to auto exposure, set explicitly.

Original entry, kept for the record:

User ran [`tracking_lab/exposure_test.py`](tracking_lab/exposure_test.py).
Results (.tmp/exposure_test_results.json):
  auto:    2.31 dets/frame, 32.5% frames with all 3 balls, conf 0.94
  8ms_g4:  1.32, 0%, conf 0.86  (too dark)
  4ms_g8:  2.14, 34.7%, conf 0.80
  2ms_g16: 2.43, 53.9%, conf 0.79  ← WINNER (+66% full-ball frames vs auto)
2ms/gain16 lock was wired in and user recorded a glowing LED cascade with it —
tracking REGRESSED (92.6% coverage, 56% event recall). Diagnosis: glowing LED
balls paint 10x more overexposed pixels under the 2ms lock (50-61k px vs 6-7k
on auto) — giant merged light trails that yolo cannot see. The OLD auto-exposure
glowing session tracked 99.7% with only 6-7k overexposed px.
FINAL RESOLUTION (after full hardware sweep): 400us/gain64 is the OPTIMAL LED
setting — 400us is the physical detection cliff edge for these LEDs (measured
dots: 400us=5.3-6.8, 350us=5.3, 300us=5.1, 250us=1.0, 200us=1.0 — balls vanish
below 400us; higher gain 80/128 only amplifies noise into phantom dots).
Auto exposure was already 0.4ms on this camera but lets trails grow in dim
moments; the 400us/g64 lock holds the cliff edge steady. Auto remains default
for non-LED content; LED lock via TRACKING_LED_LOCK=1 env var.

### 2. Retrain YOLO (V11) — detector is the bottleneck
Train set: existing annotation sessions + new recordings pre-labeled by SAM 3 +
motion-blur augmentation + failure crops (white-on-wall, glowing, blur).
NEEDS: compute time only; dataset assembly is autonomous.
**[CORRECTED 2026-11-12]** "Detector is the bottleneck" is not established. On the six videos the fused detector returns at least 3 candidates in 84% of frames (3 in 64.0%, 4 or more in 20.1%, fewer than 3 in 15.9%), while two balls share a hand in 14–44% of frames and 32% of catches have no observed flight. Those numbers point at association and event logic at least as much as at detection. The dataset work is ready when it is needed: `.tmp/v12_clean`, built by [`tracking_lab/build_v12_dataset.py`](tracking_lab/build_v12_dataset.py). It has not been trained.

### 3. Box/441 under-count root cause (2026-11-11 dense annotation)
NOT dedup, NOT missed regions: dense (step-3) GLM annotation of box30 reports
exactly 30 catch-frames (= user count), 8 temporal clusters, and ALL clusters
have tracker catches. The 4-6 gap = same-hand rapid double-catches where the
second ball's catch is debounced (tracker fires 1 event for 2 balls landing
within held_dwell frames of each other at one hand) PLUS catches that happen
while the ball is fully hand-occluded (invisible to yolo AND MOG2). The
remaining box/441 gap is bounded at ~2-6 catches and partly attributable to
the user's ±1-2 count error. Priority lowered; the per-hand catch dedup
(min_catch_gap) added this round is neutral-positive.

### 3b. Occlusion-aware carry detection — (see 3)
Under-arm carries hide the ball 5+ frames; currently a silent HELD release.
Fix v1 implemented (silent-release → other-hand catch within 0.5 s → CATCH
flagged carry=True). RESULT: 0 fires on box30 — because the box event stream
is already continuous (no >25f gaps), meaning the tracker's 26 events ARE the
airborne exchanges; the 4-catch gap is most likely hand-to-hand transfers where
the ball never leaves the wrist neighborhood of EITHER hand (it swaps hands
while both wrists are within held_radius). NEXT: side-dominance transfer needs
a lower dominance margin (10px → 6px) OR count hand-swap while HELD even when
both wrists are in radius (currently requires other hand strictly closer by
10px — in a box carry the ball stays equidistant). NEEDS: user video of box
with slow-motion, exaggerated carries to tune against.
STATUS: v1 in tracker.py (no regression, no gain).

### 4. Crossing disambiguation via depth
Depth currently only z-smooths. Two balls at same pixel = different z. Use depth
at detection centers to keep associations separate during crossings. Only needed
for 4+ balls or tight patterns. RealSense sessions already at 99%+; low priority.

### 5. Kalman constant-acceleration filter
Replace alpha velocity filter with proper CA Kalman + covariance. Sharper crossing
association + real throw-velocity output for physics side. Medium effort, medium gain.

### 6. Per-hand HELD exclusivity
A hand cannot hold 2 balls; if 2 tracks claim the same hand, the older one is
phantom → force release. Reduces phantom HELD absorbs seen in traces.
**[CORRECTED 2026-11-12]** Measured by the audit: two balls are HELD in the same hand in 37.5% of cascade60 frames, 35.2% box30, 30.4% 441_30, 44.3% shower40, 22.2% columns30, 14.5% flash_10. This is now the best-supported tracker item on the list.

### 7. Camera settings experiments
Exposure lock (see #1); also backscatter/laser emitter power for low-texture scenes;
auto-exposure priority off. needs_test.
**[CORRECTED 2026-11-12]** Any exposure experiment must report unique colour frames per second next to its result. [`tracking_lab/exposure_test.py`](tracking_lab/exposure_test.py) now does, and disqualifies settings with repeated frames. It has not been run on a camera since the rewrite.

### 8. More recordings (ask user when needed)
- box/441 with deliberate under-arm carries (for #3 ground truth)
- 4-ball cascade (top-N gate stress)
- **[CORRECTED 2026-11-12]** first of all: held-out counted takes at verified auto exposure (`python3 -m tracking_lab.verify_wizard --heldout h1`), plus one LED cascade (`--heldout led1 --only cascade60`). Both existing LED recordings are invalid.
- optional: fast shower, 5-ball flash

## Auto-annotation reliability calibration (2026-11-11)

GLM-5.3-flash (Z.ai coding-plan endpoint `api.z.ai/api/coding/paas/v4`) annotates
frames via [`tracking_lab/auto_annotate.py`](tracking_lab/auto_annotate.py);
claims are verified visually via [`tracking_lab/verify_auto.py`](tracking_lab/verify_auto.py)
which renders each claim with the flagged track circled red, all tracks green,
wrists blue.

First calibration on box30 (12 claims adjudicated by eye):
- "held on empty hand" claims: **wrong** — the ball was present but occluded by
  the hand (model misses occluded balls). Treat these as weak signals.
- "phantom" claims: mixed — track-on-face at f195 was a TRUE phantom (false det
  on face), but f90/f45 "phantoms" had real balls inside the circle (shoulder
  ball). Precision ~50% on phantoms.
- "missed ball" notes: CORRECT (found untracked floor ball at (478,445)).
- Ball counting: correct (3/3 visible).

Conclusion: usable as a *frame-finder* (its flags locate real trouble regions
~50-100% of the time) but every claim must be geometrically/visually verified
before acting. The verify_auto overlay renders make that check fast.

### Full auto-annotation run (2026-11-11, all 6 videos)

Parallelized (`--workers 3`, thinking mostly disabled): 137 frames in ~7.6 min
(0.30 req/s, 20x faster than sequential). Results: `.tmp/count_*_auto.json`,
`.tmp/annotation_summary.json`, verification overlays `.tmp/verify_auto/*/`.

Key stats + calibrated conclusions:
- "held on empty hand" = top flag everywhere (51 in cascade60). Geometry
  cross-check: EVERY flagged HELD track sits at distance 0 from a wrist; visual
  check (cascade f405, box f120): the ball IS there, gripped and occluded by
  the hand. The model cannot see occluded balls. The tracker is right; these
  are occlusion false-alarms. cascade60 ball-count histogram shows 2 visible
  balls in 20/56 frames — consistent with occlusion, not drops (coverage 99.6%).
- "phantom" flags = the real bug class. VERIFIED TRUE: track chasing a false
  detection on the juggler's FACE (box30 f195). yolo face false-positives need
  suppression (pose nose + radius → ignore ball dets in face region).
  Model precision on phantoms ~50% (shoulder balls mis-flagged).
- "carry" reports are unreliable (model calls short cascade arcs "carries",
  25/56 frames in cascade60). Need the exaggerated-carry recording to tune.
- Event timing usable as a density signal: model event frames per video
  (441:12/16 samples, columns:21/27, flash:11/21) roughly match expected
  catch density per pattern.

### Dense-annotation verdict (2026-11-11, box30 + 441_30 at --step 3)

153 additional frames annotated (box 73, 441 80). Cluster analysis of the
model's catch reports vs tracker events:

- **box30**: model = 30 catch-frames (matches user count 30!) in 8 temporal
  clusters; tracker caught 26 events — and **all 8 model clusters are TRACKED**
  (a tracker CATCH within ±14 frames of every cluster). The "missing" 4 are
  INSIDE tracked clusters: double-catches within one cluster (two balls caught
  2-3 frames apart, e.g. tracker frames 100+102, 114+116, 137+137, 227-231×4)
  are merged/deduped by the state machine when both land at the same hand
  within the dwell window.
- **441_30**: model = 34 catch-frames in 9 clusters; tracker 24 — all 9
  clusters TRACKED. Same double-catch merging (116/117/118 triple!).

Conclusion: the tracker is not missing catches; it is UNDER-COUNTING when two
balls are caught at the same hand within `held_dwell` frames — a real semantic
gap. Fix direction: per-catch dedup must consider BALL IDENTITY (two distinct
tracks arriving at one hand within the window = two catches), which requires
per-hand catch timestamps per track rather than the current per-track debounce.
Also possible the user's count double-counts the same way (their 30 might
itself merge these). Either way the gap is now precisely characterized.

~~Next code change: suppress ball detections inside the face region~~ TRIED AND
REVERTED (see scorecard): detection-level face suppression kills real balls
thrown across the face. The face-phantom needs a TRACK-LEVEL fix: when a track
sits on/near the face for >N frames with near-zero velocity while other balls
are in flight, force-release it (same pattern as the HELD occlusion timeout).
This works because the face-phantom only matters when it *holds* — a track
passing through the face region while genuinely moving is fine.

## Completed improvements (details in VERIFICATION_COUNTED_CATCHES_RESULTS.md)

- [x] Pose detector NMS (10 duplicate persons → 1)
- [x] MOG2+YOLO fusion (blur-ball recovery)
- [x] Static-bin clutter suppressor (ceiling lights)
- [x] Light mask (now OFF — suppressed cream balls; keep code for bright-LED scenes)
- [x] HELD wrist-stick survival (webcam churn fix)
- [x] Real capture fps from meta.json (19.8 vs 30 bug)
- [x] Event state machine: flight latch at first contact, HELD occlusion timeout,
      speed-gated catches, straight-across transfer THROW+CATCH, per-hand away counter
- [x] Top-N gate relaxed to n_balls+2
- [x] ID stitching via last-seen dead-track positions (kept; helps off-screen re-entry)
