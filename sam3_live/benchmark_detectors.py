#!/usr/bin/env python3
"""Head-to-head detector benchmark: SAM 3 vs fine-tuned YOLO (OpenVINO) + latency.

Runs SAM 3 (image mode) and the fine-tuned YOLO11 models deployed in
engine/models/ on the SAME stratified ground-truth sample, records
accuracy (greedy IoU matching vs YOLO-format GT) and per-image latency.

YOLO OpenVINO models output [1,6,8400] = (x,y,w,h cxcywh, cls0 ball, cls1 ball_held)
in letterboxed 640x640 input space; we undo the letterbox to map to pixels.

Usage:
  python3 -m sam3_live.benchmark_detectors --per-session 2 --detectors sam3,yolo11s,yolo11n
"""

from __future__ import annotations

import argparse
import csv
import json
import os
import statistics
import sys
import time
from collections import defaultdict

import cv2
import numpy as np

from sam3_live.eval_accuracy import (BALL_CLASSES, DEFAULT_DATA_ROOT, ImageResult,
                                     build_sample, iou_xyxy, load_yolo_balls,
                                     match_detections)

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MODELS = {
    "yolo11s": os.path.join(REPO, "engine", "models", "yolo11s.xml"),
    "yolo11n": os.path.join(REPO, "engine", "models", "yolo11n.xml"),
}


class YoloOpenVino:
    """Ultralytics-exported YOLO OpenVINO model with letterbox pre/post."""

    def __init__(self, xml_path: str, conf: float = 0.25, iou_nms: float = 0.45):
        import openvino as ov
        self.core = ov.Core()
        self.model = self.core.compile_model(xml_path, "CPU")
        self.infer = self.model.create_infer_request()
        self.input_size = int(self.model.input(0).shape[-1])  # 640 or 416
        self.conf = conf
        self.iou_nms = iou_nms
        # warm-up (first inference compiles OpenVINO kernels; exclude from latency)
        self.detect(np.zeros((360, 640, 3), np.uint8))

    def detect(self, frame_bgr: np.ndarray) -> list:
        h, w = frame_bgr.shape[:2]
        r = min(self.input_size / h, self.input_size / w)
        nh, nw = round(h * r), round(w * r)
        resized = cv2.resize(frame_bgr, (nw, nh), interpolation=cv2.INTER_LINEAR)
        canvas = np.full((self.input_size, self.input_size, 3), 114, np.uint8)
        top, left = (self.input_size - nh) // 2, (self.input_size - nw) // 2
        canvas[top:top + nh, left:left + nw] = resized
        blob = cv2.dnn.blobFromImage(canvas, 1 / 255.0, swapRB=True)
        out = self.infer.infer({0: blob})[0]  # [1,6,8400]
        pred = np.asarray(out)[0]             # [6,8400]
        boxes = pred[:4].T                    # cxcywh in 640-space
        scores_all = pred[4:]                 # [2,8400]
        cls_ids = scores_all.argmax(axis=0)
        confs = scores_all.max(axis=0)

        keep = confs > self.conf
        boxes, confs, cls_ids = boxes[keep], confs[keep], cls_ids[keep]
        if len(boxes) == 0:
            return []
        # NMS per image (all classes together like ultralytics multi-label off)
        xyxy = np.stack([boxes[:, 0] - boxes[:, 2] / 2, boxes[:, 1] - boxes[:, 3] / 2,
                         boxes[:, 0] + boxes[:, 2] / 2, boxes[:, 1] + boxes[:, 3] / 2], 1)
        idxs = cv2.dnn.NMSBoxes(xyxy.tolist(), confs.tolist(), self.conf, self.iou_nms)
        results = []
        for i in np.array(idxs).reshape(-1):
            x1, y1, x2, y2 = xyxy[i]
            # undo letterbox
            x1, x2 = (x1 - left) / r, (x2 - left) / r
            y1, y2 = (y1 - top) / r, (y2 - top) / r
            results.append(((x1, y1, x2, y2), float(confs[i]), int(cls_ids[i])))
        return results


class Sam3ImageDetector:
    """SAM 3 image-mode detector with latency accounting."""

    def __init__(self, checkpoint: str, device: str, prompt: str, conf: float):
        from sam3_live._torch_shim import enable as enable_shim
        import torch
        if device == "auto":
            device = "cuda" if torch.cuda.is_available() else "cpu"
        if device != "cuda":
            enable_shim(device)
        from sam3.model_builder import build_sam3_image_model
        from sam3.model.sam3_image_processor import Sam3Processor
        self.model = build_sam3_image_model(
            checkpoint_path=checkpoint, load_from_HF=False, device=device,
            enable_segmentation=True).eval()
        self.proc = Sam3Processor(self.model, device=device, confidence_threshold=conf)
        self.prompt = prompt
        self.device = device
        # warm-up
        import PIL.Image
        dummy = PIL.Image.new("RGB", (640, 360), (128, 128, 128))
        s = self.proc.set_image(dummy)
        self.proc.set_text_prompt(self.prompt, s)

    def detect(self, frame_bgr: np.ndarray) -> list:
        import PIL.Image
        img = PIL.Image.fromarray(cv2.cvtColor(frame_bgr, cv2.COLOR_BGR2RGB))
        s = self.proc.set_image(img)
        s = self.proc.set_text_prompt(self.prompt, s)
        return [(tuple(b.tolist()), float(sc), 0)
                for b, sc in zip(s["boxes"], s["scores"])]


def evaluate_detector(name: str, detector, sample, iou_thr: float) -> tuple:
    results = []
    latencies = []
    for session, jpg in sample:
        frame = cv2.imread(jpg)
        h, w = frame.shape[:2]
        gt = load_yolo_balls(jpg[:-4] + ".txt", w, h)
        t0 = time.perf_counter()
        dets = detector.detect(frame)
        dt = time.perf_counter() - t0
        latencies.append(dt * 1000)
        preds = []
        for box, score, _cls in dets:
            # Keep ball-adjacent classes for YOLO (0=ball, 1=ball_held both count)
            from sam3_live.eval_accuracy import Detection
            preds.append(Detection(tuple(box), score))
        tp, fp, fn = match_detections(gt, preds, iou_thr)
        tp, fp, fn = int(tp), int(fp), int(fn)
        r = ImageResult(session, os.path.basename(jpg), len(gt), len(preds), tp, fp, fn,
                        [float(p.iou) for p in preds if p.matched],
                        [float(p.score) for p in preds])
        results.append(r)
    return results, latencies


def summarize(name, results, latencies) -> dict:
    tp = sum(r.tp for r in results); fp = sum(r.fp for r in results); fn = sum(r.fn for r in results)
    prec = tp / (tp + fp) if tp + fp else 0.0
    rec = tp / (tp + fn) if tp + fn else 0.0
    f1 = 2 * prec * rec / (prec + rec) if prec + rec else 0.0
    ious = [i for r in results for i in r.ious]
    return dict(name=name, images=len(results), tp=tp, fp=fp, fn=fn,
                precision=round(prec, 3), recall=round(rec, 3), f1=round(f1, 3),
                mean_iou=round(sum(ious) / len(ious), 3) if ious else 0.0,
                lat_ms_median=round(statistics.median(latencies), 1),
                lat_ms_mean=round(statistics.fmean(latencies), 1),
                lat_ms_p95=round(sorted(latencies)[int(len(latencies) * 0.95)], 1),
                fps_at_median=round(1000 / statistics.median(latencies), 1) if statistics.median(latencies) > 0 else 0)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="SAM3 vs fine-tuned YOLO benchmark")
    ap.add_argument("--per-session", type=int, default=2)
    ap.add_argument("--max-images", type=int, default=16)
    ap.add_argument("--iou", type=float, default=0.3)
    ap.add_argument("--detectors", default="sam3,yolo11s,yolo11n")
    ap.add_argument("--prompt", default="ball")
    ap.add_argument("--sam-score", type=float, default=0.5)
    ap.add_argument("--yolo-conf", type=float, default=0.25)
    ap.add_argument("--device", default="auto", choices=["auto", "cuda", "xpu", "cpu"])
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--checkpoint", default=os.environ.get(
        "SAM3_CHECKPOINT", os.path.join(REPO, "models", "sam3", "sam3.pt")))
    ap.add_argument("--out", default=os.path.join(REPO, "sam3_live", "eval_out", "benchmark.json"))
    args = ap.parse_args(argv)

    sample = build_sample(DEFAULT_DATA_ROOT, None, args.per_session, args.max_images, args.seed)
    print(f"Sample: {len(sample)} images x detectors: {args.detectors}")
    wanted = args.detectors.split(",")

    summaries = {}
    per_image_rows = []
    for name in wanted:
        if name == "sam3":
            print("\n== SAM 3 (image mode) ==")
            t0 = time.time()
            det = Sam3ImageDetector(args.checkpoint, args.device, args.prompt, args.sam_score)
            print(f"load+warm {time.time()-t0:.0f}s")
        elif name in MODELS:
            print(f"\n== {name} (OpenVINO) ==")
            det = YoloOpenVino(MODELS[name], conf=args.yolo_conf)
        elif name.startswith("ov:"):
            xml = name[3:]
            name = "ov:" + os.path.relpath(xml, REPO)
            print(f"\n== {name} (OpenVINO) ==")
            det = YoloOpenVino(xml, conf=args.yolo_conf)
        else:
            print(f"unknown detector {name}, skipping")
            continue
        results, lat = evaluate_detector(name, det, sample, args.iou)
        summaries[name] = summarize(name, results, lat)
        print(json.dumps(summaries[name], indent=2))
        for r in results:
            per_image_rows.append(dict(detector=name, session=r.session, image=r.image,
                                       gt=r.n_gt, pred=r.n_pred, tp=r.tp, fp=r.fp, fn=r.fn))
        del det

    # leaderboard
    print("\n===== LEADERBOARD (IoU>={}) =====".format(args.iou))
    print(f"{'detector':<10} {'P':>6} {'R':>6} {'F1':>6} {'mIoU':>6} "
          f"{'med ms':>8} {'p95 ms':>8} {'FPS':>7}")
    for name, s in summaries.items():
        print(f"{name:<10} {s['precision']:>6.3f} {s['recall']:>6.3f} {s['f1']:>6.3f} "
              f"{s['mean_iou']:>6.3f} {s['lat_ms_median']:>8.1f} {s['lat_ms_p95']:>8.1f} "
              f"{s['fps_at_median']:>7.1f}")

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, "w") as f:
        json.dump(dict(iou_thr=args.iou, sample_size=len(sample),
                       prompt=args.prompt, results=summaries), f, indent=2)
    with open(args.out.replace(".json", "_per_image.csv"), "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(per_image_rows[0].keys()))
        w.writeheader()
        w.writerows(per_image_rows)
    print(f"\nWrote {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
