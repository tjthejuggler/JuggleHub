#!/usr/bin/env python3
"""SAM 3 prompt + confidence-threshold sweep.

Backbone features are computed once per image (set_image), then multiple text
prompts are scored on top (set_text_prompt) at a near-zero confidence
threshold so we can re-analyze precision/recall at any threshold offline.

Writes sam3_live/eval_out/prompt_sweep.json with per-prompt metrics at
multiple thresholds.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import time

import cv2

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, REPO)

from sam3_live.eval_accuracy import (DEFAULT_DATA_ROOT, Detection, build_sample,
                                     load_yolo_balls, match_detections)

PROMPTS = ["ball", "a ball", "tennis ball", "sports ball", "yellow ball",
           "glowing ball", "led ball"]
THRESHOLDS = [0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--per-session", type=int, default=2)
    ap.add_argument("--max-images", type=int, default=16)
    ap.add_argument("--iou", type=float, default=0.3)
    ap.add_argument("--prompts", nargs="*", default=PROMPTS)
    ap.add_argument("--device", default="cpu")
    ap.add_argument("--seed", type=int, default=0)
    args = ap.parse_args()

    from sam3_live._torch_shim import enable as enable_shim
    enable_shim(args.device)
    from sam3.model_builder import build_sam3_image_model
    from sam3.model.sam3_image_processor import Sam3Processor
    import PIL.Image

    ckpt = os.environ.get("SAM3_CHECKPOINT", os.path.join(REPO, "models", "sam3", "sam3.pt"))
    model = build_sam3_image_model(checkpoint_path=ckpt, load_from_HF=False,
                                   device=args.device, enable_segmentation=True).eval()
    proc = Sam3Processor(model, device=args.device, confidence_threshold=0.05)

    sample = build_sample(DEFAULT_DATA_ROOT, None, args.per_session, args.max_images, args.seed)
    print(f"Sweep: {len(args.prompts)} prompts x {len(sample)} images")

    # detections[prompt] = list of (session, image, [(box, score)])
    dets = {p: [] for p in args.prompts}
    gt_map = []
    t_bb_total = 0.0
    t_txt_total = 0
    n_txt = 0
    for i, (session, jpg) in enumerate(sample, 1):
        img = PIL.Image.open(jpg).convert("RGB")
        w, h = img.size
        gt = load_yolo_balls(jpg[:-4] + ".txt", w, h)
        gt_map.append((session, os.path.basename(jpg), gt))
        t0 = time.perf_counter()
        state = proc.set_image(img)
        t_bb = time.perf_counter() - t0
        t_bb_total += t_bb
        for p in args.prompts:
            t1 = time.perf_counter()
            s = proc.set_text_prompt(p, state)
            dt = time.perf_counter() - t1
            t_txt_total += dt
            n_txt += 1
            boxes = [tuple(b.tolist()) for b in s["boxes"]]
            scores = [float(x) for x in s["scores"]]
            dets[p].append(list(zip(boxes, scores)))
        print(f"[{i}/{len(sample)}] {os.path.basename(jpg)}: backbone {t_bb:.1f}s "
              f"text {sum(time.perf_counter()-t1 for _ in [0]):.1f}s", flush=True)
        # re-anchor state to avoid stale geometric prompts
        state = proc.reset_all_prompts(state)

    print(f"\nbackbone total {t_bb_total:.0f}s ({t_bb_total/len(sample):.1f}s/img), "
          f"text+grounding {t_txt_total:.1f}s avg ({t_txt_total/max(n_txt,1):.1f}s/prompt-img)")

    report = {}
    for p in args.prompts:
        best = None
        for thr in THRESHOLDS:
            tp = fp = fn = 0
            ious = []
            for (session, image, gt), dp in zip(gt_map, dets[p]):
                preds = [Detection(b, sc) for b, sc in dp if sc >= thr]
                t, f, n = match_detections(gt, preds, args.iou)
                tp += t; fp += f; fn += n
                ious += [pr.iou for pr in preds if pr.matched]
            prec = tp / (tp + fp) if tp + fp else 0.0
            rec = tp / (tp + fn) if tp + fn else 0.0
            f1 = 2 * prec * rec / (prec + rec) if prec + rec else 0.0
            report.setdefault(p, {})[str(thr)] = dict(
                tp=tp, fp=fp, fn=fn, precision=round(prec, 3), recall=round(rec, 3),
                f1=round(f1, 3), mean_iou=round(sum(ious) / len(ious), 3) if ious else 0.0)
            if best is None or f1 > best[1]:
                best = (thr, f1)
        print(f"prompt={p!r}: best F1={best[1]:.3f} @ thr={best[0]}")

    out = os.path.join(REPO, "sam3_live", "eval_out", "prompt_sweep.json")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w") as f:
        json.dump(dict(iou_thr=args.iou, prompts=args.prompts, report=report), f, indent=2)
    print(f"Wrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
