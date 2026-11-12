# JuggleHub Tracking Project — Complete Handoff Document

**Date:** 2026-11-11
**Author:** Roo (glm-5.3-flash), an inferior model. **Everything below was done by this inferior model and is open to revision.** Do not treat any method, constant, architecture choice, or conclusion as sacred — the regression suite (described below) exists precisely so you can change anything and measure whether it helped.

**AUDITED 2026-11-12.** Paragraphs marked **[CORRECTED 2026-11-12]** were rewritten by the audit, and each says what the earlier text claimed. Findings, evidence and the detailed plan: [`ROO_NOTES/AUDIT_2026-11-12.md`](ROO_NOTES/AUDIT_2026-11-12.md). Three things to know before reading on: (1) the two LED recordings are invalid (5 real fps), so every LED and exposure conclusion is withdrawn; (2) the V12 dataset that was "ready to upload" was broken and has been rebuilt; (3) the six-video suite is a tuning set, and there is no valid held-out recording yet. Remaining tasks, step by step: [`ROO_NOTES/TAKEOVER_NOTES_2026-11-12.md`](ROO_NOTES/TAKEOVER_NOTES_2026-11-12.md).

---

## 1. THE CORE GOAL (read this first)

The user juggles balls in front of a camera and wants a **real-time system that tracks every individual ball perfectly and counts catches and throws accurately**. The user's standard: perfect per-ball identity, correct catch/throw event counts that match what a human observer counts, on any ball type (matte, glowing LED, same-color, different-color), using a webcam or the Intel RealSense D455 depth camera, running in real time on the local machine (Intel Core Ultra 9 185H, CPU-only for inference; NPU/iGPU proved unusable for this workload in earlier sessions).

The user is an expert juggler who can perform named patterns (cascade, box, 441, shower, columns, flash) and count catches by hand. They have explicitly said: **do whatever works best, nothing is off-limits, all prior work can be discarded if a better approach exists.** They also said they do NOT want a two-model LED/non-LED split unless a single combined model measurably fails.

## 2. HARDWARE & CAMERA

- **Camera:** Intel RealSense D455 (USB3), 640×480 @ 30fps, color + aligned depth (z16, scale 1mm).
  - **[CORRECTED 2026-11-12]** RGB exposure option range: 1–10,000 *units*, gain 0–128. Earlier text here assumed 1 µs per unit; the saved clips fit 100 µs per unit (inferred from frame rates, see next point). The earlier figure "auto exposure settles at ~400 µs" rests on the same assumption and is unverified.
  - **[T1 DEVICE TEST 2026-11-12]** Measured: `TRACKING_EXPOSURE_MS=20` → sensor accepts `MANUAL 20.0 ms gain 64.0`, preview loop runs, **no BAD STREAM**; a following run with no variable reports `RGB exposure: AUTO` — **a manual setting does not persist on the sensor**. The picture brightness during the manual run was not explicitly reported by the user, so the 100 µs-per-unit assumption is supported but not 100% pinned; manual exposure stays off-limits for real recordings (R6) until the user confirms the picture was bright, not near-black.
  - **[ADDED 2026-11-12]** cv2's bundled Qt ships only the xcb platform plugin; this Wayland desktop needs `QT_QPA_PLATFORM=xcb` for any cv2.imshow window. Set automatically in [`tracking_lab/__init__.py`](tracking_lab/tracking_lab/__init__.py) before cv2 imports. tracking_lab must run under the system python (`/home/twain/.rye/shims/python3`) — the project venv has no cv2.
  - **[CORRECTED 2026-11-12]** **The manual exposure "lock" broke the colour stream. Every conclusion drawn from it is withdrawn.** Measured on the saved data: with the raw value 2000 (labelled "2 ms") the colour stream delivered about 5 unique frames per second, each image repeated 6 times, while depth stayed at 30 fps. 4000 gave 2.6 fps, 8000 gave 1.3 fps, auto gave 30. Both 2026-11-11 LED recordings show the same 6× repeat (about 300 unique colour frames of about 1,800). Earlier text here said the lock was "catastrophic for glowing LED balls" because of overexposure and light trails. What was actually recorded is a 5 fps long-exposure stream, so nothing is known yet about LED balls at a genuinely short exposure. Every script now sets auto exposure explicitly and counts repeated frames on screen while recording. How to confirm the unit on the camera: docstring of [`tracking_lab/camera.py`](tracking_lab/camera.py). The Gen 1 glowing session (2026-11-10, auto) is a clean 30 fps recording; its 99.7% coverage figure was not re-checked and it has no hand count.
- **Second camera:** laptop built-in USB webcam ("USB2.0 FHD UVC"), used in 2 webcam-only test recordings. Webcam sessions run at effective ~19.8 fps (not 30) — the tracker reads real fps from meta.json.
- **Compute:** local machine is CPU-only for inference (OpenVINO). **[CORRECTED 2026-11-12]** Measured in the audit on this machine, offline: ball model 21.1 ms + pose model 23.9 ms + MOG2 6.8 ms + colour histogram 0.4 ms = **52.3 ms per frame (19 fps)**. Earlier text gave only "YOLO11n ~17 ms". The pipeline does not keep up with a 30 fps camera, and no live runner exists yet. Training is done on Google Colab (user has an **L4 GPU** runtime available via SSH tunnel; see §9).
- **Environment quirks:** opencv-python (NOT headless — GUI needed), pyrealsense2 2.59.1 pip package, rye-managed Python 3.12.9.

## 3. DATA — WHAT RECORDINGS EXIST

All recordings live in `tracking_lab/recordings/<timestamp>_<label>/` with layout `no_boxes/*.jpg` + `depth/*.png` (RealSense only) + `meta.json` (incl. per-frame timestamps and effective fps). Two generations:

**Gen 1 (2026-11-10, auto exposure, older LED balls):** 8 sessions, ~12k frames total:
cascade3_mixed (distinct colors), cascade3_same (identical matte), cascade3_glow_mixed (LED, distinct colors — tracked 99.7%), cascade3_glow_same (mislabeled — user has NO 3 same-color LEDs; skip), mixed_webcam, same_webcam, columns_shower, plus 1 single-ball session.

**Gen 2 (2026-11-10 evening, counted sessions):** 6 sessions with user-counted catches (ground truth!):
cascade60 (60), box30 (30), 441_30 (30), shower40 (40), columns30 (30), flash_10 (30). User counts are ±1–2.

**Gen 3 (2026-11-11, cascade3_glow_same, 1,799 frames) — INVALID.** **[CORRECTED 2026-11-12]** Only 301 unique colour frames; each is repeated 6 times (5 real fps). User counted **212 throws**. The earlier tracker result (119/137, coverage 92.6%) and its "merged light-trails" root cause are withdrawn: no tracker can be scored on 5 fps footage.

**Gen 4 (2026-11-11, cascade3_glow_mixed_locked, 1,801 frames) — INVALID.** **[CORRECTED 2026-11-12]** Described earlier as "auto exposure restored", but it has the same signature: 300 unique colour frames, each repeated 6 times. Either the manual setting persisted across pipeline restarts or the lock variable was still set for that run; which one is unknown. User counted **216 catches/throws, no drops**. The earlier result (125 catches / 115 throws, 58%) and the claim that "these LED balls paint bright streaks at ANY exposure" are withdrawn.

**Consequence **[CORRECTED 2026-11-12]**:** there is no valid counted LED recording, and no valid held-out recording of any kind. The other 12 recordings pass the repeated-frame check. `meta.json`'s `effective_fps` cannot reveal this problem (it is the loop rate); use `RecordingSession.integrity()` or run [`tracking_lab/evaluate.py`](tracking_lab/evaluate.py), which rejects such sessions.

Also 8 older engine recordings at `engine/data/1_raw_recordings/` (single-ball sessions, low value) and engine annotation sessions at `engine/data/annotation_sessions/` (hand-labeled, 4-class: 0=led_on, 1=led_off, 2=dropped_ball, 3=hand) — several thousand frames including LED balls, low light, edge cases.

## 4. THE DETECTORS

**V8 (deployed champion):** **[CORRECTED 2026-11-12]** `engine/models/yolo11n.xml` (OpenVINO), YOLO11n 640×640, **2 classes (0 ball, 1 ball_held)**, output `[1,6,8400]`. Earlier text said 1 class. The tracker unpacks the class id but never uses it. The model file is dated 2025-10-02, the same day as `engine/models/zipped/V8_balls_held/best.pt`, so it was most likely trained on `V8_held_balls` (4,476 frames). That is inferred from file dates, not from a training log. No trained V9.2, V9.3 or V10 model exists on disk. Measured 21.1 ms/frame on CPU.

**V11:** **[CORRECTED 2026-11-12]** trained on Colab L4 from 559 GLM-labeled frames ([`tracking_lab/build_v11_dataset.py`](tracking_lab/build_v11_dataset.py), labels via the GLM-5.3-flash vision API). Of the 559 frames, **240 come from the six regression videos** (40 each) and 80 from the two invalid LED sessions. V11 was therefore trained on the videos it is scored on, and its regression numbers are contaminated. Its reported mAP50 (ball 0.709, held_ball 0.536) is on a frame-level split of the same recordings. Label quality looks poor: 39% of 1,611 GLM boxes overlap a V8 detection at IoU ≥ 0.5 (V8 is not ground truth, so this is a rough indicator only). Re-run 2026-11-12: 59/24/27/29/39/30, mean |catch err| **5.0**. Earlier text said "~3.3"; the same six counts give 5.0. Exported to `yolo11n_v11_openvino_model/`, selectable with `TRACKING_MODEL_V11=1`. V8 remains default.

**V12:** **[CORRECTED 2026-11-12]** **The dataset described here earlier was broken. Do not upload or train `.tmp/v12_dataset.zip`.** Measured problems with it:
- Its yaml said `nc: 1` while the labels contain class 1. Ultralytics rejects such images: 15,310 of 22,969 train and 3,677 of 4,856 val.
- Only 10,656 images were unique. The five "V8-era training sets" are generations of the same frames (V8_held_balls ⊂ V9_1_1 ⊂ V9_2 ⊂ V9_3, plus a 640×360 crop of them called V10).
- 2,783 val files were byte-identical to a train file, and 7,117 images carried conflicting labels.
- 240 frames came from the six regression videos.

[`tracking_lab/build_v12_dataset.py`](tracking_lab/build_v12_dataset.py) was rewritten. It builds `.tmp/v12_clean` from the newest label generation only (`V9_3_targeted`): **4,103 train / 887 val**, 2 classes, split **by recording** (78 / 19 recordings, none shared), no counted session included. Ultralytics' own loader accepts every image (0 corrupt). `--with-glm` adds 239 GLM-labelled frames from the six uncounted 2026-11-10 recordings to train only. Known gaps: two categories (lonely_hands, targeted_lowish_light) have no val frames, and without `--with-glm` the set contains nothing from the 2026-11 recordings.

**Not trained.** The new Colab cell was run end to end for 1 epoch on a 96-image subset on CPU, with the `/content/` paths and the pip line substituted. That proves the code path only. Training is no longer the immediate next step; see §12.

## 5. THE TRACKER (`tracking_lab/tracker.py`)

Custom state-machine tracker (NOT a stock MOT tracker). Per frame:
1. Predict all tracks (gravity constant 2500 px/s², HELD balls exempt).
2. Associate detections to tracks via Hungarian assignment (scipy), cost = distance + color-histogram similarity. Gate 80 px, widened ×(1+0.5·min(misses,4)) for lost tracks. Top-N gate relaxed to n_balls+2.
3. Update matched tracks: EMA velocity (α=0.4), position, depth, histogram.
4. Unmatched: IN_FLIGHT→COAST; HELD sticks to its wrist for up to 10 frames (occlusion tolerance) then SILENT-releases to IN_FLIGHT; tracks coasting past 30 frames die (last-seen position recorded for ID stitching, which never fired in practice).
5. Unmatched detections → pending tracks (need `min_hits=3` consecutive hits, die on first miss).
6. State machine per track:
   - HELD: if matched det near wrist → follow hand; if not near wrist for `throw_dwell=3` frames → THROW event → IN_FLIGHT. If unmatched ≥10 frames → silent release (no event).
   - IN_FLIGHT/COAST near a wrist: held_frames++; when held_frames≥2 AND (away_peak≥2 OR away_from_caught≥2) AND (speed<450 or long_rest≥7): CATCH → HELD. Per-hand counters (`away_from_caught`) handle straight-across passes that never leave wrist neighborhoods. Same-hand catch dedup (`min_catch_gap=4`) with THROW-reset.
   - Hand-jump transfer: HELD track whose ball clearly moves to the OTHER hand (dominant by >3px, slow) → THROW+CATCH pair.
7. Static-bin suppressor: detections appearing in the same 32px bin >85% of frames suppressed (kills ceiling-light FPs).

**Key constants (all tuned on regression data):** held_radius=**75px** (**[CORRECTED 2026-11-12]** earlier text said 45; the code has 75, and 45 scores 8.33 on the suite), held_dwell=2, throw_dwell=3, min_flight=2 (away-latched), max_catch_speed=450 (persistence override: long_rest≥7 frames), coast=30, min_hits=3, held_max_occluded=10, assoc_gate=80, min_catch_gap=4.

## 6. THE DETECTION FUSION (`tracking_lab/__main__.py`, `detectors.py`)

BallDetector (YOLO OpenVINO) + MotionDetector (MOG2 background subtraction, varThreshold=25) fused: motion blobs not near a YOLO det are added as low-confidence candidates (score 0.5). This recovers motion-blurred balls YOLO misses entirely (white balls against white walls). Face-region suppression (suppress dets within 55px of nose) was TRIED AND REVERTED — it killed balls thrown across the face (scorecard entry, mean err 13.7). Light mask (static bright-region suppressor) was also tried and REVERTED — it suppressed real cream balls crossing the top of frame.

## 7. EVALUATION — THE REGRESSION SUITE (the source of truth)

6 counted sessions (Gen 2) with user ground truth. Command loop is in TRACKING_IMPROVEMENTS.md. Scorecard history (mean abs catch error across 6 patterns):

| date | change | cascade60 | box30 | 441_30 | shower40 | columns30 | flash_10 | mean err |
|---|---|---|---|---|---|---|---|---|
| 11-10 | baseline | 25/60 | 15/30 | 10/30 | 13/40 | 19/30 | 17/30 | ~12.8 |
| 11-10 | tuning round 1 | 35/60 | 11/30 | 19/30 | 22/40 | 27/30 | 23/30 | ~8.0 |
| 11-11 | per-hand away + transfer detect (veto 450) | 62/60 | 26/30 | 24-28/30 | 38-39/40 | 28-30/30 | 29-32/30 | **~2.2** |
| 11-11 | face suppression 55px | — | — | — | — | — | — | **REVERTED** (killed crossing balls, err 13.7) |
| 11-11 | V11 detector (all sessions) | 59/60 | 24/30 | 27/30 | 29/40 | 39/30 | 30/30 | ~~~3.3~~ **5.0** (arithmetic corrected) — V8 kept as default |
| 11-12 | audit re-run, V8, code as it stands (deterministic) | 63/60 | 26/30 | 24/30 | 38/40 | 28/30 | 28/30 | **3.17** |
| 11-12 | audit re-run, V11 | 59/60 | 24/30 | 27/30 | 29/40 | 39/30 | 30/30 | **5.00** |

Current champion config: V8 detector, fusion ON, light mask OFF, veto=450, dwell=2, min_flight=2, per-hand away counters, transfer detection, HELD wrist-stick + silent release.

**[CORRECTED 2026-11-12]** **What the audit found about this suite.**
- The "~2.2" row is a set of ranges over several configurations; its ranges work out to anything from 1.5 to 3.0. The code as it stands is deterministic and gives 63/26/24/38/28/28 on every run: mean |catch err| **3.17**, mean |throw err| 4.67, mean |catches − throws| 1.83. Flash is 28, below the 29–32 range in that row.
- Other "mean err" values in the table also disagree with the counts in their own rows: the baseline row works out to 20.2 (stated ~12.8), tuning round 1 to 13.8 (stated ~8.0), V11 to 5.0 (stated ~3.3). How the stated values were calculated is not recorded. The direction of the history (large improvement from baseline) still holds.
- A total can be right for the wrong reasons. Only **68%** of the catches follow an observed flight of the same ball; the rest come from the same-frame transfer heuristic. Two balls are HELD in the same hand in **14–44%** of frames over whole clips, and in 18.5–49.4% counting only from the first throw to the last catch, where it cannot happen. So part of the agreement with the hand counts is error cancellation.
- Every tracker constant was chosen on these six videos, and there is no other valid counted recording. This is a **tuning set**. It says how well the constants fit these six clips, not how the tracker behaves on a new one.
- One-at-a-time sweep (`.tmp/audit/sweep.txt`, same six videos): the score has cliffs right next to the current values. held_radius 65 → 6.50, 75 → 3.17, 110 → 7.00. held_dwell 2 → 3.17, 3 → 7.17. assoc_gate 60 → 10.00, 80 → 3.17. `held_max_occluded` has no effect at any value tried. A few values score slightly better than the current ones (max_catch_speed 600 → 2.33). They were **not** adopted: that would be more fitting to the same six clips.

New command (replays cached detections, under a second once cached):
```bash
python3 -m tracking_lab.evaluate            # tuning table + held-out table + integrity check
python3 -m tracking_lab.evaluate --sweep    # sensitivity of each constant
TRACKING_BALL_MODEL=/path/to/model.xml python3 -m tracking_lab.evaluate   # any other detector
```

## 8. WHAT IS KNOWN-BAD / STUCK (do not repeat these)

1. **Face-region detection suppression** — regressed badly (crossing balls suppressed). If face-phantoms matter, fix at TRACK level (kill stationary slow tracks on face for 20+ frames) — designed but NOT yet implemented/tested.
2. **Light mask** — suppressed real cream balls. Off by default. The static-bin suppressor handles lights instead.
3. **[CORRECTED 2026-11-12]** **Manual RGB exposure with unverified units.** The "2 ms / 4 ms / 8 ms" settings produced 5 / 2.6 / 1.3 unique colour frames per second. Both findings recorded here earlier are withdrawn: "catastrophic for LED balls" and "helped matte balls (+66% full-ball frames)". The second came from counting detections on repeated, smeared frames. `TRACKING_EXPOSURE_LOCK` and `TRACKING_LED_LOCK` are retired (setting them prints a warning and does nothing). Manual exposure is now `TRACKING_EXPOSURE_MS=<ms>` with optional `TRACKING_GAIN=<g>`, and a recording with more than 5% repeated colour frames is marked invalid.
4. **Speed gate alone as catch criterion** — at 3.5 catches/s, real catches arrive at 700–1900 px/s and were denied. The persistence override (long_rest) fixed it. Absolute speed thresholds are tempo-dependent — be suspicious of them.
5. **flight_frames as gate** — froze when ball hovered near wrist, permanently denying catches. Fixed by latching away-frames at first wrist contact (`away_peak`).
6. **GLM-5.3-flash as a judge** — ~50% precision on "phantom" claims, systematically blind to occluded balls (claims "held on empty hand" when ball is gripped behind fingers). Useful as a FRAME-FINDER only; every claim must be geometrically verified (wrist-distance check in the auto_annotate JSON) and visually confirmed via verify_auto.py overlays before acting.
7. **Sequential API calls** — 0.02 req/s; parallelized to 0.30 req/s with 3–6 workers. Parallel+thinking-disabled is the way.
8. **120-frame "held for 664 frames" deadlock** — HELD tracks that silently release must NOT re-deadlock; the occlusion timeout fixed this. Watch for regressions here.
9. **[CORRECTED 2026-11-12]** **Tuning constants on the six counted videos.** Every constant was fitted to them and the sweep shows cliffs beside the current values. Do not change a constant because it improves the six-video total. Judge changes on held-out takes and on the structural diagnostics.
10. **[CORRECTED 2026-11-12]** **Scoring a detector on videos it was trained on.** V11 was. Counted sessions (tuning and held-out) must never enter a training set; the new dataset builder enforces this.
11. **[CORRECTED 2026-11-12]** **Frame-level train/val splits.** Neighbouring frames of one recording are near-copies, so val mAP from such a split is inflated. Split by recording.

## 9. COLAB TRAINING SETUP (working, tested)

User has an L4 GPU Colab runtime connected via SSH:
- Local: cloudflared at /tmp/cloudflared (used initially, flaky — tunnels died repeatedly), now **ngrok** (stable; token is no longer embedded in colab_setup_cell.py; it is read from a Colab secret or a prompt, and kept locally in the gitignored tracking_lab/ngrok_token.txt).
- Tunnel address and root password: printed by the setup cell on every run (new random password each time). **Deliberately not recorded here** [redacted 2026-11-12: a live address and password were written on this line in plaintext]. Working method: ngrok TCP tunnel + paramiko direct to the printed host:port. The tunnel dies when the Colab tab sleeps.
- **[CORRECTED 2026-11-12]** Training cell: [`tracking_lab/colab_train_v12_cell.py`](tracking_lab/colab_train_v12_cell.py) (plain Python). It unzips `/content/v12_clean.zip` (not the old `v12_dataset.zip`), refuses to start if any label has a class id outside the yaml's `nc` or if image and label counts differ, trains yolo11n for 120 epochs at imgsz=640 batch=32, validates, prints per-class mAP50 and exports to OpenVINO. Build the zip with `cd .tmp && zip -q -r v12_clean.zip v12_clean`. The val mAP it prints is not comparable with V8–V11 figures: those were measured on frame-level splits where val frames had near-copies in train, so expect a lower number.
- V11's training already succeeded once this way (see §4). Note: ultralytics `blur` arg is INVALID (rejected) — motion blur robustness comes from real blurry frames in the dataset.
- **[CORRECTED 2026-11-12]** The new `data.yaml` has no `path:` key, so Ultralytics resolves it relative to the yaml file and nothing needs patching on Colab. The setup cell reads the ngrok token from a Colab secret named `NGROK_TOKEN` (or prompts for it). That lookup has not been run on Colab yet.

## 10. OPEN QUESTIONS / WHAT A SMARTER MODEL SHOULD RECONSIDER

1. **[CORRECTED 2026-11-12]** **Was V8 trained on V9/V10 data?** Most likely not: see §4 (file dates point to `V8_held_balls`). The earlier reasoning "V12 adds 11k frames the champion never saw" was wrong. V9.3 has 4,990 frames against V8's 4,476 (514 more), with the same boxes resized by a median of 2 px. `v12_clean` trains on 4,103 of them, because 887 are held back as a by-recording validation set. So a model trained on it sees roughly the data V8 saw, not 11k new frames. Expect a small change, if any.
2. **[CORRECTED 2026-11-12]** **LED balls: open again.** The "125/216 gap" was measured on a 5 fps recording and means nothing. Whether LED balls are a problem at a real 30 fps, and at what exposure, is unknown. `led_tune.py`'s earlier scores ("auto 6.5/10, shorter exposures 1–2/10") used the same mis-scaled exposure values and are withdrawn. Streak-endpoint detection should wait until a valid LED recording shows it is needed.
3. **Carry semantics** — box/441 patterns involve hand-to-hand transfers with no airborne arc; a juggler counts them as catches but a flight-based detector sees nothing. Transfer detection exists but only fires when the ball visibly moves between hands.
4. **Catches≠throws balance** — must be equal in no-drop sessions. Current: silent releases (no THROW on HELD→occlusion timeout) and unpaired carry catches create asymmetry. Consider: make silent releases emit paired events.
5. **Per-hand HELD exclusivity** — a hand cannot hold 2 balls; 2 tracks claiming the same hand means one is phantom. Designed in backlog, NOT implemented.
6. **Kalman CA filter** — replaces EMA velocity; sharper crossing association + real throw velocities. Backlog.
7. **ID stitching never fired** — infrastructure exists but produced no effect. The dead-track propagation may be wrong or unnecessary. Investigate or remove.
8. **Top-N gate n_balls+2** — may admit phantoms in cluttered scenes; the pending-track gate handles most. Watch.

## 11. FILE MAP (everything lives in tracking_lab/ unless noted)

- `tracker.py` — the tracker (state machine + association). All constants in TrackingConfig.
- `detectors.py` — BallDetector (OpenVINO YOLO, env TRACKING_MODEL_V11=1 switches to V11), MotionDetector (MOG2), PoseDetector (wrists/nose; NMS fixed).
- `__main__.py` — offline pipeline runner: fusion, light mask (OFF), face suppression (REVERTED — note remains), static-bin suppressor, metrics, render.
- `wizard.py` / `verify_wizard.py` / `counter.py` — recording UX for the user.
- `auto_annotate.py` + `verify_auto.py` — GLM frame-finder + claim-verification overlays.
- `build_v11_dataset.py` / `build_v12_dataset.py` — dataset builders.
- `exposure_test.py` / `led_tune.py` — camera experiments (results in §8).
- `recording.py` — session loader (reads real fps from meta.json; `integrity()` detects repeated colour frames).
- **[ADDED 2026-11-12]** `camera.py` — the only place that sets RealSense RGB exposure; `FrameGuard` live repeated-frame counter; `scan_recording`.
- **[ADDED 2026-11-12]** `evaluate.py` — regression suite with diagnostics, integrity gate, held-out table, `--sweep`. `compare_eval.py` + `baseline_eval_v8.json` — check a run against the stored baseline.
- **[ADDED 2026-11-12]** `perception.py` — the fusion code shared by every runner. `detcache.py` — per-recording detection cache in `.tmp/detcache/`.
- **[ADDED 2026-11-12]** `../ROO_NOTES/AUDIT_2026-11-12.md` — audit findings and plan. `../ROO_NOTES/TAKEOVER_NOTES_2026-11-12.md` — remaining tasks, step by step.
- `colab_train_v12_cell.py`, `colab_setup_cell.py`, `colab_train_v11.md` — Colab integration.
- `../ROO_NOTES/TRACKING_IMPROVEMENTS.md` — scorecard + backlog (KEEP UPDATED).
- `../ROO_NOTES/VERIFICATION_COUNTED_CATCHES_RESULTS.md` — counted-catch results.
- Model files: `engine/models/yolo11n.xml` (V8, deployed), `yolo11n_v11_openvino_model/` (V11), `yolo11n_v11.pt` (V11 raw).

## 12. IMMEDIATE NEXT STEPS (in order)

**[CORRECTED 2026-11-12]** The earlier list started with "Train V12 (zip ready)". That zip was broken (§4), and there is no valid data to judge a new detector on, so the order has changed. Detail, commands and decision rules for every step: [`ROO_NOTES/AUDIT_2026-11-12.md`](ROO_NOTES/AUDIT_2026-11-12.md).

1. **Confirm the camera** (5 minutes, needs the D455): check auto exposure gives 0% repeated frames, then confirm the exposure unit with `TRACKING_EXPOSURE_MS=20`.
2. **Record held-out counted takes**: `python3 -m tracking_lab.verify_wizard --heldout h1` (the six patterns once more, ordinary balls), then `--heldout led1 --only cascade60` with LED balls. Run `python3 -m tracking_lab.evaluate`. This is the first honest number the project will have.
3. **Per-event ground truth** for two or three takes (which frame, which hand), so missed and phantom catches can be counted separately instead of cancelling.
4. **A small hand-labelled detection test set** from the new takes, then train on `v12_clean` and compare V8 against it on that set and on the held-out takes.
5. **Tracker structure**: per-hand HELD exclusivity, the transfer heuristic, catches = throws. Judged on the diagnostics and held-out takes.
6. **Real time**: 52 ms per frame today. Measure pose every Nth frame, the 416-px model and parallel inference; build a live runner.
7. **LED exposure**: re-run `exposure_test.py` once the unit is confirmed.
Not on the list any more until a valid recording shows a need: tempo-adaptive gating, streak-endpoint detection, Kalman filter.

## 13. METRICS SUMMARY (current state)

**[CORRECTED 2026-11-12]**
- Six-video tuning suite, V8, as the code stands: cascade 63/60, box 26/30, 441 24/30, shower 38/40, columns 28/30, flash 28/30. Mean |catch err| 3.17 (8.6% of expected), mean |throw err| 4.67.
- 68% of catches are backed by an observed flight; two balls share a hand in 14–44% of frames. Coverage 91.6–96.8% on these six.
- Held-out: **none valid.** LED: **none valid.** The earlier "125/216 (58%)" LED figure is withdrawn.
- Speed: 52.3 ms per frame offline (19 fps); not real-time.
- The user's counts are ±1–2, so differences of that size between two configurations mean nothing on six clips.

## 14. USER PREFERENCES (respect these)

- Interactive scripts must show a LIVE camera view with on-screen countdown/info (never a silent headless run).
- The user counts catches by hand and wants real-time event counting to match; catches must equal throws in no-drop sessions.
- The user will record anything needed on request; keep recording requests minimal and specific.
- The user does NOT want two LED/non-LED models unless a combined model measurably fails (give it the chance first).
- All work must keep the 6-video regression suite green (or explicitly better where it counts) — reverting regressions is standard procedure.
- Colab tab must stay open during training (tunnel + runtime die otherwise).
- API key for GLM: `tracking_lab/zai_apikey.txt` (gitignored). Coding-plan endpoint: `https://api.z.ai/api/coding/paas/v4/chat/completions` (model glm-5.3-flash). Note: image+text requests work; watch rate limits (429s with parallel>3).
