# SAM 3 Live Ball Tracker

2026-11-07: Replaces the phone-era color-blob approach as the default
desktop tracker. Source-pluggable (`webcam` | `realsense`) and
tracker-pluggable (`sam3` | `legacy` color-blob) live pipeline.

## Setup — no HuggingFace form, no account, no auth

Everything is fetched from un-gated sources:

```bash
# 1. Runtime deps
pip install -r sam3_live/requirements.txt

# 2. Official SAM 3 code (Apache-2.0, un-gated)
git clone --depth 1 https://github.com/facebookresearch/sam3.git .third_party/sam3
pip install -e .third_party/sam3
pip install einops iopath hydra-core triton pycocotools psutil timm pandas submitit

# 3. Weights — official sam3.pt rehosted un-gated (1038lab mirror)
bash sam3_live/fetch_weights.sh     # -> models/sam3/sam3.pt (3.4 GB)
#    sha256 9999e2341ceef5e136daa386eecb55cb414446a00ac2b55eb2dfd2f7c3cf8c9e

# RealSense input additionally needs: pip install pyrealsense2
```

Or download manually:
`wget -O models/sam3/sam3.pt https://huggingface.co/1038lab/sam3/resolve/main/sam3.pt`
(For SAM 3.1, `unsloth/sam3.1` is likewise un-gated but ships
transformers-format safetensors — the official `.pt` layout from the
1038lab mirror is what `build_sam3_predictor` consumes.)

CUDA required for inference (torch≥2.7, CUDA 12.6+, ~5–15 FPS for 3–5
balls on consumer NVIDIA GPUs; the overlay interpolates between result
frames so display stays smooth). No CUDA → clean error suggesting
`--tracker legacy`.

## Usage

```bash
# Baseline test: laptop webcam + SAM 3, 3 balls
python -m sam3_live --source webcam --tracker sam3 --balls 3

# RealSense depth: background rejection (1–4 m), 3D trajectories
python -m sam3_live --source realsense --balls 3 --csv runs/t1.csv

# Tuned prompt + A/B against legacy tracker
python -m sam3_live --prompt "ball" --balls 3
python -m sam3_live --tracker legacy --balls 3
```

### Accuracy evaluation & benchmarking (vs ground-truth dataset)

```bash
# score SAM 3 against engine/data/annotation_sessions GT (YOLO format)
python3 -m sam3_live.eval_accuracy --per-session 2 --iou 0.3

# head-to-head: SAM 3 vs fine-tuned yolo11s/yolo11n (OpenVINO) + latency
python3 -m sam3_live.benchmark_detectors --detectors sam3,yolo11s,yolo11n --sam-score 0.2

# prompt/threshold sweep (best found: "ball" @ conf 0.2 -> F1 0.815)
python3 -m sam3_live.prompt_sweep
```

Results land in `sam3_live/eval_out/` (JSON + per-image CSV). Verdict: the
fine-tuned **yolo11n (V8)** remains the production tracker (F1 0.885 at
17-31 ms, real-time on CPU); SAM 3 (F1 0.815 at ~28 s/frame on CPU) is kept
for offline auto-labeling. See `ROO_NOTES/DETECTOR_HEAD_TO_HEAD_COMPARISON.md`.

Keys: `q`/Esc quit. HUD shows processed FPS, display FPS, track count.
Per-frame CSV columns: `frame, ts, id, x1..y2, cx, cy, score,
mask_area, depth_m, xyz, inference_ms` for coverage/ID-switch analysis.

Checkpoint override: `--checkpoint /path/to/ckpt.pt --sam-version sam3.1`.

## Non-CUDA devices (NPU / iGPU / CPU) — verified Nov 2026 on a
## Core Ultra 9 185H (Meteor Lake: Arc iGPU + NPU)

- **NPU: not possible today.** No public SAM 3 → OpenVINO export exists,
  and an ~848M-param fp32 transformer exceeds what the Meteor Lake NPU
  can host anyway. Revisit if Meta or the community ships an OpenVINO/
  LiteRT export.
- **iGPU (`--sam-device xpu`): experimental.** PyTorch XPU wheels +
  our `_torch_shim.py` (redirects the repo's hardcoded `device="cuda"`
  / `.cuda()` / bf16 casts) make it loadable, but Meteor Lake Arc in
  fp32 is roughly CPU-class — expect ~5–10 s/frame, not live. Requires
  `pip install torch torchvision --index-url https://download.pytorch.org/whl/xpu`.
- **CPU (`--sam-device cpu`): WORKS for offline validation.** Verified
  end-to-end: model builds and propagates windows at ~10 s/frame
  (Ultra 9 185H). Not live, but good enough to validate tracking
  quality on short recorded sequences without the GPU box.
- The shim also patches one upstream bug (CPU connected-components
  crash on empty object batches) in `.third_party/sam3`.

## Design notes

- SAM 3 runs through the official `build_sam3_predictor(checkpoint_path=...)`
  with a **local checkpoint** — no HF login, no license acceptance, no
  network at runtime. The predictor's session API is window-based, so the
  live adapter buffers camera frames as PIL images and runs sliding-window
  sessions (`start_session` → one text `add_prompt` →
  `propagate_in_video` → `close_session`), re-feeding the previous
  window's final boxes as prompts so object IDs carry across windows
  (`--window`, default 48 frames).
- This dedup-free live path can emit duplicate/false-positive tracks —
  filtered by score threshold (`--score`) and expected ball count
  (`--balls N` keeps top-N).
- Depth is never used for tracking — SAM 3 is RGB-only. RealSense
  depth is only *looked up*: box-median gating in a working range
  (default 1–4 m) kills wall/furniture false positives; last-known
  depth bridges stereo dropouts on small fast balls; overlapping 2D
  boxes are audited via per-box median depth; mask centroids are
  deprojected to camera space for real apex/flight-time metrics.
- RealSense colour exposure: **auto by default** (changed 2026-11-12).
  Earlier versions locked the option to the raw value 2500, described here
  as "2.5 ms", and this README listed an `--auto-exposure` flag that the CLI
  never had, so the lock could not be turned off. On this project's D455 the
  raw value 2000 was measured to cut the colour stream to about 5 unique
  frames/s (see `tracking_lab/camera.py`), so 2500 was very likely far
  longer than 2.5 ms. Any RealSense result produced by `sam3_live` before
  this change may have been computed on repeated, smeared colour frames and
  should be re-checked. A manual lock is still available from Python via
  `RealSenseSource(auto_exposure=False, exposure_us=<raw value>)`.
- `.third_party/sam3/` and `models/` are git-ignored.

## Modules

- `sources.py` — `WebcamSource`, `RealSenseSource` (aligned depth + intrinsics)
- `trackers.py` — `Sam3Tracker` (official predictor, local ckpt, sliding windows), `LegacyColorTracker` (HSV baseline)
- `depth.py` — `DepthContext` gating/3D lift, overlap disambiguation
- `overlay.py` — boxes, per-ID trails, FPS HUD, interp between inferences
- `csv_logger.py` — `TrackCsvLogger`, `StabilityStats` (coverage, ID churn)
- `pipeline.py` — `LivePipeline` main loop
- `__main__.py` — CLI
