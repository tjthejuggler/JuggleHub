"""Live catch counter: watch the camera, count catches while you juggle.

Same pipeline that is scored offline (perceive() + JuggleTracker), same
constants — nothing is tuned here. The overlay shows the live CATCH/THROW
counts (total + per hand), the last few events, the processing fps and the
camera status lines (exposure mode, repeated-frame warning).

Keys:  r = reset counts (drops the pattern / picks up balls), q or Esc = quit.

Usage:
  python3 -m tracking_lab.live                      # RealSense D455
  python3 -m tracking_lab.live --source webcam      # webcam 0
  python3 -m tracking_lab.live --set held_radius_px=85   # config overrides
  python3 -m tracking_lab.live --selftest           # headless wiring test

The whole loop runs at ~19 fps on this machine (51 ms/frame of inference),
slower than the 30 fps camera. Counts stay correct — the tracker works on
whatever frames arrive — but the preview can lag behind reality by up to
about a second during fast patterns.
"""
from __future__ import annotations

import argparse
import sys
import time

import cv2
import numpy as np

from .detectors import BallDetector, MotionDetector, PoseDetector
from .perception import make_depth_fn, perceive
from .tracker import JuggleTracker, TrackingConfig, _color_hist
from .wizard import Cam


def process_frame(tracker: JuggleTracker, rgb, depth, det, pose, motion,
                  idx: int) -> list[dict]:
    """One live frame through the exact offline pipeline -> events."""
    wrists, face_pt, dets_raw, _n = perceive(rgb, det, pose, motion)
    depth_fn = make_depth_fn(depth, rgb.shape[:2]) if depth is not None else None
    dets = [(box, score, cls, _color_hist(rgb, box)) for box, score, cls in dets_raw]
    return tracker.step(idx, dets, wrists, rgb, depth_fn, face_pt)


def draw(img, tracker: JuggleTracker, events: list[dict], fps: float,
         status: list, paused: bool) -> np.ndarray:
    frame = img.copy()
    catches = [e for e in tracker.events if e["event"] == "CATCH"]
    throws = [e for e in tracker.events if e["event"] == "THROW"]
    left = sum(1 for e in catches if e.get("hand") == "left_wrist")
    right = sum(1 for e in catches if e.get("hand") == "right_wrist")

    # tracks: box + state colour
    for t in tracker.tracks:
        if getattr(t, "pending", False):
            continue
        col = {"HELD": (0, 200, 0), "IN_FLIGHT": (0, 180, 255),
               "COAST": (128, 128, 128)}.get(t.state, (255, 255, 255))
        x, y = int(t.x), int(t.y)
        cv2.circle(frame, (x, y), 12, col, 2)
        cv2.putText(frame, f"{t.track_id}", (x + 14, y - 8),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.45, col, 1)

    big = str(len(catches))
    cv2.putText(frame, big, (24, 96), cv2.FONT_HERSHEY_SIMPLEX, 2.6,
                (0, 255, 0), 6)
    lines = [f"catches {len(catches)}   L {left}  R {right}",
             f"throws {len(throws)}   C-T {len(catches) - len(throws)}",
             f"{fps:.1f} fps"]
    if paused:
        lines.insert(0, "PAUSED (space to resume)")
    for i, txt in enumerate(lines):
        cv2.putText(frame, txt, (24, 128 + 26 * i),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.6, (255, 255, 255), 2)
    for i, (txt, _scale, col) in enumerate(status):
        cv2.putText(frame, txt, (24, frame.shape[0] - 14 - 22 * i),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.55, col, 2)
    # last events, bottom right
    for i, ev in enumerate(reversed(tracker.events[-5:])):
        side = {"left_wrist": "L", "right_wrist": "R"}.get(ev.get("hand"), "?")
        txt = f"f{ev['frame']} {ev['event']} {side}#{ev['track']}"
        cv2.putText(frame, txt, (frame.shape[1] - 200,
                                 frame.shape[0] - 14 - 20 * i),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.5, (200, 200, 200), 1)
    return frame


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--source", default="realsense",
                    choices=["realsense", "webcam"])
    ap.add_argument("--set", action="append", default=[], metavar="NAME=VALUE",
                    help="override one TrackingConfig field (as in evaluate)")
    ap.add_argument("--selftest", action="store_true",
                    help="headless: run the live frame path on a recording")
    a = ap.parse_args(argv)

    cfg = TrackingConfig()
    for item in a.set:
        name, sep, raw = item.partition("=")
        if not sep or not hasattr(cfg, name):
            ap.error(f"--set {item}: expected NAME=VALUE with NAME a "
                     f"TrackingConfig field")
        cur = getattr(cfg, name)
        try:
            val = (raw.lower() in ("1", "true", "yes") if isinstance(cur, bool)
                   else type(cur)(raw))
        except ValueError:
            ap.error(f"--set {item}: cannot read {raw!r} as "
                     f"{type(cur).__name__}")
        setattr(cfg, name, val)
        print(f"OVERRIDE {name} = {val!r} (default {cur!r})")

    if a.selftest:
        return _selftest(cfg)

    det = BallDetector()
    pose = PoseDetector()
    motion = MotionDetector()
    cam = Cam(a.source)
    tracker = JuggleTracker(cfg)
    idx = 0
    fps, last_t, last_show = 0.0, time.perf_counter(), time.perf_counter()
    paused = False
    print("live counter running: r=reset  q=quit  space=pause")
    try:
        while True:
            color, depth = cam.read()
            if color is None:
                print("camera read failed twice — stopping", file=sys.stderr)
                break
            if not paused:
                process_frame(tracker, color, depth, det, pose, motion, idx)
                idx += 1
            now = time.perf_counter()
            fps = fps * 0.9 + (1.0 / max(now - last_t, 1e-6)) * 0.1
            last_t = now
            if now - last_show > 0.03:
                cv2.imshow("live", draw(color, tracker, tracker.events, fps,
                                        cam.status_lines(), paused))
                last_show = now
            key = cv2.waitKey(1) & 0xFF
            if key in (ord("q"), 27):
                break
            if key == ord("r"):
                tracker = JuggleTracker(cfg)
                idx = 0
                print("counts reset")
            if key == ord(" "):
                paused = not paused
    finally:
        cam.close()
        cv2.destroyAllWindows()
    catches = [e for e in tracker.events if e["event"] == "CATCH"]
    print(f"session total: {len(catches)} catches")
    return 0


def _selftest(cfg: TrackingConfig) -> int:
    """Headless: the exact live frame path over a cached recording's raw
    frames (real detectors, no camera, no GUI). Verifies wiring only —
    scores come from evaluate/event_score, not here."""
    from . import evaluate
    from .recording import RecordingSession

    rec = evaluate.find_rec("box30")
    if rec is None:
        print("no box30 recording for selftest", file=sys.stderr)
        return 2
    sess = RecordingSession(rec, load_depth=True)
    det, pose, motion = BallDetector(), PoseDetector(), MotionDetector()
    tracker = JuggleTracker(cfg)
    n = min(120, len(sess))
    for i in range(n):
        fr = sess[i]
        process_frame(tracker, fr.rgb, fr.depth, det, pose, motion, i)
    catches = [e for e in tracker.events if e["event"] == "CATCH"]
    print(f"selftest: {n} frames -> {len(catches)} catches, "
          f"{len(tracker.events)} events, tracks {len(tracker.tracks)}")
    assert len(catches) > 0, "no catches in 120 frames of box30 — wiring broken"
    print("selftest OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
