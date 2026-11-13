"""Live catch counter + test harness: measure how good live counting is.

Same pipeline that is scored offline (perceive() + JuggleTracker), same
constants — nothing is tuned here. Every take is RECORDED and LABELLED, so
failures can be replayed offline frame-by-frame afterwards.

Session protocol (all on-screen):
  * the overlay shows the TARGET catch count (--target, default 50);
  * press  s  to START a take: a fresh tracker begins counting and the
    camera frames are written as a recording;
  * juggle the pattern, doing exactly the target number of catches;
  * press  s  to STOP: the take ends, the tracker's count is frozen and
    the terminal asks for your TRUE count — type it and press Enter;
  * meta.json for the take stores BOTH numbers (tracker vs truth) plus
    per-frame telemetry (fps believed, wrists, detections, catch count),
    so the offline analysis knows exactly what to match.
Other keys:  r = reset counts mid-take (drop recovery),  q = quit.

Speed (v5): pose runs in a worker thread parallel to the ball net and
recording writes are async, so the loop feeds the tracker ~18-20 fps
instead of ~11. The tracker's dt comes from the measured interval between
PROCESSED frames (an earlier bug measured only the queue poll and
believed fps=60 -> predictions covered a quarter of the real ball
displacement -> 8 of 50 catches). Frame-count constants are scaled to the
real fps so gates mean the same WALL TIME as at 30 fps offline.

Usage:
  python3 -m tracking_lab.live --target 50            # RealSense, 50 catches
  python3 -m tracking_lab.live --source webcam
  python3 -m tracking_lab.live --set held_radius_px=85  # config overrides
  python3 -m tracking_lab.live --selftest             # headless wiring test
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
from .perception import fuse, make_depth_fn
from .proxcounter import ProxCounter, make_intrinsics, prox_samples
from .tracker import JuggleTracker, TrackingConfig, _color_hist
from .wizard import Cam, OUT_ROOT

color_w, color_h = 640, 480     # set from the first frame at runtime

# frame-count config fields, with their 30 fps design values: live scales
# them by real_fps/30 so every gate means the same wall time as offline
FRAME_FIELDS = {
    "held_dwell": 2, "min_catch_gap": 4, "throw_dwell": 3,
    "min_flight_frames": 2, "coast_frames": 30, "held_max_occluded": 10,
    "face_stall_frames": 20, "min_hits": 3,
}

POSE_EVERY = 2   # run the pose net every n-th tracked frame, hold wrists
                 # between (measured on live50 2026-11-13: 21 -> 27 catches
                 # on the same take — held wrists beat per-frame jitter at
                 # ~16 fps, and it halves pose cost)


def scale_cfg(cfg: TrackingConfig, real_fps: float) -> None:
    """Scale frame-count fields to the real processing rate (in place)."""
    s = max(min(real_fps / 30.0, 1.0), 0.2)
    for name, base in FRAME_FIELDS.items():
        setattr(cfg, name, max(1, round(base * s)))


_last_wrists: dict = {}
_last_face = None
_fx_fy: tuple | None = None     # color intrinsics, from the first frame


def process_frame(tracker: JuggleTracker, rgb, depth, det, pose, motion,
                  idx: int) -> tuple[list[dict], dict, int, dict]:
    """One live frame: ball net every frame; pose net every POSE_EVERY-th
    frame (wrists held in between) -> (events, wrists, n_detections,
    prox_samples)."""
    global _last_wrists, _last_face, _fx_fy
    if idx % POSE_EVERY == 0:
        persons = pose.detect(rgb)
        _last_wrists, _last_face = {}, None
        for p in persons:
            for name in ("left_wrist", "right_wrist"):
                if name in p["kpts"]:
                    _last_wrists[name] = (p["kpts"][name][0],
                                          p["kpts"][name][1])
            if _last_face is None and "nose" in p["kpts"]:
                _last_face = (p["kpts"]["nose"][0], p["kpts"]["nose"][1])
    wrists, face_pt = _last_wrists, _last_face
    ball_dets = det.detect(rgb)
    mot_dets = motion.detect(rgb)
    dets_raw = fuse(ball_dets, mot_dets)
    depth_fn = make_depth_fn(depth, rgb.shape[:2]) if depth is not None else None
    dets = [(box, score, cls, _color_hist(rgb, box))
            for box, score, cls in dets_raw]
    events = tracker.step(idx, dets, wrists, rgb, depth_fn, face_pt)
    if _fx_fy is None:
        _fx_fy = make_intrinsics(rgb.shape[1], rgb.shape[0])
    prox = prox_samples(dets_raw, depth, rgb.shape[:2], wrists,
                        rgb.shape[1], rgb.shape[0], *_fx_fy)
    return events, wrists, len(dets_raw), prox


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


class AsyncWriter:
    """Writes recording frames off the tracking thread (imwrite of jpeg+png
    cost ~15 ms/frame — enough to halve the tracking rate)."""

    def __init__(self):
        self.q: queue.Queue = queue.Queue(maxsize=64)
        self.stop = threading.Event()
        self.t = threading.Thread(target=self._loop, daemon=True)
        self.t.start()

    def _loop(self):
        while not self.stop.is_set() or not self.q.empty():
            try:
                item = self.q.get(timeout=0.2)
            except queue.Empty:
                continue
            if item is None:
                break
            path, img, params = item
            cv2.imwrite(path, img, params or [])

    def add(self, path, img, params=None):
        try:
            self.q.put_nowait((path, img, params))
        except queue.Full:      # never block tracking for disk
            pass

    def close(self):
        self.stop.set()
        self.t.join(timeout=10.0)


def n_catches(tracker: JuggleTracker) -> int:
    return sum(1 for e in tracker.events if e["event"] == "CATCH")


def draw(img, tracker: JuggleTracker, fps: float, status: list,
         target: int, running: bool, wrists: dict,
         last_result: str | None, believed_fps: float,
         prox: ProxCounter | None = None) -> np.ndarray:
    frame = img.copy()
    catches = n_catches(tracker)
    throws = sum(1 for e in tracker.events if e["event"] == "THROW")
    left = sum(1 for e in tracker.events
               if e["event"] == "CATCH" and e.get("hand") == "left_wrist")
    right = sum(1 for e in tracker.events
                if e["event"] == "CATCH" and e.get("hand") == "right_wrist")

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

    pcount = prox.count if prox is not None else catches
    pl, pr = (prox.side_counts() if prox is not None else (left, right))
    big_txt = str(target) if not running else str(pcount)
    big_col = (0, 255, 255) if not running else \
        ((0, 255, 0) if pcount >= target else (0, 200, 255))
    cv2.putText(frame, big_txt, (24, 96), cv2.FONT_HERSHEY_SIMPLEX, 2.6,
                big_col, 6)
    wrist_txt = f"wrists: {'L' if 'left_wrist' in wrists else '-'}" \
                f"{'+R' if 'right_wrist' in wrists else ''}"
    if len(wrists) < 2:
        wrist_txt += "  (pose missing a wrist!)"
    lines = [f"PROX {pcount} / target {target}   L {pl}  R {pr}",
             f"tracker {catches} (L {left} R {right})  throws {throws}",
             f"{fps:.1f} fps (tracker dt {believed_fps:.1f} fps)  {wrist_txt}",
             "press S to START the take" if not running
             else "recording... press S to STOP at the target"]
    if last_result:
        lines.insert(0, last_result)
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


class Take:
    """One recorded, labelled take: frames (async-written), telemetry,
    stamps; finished with tracker/truth counts in meta.json."""

    def __init__(self, label: str, has_depth: bool, writer: AsyncWriter):
        ts = datetime.datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
        self.out = os.path.join(OUT_ROOT, f"{ts}_{label}")
        self.rgb_dir = os.path.join(self.out, "no_boxes")
        os.makedirs(self.rgb_dir, exist_ok=True)
        self.depth_dir = os.path.join(self.out, "depth")
        if has_depth:
            os.makedirs(self.depth_dir, exist_ok=True)
        self.label, self.has_depth, self.writer = label, has_depth, writer
        self.n = 0
        self.stamps: list[float] = []
        self.telemetry: list[dict] = []

    def add(self, color, depth, t0: float):
        self.writer.add(os.path.join(self.rgb_dir, f"frame_{self.n}.jpg"),
                        color, [cv2.IMWRITE_JPEG_QUALITY, 95])
        if depth is not None:
            self.writer.add(
                os.path.join(self.depth_dir, f"frame_{self.n}_depth.png"),
                depth, [])
        self.stamps.append(round(time.time() - t0, 4))
        self.n += 1

    def finish(self, meta_extra: dict, cam: Cam | None = None,
               fps_effective: float = 0.0, events: list | None = None):
        meta = dict(label=self.label, source=(cam.source if cam else "unknown"),
                    width=color_w, height=color_h,
                    fps=round(fps_effective, 1), frames=self.n,
                    duration_s=round(self.stamps[-1] if self.stamps else 0.0, 2),
                    live_take=True)
        if cam is not None:
            meta["rgb_exposure"] = cam.exposure
            meta.update(cam.guard.meta())
        meta["stamps"] = self.stamps
        meta.update(meta_extra)
        if events is not None:
            meta["tracker_events"] = events
        with open(os.path.join(self.out, "telemetry.json"), "w") as f:
            json.dump(self.telemetry, f)
        with open(os.path.join(self.out, "meta.json"), "w") as f:
            json.dump(meta, f, indent=2)
        return self.out


def ask_true_count(default: int) -> int:
    """Ask the user for the real catch count of the take they just did."""
    try:
        raw = input(f"\nHow many catches did you JUST do? "
                    f"[tracker said {default}; Enter = {default}]: ").strip()
        return int(raw) if raw else default
    except (ValueError, EOFError):
        print("could not read a number — using the tracker count")
        return default


def main(argv=None) -> int:
    global color_w, color_h
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--source", default="realsense",
                    choices=["realsense", "webcam"])
    ap.add_argument("--target", type=int, default=50,
                    help="catches you will juggle per take (shown on screen)")
    ap.add_argument("--label", default="live", help="take label prefix")
    ap.add_argument("--set", action="append", default=[], metavar="NAME=VALUE",
                    help="override one TrackingConfig field (as in evaluate)")
    ap.add_argument("--selftest", action="store_true",
                    help="headless: run the live frame path on a recording")
    a = ap.parse_args(argv)

    base_cfg = TrackingConfig()
    for item in a.set:
        name, sep, raw = item.partition("=")
        if not sep or not hasattr(base_cfg, name):
            ap.error(f"--set {item}: expected NAME=VALUE with NAME a "
                     f"TrackingConfig field")
        cur = getattr(base_cfg, name)
        try:
            val = (raw.lower() in ("1", "true", "yes") if isinstance(cur, bool)
                   else type(cur)(raw))
        except ValueError:
            ap.error(f"--set {item}: cannot read {raw!r} as "
                     f"{type(cur).__name__}")
        setattr(base_cfg, name, val)
        print(f"OVERRIDE {name} = {val!r} (default {cur!r})")

    if a.selftest:
        return _selftest(base_cfg)

    det = BallDetector()
    pose = PoseDetector()
    motion = MotionDetector()
    cam = Cam(a.source)
    grab = LatestFrame(cam)
    writer = AsyncWriter()
    cfg_work = TrackingConfig(**{f.name: getattr(base_cfg, f.name)
                                 for f in base_cfg.__dataclass_fields__.values()})
    tracker = JuggleTracker(cfg_work)
    prox: ProxCounter | None = None      # 3D proximity counter (per take)
    running, take, t_take0 = False, None, 0.0
    intervals: list[float] = []
    last_proc: float | None = None   # stamp of the PREVIOUS PROCESSED frame
    idx = 0
    fps, last_show = 0.0, time.perf_counter()
    last_result = None
    wrists: dict = {}
    results: list[dict] = []
    take_no = 0
    print(f"live harness: target {a.target} catches per take. "
          "S=start/stop take, r=reset, q=quit.")
    try:
        while True:
            color, depth = grab.get()
            color_h, color_w = color.shape[:2]
            if running:
                # dt between consecutive PROCESSED frames (the old version
                # measured only the queue poll and believed fps=60 while
                # the loop ran ~11-15 -> predictions covered a fraction of
                # the real displacement -> 8 of 50 catches)
                stamp = time.perf_counter()
                if last_proc is not None:
                    intervals.append(stamp - last_proc)
                last_proc = stamp
                if len(intervals) > 60:
                    intervals.pop(0)
                if len(intervals) >= 10:
                    med = sorted(intervals)[len(intervals) // 2]
                    cfg_work.fps = max(min(1.0 / max(med, 1e-3), 30.0), 5.0)
                    scale_cfg(cfg_work, cfg_work.fps)
                _ev, wrists, n_det, prox_s = process_frame(
                    tracker, color, depth, det, pose, motion, idx)
                if prox is not None:
                    prox.update(time.time() - t_take0, prox_s)
                take.add(color, depth, t_take0)
                take.telemetry.append(dict(
                    i=idx, fps=round(cfg_work.fps, 2), w=len(wrists),
                    d=n_det, c=n_catches(tracker),
                    p=prox.count if prox is not None else None))
                idx += 1
            else:
                wrists = {}   # idle: no tracking, no stale wrist dots
                last_proc = None
            now = time.perf_counter()
            loop_dt = now - last_show
            if 1e-3 < loop_dt < 1.0:
                fps = fps * 0.9 + (1.0 / loop_dt) * 0.1
            if now - last_show > 0.03:
                cv2.imshow("live", draw(color, tracker, fps,
                                        cam.status_lines(), a.target,
                                        running, wrists, last_result,
                                        cfg_work.fps, prox))
                last_show = now
            key = cv2.waitKey(1) & 0xFF
            if key in (ord("q"), 27):
                break
            if key == ord("s"):
                if not running:
                    take_no += 1
                    cfg_work = TrackingConfig(**{
                        f.name: getattr(base_cfg, f.name)
                        for f in base_cfg.__dataclass_fields__.values()})
                    tracker = JuggleTracker(cfg_work)
                    prox = ProxCounter() if cam.source == "realsense" else None
                    take = Take(f"{a.label}{a.target}_t{take_no}",
                                cam.source == "realsense", writer)
                    t_take0 = time.time()
                    intervals, idx = [], 0
                    last_proc = None
                    running, last_result = True, None
                    cam.guard.reset()
                    print(f"take {take_no} STARTED (target {a.target})")
                else:
                    running = False
                    trk = n_catches(tracker)
                    pcount = prox.count if prox is not None else trk
                    true_n = ask_true_count(pcount)
                    eff_fps = take.n / max(time.time() - t_take0, 0.01)
                    meta_extra = dict(
                        target=a.target, tracker_catch_count=trk,
                        tracker_throw_count=sum(
                            1 for e in tracker.events
                            if e["event"] == "THROW"),
                        user_count=true_n)
                    if prox is not None:
                        meta_extra.update(
                            prox_catch_count=prox.count,
                            prox_events=[dict(t=round(e["t"], 3),
                                              hand=e["hand"], d=e["d"])
                                         for e in prox.events])
                    path = take.finish(
                        meta_extra, cam, eff_fps,
                        events=[dict(frame=e["frame"], event=e["event"],
                                     hand=e.get("hand"), track=e["track"])
                                for e in tracker.events])
                    results.append(dict(take=take_no, true=true_n, trk=trk,
                                        prox=pcount, path=path))
                    last_result = (f"take {take_no}: TRUE {true_n} vs "
                                   f"PROX {pcount} (tracker {trk})")
                    print(f"take {take_no} saved: {path}\n"
                          f"  TRUE {true_n} vs PROX {pcount} "
                          f"({pcount - true_n:+d})"
                          f"   TRACKER {trk} ({trk - true_n:+d})")
            if key == ord("r") and running:
                tracker = JuggleTracker(cfg_work)
                if prox is not None:
                    prox = ProxCounter()
                idx = 0
                print("counts reset (mid-take)")
    except KeyboardInterrupt:
        pass
    finally:
        grab.close()
        cam.close()
        cv2.destroyAllWindows()
        writer.close()      # flush pending frame writes
    if results:
        print("\n=== live harness session summary ===")
        for r in results:
            print(f"take {r['take']}: true {r['true']}  prox {r['prox']} "
                  f"({r['prox'] - r['true']:+d})  tracker {r['trk']} "
                  f"({r['trk'] - r['true']:+d})  {r['path']}")
    else:
        print("no completed takes (press s to start/stop a take)")
    return 0


def _selftest(cfg: TrackingConfig) -> int:
    """Headless: the live frame path (threaded pose, scaled constants) over
    a recording's raw frames. Verifies wiring only — scores come from
    evaluate/event_score, not here."""
    from . import evaluate
    from .recording import RecordingSession

    rec = evaluate.find_rec("box30")
    if rec is None:
        print("no box30 recording for selftest", file=sys.stderr)
        return 2
    sess = RecordingSession(rec, load_depth=True)
    det, pose, motion = BallDetector(), PoseDetector(), MotionDetector()
    tracker = JuggleTracker(cfg)
    prox = ProxCounter()
    n = min(120, len(sess))
    for i in range(n):
        fr = sess[i]
        _ev, _w, _nd, prox_s = process_frame(tracker, fr.rgb, fr.depth,
                                             det, pose, motion, i)
        prox.update(i / 30.0, prox_s)
    catches = n_catches(tracker)
    print(f"selftest: {n} frames -> {catches} catches, "
          f"prox {prox.count} (L{prox.side_counts()[0]} "
          f"R{prox.side_counts()[1]}), "
          f"{len(tracker.events)} events, tracks {len(tracker.tracks)}")
    assert catches > 0, "no catches in 120 frames of box30 — wiring broken"
    print("selftest OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
