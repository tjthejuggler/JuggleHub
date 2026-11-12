"""Live catch counter: watch the camera, count catches while you juggle.

Same pipeline that is scored offline (perceive() + JuggleTracker), same
constants — nothing is tuned here. The overlay shows the live CATCH/THROW
counts (total + per hand), the last few events, the processing fps and the
camera status lines (exposure mode, repeated-frame warning).

Delivery notes (fixed after the first live test counted 10 of 50 catches):
  * a capture thread keeps only the NEWEST camera frame — at ~19 fps of
    inference we cannot consume 30 fps, and reading queued frames made the
    tracker see ever-staler frames with overflow gaps right at catches;
  * the tracker's dt comes from the MEASURED frame interval. Processing
    every-other-ish frame with dt fixed at 1/30 inflated ball speeds ~60%
    and the catch speed gate denied real catches.

Keys:  r = reset counts, q or Esc = quit, space = pause.
--record <label> also saves the session as a normal recording under
tracking_lab/recordings/ (scorable afterwards with mark_catches/evaluate).

Usage:
  python3 -m tracking_lab.live                          # RealSense D455
  python3 -m tracking_lab.live --source webcam
  python3 -m tracking_lab.live --record cascade50live
  python3 -m tracking_lab.live --set held_radius_px=85   # config overrides
  python3 -m tracking_lab.live --selftest               # headless wiring test
"""
from __future__ import annotations

import argparse
import datetime
import json
import os
import queue
import sys
import threading
import time

import cv2
import numpy as np

from .detectors import BallDetector, MotionDetector, PoseDetector
from .perception import make_depth_fn, perceive
from .tracker import JuggleTracker, TrackingConfig, _color_hist
from .wizard import Cam, OUT_ROOT


def process_frame(tracker: JuggleTracker, rgb, depth, det, pose, motion,
                  idx: int) -> tuple[list[dict], dict]:
    """One live frame through the exact offline pipeline -> (events, wrists)."""
    wrists, face_pt, dets_raw, _n = perceive(rgb, det, pose, motion)
    depth_fn = make_depth_fn(depth, rgb.shape[:2]) if depth is not None else None
    dets = [(box, score, cls, _color_hist(rgb, box)) for box, score, cls in dets_raw]
    events = tracker.step(idx, dets, wrists, rgb, depth_fn, face_pt)
    return events, wrists


class LatestFrame:
    """Capture thread that keeps ONLY the newest camera frame (queue of 1).
    Slow consumers sample the live stream instead of falling behind."""

    def __init__(self, cam: Cam):
        self.cam = cam
        self.q: queue.Queue = queue.Queue(maxsize=1)
        self.stop = threading.Event()
        self.t = threading.Thread(target=self._loop, daemon=True)
        self.t.start()

    def _loop(self):
        while not self.stop.is_set():
            color, depth = self.cam.read()
            if color is None:
                time.sleep(0.01)
                continue
            try:
                self.q.get_nowait()      # drop stale frame
            except queue.Empty:
                pass
            self.q.put((color, depth))

    def get(self, timeout: float = 5.0):
        return self.q.get(timeout=timeout)

    def close(self):
        self.stop.set()
        self.t.join(timeout=2.0)


class LiveRecorder:
    """Writes the processed frames as a normal recording session (same layout
    the wizard produces), so a live run can be scored offline afterwards."""

    def __init__(self, label: str, has_depth: bool):
        ts = datetime.datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
        self.out = os.path.join(OUT_ROOT, f"{ts}_{label}")
        self.rgb_dir = os.path.join(self.out, "no_boxes")
        os.makedirs(self.rgb_dir, exist_ok=True)
        self.depth_dir = os.path.join(self.out, "depth")
        self.has_depth = has_depth
        if has_depth:
            os.makedirs(self.depth_dir, exist_ok=True)
        self.label = label
        self.n = 0
        self.stamps: list[float] = []

    def add(self, color, depth, t0: float):
        cv2.imwrite(os.path.join(self.rgb_dir, f"frame_{self.n}.jpg"), color,
                    [cv2.IMWRITE_JPEG_QUALITY, 95])
        if depth is not None:
            cv2.imwrite(os.path.join(self.depth_dir,
                                     f"frame_{self.n}_depth.png"), depth)
        self.stamps.append(round(time.time() - t0, 4))
        self.n += 1

    def finish(self, fps_effective: float, cam: Cam | None = None):
        meta = dict(label=self.label, source=(cam.source if cam else "unknown"),
                    width=color_w, height=color_h,
                    fps=round(fps_effective, 1), frames=self.n,
                    duration_s=round(self.stamps[-1] if self.stamps else 0.0, 2),
                    live=True)
        if cam is not None:
            meta["rgb_exposure"] = cam.exposure
        meta["stamps"] = self.stamps
        with open(os.path.join(self.out, "meta.json"), "w") as f:
            json.dump(meta, f, indent=2)
        print(f"saved live recording: {self.out} ({self.n} frames @ "
              f"{fps_effective:.1f} fps)")
        return self.out


color_w, color_h = 640, 480     # set from the first frame at runtime


def draw(img, tracker: JuggleTracker, fps: float, status: list,
         paused: bool, wrists: dict) -> np.ndarray:
    frame = img.copy()
    catches = [e for e in tracker.events if e["event"] == "CATCH"]
    throws = [e for e in tracker.events if e["event"] == "THROW"]
    left = sum(1 for e in catches if e.get("hand") == "left_wrist")
    right = sum(1 for e in catches if e.get("hand") == "right_wrist")

    for t in tracker.tracks:
        if getattr(t, "pending", False):
            continue
        col = {"HELD": (0, 200, 0), "IN_FLIGHT": (0, 180, 255),
               "COAST": (128, 128, 128)}.get(t.state, (255, 255, 255))
        x, y = int(t.x), int(t.y)
        cv2.circle(frame, (x, y), 12, col, 2)
        cv2.putText(frame, f"{t.track_id}", (x + 14, y - 8),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.45, col, 1)
    for hand, (wx, wy) in wrists.items():
        cv2.circle(frame, (int(wx), int(wy)), 8, (255, 0, 255), 2)

    cv2.putText(frame, str(len(catches)), (24, 96),
                cv2.FONT_HERSHEY_SIMPLEX, 2.6, (0, 255, 0), 6)
    wrist_txt = f"wrists: {'L' if 'left_wrist' in wrists else '-'}" \
                f"{'+R' if 'right_wrist' in wrists else ''}"
    if len(wrists) < 2:
        wrist_txt += "  (pose missing a wrist!)"
    lines = [f"catches {len(catches)}   L {left}  R {right}",
             f"throws {len(throws)}   C-T {len(catches) - len(throws)}",
             f"{fps:.1f} fps   {wrist_txt}"]
    if paused:
        lines.insert(0, "PAUSED (space to resume)")
    for i, txt in enumerate(lines):
        cv2.putText(frame, txt, (24, 128 + 26 * i),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.6, (255, 255, 255), 2)
    for i, (txt, _scale, col) in enumerate(status):
        cv2.putText(frame, txt, (24, frame.shape[0] - 14 - 22 * i),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.55, col, 2)
    for i, ev in enumerate(reversed(tracker.events[-5:])):
        side = {"left_wrist": "L", "right_wrist": "R"}.get(ev.get("hand"), "?")
        txt = f"f{ev['frame']} {ev['event']} {side}#{ev['track']}"
        cv2.putText(frame, txt, (frame.shape[1] - 200,
                                 frame.shape[0] - 14 - 20 * i),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.5, (200, 200, 200), 1)
    return frame


def main(argv=None) -> int:
    global color_w, color_h
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--source", default="realsense",
                    choices=["realsense", "webcam"])
    ap.add_argument("--record", default=None, metavar="LABEL",
                    help="also save the session as a recording under "
                         "tracking_lab/recordings/")
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
    grab = LatestFrame(cam)
    tracker = JuggleTracker(cfg)
    rec = LiveRecorder(a.record, cam.source == "realsense") if a.record else None
    idx = 0
    fps, last_t, last_show = 0.0, time.perf_counter(), time.perf_counter()
    intervals: list[float] = []
    paused, wrists = False, {}
    t0 = time.time()
    print("live counter running: r=reset  q=quit  space=pause")
    try:
        while True:
            color, depth = grab.get()
            color_h, color_w = color.shape[:2]
            if not paused:
                intervals.append(time.perf_counter() - last_t)
                if len(intervals) > 60:      # keep the interval window fresh
                    intervals.pop(0)
                if len(intervals) >= 20:
                    # honest dt: the tracker must know how much real time
                    # actually passed between the frames it sees
                    med = sorted(intervals)[len(intervals) // 2]
                    cfg.fps = max(min(1.0 / max(med, 1e-3), 60.0), 5.0)
                _ev, wrists = process_frame(tracker, color, depth, det, pose,
                                            motion, idx)
                if rec is not None:
                    rec.add(color, depth, t0)
                idx += 1
            now = time.perf_counter()
            fps = fps * 0.9 + (1.0 / max(now - last_t, 1e-6)) * 0.1
            last_t = now
            if now - last_show > 0.03:
                cv2.imshow("live", draw(color, tracker, fps,
                                        cam.status_lines(), paused, wrists))
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
    except KeyboardInterrupt:
        pass
    finally:
        grab.close()
        cam.close()
        cv2.destroyAllWindows()
        if rec is not None:
            rec.finish(idx / max(time.time() - t0, 0.01), cam)
    catches = [e for e in tracker.events if e["event"] == "CATCH"]
    print(f"session total: {len(catches)} catches "
          f"(tracker ran at {cfg.fps:.1f} fps)")
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
