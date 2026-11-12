# Takeover notes — 2026-11-12

For the next LLM (or person) continuing the tracking work. Read this file first, then [`AUDIT_2026-11-12.md`](ROO_NOTES/AUDIT_2026-11-12.md) for the evidence, then the corrected [`HANDOFF_STATE_OF_THE_PROJECT.md`](ROO_NOTES/HANDOFF_STATE_OF_THE_PROJECT.md) for background. Where an older note disagrees with this file or the audit, the older note is wrong.

Each statement below is marked **Measured** (run during the audit, output seen), **Inferred** (read from code or file dates, not run) or **Not tested**.

---

## 1. Where the project stands in five lines

1. The tracker counts catches within about 3 per take on six videos (**Measured**: mean |catch error| 3.17). Every tracker constant was fitted to those same six videos, so this number is optimistic by an unknown amount.
2. There is **no valid held-out or LED counted recording**. The two LED sessions recorded on 2026-11-11 contain only 5 real colour frames per second (**Measured**: 300 and 301 unique frames out of about 1,800). Everything concluded from them is withdrawn.
3. The old V12 dataset is broken and must not be trained (**Measured**). A clean replacement exists at `.tmp/v12_clean` and is **not trained**.
4. The pipeline is not real-time: 52.3 ms per frame, 19 fps (**Measured** offline). There is no live runner.
5. All new camera code is **Not tested** on a device. All Colab cells are **Not tested** on Colab.

The next useful thing is data, not code: tasks T1 and T2 below need the user and the camera, take about 15 minutes, and unblock everything else.

---

## 2. Rules. Do not break these.

| # | Rule | Reason |
|---|---|---|
| R1 | Do not change any constant in `TrackingConfig` ([`tracker.py`](tracking_lab/tracker.py:27)) to improve the six-video score. | They are already fitted to those six. The sweep shows cliffs next to several current values (see audit §1). |
| R2 | Do not judge any change on the six videos alone. Judge on held-out takes (T2) and per-event truth (T3). | A total can be right through cancelling errors: only 68% of catches follow an observed flight. |
| R3 | Do not upload or train `.tmp/v12_dataset` or `.tmp/v12_dataset.zip`. | Wrong class count, duplicates, train/val leakage, frames from the test videos. |
| R4 | Do not run `build_v11_dataset.py`. | It samples every recording, including the counted ones, and splits by frame. |
| R5 | Never put a counted recording (the six, or any `--heldout` take) into a training set. | It destroys the only test data. `build_v12_dataset.py` already refuses them. |
| R6 | Do not use manual exposure (`TRACKING_EXPOSURE_MS`) for real recordings until T1 has confirmed the unit. | The unit of the RealSense exposure option is assumed, not confirmed. |
| R7 | Do not score a recording that the integrity check rejects. | Repeated colour frames make it a 5 fps recording. `evaluate.py` already refuses. |
| R8 | Do not write tokens, passwords or tunnel addresses into any file that is not gitignored. | Two secrets sat in plaintext before the audit. |
| R9 | After any change that is not meant to alter tracking results, run the check in §3. The answer must be `identical`. | It is the only protection against silent regressions. |
| R10 | Do not build a two-model LED / non-LED split. | User preference; nothing measured supports it. |
| R11 | Interactive scripts must show a live camera view. Keep recording requests to the user short and specific. | User preferences. |
| R12 | Say plainly what was run and what was not. Do not report a task as done on the strength of a compile. | Several earlier notes reported untested things as finished; the audit had to undo them. |

---

## 3. The regression check (run it before and after every change)

Run from the repo root, `/home/twain/Projects/JuggleHub`, with the system `python3`.

```
python3 -m tracking_lab.evaluate --json .tmp/now.json
python3 -m tracking_lab.compare_eval tracking_lab/baseline_eval_v8.json .tmp/now.json
```

**Measured** expected output of the first command (V8 detector, default config):

```
session      exp    C    T  Cerr  Terr  C-T  flt  unbk  xfer  2held%   cov%
cascade60     60   63   62     3     2    1   54     9     6    37.5   96.8
box30         30   26   23    -4    -7    3   13    13    10    35.2   93.3
441_30        30   24   21    -6    -9    3   17     7     4    30.4   93.4
shower40      40   38   37    -2    -3    1   18    20    16    44.3   93.8
columns30     30   28   26    -2    -4    2   20     8     5    22.2   93.4
flash_10      30   28   27    -2    -3    1   19     9     5    14.5   91.6
mean |catch err| 3.17   mean |throw err| 4.67   mean |C-T| 1.83   flight-backed catches 68%
HELD-OUT SET: (no valid sessions), two REJECTED lines for the LED sessions
```

The second command prints `identical` and exits 0. It prints each differing field and exits 1 if anything differs, and exits 2 on wrong usage (**Measured**, all three).

Details that matter:
- The first run after a new recording or a new model is slow (about 50 ms per frame) because detections are computed and cached in `.tmp/detcache/`. Later runs take under a second.
- The cache is keyed on the model files, so switching models cannot mix results.
- `tracking_lab/baseline_eval_v8.json` has an exception in `.gitignore` (the repo ignores `*.json`). It is not committed yet, like the rest of `tracking_lab/`.
- When a change is **meant** to alter results (T5), the comparison will differ. Then read the differences, do not just regenerate the baseline. Replace the baseline file only after the user accepts the change.

Other evaluation commands:
- `python3 -m tracking_lab.evaluate --sweep` prints how the score moves when one constant changes. For understanding only (R1).
- `python3 -m tracking_lab.evaluate --set held_radius_px=85` runs once with one constant overridden and prints an `OVERRIDE` line. Repeatable. **Measured**: this example gives mean |catch err| 3.00; an unknown name or unreadable value exits 2. For experiments only (R1).
- `TRACKING_BALL_MODEL=<path to .xml> python3 -m tracking_lab.evaluate` scores another detector. A wrong path raises an error.
- `TRACKING_MODEL_V11=1 python3 -m tracking_lab.evaluate` scores V11 (**Measured**: 5.00, worse than V8, and V11 was trained on frames from these videos).

Shell note: the command tool uses `/bin/sh`. Process substitution `<( )` does not work, and `time VAR=value command` fails; write `env VAR=value command`.

---

## 4. Tasks, in order

T1 and T2 need the user at the camera. Ask for them first, in one short message, then work on T3 while waiting. T3 can be built and tested on the existing six videos.

### T1 — Confirm the camera code on the device (user, about 5 minutes)

Why: [`camera.py`](tracking_lab/camera.py:89) and every recording script that uses it have only been compiled.

Ask the user to run these and paste the terminal output:

1. `python3 -m tracking_lab.wizard`
   - Expected in the terminal: a line `RGB exposure: AUTO` and a line `RGB exposure_range_units: [min, max, step, default]`.
   - Expected in the window: `exposure: AUTO` at the top, and no red `BAD STREAM` line while moving for ten seconds. Then `q`.
2. `TRACKING_EXPOSURE_MS=20 python3 -m tracking_lab.wizard`, then look at the picture:
   - Normal or bright picture, no `BAD STREAM`: the unit is 100 µs as assumed. Nothing to change.
   - Nearly black picture: the unit is 1 µs. Change `RGB_EXPOSURE_UNIT_MS` in [`camera.py`](tracking_lab/camera.py:66) from `0.1` to `0.001`. In that case the cause of the 5 fps LED recordings is something else; tell the user and do not use manual exposure.
   - `BAD STREAM` shown: the conversion is wrong the other way. Stop and report the printed lines to the user.
3. `python3 -m tracking_lab.wizard` again with no variable. It must say `AUTO` and show no warning. This checks that a manual setting does not stay on the sensor.

If a script crashes on the device: read the traceback, fix the smallest thing, run §3, ask the user to retry. The likely places are `configure_rgb` (an option the sensor does not support) and the `FrameGuard` calls in the recording loops of [`wizard.py`](tracking_lab/wizard.py:176) and [`verify_wizard.py`](tracking_lab/verify_wizard.py:82).

Done when: the user has pasted the two printed lines, step 2 has one of the three outcomes, and step 3 says AUTO. Record the outcome in §2 of the handoff doc (replace "unconfirmed").

### T2 — Record held-out counted takes (user, about 10 minutes)

Why: this gives the first number that was not fitted.

1. Ordinary balls, same room and framing as the six: `python3 -m tracking_lab.verify_wizard --heldout h1`
   - It asks for six patterns with these catch counts: cascade 60, box 30, 441 30, shower 40, columns 30, flash 30.
   - The user stops at the count and presses SPACE. `R` redoes a take after a drop or a lost count.
2. LED balls: `python3 -m tracking_lab.verify_wizard --heldout led1 --only cascade60`
3. A take that shows the red `BAD STREAM` line, or ends with a terminal warning about repeated frames, is unusable. Redo it.
4. Then run §3's first command. The new takes appear in the HELD-OUT table automatically (they are found through `"heldout": true` in each take's `meta.json`; see [`discover_heldout()`](tracking_lab/evaluate.py:62)).

The queue logic and argument checks were tested offline (**Measured**). The recording loop with `--heldout` is **Not tested** on a device.

How to read the result, and what to tell the user:
- Held-out error near 3 per take: the constants generalise. Continue with T3 then T5.
- Held-out error much larger: the constants are overfitted. T5 becomes the priority, and it should remove thresholds, not retune them.
- LED take similar to the others: the LED problem was the broken recording. Drop the LED-specific ideas (streak endpoints, special exposure). LED take much worse: do T7.

Keeping it honest: use held-out takes to choose between ideas, not to tune numbers. Once a held-out set has guided several decisions, ask for a fresh one (`--heldout h2`) to confirm.

### T3 — Per-event ground truth (LLM builds the tool, user spends about 10 minutes per take)

Why: "26 of 30" does not say which catches were missed and which were invented. The existing counter ([`counter.py`](tracking_lab/counter.py:243)) adds 1 to a total on `C` and stores neither the frame nor the hand.

Nothing in this task is built. This is a proposed design.

**Part A, a marking tool.** New file `tracking_lab/mark_catches.py`, about 150 lines. Do not extend `counter.py`: it plays a rendered video, its queue is hard-coded to the six, and its displayed frame number is off by one from `player.pos` (**Inferred** from the code).

- Argument: a session suffix, for example `box30` or `cascade60_h1`. Find the folder with `find_rec()` from [`evaluate.py`](tracking_lab/evaluate.py:57).
- Load frames with `RecordingSession(path, load_depth=False)` from [`recording.py`](tracking_lab/recording.py:24). `sess[i].rgb` is the image and `sess[i].idx` is the frame number. Use `sess[i].idx` as the stored frame number.
- Show the raw frame with the frame number drawn on it. Do not draw tracker output; the person marking must not be influenced by it.
- Keys (reuse the key codes from `counter.py` lines 231–253): SPACE play/pause, LEFT/RIGHT one frame, UP/DOWN speed, `L` = a catch by the hand on the **left side of the picture** at this frame, `R` = the same for the right side, `X` removes the last mark, ENTER saves, `q` quits without saving.
- Use picture side, not "left hand": the tracker's hand names are `left_wrist` / `right_wrist` from the pose model, and whether those match the viewer's left and right has not been checked.
- Show the running total and the last five marks on screen, so the user can see that the total equals their known count.
- Save to `tracking_lab/ground_truth/<suffix>_catches.json` as `{"session": ..., "catches": [{"frame": 123, "side": "L"}, ...]}`. Add `!tracking_lab/ground_truth/*.json` to `.gitignore` under the existing `*.json` rule, or the files will be ignored.
- Mark the frame where the ball first stops in the hand. Write that definition on screen and in the docstring; consistency matters more than the exact choice.

Test without the user: run it on `box30`, mark three catches, save, reload the file, check the frames.

**Part B, a scorer.** Add to `evaluate.py` (or a new `tracking_lab/event_score.py` if `evaluate.py` would pass 400 lines):

1. Tracker catches: `[e for e in tracker.events if e["event"] == "CATCH"]`. Each has `frame` and `hand` (`left_wrist` or `right_wrist`).
2. Convert `hand` to a picture side: in `tracker.per_frame`, the entry for that frame has `wrists = {name: (x, y)}`. The wrist with the smaller x is `L`. If only one wrist is present in that frame, the side is unknown; match such a catch on frame alone and count it separately.
3. Match in time order, one to one: a tracker catch matches the nearest unmatched true catch with the same side within ±4 frames.
4. Report per take: matched, missed (true with no match), phantom (tracker with no match), wrong-side (a match exists within ±4 frames only if the side is ignored), and the median of (tracker frame − true frame).
5. The tracker reports a catch only after the ball has stayed at the wrist for `held_dwell` frames, so a constant delay of a few frames is expected. If the median offset is 3 or more, centre the ±4 window on the median. Do not widen the window to improve the score.

Done when: the scorer runs on at least cascade60 and box30 (or their held-out versions) and prints missed / phantom / wrong-side counts. Ask the user to mark the held-out takes too if T2 is done.

### T4 — Decide about the detector with evidence (LLM plus about 30 minutes of user labelling)

Do not train first. **Measured**: on the six videos the fused detector returns at least 3 candidates in 84% of frames (3 in 64.0%, 4 or more in 20.1%, fewer than 3 in 15.9%). That does not show the detector is the limit.

1. Pick 25 evenly spaced frames from each held-out take (about 150 in total) and copy them to `tracking_lab/detection_testset/images/`.
2. The user labels every ball with a box, in YOLO format, into `tracking_lab/detection_testset/labels/`. The earlier labelling process is described in `Notes/tier1_dataset_creation/` (the audit did not review those notes). These frames must never enter a training set (R5).
3. Write `tracking_lab/score_detector.py` (not built): for each labelled frame run `BallDetector.detect` from [`detectors.py`](tracking_lab/detectors.py:50) and count a label as found when a detection's centre lies inside the label box enlarged by half its size. Report recall, and false detections per frame. Ignore the class id (the tracker does).
4. Score V8. If recall is high (roughly 95% or more), leave the detector and go to T5.

Only if recall is clearly low, train:
1. `python3 -m tracking_lab.build_v12_dataset --check` must pass (**Measured** earlier: train 4,103 images from 78 recordings, val 887 from 19, none shared).
2. `cd .tmp && zip -q -r v12_clean.zip v12_clean` (441 MB; JPEGs barely compress).
3. On Colab: add a secret named `NGROK_TOKEN`, run [`colab_setup_cell.py`](tracking_lab/colab_setup_cell.py:1), upload the zip to `/content/`, run [`colab_train_v12_cell.py`](tracking_lab/colab_train_v12_cell.py:1). Both cells are **Not tested** on Colab; the training cell ran for 1 epoch on 96 images on this machine's CPU with paths substituted.
4. Download the exported OpenVINO folder, then `TRACKING_BALL_MODEL=<folder>/best.xml python3 -m tracking_lab.evaluate` and run `score_detector.py` with the same variable.
5. Keep the new model only if it is better on the labelled test set **and** not worse on the held-out takes.

Expectations to give the user: `v12_clean` is nearly the data V8 was trained on (**Inferred** for V8's training set, from file dates), so a large gain is unlikely. Its validation mAP will read lower than older figures because the split is now by recording; the numbers are not comparable. `--with-glm` adds 239 machine-labelled frames to train only; try it as a second run.

### T5 — Tracker structure (LLM; needs T2 and T3 to judge)

Three defects were measured. For each, the fix below is a **proposal that has not been tried**; its effect is unknown.

Line numbers are for [`tracker.py`](tracking_lab/tracker.py:156) as of 2026-11-12 and will drift. Search for the quoted text.

**Defect 1: two balls HELD in one hand.**
- **Measured** over whole clips: 14.5–44.3% of frames. Over the juggling run only (first THROW to last CATCH, which removes the start and end where a juggler really does hold two balls): cascade60 39.7%, box30 41.6%, 441_30 34.3%, shower40 49.4%, columns30 24.0%, flash_10 18.5%. The longest unbroken stretch is 42 frames in cascade60 and 9–21 in the others. Script: `.tmp/audit/double_held_window.py`.
- A track becomes HELD in two places: the normal catch (search `t.state = "HELD"` after `deduped_catches.append`, near line 460) and the transfer branch (search `straight-across pass / carry`, near line 360).
- Proposal: at both places, when a track becomes HELD in hand H, look for another track already HELD in H. If that other track had no detection this frame (`misses > 0`), it is stale: release it. Try two variants and compare: (a) release with no event, (b) release with a THROW event dated at this frame. Variant (b) may also reduce Defect 3; that is a guess.
- Add a config flag for it, default off, so §3 stays `identical` with the flag off. Use `--set` to turn it on.

**Defect 2: catches with no observed flight.**
- **Measured**: 32% of catches. Most are same-frame THROW+CATCH pairs from the transfer branch (column `xfer`: 6, 10, 4, 16, 5, 5).
- Do not remove the branch on suspicion. Use T3: for each transfer catch, is there a true catch within ±4 frames on that side? If most are real, keep it. If most are phantoms, remove it and then fix the misses that it was hiding.

**Defect 3: throws under-counted.**
- **Measured**: mean |throw error| 4.67 against 3.17 for catches; catches exceed throws in all six.
- The silent release (search `silent_releases.append`, near line 243) moves a HELD track to IN_FLIGHT with no THROW. Column `sil` is 0 on all six, so this path is not the cause here. The gap comes from somewhere else, and where is **not known**.
- First step is diagnosis, not a fix: for each track, list consecutive CATCH events with no THROW between them (`diagnose()` in [`evaluate.py`](tracking_lab/evaluate.py:87) counts these as `CC`; it shows 0, so also list catches whose track has no earlier event at all, and tracks that end HELD). Print them for box30 and 441_30, look at the frames, then propose a rule.

How to judge any T5 change, in this order: (1) per-event missed and phantom counts from T3, (2) held-out table from T2, (3) the diagnostics `2held%`, `flt`, `C-T`. The six-video catch error is the last thing to look at. Revert anything that is worse on held-out.

Deferred, no evidence yet: Kalman filter, tempo-adaptive gating, identity stitching.

### T6 — Real time (LLM, after T5 or in parallel)

**Measured** offline on this machine (22 logical cores, CPU): pose 23.9 ms + ball 21.1 ms + motion 6.8 ms + histogram 0.4 ms = 52.3 ms per frame. Target: under 33 ms.

There is no timing tool in the repo. Write `tracking_lab/bench.py`: load 300 frames of cascade60, discard the first 20 for warm-up, time `PoseDetector.detect`, `BallDetector.detect` and `MotionDetector.detect` separately, print the mean and the 95th percentile.

Options, none measured:
1. Run pose every second or third frame and reuse or interpolate the wrists. Pose is the largest cost. Check with §3 how much the counts move.
2. 416-pixel models exist: `engine/models/zipped/V8_balls_held416/best_openvino_model/best.xml` and `engine/models/zipped/yolo11n-pose416/best_openvino_model/best.xml`. The detectors read the input size from the model, so `TRACKING_BALL_MODEL=<416 xml>` works unchanged. **Measured** at the end of the audit (416 ball model, 640 pose model, default constants, six tuning videos): mean |catch err| **8.83** against 3.17 for the 640 model. Counts: cascade60 65, box30 22, 441_30 26, shower40 37, columns30 47, flash_10 46 (expected 60 / 30 / 30 / 40 / 30 / 30). Coverage falls to 87.7–88.6% on three takes. Output in `.tmp/audit/eval_v8_416.txt`. So the 416 ball model is not a drop-in replacement. Its speed was not measured, and whether retuned constants would recover the accuracy is unknown (and R1 forbids retuning on these six). The 416 pose model has not been tried. The pose model path is hard-coded at [`detectors.py`](tracking_lab/detectors.py:23); add a `TRACKING_POSE_MODEL` override the same way as the ball one.
3. Run the two networks in parallel with OpenVINO async infer requests.
4. OpenVINO lists a `GPU` device. The handoff says it was unusable in earlier sessions. One short measurement with these small models is worth it.
5. Then write the live runner: camera → `perceive()` ([`perception.py`](tracking_lab/perception.py:1)) → `tracker.step()` → counts drawn on the live view, with `FrameGuard` from `camera.py` included. Measure latency on the device.

Any speed change that alters detections changes the counts. Accept it only with T2/T3 evidence, not on speed alone.

### T7 — LED exposure (only if T2 shows LED is worse; needs T1)

`python3 -m tracking_lab.exposure_test` sweeps manual settings of 16 / 8 / 4 / 2 ms and reports unique frames per second beside detections. A setting with repeated frames is disqualified. **Not tested** on a device; its gain values are starting guesses.

### T8 — Housekeeping (ask the user before each)

1. **Rotate the ngrok token and the vision-API key.** Both were in plaintext before the audit. The token now lives in `tracking_lab/ngrok_token.txt`, which is gitignored. Only the user can rotate them.
2. **Disk** (95% full, 33 GB free). `rm -r .tmp/v12_dataset .tmp/v12_dataset.zip` frees about 1.9 GB: the zip is 1.8 GB; the folder is mostly hard links and frees about 0.12 GB (**Measured**). Ask first; it is a deletion.
3. **Commit.** `tracking_lab/`, `sam3_live/`, `yolo11n_v11_openvino_model/` and the tracking notes in `ROO_NOTES/` are untracked. Before `git add`, run `git status --short` and confirm that `tracking_lab/ngrok_token.txt`, `tracking_lab/recordings/` and `.tmp/` do not appear. Then search the files to be added for hard-coded keys (`grep -nE "api[_-]?key|token|Bearer" tracking_lab/*.py`) and read each hit.

---

## 5. What has not been tested (complete list)

- Every line that talks to the camera: `camera.configure_rgb`, `FrameGuard` inside the live loops, `wizard.py`, `verify_wizard.py` (including `--heldout`), `record.py`, `exposure_test.py`, `led_tune.py`, and the exposure default in `sam3_live/sources.py`. Compiled only. `FrameGuard` and `scan_recording` were tested on saved frames.
- The exposure unit (assumed 100 µs per unit) and whether a manual setting persists on the sensor between runs.
- Both Colab cells on Colab, including the secret lookup.
- V12 training. No V12 model exists.
- The 416-pixel pose model, and the speed of either 416 model. (The 416 ball model's accuracy was measured: see T6.)
- Every proposal in T3, T4 step 3, T5 and T6. None is built.

## 5a. Addendum — session 2026-11-12 (post-audit work)

**Measured / fixed — display:** The desktop session is Wayland and cv2's bundled
Qt ships only the `xcb` platform plugin, so `wizard.py` (and every cv2.imshow
loop) ran invisibly and appeared to hang. XWayland IS running (`:1`). Fix:
[`tracking_lab/__init__.py`](tracking_lab/tracking_lab/__init__.py) now sets
`QT_QPA_PLATFORM=xcb` (via `os.environ.setdefault`, user override preserved)
before cv2 imports. Interactive scripts must be run with the SYSTEM python
(`/home/twain/.rye/shims/python3`), NOT the project venv (venv has no cv2).
T1 step 1 lines captured: `RGB exposure: AUTO`,
`RGB exposure_range_units: [1.0, 10000.0, 1.0, 166.0]`. T1 steps 2–3
(exposure-unit confirmation) still Not tested.

**Built and self-tested — T3 tooling (no GUI test yet on device):**
- [`mark_catches.py`](tracking_lab/mark_catches.py): frame-stepping ground-truth
  marker per the T3 Part A design (picture-side keys `l`/`r`, x undo, ENTER
  saves to `tracking_lab/ground_truth/<suffix>_catches.json`; resumable).
  Headless selftest (`--selftest`) passes: session load, idx extraction, save/
  reload round-trip. `.gitignore` got `!tracking_lab/ground_truth/*.json`.
  Catch definition on screen: ball FIRST STOPS in the hand.
- [`event_score.py`](tracking_lab/event_score.py): per-event scorer per T3
  Part B (hand→picture side via per-frame wrists' smaller-x; ±4-frame
  one-to-one same-side matching; window re-centred once if median delay ≥ 3;
  wrong-side and unknown-side counted separately). Matcher selftest passes;
  full pipeline smoke-tested on box30 with synthetic marks (temp file removed;
  no fake ground truth committed). Nothing here alters tracker results:
  §3 check re-run after each change, `identical` every time.

**Measured — first per-event result (box30, user-marked 2026-11-12):**
User marked 31 catches (L16/R15; expected total was 30 — one extra mark,
unresolved). Scorer (±4 window, re-centred on the measured median): matched
20/31 (65%), missed 11, phantom 6, wrong-side 0. The totals-only view
("26 of 30, −4") was missed-and-phantom cancellation, exactly as R2
predicted. Structure in the detail:
- Offset distribution tracker−true: {−4: 11, −3: 4, −2: 2, −1: 2, 0: 1} —
  the user's marks sit ~4 frames AFTER the tracker's events (marking
  latency and/or the tracker firing while the ball is still inside the
  75 px held radius, before the visual stop). No offset ≥ +1. Re-centring
  absorbs this; do not widen the window further (T3 rule).
- ALL 6 phantoms are at frames 227–246 — after the user's last true mark
  (~214), while balls rest in hands at the take's end; two different
  tracks are "caught" at the same hand on the same frame (f229 R, tracks
  3 and 4). End-of-take rest is a phantom generator; zero phantoms
  mid-pattern.
- 10 of the 11 missed catches are one side of a near-simultaneous
  opposite-side pair (box catches arrive L/R 0–2 frames apart) where the
  tracker matched only the other side — consistent with Defect 1 (stale
  second HELD in one hand; 2held% is 35.2 on this take): a ball the
  tracker still believes is HELD cannot be re-caught.
- 441_30 corroborates: track 2's last event is CATCH at f209 and it stays
  HELD, glued to the left wrist, for the remaining 63 frames while the
  pattern continues (diagnostic script:
  `.tmp/audit/throw_gap_diagnosis.py`).
T5 Defect 1's stale-HELD release now has per-event evidence to be judged
against: if it works, box30's 11 missed should convert to matched.
**Measured — cascade60 per-event (user-marked 2026-11-12):** 61 marks
(L30/R31; expected 60 — one extra again, likely same double-press habit).
Matched 58/61 (**95%**), missed 3–4, phantom 5–6, wrong-side 0. Offsets
tightly −3-centred (27 of 57 at exactly −3; marking latency, absorbed by
re-centring). Missed true frames 136/422/501/642 — scattered, NOT
pair-structured. Phantoms: 825/861 are end-of-take (after last true mark
813; same rest artifact as box30), 495/623/625/736 mid-pattern.
Contrast with box30 (65% matched, misses pair-structured) fits Defect 1:
box's near-simultaneous L/R catches are the worst case for a stale second
HELD; cascade's rhythmic alternation rarely hits it. Combined per-event
truth: matched 78/92 (85%), missed 14, phantom 11, wrong-side 0 — the
hand→picture-side conversion has zero disagreements on 92 events.

**Measured — T2 held-out takes recorded 2026-11-12 (user, camera D455):**
cascade60_h1, box30_h1, 441_30_h1, shower40_h1, flash_10_h1,
cascade60_led1 (all realsense, valid integrity, heldout=true in meta).
columns30_h1 FAILED: camera disconnected mid-take; folder exists but has
no meta.json and no usable frames. The wizard then skipped it on rerun
because its done-check only looked at folder names — fixed in
[`verify_wizard.py`](tracking_lab/verify_wizard.py:179) (a pattern counts
as done only if its folder has a meta.json). Baseline note: after these
takes, baseline_eval_v8.json was regenerated; compare showed the 6
held-out rows as new with ZERO tuning-table differences, so no tracking
change. Future §3 checks must remain identical against this baseline.

HELD-OUT RESULT (the first honest generalisation numbers):
- ordinary balls (5 takes): +6, -6, -7, -4, -2 -> mean |catch err| 5.00
  vs 3.17 on the tuning set (~1.6x worse). Per T2's decision rule the
  constants are overfitted: T5 becomes the priority, and it should remove
  thresholds, not retune them. Direction flips per pattern (cascade
  over-counts +6, box/441/shower under-count -4..-7) — consistent with
  the per-event finding that box misses are Defect-1 structured while
  cascade phantoms come from end-of-take rest.
- LED cascade60_led1: 47 counted of 60 (-13), flight-backed 85%,
  coverage 98.9%, 2held% 18. Clearly worse than every ordinary take
  (worst ordinary: -7). Per T2's rule: LED IS worse -> T7
  (exposure_test on device) is justified; T1 is done so the prerequisite
  holds.
- Not done: columns30_h1 redo (~2 min of user time; wizard now offers
  it); marking the held-out takes for per-event scoring.

**Measured — T5 variants a–e ALL WORSE than baseline (2026-11-12).**
Implemented flag-gated (default off; §3 `identical` with flags off,
verified after every edit), judged on per-event + held-out + tuning:
(a) release stale HELD and (b) release+THROW: tuning 3.17→4.67,
held-out 6.33→7.17, box30 missed 11→16. (c) kill ghost: held-out 8.50.
(d) disable transfer branch: catastrophic (tuning 9.67, held-out 10.67,
box30_h1 −13) — the branch is LOAD-BEARING for box: straight-across
passes never leave both wrist radii, so it is box's only observer.
(e) kill twin HELD at same hand+position, r=40/60/80: tuning 4.50/
4.67/4.83, held-out 7.50/7.83/8.50. All flags remain in tracker.py,
default off, inert.
Deeper evidence: detector starvation RULED OUT (9 of 11 missed box30
frames have all 3 balls detected, scores 0.5–0.96). Transfer CATCHes
vs ground truth: box30 5/10 real, cascade60 3/6 — a coin flip;
flight-backed 94–96% real. Miss mechanism: f200/f214 two tracks HELD
at the SAME hand+position (identity duplicated by the transfer branch);
f29 an IN_FLIGHT track dwelling at a wrist with away_peak=0, blocked
by min_flight_frames. Next lever: redesign transfer-branch identity
handling (keeps counting role, stops minting duplicate HELD tracks) —
bigger than a flag; needs its own session.

**Measured — canonical-HELD redesign (T5 session 2, 2026-11-12): REFUTED.**
The "next lever" from the previous session was built flag-gated and judged
on the 124 marked events (baseline: matched 100, missed 24, phantom 13).
Mechanisms implemented in tracker.py (all default-off, inert; §3 identical
after every edit; event_score gained `--set` to judge flags per-event):
- canonical_held (master; also = transfer twin veto, same as Fix C)
- canonical_dedup_merge  — a deduped catch must not latch a second HELD
- canonical_transfer_ghost — a transfer retires a ghost occupant first
- canonical_displace     — a legal re-catch displaces the old occupant
- canonical_dwell_vacant — dwell-promote an IN_FLIGHT track ONLY into a
  hand with no HELD occupant (strict subset of Fix B)
Results (matched/missed/phantom over 124 events):
- baseline flags-off ......... 100 / 24 / 13
- canonical_held (veto only) .  95 / 29 / 14  (= transfer_twin_guard=1;
  the veto deletes real box transfer catches)
- +transfer_ghost ............  92 / 32 / 12
- +displace ..................  89 / 35 / 16
- +dedup_merge ...............  no change vs veto-only (rarely fires)
- dwell_vacant alone .........  99 / 25 / 18  (-1 missed, +5 phantom)
- dwell_vacant + canonical ...  93 / 31 / 20
Every enforcement of the one-HELD-per-hand invariant LOSES matched events:
the baseline's duplicate-HELD minting is load-bearing — its phantoms and
misses cancel toward the truth better than any principled identity scheme
tried (now 12 structural variants total). Tracker identity surgery is
CLOSED as a research direction; flags remain in tracker.py, default off.
What remains forward-looking: T6 async/live runner, h3 set before any
default flip could ever be reconsidered, T7 exposure test (LED is worse),
T8 housekeeping (needs user approval).

**Measured — T6 realtime levers, session 2 (2026-11-12): both CLOSED.**
- OpenVINO async overlap of ball+pose on the same frame (new
  `bench.py --async` probe, start_async on two infer requests): infer pair
  74.6 ms (p95 99.8) + preproc 7.3 vs 48.3 ms sequential sync — WORSE by
  55%; the CPU plugin's intra-model threading thrashes when two models
  contend. Async CLOSED, production detectors untouched.
- 416-pose model (engine/models/yolo11n-pose416/, pre-existing export):
  speed 37.5 ms vs 47.6 ms for 640 (-21%), same person/wrist coverage
  (130/130 frames with wrists). Added `TRACKING_POSE_MODEL` env override
  in detectors.py (default unchanged; detcache _model_tag includes the
  pose path so caches cannot silently mix). Judged per-event on the 124
  marked catches: matched 98 / missed 26 / phantom 14 vs baseline
  100/24/13 — slightly WORSE. 416-pose CLOSED for accuracy; 640 stays.
§3 identical after all edits. Offline T6 levers are exhausted; the live
runner and any further speed work need the camera/user.

**Measured — h3 held-out set + T7 exposure sweep (user, 2026-11-12 ~18:00).**
- h3 (6 takes, all valid): catch err +6/-4/-4/-10/-4/+4, mean |err| 5.33
  vs h1+h2 5.31 — generalisation steady, no regression. Tuning table
  byte-identical; only h3 rows new, so baseline_eval_v8.json was
  regenerated (same precedent as the h1/h2 additions) and §3 verified
  `identical` after. shower40_h3 (-10) is the worst single take so far.
  h3 stays UNTOUCHED as the referee for any future candidate change.
- T7 exposure (ordinary balls, results in .tmp/exposure_test_results.json):
  auto (16.6 ms, gain 64) dets 2.52/frame, 52% frames with all 3; every
  manual setting (16ms/g16, 8ms/g32, 4ms/g64, 2ms/g128) is slightly WORSE
  (2.43-2.46). Lower saturation does not buy detections. VERDICT: keep
  AUTO, T7 closed for ordinary balls. (LED-ball exposure was not part of
  this sweep; LED stays 'worse, avoid'.)

**Built — live counter (T6 live runner, 2026-11-12).**
[`tracking_lab/live.py`](tracking_lab/live.py): camera (realsense/webcam via
wizard.Cam) -> the exact offline pipeline (perceive + JuggleTracker, same
constants, no tuning) -> overlay window with live CATCH/THROW counts
(total + L/R + C-T), track states, last events, fps and camera status.
Keys: r reset, q/Esc quit, space pause. `--set` overrides, `--selftest`
runs the identical frame path headless over real box30 frames (passes:
120 frames -> 10 catches, 3 tracks). Expect ~19 fps processing; counts are
frame-rate independent. §3 identical. NOT yet tested on the camera —
first live run is the user's.

## 6. Quick reference

| thing | where |
|---|---|
| Tracker and constants | [`tracker.py`](tracking_lab/tracker.py:27) (`TrackingConfig`), [`step()`](tracking_lab/tracker.py:156) |
| Detection + fusion | [`detectors.py`](tracking_lab/detectors.py:50), [`perception.py`](tracking_lab/perception.py:1) |
| Detection cache | [`detcache.py`](tracking_lab/detcache.py:45), files in `.tmp/detcache/` |
| Suite, diagnostics, sweep | [`evaluate.py`](tracking_lab/evaluate.py:264) |
| Baseline comparison | [`compare_eval.py`](tracking_lab/compare_eval.py:1), `tracking_lab/baseline_eval_v8.json` |
| Camera exposure and repeated-frame guard | [`camera.py`](tracking_lab/camera.py:89) |
| Counted recording wizard | [`verify_wizard.py`](tracking_lab/verify_wizard.py:167) |
| Dataset builder | [`build_v12_dataset.py`](tracking_lab/build_v12_dataset.py:278), output `.tmp/v12_clean` |
| Recordings | `tracking_lab/recordings/<timestamp>_<label>/` with `no_boxes/frame_N.jpg`, `depth/`, `meta.json` (gitignored) |
| Audit evidence and scripts | `.tmp/audit/` (gitignored, this machine only) |

Environment variables: `TRACKING_BALL_MODEL` (path to an OpenVINO `.xml`), `TRACKING_MODEL_V11=1`, `TRACKING_EXPOSURE_MS`, `TRACKING_GAIN`. The older exposure-lock variables are retired and print a warning.

Models: V8 (deployed) `engine/models/yolo11n.xml`; V11 `yolo11n_v11_openvino_model/yolo11n_v11.xml`; pose `engine/models/yolo11n-pose.xml`. Both ball models have two classes (0 ball, 1 ball_held); the tracker ignores the class.

The `codebase-memory` graph server was not connected during the audit, so nothing was stored there. These three notes are the record.

## 7. When to stop and ask the user

- Before deleting anything, committing, or changing a tracker constant or default.
- When a held-out result contradicts an expectation in this file.
- When a device test fails in a way the traceback does not explain.
- When a task needs more than about 15 minutes of the user's time.

**Measured — T6 first results (2026-11-12, all offline on this machine):**
- [`bench.py`](tracking_lab/bench.py) built (T6's missing timing tool):
  300 frames cascade60, 20 warm-up: pose 25.6 ms (p95 31.1), ball 22.7
  (28.1), motion 3.1 (5.2) = 51.3 ms sum, 54.4 wall, i.e. 18 fps. Matches
  the audit's 52.3 ms within noise.
- GPU (T6 option 4) CLOSED: OpenVINO's GPU plugin segfaults inside
  compile_model itself on this machine (Intel Arc iGPU; faulthandler
  traceback captured, core dumped). Confirms the handoff's "unusable"
  with hard evidence; do not retry without a driver/plugin update.
- TRACKING_DEVICE env override added to [`detectors.py`](tracking_lab/detectors.py)
  (default CPU, the scored configuration; section-3 check `identical`
  verified after the edit).
- T6 option 1 (pose throttle) MEASURED — the first positive lever.
  New TrackingConfig field pose_every_n (default 1, settable via --set;
  detcache.replay holds the last wrists between pose updates, which
  exactly reproduces a throttled live runner given deterministic pose).
  every-2: tuning 2.67, held-out 6.33 (unchanged), LED -12; saves about
  12.8 ms (about 38 ms/frame). every-3: tuning 3.50 (slightly worse),
  held-out 6.33 improved to 5.00, LED -13 improved to -8; saves about
  17 ms (about 34 ms/frame, nearly the 33.3 target).
  NOT yet the default: held-out has now guided one decision, so per the
  honesty rules confirm on a FRESH held-out set (--heldout h2) before
  flipping, and re-check the per-event marks with the throttle on.
- Not done: 416-model speed measurement (accuracy already measured bad:
  8.83), async infer requests, the live runner itself, fresh held-out h2.

**Measured — h2 verdict + box30_h1 per-event (2026-11-12 evening):**
User recorded columns30_h1 redo (valid now), all six h2 takes (integrity
valid), and marked box30_h1 (32 marks, L16/R16).
- THROTTLE VERDICT: h2 REFUTES the h1 gain. pose_every_n=1: held-out
  5.31 (h1 4.50, h2 4.83). pose_every_n=3: held-out 5.15 (h1 4.00,
  h2 5.83). On the fresh set throttle is WORSE (5.83 vs 4.83); the
  combined 5.15 vs 5.31 difference is inside the user's stated mental-
  count noise. DEFAULT STAYS pose_every_n=1. The h1/LED improvement was
  set-specific — exactly what the fresh-held-out rule exists to catch.
- box30_h1 per-event: matched 22/32 (69%), missed 10, phantom 2,
  wrong-side 0, medoff -4. Compared with tuning-set box30 (20/31, miss
  11, ph 6): the miss-dominant, low-phantom structure REPLICATES on
  held-out data — consistent with the transfer-branch identity-
  duplication diagnosis (missed second-of-pair catches, few fakes).
  Combined per-event truth: 100/124 (81%) matched, 24 missed, 13
  phantom, 0 side disagreements on 124 events.
- Next session's open items: transfer-branch identity redesign (T5's
  real fix); h1+h2 now both guided decisions — any future lever needs
  h3; LED -13 on h1 stands (led1 was h1-only).

**Measured — transfer-fixes B and C (2026-11-12, flags default-off; sec-3
identical verified with flags off):**
- Fix B (dwell_catch_frames: accept a catch after N frames parked at a
  wrist even with away_peak==0): B=12 tuning 3.17 to 1.83, held-out
  5.31 to 5.15 — BUT per-event (the priority metric) it converts only
  2 misses into matches while ADDING 3 phantoms (combined 100 matched /
  24 missed / 13 phantom becomes 102 / 22 / 16). The tuning gain is
  cancellation, not precision: the dwell trap at f29 is real but
  dwell-accept also fires on non-catches. NOT flipped to default.
  A discriminating version (depth or velocity signature at dwell time)
  is the possible refinement.
- Fix C (transfer_twin_guard: block transfer CATCH when the target hand
  visibly holds a detected HELD ball): WORSE everywhere (tuning 5.67,
  box30_h1 matched 22 to 19). The "twin" is usually the same physical
  ball legitimately re-observed, not a duplicate — blocking real
  transfer counts hurts box badly. REJECTED, flag stays off.
- Both flags remain in TrackingConfig, inert by default. The real fix
  remains a transfer-branch identity redesign (single canonical HELD
  per hand, transfers re-point identity instead of minting tracks) —
  a bigger change for a fresh session, to be judged on h3 (h1+h2 are
  now both spent).

**Measured — T4 no-labels detector analysis (2026-11-12, detcache +
fresh MotionDetector pass over all 19 valid sessions, 8,348 frames):**
- Raw YOLO: 15.7% of frames have fewer than 3 detections (0.0% have
  zero); confidence bands: 75.5% high (>=0.70), 24.2% mid, 0.3% low.
- Motion fusion covers 55% of starved frames. Of the 588 uncovered
  frames, 74% sit in the FIRST 20% of a take (setup: balls in hand,
  juggler settling) and 8.5% in the last 20% (finish rest); the MIDDLE
  60% of takes — actual juggling — has only 71 uncovered frames out of
  about 6,600 (about 1.1%).
- Conclusion: during juggling the detector stack (YOLO V8 + motion
  fusion) leaves about 1% of frames short of three detections, and
  those are single frames, not stretches. YOLO RETRAINING IS NOT
  JUSTIFIED by this evidence. The audit's warning stands: low
  candidate counts were mostly setup-phase, not mid-pattern misses.
- The 24% mid-confidence band is the only retraining argument left, but
  the tracker's persistence logic already tolerates marginal
  detections; testing that hypothesis needs the labelled T4 test set.
- PREPARED: tracking_lab/detection_testset/images/ has 325 evenly
  spaced frames from all 13 valid held-out sessions (about 25 each),
  ready for user YOLO labelling if we ever want the precise
  score_detector.py recall number. Not urgent.

**Measured — missed-catch vs detection-hole correlation (2026-11-12,
closes the retraining question):** Of the 24 missed true catches across
box30 / cascade60 / box30_h1, only 3 (12.5%) occurred on frames with
fewer than 3 raw detections, and motion fusion supplied nothing on any
of the 24. The other 21 misses (87.5%) happened with ALL balls
confidently detected. Combined with the earlier proxy result (mid-juggle
starvation about 1.1% of frames), the detector stack is NOT the limit on
catch accuracy — the misses are tracker identity/state-machine logic.
VERDICT: YOLO retraining is NOT justified for catch accuracy at this
time. Colab training remains available if we later want to firm up the
24% mid-confidence band or chase the last 3 starvation-linked misses,
with the 325-frame labelled test set already prepared; but no recall
crisis exists.
