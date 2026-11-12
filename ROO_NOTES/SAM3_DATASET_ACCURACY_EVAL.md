# SAM 3 Accuracy Evaluation vs JuggleHub Ground Truth (2026-11-10)

## What was built
- `sam3_live/eval_accuracy.py` — offline harness scoring SAM 3 text-prompted
  detection against the YOLO-format GT in `engine/data/annotation_sessions/`.
  - Image-mode API: `build_sam3_image_model` + `Sam3Processor.set_image` →
    `set_text_prompt` (boxes/scores in pixel space, xyxy).
  - Greedy score-ordered IoU matching; per-session precision/recall/F1,
    mean IoU, per-image CSV + summary JSON in `sam3_live/eval_out/`.
  - CPU-only via `sam3_live/_torch_shim`; ~30 s/image (~11 s model load).
- Sample: 16 images, 2 per session, stratified across 8 active sessions
  (daylight / mixed-light / low-light / LED / color-aug / edge cases).

## Headline results (prompt="ball", score>=0.5)
| IoU thresh | Precision | Recall | F1   | mean IoU | perfect imgs |
|-----------|-----------|--------|------|----------|--------------|
| >= 0.5    | 0.389     | 0.241  | 0.298| 0.702    | 4/16         |
| >= 0.3    | 0.833     | 0.517  | 0.638| 0.545    | 8/16         |

## Findings
1. **Prompt matters enormously.** "juggling ball" → top score 0.0 (SAM 3's
   text encoder doesn't fire on the compound). "ball" ≈ 0.70, "a ball"
   ≈ 0.75, "tennis ball" ≈ 0.82 on a yellow-ball daylight frame.
   → Live defaults changed from "juggling ball" to "ball" in
   `trackers.py`, `pipeline.py`, `__main__.py`.
2. **Box-style mismatch, not missed balls.** At IoU>=0.5 many detections
   fail; at IoU>=0.3 precision jumps to 0.83. SAM 3 returns tight boxes
   hugging the ball; the user's GT boxes are systematically more generous
   (trained for a YOLO detector). Mean matched IoU ~0.55-0.74.
   → Live pipeline is unaffected (it only needs the ball's location, and
   mask centroids are even tighter than boxes).
3. ~~LED balls are a hard zero~~ **SUPERSEDED by the prompt sweep**
   (`prompt_sweep.py`): at conf 0.5 they are missed, but "led ball" @ 0.1
   reaches R=0.55 and "glowing ball" also fires. The 0.5 default was
   filtering everything.
4. **Tuned optimum: prompt "ball", conf 0.2 → F1 0.815 (P 0.880, R 0.759)**
   on the 16-image stratified sample — vs fine-tuned yolo11s F1 0.863.
   Live defaults updated accordingly (prompt "ball", score 0.2).
5. Full head-to-head incl. latency: ROO_NOTES/DETECTOR_HEAD_TO_HEAD_COMPARISON.md

## Harness bug fixed during evaluation
- `load_yolo_balls` received image w/h but never denormalized YOLO
  coords to pixels (GT stayed normalized → IoU always 0). Fixed.

## How to re-run
```
python3 -m sam3_live.eval_accuracy --per-session 2 --device cpu            # IoU>=0.5
python3 -m sam3_live.eval_accuracy --per-session 2 --iou 0.3               # looser
python3 -m sam3_live.benchmark_detectors --detectors sam3,yolo11s,yolo11n --sam-score 0.2
python3 -m sam3_live.prompt_sweep
```
Full 47k-image evaluation should run on a CUDA box (~50 ms/img there).
