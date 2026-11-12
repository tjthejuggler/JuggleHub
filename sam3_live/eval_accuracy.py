#!/usr/bin/env python3
"""Offline SAM 3 accuracy evaluation against the JuggleHub ground-truth dataset.

Ground truth lives in engine/data/annotation_sessions/<session>/images/ as
YOLO-format sidecars: "class cx cy w h" normalized, classes from
engine/data/classes.txt: 0=led_on 1=led_off 2=dropped_ball 3=hand.
Ball targets = classes {0, 1, 2}; hands (3) are ignored for ball matching.

Uses the SAM 3 image-mode API (build_sam3_image_model + Sam3Processor) which
is much cheaper per frame than the video session path and matches how a
detector would be scored. Runs on CPU via the _torch_shim (~10 s/image).

Usage:
  python3 -m sam3_live.eval_accuracy --per-session 2
  python3 -m sam3_live.eval_accuracy --sessions "V10rs455*" --max-images 10
"""

from __future__ import annotations

import argparse
import csv
import glob
import json
import os
import random
import sys
import time
from dataclasses import dataclass, field

DEFAULT_DATA_ROOT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "engine", "data")
BALL_CLASSES = {0, 1, 2}  # led_on, led_off, dropped_ball; 3=hand is ignored


@dataclass
class Detection:
    box: tuple  # xyxy pixels
    score: float
    matched: bool = False
    iou: float = 0.0


@dataclass
class ImageResult:
    session: str
    image: str
    n_gt: int
    n_pred: int
    tp: int = 0
    fp: int = 0
    fn: int = 0
    ious: list = field(default_factory=list)
    pred_scores: list = field(default_factory=list)


def iou_xyxy(a, b) -> float:
    ix1, iy1 = max(a[0], b[0]), max(a[1], b[1])
    ix2, iy2 = min(a[2], b[2]), min(a[3], b[3])
    iw, ih = max(0.0, ix2 - ix1), max(0.0, iy2 - iy1)
    inter = iw * ih
    if inter <= 0:
        return 0.0
    area_a = max(0.0, a[2] - a[0]) * max(0.0, a[3] - a[1])
    area_b = max(0.0, b[2] - b[0]) * max(0.0, b[3] - b[1])
    return inter / (area_a + area_b - inter)


def load_yolo_balls(txt_path: str, w: int, h: int) -> list:
    """Return GT ball boxes (classes 0-2) as pixel xyxy."""
    boxes = []
    if not os.path.exists(txt_path):
        return boxes
    with open(txt_path) as f:
        for line in f:
            parts = line.split()
            if len(parts) != 5:
                continue
            cls = int(float(parts[0]))
            if cls not in BALL_CLASSES:
                continue
            cx, cy, bw, bh = (float(v) for v in parts[1:])
            cx, cy, bw, bh = cx * w, cy * h, bw * w, bh * h  # normalized -> pixels
            boxes.append((cx - bw / 2, cy - bh / 2, cx + bw / 2, cy + bh / 2))
    return boxes


def match_detections(gt_boxes: list, preds: list, iou_thr: float) -> tuple:
    """Greedy IoU matching: each GT matched by best-scoring available pred."""
    pairs = []
    for gi, g in enumerate(gt_boxes):
        for pi, p in enumerate(preds):
            iou = iou_xyxy(g, p.box)
            if iou >= iou_thr:
                pairs.append((p.score, iou, gi, pi))
    pairs.sort(key=lambda t: -t[0])
    used_g, used_p = set(), set()
    for score, iou, gi, pi in pairs:
        if gi in used_g or pi in used_p:
            continue
        used_g.add(gi)
        used_p.add(pi)
        preds[pi].matched = True
        preds[pi].iou = iou
    tp = len(used_g)
    return tp, sum(1 for p in preds if not p.matched), len(gt_boxes) - tp


def build_sample(data_root: str, session_patterns: list, per_session: int,
                 max_images: int, seed: int) -> list:
    sessions_dir = os.path.join(data_root, "annotation_sessions")
    sessions = sorted(os.listdir(sessions_dir)) if os.path.isdir(sessions_dir) else []
    if session_patterns:
        import fnmatch
        sessions = [s for s in sessions
                    if any(fnmatch.fnmatch(s, pat) for pat in session_patterns)]
    rng = random.Random(seed)
    sample = []  # (session, jpg_path)
    for s in sessions:
        img_dir = os.path.join(sessions_dir, s, "images")
        if not os.path.isdir(img_dir):
            continue
        jpgs = sorted(glob.glob(os.path.join(img_dir, "*.jpg")))
        # keep only images with at least one ball annotation
        candidates = [j for j in jpgs
                      if os.path.exists(j[:-4] + ".txt")
                      and any(int(float(l.split()[0])) in BALL_CLASSES
                              for l in open(j[:-4] + ".txt") if len(l.split()) == 5)]
        if not candidates:
            continue
        rng.shuffle(candidates)
        for j in candidates[:per_session]:
            sample.append((s, j))
            if max_images and len(sample) >= max_images:
                return sample
    return sample


def load_image_model(checkpoint: str, device: str, version: str):
    from sam3_live._torch_shim import enable as enable_shim
    import torch
    if device == "auto":
        device = "cuda" if torch.cuda.is_available() else "cpu"
    if device != "cuda":
        enable_shim(device)
    from sam3.model_builder import build_sam3_image_model
    model = build_sam3_image_model(
        checkpoint_path=checkpoint,
        load_from_HF=False,
        device=device,
        enable_segmentation=True,
    )
    model.eval()
    return model, device


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="SAM 3 accuracy vs JuggleHub GT boxes")
    ap.add_argument("--data-root", default=DEFAULT_DATA_ROOT)
    ap.add_argument("--sessions", nargs="*", default=None,
                    help="fnmatch patterns for session dir names (default: all)")
    ap.add_argument("--per-session", type=int, default=2)
    ap.add_argument("--max-images", type=int, default=0, help="0 = unlimited")
    ap.add_argument("--prompt", default="ball",
                    help="text prompt; NOTE 'juggling ball' scores 0.0 with SAM 3, "
                         "use 'ball' / 'tennis ball' / 'a ball'")
    ap.add_argument("--score", type=float, default=0.5)
    ap.add_argument("--iou", type=float, default=0.5)
    ap.add_argument("--device", default="auto", choices=["auto", "cuda", "xpu", "cpu"])
    ap.add_argument("--checkpoint", default=os.environ.get(
        "SAM3_CHECKPOINT",
        os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                     "models", "sam3", "sam3.pt")))
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--out-dir", default=None,
                    help="write per-image CSV + summary JSON here (default: sam3_live/eval_out)")
    args = ap.parse_args(argv)

    sample = build_sample(args.data_root, args.sessions or None,
                          args.per_session, args.max_images, args.seed)
    if not sample:
        print("No annotated images found (check --data-root / --sessions).", file=sys.stderr)
        return 2
    print(f"Evaluating {len(sample)} images across "
          f"{len(set(s for s, _ in sample))} sessions | prompt={args.prompt!r} "
          f"score>={args.score} IoU>={args.iou} device={args.device}")

    t0 = time.time()
    model, device = load_image_model(args.checkpoint, args.device, "sam3")
    print(f"Model loaded on {device} in {time.time()-t0:.1f}s")

    from sam3.model.sam3_image_processor import Sam3Processor
    import PIL.Image
    proc = Sam3Processor(model, device=device, confidence_threshold=args.score)

    results: list[ImageResult] = []
    for i, (session, jpg) in enumerate(sample, 1):
        img = PIL.Image.open(jpg).convert("RGB")
        w, h = img.size
        gt = load_yolo_balls(jpg[:-4] + ".txt", w, h)
        t1 = time.time()
        state = proc.set_image(img)
        state = proc.set_text_prompt(args.prompt, state)
        dt = time.time() - t1
        preds = [Detection(tuple(b.tolist()), float(s))
                 for b, s in zip(state["boxes"], state["scores"])]
        tp, fp, fn = match_detections(gt, preds, args.iou)
        r = ImageResult(session, os.path.basename(jpg), len(gt), len(preds), tp, fp, fn,
                        [p.iou for p in preds if p.matched],
                        [p.score for p in preds])
        results.append(r)
        print(f"[{i}/{len(sample)}] {session}/{r.image}: "
              f"gt={r.n_gt} pred={r.n_pred} TP={r.tp} FP={r.fp} FN={r.fn} "
              f"({dt:.1f}s)")

    # aggregate
    tot_tp = sum(r.tp for r in results)
    tot_fp = sum(r.fp for r in results)
    tot_fn = sum(r.fn for r in results)
    prec = tot_tp / (tot_tp + tot_fp) if tot_tp + tot_fp else 0.0
    rec = tot_tp / (tot_tp + tot_fn) if tot_tp + tot_fn else 0.0
    f1 = 2 * prec * rec / (prec + rec) if prec + rec else 0.0
    all_ious = [i for r in results for i in r.ious]
    mean_iou = sum(all_ious) / len(all_ious) if all_ious else 0.0
    images_with_all = sum(1 for r in results if r.fn == 0 and r.fp == 0)

    print("\n===== SAM 3 accuracy summary =====")
    print(f"images:        {len(results)}")
    print(f"GT balls:      {sum(r.n_gt for r in results)}")
    print(f"predictions:   {sum(r.n_pred for r in results)}")
    print(f"TP={tot_tp}  FP={tot_fp}  FN={tot_fn}")
    print(f"precision:     {prec:.3f}")
    print(f"recall:        {rec:.3f}")
    print(f"F1:            {f1:.3f}")
    print(f"mean IoU:      {mean_iou:.3f}  (matched pairs: {len(all_ious)})")
    print(f"perfect imgs:  {images_with_all}/{len(results)}")
    print("---- per session ----")
    for s in sorted(set(r.session for r in results)):
        rs = [r for r in results if r.session == s]
        tp = sum(r.tp for r in rs); fp = sum(r.fp for r in rs); fn = sum(r.fn for r in rs)
        p = tp / (tp + fp) if tp + fp else 0.0
        rc = tp / (tp + fn) if tp + fn else 0.0
        print(f"  {s}: imgs={len(rs)} P={p:.2f} R={rc:.2f} TP={tp} FP={fp} FN={fn}")

    # persist results
    out_dir = args.out_dir or os.path.join(os.path.dirname(os.path.abspath(__file__)), "eval_out")
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, "per_image.csv"), "w", newline="") as f:
        wcsv = csv.writer(f)
        wcsv.writerow(["session", "image", "n_gt", "n_pred", "tp", "fp", "fn",
                       "mean_iou_matched", "pred_scores"])
        for r in results:
            mi = sum(r.ious) / len(r.ious) if r.ious else 0.0
            wcsv.writerow([r.session, r.image, r.n_gt, r.n_pred, r.tp, r.fp, r.fn,
                           f"{mi:.3f}", " ".join(f"{s:.2f}" for s in r.pred_scores)])
    summary = dict(prompt=args.prompt, score_thr=args.score, iou_thr=args.iou,
                   device=device, images=len(results),
                   tp=tot_tp, fp=tot_fp, fn=tot_fn,
                   precision=round(prec, 4), recall=round(rec, 4), f1=round(f1, 4),
                   mean_iou=round(mean_iou, 4))
    with open(os.path.join(out_dir, "summary.json"), "w") as f:
        json.dump(summary, f, indent=2)
    print(f"\nWrote {out_dir}/per_image.csv and summary.json")
    return 0


if __name__ == "__main__":
    sys.exit(main())
