"""Offline pipeline runner: recording -> detector -> tracker -> report + video.

Usage:
  python3 -m tracking_lab --rec engine/data/1_raw_recordings/continuous_2025-11-07_11-11-23
  python3 -m tracking_lab --rec <dir> --balls 3 --render out.mp4
"""
from __future__ import annotations

import argparse
import json
import os
import sys
import time

import cv2
import numpy as np

from .detectors import BallDetector, MotionDetector, PoseDetector
from .perception import fuse as _fuse
from .perception import make_depth_fn, perceive
from .recording import RecordingSession
from .tracker import JuggleTracker, TrackingConfig, _color_hist

PALETTE = [(60, 200, 60), (60, 160, 255), (0, 200, 255), (255, 120, 120),
           (200, 120, 255), (120, 255, 255), (255, 255, 120)]


def run(rec_path: str, balls: int = 3, render_path: str | None = None,
        conf: float = 0.25, use_depth: bool = True, max_frames: int = 0,
        verbose: bool = True, use_light_mask: bool = False) -> dict:
    sess = RecordingSession(rec_path, load_depth=use_depth)
    n = min(len(sess), max_frames) if max_frames else len(sess)
    det = BallDetector(conf=conf)
    pose = PoseDetector()
    motion = MotionDetector()
    tracker = JuggleTracker(TrackingConfig(fps=sess.fps, n_balls=balls))

    # light mask is OFF by default: bright/cream balls crossing the top of
    # frame match its signature; the tracker's static-bin suppressor handles
    # the lights instead
    light_mask = _light_mask(sess) if use_light_mask else None

    writer = None
    if render_path:
        os.makedirs(os.path.dirname(render_path) or ".", exist_ok=True)
        h, w = sess[0].rgb.shape[:2]
        writer = cv2.VideoWriter(render_path, cv2.VideoWriter_fourcc(*"mp4v"),
                                 sess.fps, (w, h))

    t_start = time.time()
    for i in range(n):
        fr = sess[i]
        t0 = time.perf_counter()
        # pose + ball + MOG2 fusion (shared with the detection cache so the
        # cached replay in tracking_lab.evaluate sees identical inputs)
        wrists, face_pt, dets_raw, n_persons = perceive(fr.rgb, det, pose, motion)
        # NOTE: face-region suppression was tried and REVERTED (2026-11-11):
        # balls thrown across the face got suppressed -> massive under-count.
        # The face-phantom instead needs track-level handling (see backlog).
        if light_mask is not None:
            dets_raw = [(b, s, c) for b, s, c in dets_raw
                        if light_mask[int((b[1]+b[3])/2), int((b[0]+b[2])/2)] == 0]
        t_det = (time.perf_counter() - t0) * 1000

        dets = []
        for box, score, cls in dets_raw:
            hist = _color_hist(fr.rgb, box)
            dets.append((box, score, cls, hist))

        # depth is aligned to rgb; make_depth_fn handles resolution mismatch
        depth_fn = make_depth_fn(fr.depth, fr.rgb.shape[:2])

        events = tracker.step(fr.idx, dets, wrists, rgb=fr.rgb, depth_at=depth_fn,
                              face_pt=face_pt)
        if verbose:
            note = f" ev={events}" if events else ""
            print(f"[{i+1}/{n}] det={len(dets)} pose={n_persons} "
                  f"tracks={len(tracker.tracks)} {t_det:.0f}ms{note}", flush=True)

        if writer is not None:
            writer.write(_draw(fr, tracker, dets_raw, wrists, events))

    if writer:
        writer.release()
    # dump per-frame track data next to the render for the annotation player
    if render_path:
        def _clean(o):
            if isinstance(o, dict):
                return {k: _clean(v) for k, v in o.items()}
            if isinstance(o, (list, tuple)):
                return [_clean(v) for v in o]
            if isinstance(o, (np.floating, np.integer)):
                return o.item()
            return o
        tpath = render_path.replace(".mp4", "_tracks.json")
        with open(tpath, "w") as f:
            json.dump(dict(fps=sess.fps,
                           frames=n,
                           per_frame=_clean(tracker.per_frame),
                           events=_clean(tracker.events)), f)
    summary = tracker.summary()
    summary["session"] = sess.name
    summary["frames_processed"] = n
    summary["wall_s"] = round(time.time() - t_start, 1)
    summary["fps_offline"] = round(n / max(time.time() - t_start, 0.01), 1)
    summary["engine_events"] = len(sess.log_events())
    return summary


def _light_mask(sess, warmup: int = 40) -> np.ndarray | None:
    """Persistent bright/low-saturation mask (ceiling lights etc.) built from
    the per-pixel MINIMUM brightness over a warmup window: a pixel that is
    ever dark (a ball passing) is excluded, only always-bright pixels stay."""
    s_acc, v_acc = [], []
    step = max(1, len(sess) // warmup)
    for i in range(0, len(sess), step):
        hsv = cv2.cvtColor(sess[i].rgb, cv2.COLOR_BGR2HSV)
        s_acc.append(hsv[..., 1])
        v_acc.append(hsv[..., 2])
    if not s_acc:
        return None
    s_min = np.min(np.stack(s_acc), axis=0)
    v_min = np.min(np.stack(v_acc), axis=0)
    mask = np.zeros(v_min.shape, np.uint8)
    mask[(v_min > 170) & (s_min < 40)] = 255
    # only keep the top 40% of frame: mounted lights, lamps
    mask[int(mask.shape[0] * 0.4):] = 0
    mask = cv2.dilate(mask, np.ones((35, 35), np.uint8))
    return mask


def _draw(fr, tracker, dets_raw, wrists, events):
    img = fr.rgb.copy()
    for t in tracker.tracks:
        c = PALETTE[t.track_id % len(PALETTE)]
        x, y = int(t.x), int(t.y)
        cv2.circle(img, (x, y), 12, c, 2)
        cv2.putText(img, f"#{t.track_id} {t.state[:4]}", (x + 14, y - 8),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.45, c, 1)
        if len(t.history) > 2:
            pts = [(int(px), int(py)) for _, px, py, _ in t.history[-30:]]
            for p in pts:
                cv2.circle(img, p, 1, c, -1)
    for box, score, cls in dets_raw:
        cv2.rectangle(img, (int(box[0]), int(box[1])), (int(box[2]), int(box[3])),
                      (120, 120, 120), 1)
    for hand, (x, y) in wrists.items():
        cv2.circle(img, (int(x), int(y)), 8, (0, 0, 255), 2)
    for e in events:
        cv2.putText(img, e["event"], (20, 40), cv2.FONT_HERSHEY_SIMPLEX, 1.0,
                    (0, 255, 255), 2)
    return img


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Offline juggling tracker run")
    ap.add_argument("--rec", required=True, help="recording dir (1_raw_recordings/<name>)")
    ap.add_argument("--balls", type=int, default=3)
    ap.add_argument("--conf", type=float, default=0.25)
    ap.add_argument("--max-frames", type=int, default=0)
    ap.add_argument("--render", default=None, help="output mp4 path")
    ap.add_argument("--quiet", action="store_true")
    ap.add_argument("--summary-json", default=None)
    ap.add_argument("--expected", type=int, default=0,
                    help="expected catch count (from verify_wizard) -> PASS/FAIL")
    args = ap.parse_args(argv)
    s = run(args.rec, balls=args.balls, render_path=args.render,
            conf=args.conf, max_frames=args.max_frames, verbose=not args.quiet)
    if args.expected:
        got = s["catches"]
        err = got - args.expected
        pct = 100.0 * got / max(args.expected, 1)
        verdict = "PASS" if abs(err) <= max(2, int(0.05 * args.expected)) else "FAIL"
        s["expected_catches"] = args.expected
        s["catch_error"] = err
        s["catch_accuracy_pct"] = round(pct, 1)
        s["verdict"] = verdict
        print(f"\n=== {verdict}: expected {args.expected} catches, detected {got} "
              f"({err:+d}, {pct:.1f}% accuracy) ===")
    print(json.dumps(s, indent=2))
    if args.summary_json:
        with open(args.summary_json, "w") as f:
            json.dump(s, f, indent=2)
    return 0


if __name__ == "__main__":
    sys.exit(main())
