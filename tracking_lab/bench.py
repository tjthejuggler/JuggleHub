"""T6 timing benchmark: where do the 52.3 ms/frame go?

Loads frames of a recording, discards the first N for warm-up, times
PoseDetector.detect, BallDetector.detect and MotionDetector.detect
separately, prints mean and 95th percentile per stage. Read-only; no
constants touched, no results altered (§3 unaffected).

Usage:
  python3 -m tracking_lab.bench                 # cascade60, 300 frames
  python3 -m tracking_lab.bench --frames 500 --suffix box30
  TRACKING_BALL_MODEL=<416 xml> python3 -m tracking_lab.bench   # alt model
"""
from __future__ import annotations

import argparse
import os
import statistics
import sys
import time

import cv2

import numpy as np

from . import evaluate
from .detectors import BallDetector, MotionDetector, PoseDetector
from .perception import make_depth_fn
from .recording import RecordingSession

WARMUP = 20


def pct(values: list, p: float) -> float:
    if not values:
        return 0.0
    return float(np.percentile(values, p))


def bench(suffix: str, n_frames: int, async_pair: bool = False) -> int:
    rec = evaluate.find_rec(suffix)
    if rec is None:
        print(f"no recording matching *_{suffix}", file=sys.stderr)
        return 2
    sess = RecordingSession(rec, load_depth=False)
    total = min(n_frames, len(sess))
    ball = BallDetector()
    pose = PoseDetector()
    motion = MotionDetector()

    if async_pair:
        return _bench_async(sess, total, ball, pose)

    times = dict(ball=[], pose=[], motion=[])
    start_all = time.perf_counter()
    for i in range(total):
        fr = sess[i]
        t0 = time.perf_counter()
        pose.detect(fr.rgb)
        t1 = time.perf_counter()
        ball.detect(fr.rgb)
        t2 = time.perf_counter()
        motion.detect(fr.rgb)
        t3 = time.perf_counter()
        if i >= WARMUP:
            times["pose"].append((t1 - t0) * 1000)
            times["ball"].append((t2 - t1) * 1000)
            times["motion"].append((t3 - t2) * 1000)
    wall = (time.perf_counter() - start_all) * 1000 / total

    model_name = os.path.basename(
        os.environ.get("TRACKING_BALL_MODEL", "V8 default (640)"))
    print(f"\nBENCH {sess.name}: {total} frames ({WARMUP} warm-up "
          f"discarded), model: {model_name}")
    print(f"{'stage':<8}{'mean ms':>9}{'p50':>8}{'p95':>8}")
    measured_sum = 0.0
    for stage in ("pose", "ball", "motion"):
        v = times[stage]
        measured_sum += statistics.mean(v)
        print(f"{stage:<8}{statistics.mean(v):>9.1f}{statistics.median(v):>8.1f}"
              f"{pct(v, 95):>8.1f}")
    print(f"\nmeasured sum {measured_sum:.1f} ms | wall incl. I/O "
          f"{wall:.1f} ms/frame -> {1000 / wall:.0f} fps "
          f"(target < 33.3 ms)")
    return 0


def _bench_async(sess, total: int, ball, pose) -> int:
    """Overlap the ball and pose inferences of the SAME frame via
    start_async on two dedicated infer requests (probe only: postprocess
    excluded, results not scored, §3 unaffected)."""
    from .detectors import _letterbox

    b_req = ball.model.create_infer_request()
    p_req = pose.model.create_infer_request()

    def blob_of(frame):
        canvas, r, top, left = _letterbox(frame.rgb, 640)
        return cv2.dnn.blobFromImage(canvas, 1 / 255.0, swapRB=True)

    blob_of(sess[0])  # warm-up path
    b_req.infer({0: blob_of(sess[0])})
    p_req.infer({0: blob_of(sess[0])})

    times = []
    start_all = time.perf_counter()
    for i in range(total):
        fr = sess[i]
        t0 = time.perf_counter()
        b_blob = blob_of(fr)
        p_blob = blob_of(fr)
        t1 = time.perf_counter()
        b_req.start_async({0: b_blob})
        p_req.start_async({0: p_blob})
        b_req.wait()
        p_req.wait()
        t2 = time.perf_counter()
        if i >= WARMUP:
            times.append(dict(pre=(t1 - t0) * 1000,
                              infer=(t2 - t1) * 1000))
    wall = (time.perf_counter() - start_all) * 1000 / total
    pre = statistics.mean([t["pre"] for t in times])
    inf = statistics.mean([t["infer"] for t in times])
    inf95 = pct([t["infer"] for t in times], 95)
    print(f"\nBENCH-ASYNC {sess.name}: {total} frames — ball+pose overlapped")
    print(f"{'stage':<12}{'mean ms':>9}{'p95':>8}")
    print(f"{'preproc':<12}{pre:>9.1f}{'':>8}")
    print(f"{'infer pair':<12}{inf:>9.1f}{inf95:>8.1f}")
    print(f"\npre+infer {pre + inf:.1f} ms/frame (+ motion ~3.1 + postproc) "
          f"| wall incl. I/O {wall:.1f} ms/frame")
    print("reference: sequential sync pose+ball infer = 48.3 ms "
          "(pose 25.6 + ball 22.7)")
    return 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--suffix", default="cascade60")
    ap.add_argument("--frames", type=int, default=300)
    ap.add_argument("--async", dest="async_pair", action="store_true",
                    help="overlap ball+pose inference of the same frame "
                         "(probe; postprocess excluded)")
    a = ap.parse_args(argv)
    return bench(a.suffix, a.frames, a.async_pair)


if __name__ == "__main__":
    sys.exit(main())
