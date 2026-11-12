# ADR: Detector selection for live ball tracking — fine-tuned YOLO over SAM 3 (2026-11-10)

> Persisted as file because codebase-memory MCP was disconnected.
> Re-emit via `manage_adr` when the graph is back online.

## Context
Evaluated SAM 3 (zero-shot, image+video modes, CPU via torch shim), the project's
fine-tuned YOLO11n (V8) / YOLO11s (V9.1) OpenVINO models, and the legacy HSV
tracker on a 16-image stratified sample of the JuggleHub ground-truth dataset
(29 GT ball boxes, 8 sessions: daylight/mixed/low-light/LED/color-aug).
Full data: ROO_NOTES/DETECTOR_HEAD_TO_HEAD_COMPARISON.md

## Decision
Keep the fine-tuned **YOLO11n OpenVINO (V8) as the production live tracker**;
YOLO11s (V9.1) as accuracy reference. SAM 3 is NOT used for live tracking but is
kept in `sam3_live/` for offline auto-labeling / pseudo-labeling of future
dataset versions (V11+). Live SAM 3 defaults tuned to prompt "ball", conf 0.2.

## Evidence (IoU>=0.3)
- yolo11n: P=1.000 R=0.793 F1=0.885, 17-31 ms median (real-time, CPU)
- yolo11s: P=1.000 R=0.759 F1=0.863, 44-75 ms (near real-time)
- SAM 3 "ball"@0.2: P=0.880 R=0.759 F1=0.815, ~28.5 s/frame (860x over budget)
- SAM 3 boxes loose (mIoU 0.50 vs YOLO 0.79-0.89); fails strict IoU 0.5 scoring
- legacy HSV: 1.2 ms but 3/29 on multi-color sample (calibration-bound)
- "juggling ball" prompt scores 0.0 in SAM 3's text vocabulary
- LED balls recoverable by SAM 3 only at low thresholds ("led ball"@0.1 → R 0.55)

## Consequences
- Engine default stays yolo11n; no engine changes needed.
- SAM 3 becomes an offline labeling accelerator (V11+ datasets).
- Revisit only if a CUDA GPU box is added (SAM3 video ~60 ms/frame there).
- Benchmarks reproducible: `python3 -m sam3_live.benchmark_detectors`,
  results in sam3_live/eval_out/, reports in ROO_NOTES/.
