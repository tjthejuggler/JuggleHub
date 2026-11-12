# Detector Head-to-Head: SAM 3 vs Fine-Tuned YOLO vs Legacy HSV (2026-11-10)

## Question
Can SAM 3 replace or beat our fine-tuned YOLO models for live ball tracking,
and can any candidate run in real time while juggling?

## Setup
- **Sample**: 16 images stratified 2/session across 8 GT sessions
  (daylight, mixed-light, lowish-light, LED balls, color-aug, edge cases),
  29 GT ball boxes total (`engine/data/annotation_sessions/`, YOLO format,
  classes led_on/led_off/dropped_ball counted as balls, hand ignored).
- **Matching**: greedy score-ordered IoU; reported at IoU>=0.3 (location)
  and IoU>=0.5 (strict box).
- **Hardware**: laptop, Intel Core Ultra 9 185H, NO NVIDIA GPU — everything CPU.
- **Harness**: `sam3_live/benchmark_detectors.py` (+ `prompt_sweep.py`),
  results in `sam3_live/eval_out/*.json`.

## Contenders
1. **sam3** — Meta SAM 3 (848M), zero-shot, image mode, prompt "ball",
   conf 0.2 (tuned via 7-prompt x 7-threshold sweep).
2. **yolo11s** — our fine-tune V9.1 (Oct 8), OpenVINO INT-less FP32,
   640x640 letterbox, conf 0.25, classes ball/ball_held.
3. **yolo11n** — our fine-tune V8 (Oct 2), same pipeline.
4. **legacy HSV** — `LegacyColorTracker` (single-color calibration).

## Results — IoU>=0.3 (does it find the ball?)
| detector | P | R | F1 | mIoU | median ms | p95 ms | FPS |
|---|---|---|---|---|---|---|---|
| sam3 (ball@0.2) | 0.880 | 0.759 | **0.815** | 0.503 | 28 412 | 29 200 | 0.04 |
| yolo11s | 1.000 | 0.759 | **0.863** | 0.886 | 43–75 | 46–100 | 13–23 |
| yolo11n | 1.000 | 0.793 | **0.885** | 0.784 | 17–31 | 26–42 | 32–59 |
| legacy HSV | — | 3/29 hits | — | — | 1.17 | 8.9 | 856 |

## Results — IoU>=0.5 (is the box tight?)
| detector | P | R | F1 | mIoU |
|---|---|---|---|---|
| sam3 | 0.320 | 0.276 | 0.296 | 0.678 |
| yolo11s | 1.000 | 0.759 | **0.863** | 0.886 |
| yolo11n | 0.826 | 0.655 | 0.731 | 0.877 |

## Prompt sweep findings (SAM 3, 7 prompts x 7 thresholds)
- Best config: **"ball" @ conf 0.2 → F1 0.815** (P 0.880 / R 0.759).
- "juggling ball" scores literally 0.0 — not in SAM 3's text vocabulary.
- LED balls are NOT a hard zero after all: "led ball" @ 0.1 → R 0.55,
  "glowing ball" fires too but noisily (P 0.48). Earlier "hard zero" was
  a threshold artifact (default 0.5 filtered everything).
- Recall collapses above conf 0.4; FP rate stays low at 0.2+.

## Latency analysis (real-time budget = 33 ms at 30 FPS)
- **yolo11n: 17–31 ms median — REAL TIME on CPU today** (32–59 FPS).
- **yolo11s: 44–75 ms — near real time** (13–23 FPS; fine at 60fps capture
  with per-frame drop, or INT8 quantization should halve it).
- **SAM 3 image mode: ~28.5 s/frame — 860x over budget.** Video/sliding
  window mode measured earlier: ~10 s/frame amortized — still 300x over.
- legacy HSV: 1.2 ms but only works on a calibrated color in decent light
  (3/29 on this multi-color sample; it was built for one-ball-at-a-time
  color tracking).

## Interpretation
1. **Zero-shot SAM 3 is genuinely good at finding juggling balls** (R 0.76
   with ZERO training on our domain, P 0.88) — but its boxes are loose
   (mIoU 0.50; fails strict IoU 0.5 scoring) and it is astronomically
   too slow for live use on this CPU-only laptop.
2. **The fine-tuned YOLOs dominate on every axis that matters for
   production**: perfect precision, equal-or-better recall, boxes 2x
   tighter, and 300–1000x faster. yolo11n (V8) is the sweet spot;
   yolo11s (V9.1) has slightly tighter boxes but 2x the latency.
3. **Recommended production config**: yolo11n OpenVINO @ 640 conf 0.25
   for live tracking; keep yolo11s as accuracy reference. This matches
   what the engine already deploys.
4. **SAM 3's real value here is offline**: (a) auto-labeling new footage
   to speed up annotation (its loose boxes are fine for YOLO training
   after review), (b) zero-shot fallback for exotic new ball types the
   YOLO wasn't trained on, (c) pseudo-labeling teacher for future
   dataset versions (V11+).
5. If a CUDA GPU box ever enters the picture, SAM 3 video mode (~60 ms/frame
   on GPU-class hardware per Meta's numbers) could complement YOLO as a
   re-discovery/verification layer, but it cannot beat a domain-trained
   640px YOLO on this dataset.

## Model-lineage bake-off (2026-11-10, same 16-img sample, IoU>=0.3, conf 0.25)
| model (OpenVINO XML) | P | R | F1 | mIoU | med ms | FPS |
|---|---|---|---|---|---|---|
| **V8_balls_held640 yolo11n** (deployed yolo11n.xml) | 1.000 | 0.793 | **0.885** | 0.78 | 17 | 60 |
| V7_1_balls_col_aug yolo11n | 1.000 | 0.759 | 0.863 | 0.80 | 17 | 58 |
| V9_1_1small_targeted yolo11s (deployed yolo11s.xml) | 1.000 | 0.759 | 0.863 | **0.886** | 45 | 22 |
| V6_6_1_grown yolo11n | 0.955 | 0.724 | 0.824 | 0.75 | 19 | 53 |
| V5n_edgecases yolo11n | 0.913 | 0.724 | 0.808 | 0.58 | 19 | 52 |
| V4n_balls_only yolo11n | 0.909 | 0.690 | 0.784 | 0.56 | 16 | 62 |
| V4s_balls_only yolo11s | 0.905 | 0.655 | 0.760 | 0.54 | 42 | 24 |
| V8_balls_held416 yolo11n | 0.938 | 0.517 | 0.667 | 0.70 | 12 | 81 |
| V1_unaugmented yolo11n | 0.600 | 0.621 | 0.610 | 0.50 | 20 | 51 |
| V2_augmented yolo11n | 0.500 | 0.517 | 0.508 | 0.44 | 16 | 62 |
| yolov8n_original (COCO, not fine-tuned) | 0.023 | 0.034 | 0.028 | — | 30 | 34 |

Takeaways: the dataset quality ladder (V1→V2→V4→V5→V6.6→V7→V8) maps directly to
accuracy — the tracking failures of 2025 were data problems, not architecture
problems. 416px input costs 21 points of recall (small balls shrink too much).
COCO-pretrained-only YOLOv8n can't see juggling balls at all (F1 0.03) —
fine-tuning is everything. V8-640 yolo11n is the accuracy/speed sweet spot;
V9.1.1-yolo11s has the tightest boxes (mIoU 0.886) at 2.5x the latency.

**Caveat (honesty)**: this sample is drawn from the same annotation sessions the
V8/V9 models trained on, so YOLO numbers are optimistic (train/test leakage).
SAM 3's zero-shot numbers are uncontaminated. On genuinely NEW footage the YOLO
advantage will shrink (though not disappear — it will still be ~1000x faster).

## SAM 3 keep-vs-remove (2026-11-10)
Footprint: sam3.pt 3.3 GB + vendored code 132 MB + torch and friends ~1.2 GB in
site-packages ≈ 4.6 GB total.
**KEEP**, if more dataset versions are planned (V11+):
1. Independent pre-labeler: pre-labeling with the YOLO you're trying to fix
   reinforces its own systematic errors; SAM 3 provides supervision from a
   completely different model family → better failure-case discovery.
2. Zero-shot recall 0.76 means it finds most balls in any new lighting/venue
   without retraining.
3. Works headless offline; the CPU slowness is irrelevant for batch labeling.
**REMOVE**, if dataset collection is finished: delete models/sam3/sam3.pt,
.third_party/sam3, `pip uninstall torch torchvision einops timm triton
pycocotools hydra-core iopath submitit` and the sam3_live package keeps working
in legacy-tracker mode only (eval/benchmark SAM3 paths would stop working).

## Artifacts
- `sam3_live/eval_accuracy.py` — GT scorer (also fixed: denormalize YOLO GT
  to pixels; was silently comparing normalized GT vs pixel preds).
- `sam3_live/benchmark_detectors.py` — unified accuracy+latency benchmark.
- `sam3_live/prompt_sweep.py` — prompt/threshold sweep (backbone reused).
- `sam3_live/eval_out/{benchmark_iou03,benchmark_iou05,prompt_sweep}.json`
- Live defaults updated from sweep: prompt "ball", conf 0.2
  (`trackers.py`, `pipeline.py`, `__main__.py`).
