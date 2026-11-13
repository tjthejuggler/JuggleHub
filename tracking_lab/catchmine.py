"""First-principles catch mining: do catches have a 3D proximity signature?

No tracker state machine. For every cached detection and wrist keypoint we
deproject into camera-frame 3D using depth + a pinhole model, then look at
the time series of ball-to-hand 3D distance per hand. A catch should be a
local minimum of that series (ball arrives at hand, velocity collapses).

Usage:
  python3 -m tracking_lab.catchmine --rec tracking_lab/recordings/<take>
"""
from __future__ import annotations

import argparse
import json
import os
import sys

import numpy as np

from . import detcache
from .perception import make_depth_fn
from .recording import RecordingSession

# D435 color sensor HFOV at 640x480 is ~69.4 deg -> fx = (W/2)/tan(HFOV/2)
HFOV_DEG = 69.4


def _intrinsics(w: int, h: int):
    fx = (w / 2.0) / np.tan(np.radians(HFOV_DEG) / 2.0)
    return fx, fx * h / w  # fy from square-pixel assumption


def deproject(px, py, z, w, h, fx, fy):
    """Pixel + depth(m) -> camera-frame metres (x right, y down, z fwd)."""
    x = (px - w / 2.0) * z / fx
    y = (py - h / 2.0) * z / fy
    return np.array([x, y, z])


def analyse(rec_path: str, prox_thr=0.22, min_gap_s=0.30, arrive_from=0.35,
            verbose=True):
    cache = detcache.load(rec_path)
    meta_path = os.path.join(rec_path, "meta.json")
    meta = {}
    if os.path.exists(meta_path):
        with open(meta_path) as f:
            meta = json.load(f)

    # Real fps from frame stamps (meta 'fps' includes typing time on live takes)
    stamps = meta.get("stamps") or []
    n = len(cache["frames"])
    if len(stamps) >= n and n > 1:
        d = np.diff(stamps[:n])
        d = d[(d > 0.01) & (d < 1.0)]
        fps = 1.0 / float(np.median(d))
    else:
        fps = cache["fps"]
    dt = 1.0 / fps

    sess = RecordingSession(rec_path, load_depth=True)
    h, w = None, None

    fx, fy = None, None
    # per-frame records
    balls = []      # list per frame: [(x,y,z,score,cls)]
    hands = []      # list per frame: {hand: (x,y,z)}
    for k, f in enumerate(cache["frames"]):
        fr = sess[k]
        if fx is None:
            h, w = fr.rgb.shape[:2]
            fx, fy = _intrinsics(w, h)
        depth_fn = make_depth_fn(fr.depth, fr.rgb.shape[:2])
        bl = []
        for box, score, cls, _hist, z in f["dets"]:
            cx, cy = (box[0] + box[2]) / 2, (box[1] + box[3]) / 2
            if z is None and depth_fn is not None:
                z = depth_fn(cx, cy)
            if z is not None:
                bl.append((*deproject(cx, cy, z, w, h, fx, fy), score, cls))
        hd = {}
        for name, (px, py) in (f["wrists"] or {}).items():
            z = depth_fn(px, py) if depth_fn else None
            if z is not None:
                hd[name] = deproject(px, py, z, w, h, fx, fy)
        balls.append(bl)
        hands.append(hd)

    # TWO series per hand:
    #   all    - nearest ball of any class (sharp ~8cm minima at capture)
    #   flight - nearest class-0 (in-flight) ball; excludes the ball already
    #            held in that hand, so it shows a true far->near ARRIVAL.
    series = {"left_wrist": [], "right_wrist": []}
    flight = {"left_wrist": [], "right_wrist": []}
    for bl, hd in zip(balls, hands):
        for hand in series:
            if hand in hd and bl:
                series[hand].append(
                    min(float(np.linalg.norm(b[:3] - hd[hand])) for b in bl))
            else:
                series[hand].append(np.nan)
            fl = [b for b in bl if b[4] == 0]
            if hand in hd and fl:
                flight[hand].append(
                    min(float(np.linalg.norm(b[:3] - hd[hand])) for b in fl))
            else:
                flight[hand].append(np.nan)

    events = []
    truth = meta.get("user_count") or meta.get("required_catches")
    lookback = max(2, int(0.5 / dt))
    for hand, s in series.items():
        arr = np.array(s, dtype=float)
        fl_arr = np.array(flight[hand], dtype=float)
        for i in range(1, len(arr) - 1):
            if np.isnan(arr[i]):
                continue
            if arr[i] <= prox_thr and arr[i] <= arr[i - 1] and arr[i] < arr[i + 1]:
                # CONFIRM: a class-0 ball arrived near this hand recently
                if arrive_from > 0:
                    w = fl_arr[max(0, i - lookback):i + 1]
                    w = w[~np.isnan(w)]
                    if not len(w) or np.nanmin(w) > arrive_from:
                        continue
                t = i * dt
                # suppress neighbours closer than min_gap_s
                if events and t - events[-1][0] < min_gap_s and events[-1][2] == hand:
                    if arr[i] < events[-1][1]:
                        events[-1] = (t, arr[i], hand, i)
                    continue
                events.append((t, arr[i], hand, i))
    events.sort()
    merged = []
    for ev in events:
        if merged and ev[0] - merged[-1][0] < min_gap_s * 0.5:
            if ev[1] < merged[-1][1]:
                merged[-1] = ev
        else:
            merged.append(ev)

    # A cascade alternates hands: two same-hand catches with no opposite-hand
    # event between them, closer than alt_window, are a double-dip -> keep
    # the deeper minimum only.
    alt_window = 0.60
    changed = True
    while changed:
        changed = False
        for a in range(len(merged) - 1):
            b = a + 1
            if (merged[b][0] - merged[a][0] < alt_window
                    and merged[b][2] == merged[a][2]):
                drop = a if merged[a][1] >= merged[b][1] else b
                merged.pop(drop)
                changed = True
                break

    if verbose:
        both = np.array(series["left_wrist"] + series["right_wrist"])
        both = both[~np.isnan(both)]
        print(f"take {meta.get('label', rec_path)}: frames={n} fps={fps:.1f}")
        print(f"  wrist-3D coverage: "
              f"L={sum(1 for x in series['left_wrist'] if not np.isnan(x))}/{n} "
              f"R={sum(1 for x in series['right_wrist'] if not np.isnan(x))}/{n}")
        print(f"  prox dist percentiles (m): "
              f"p10={np.percentile(both, 10):.2f} p25={np.percentile(both, 25):.2f} "
              f"p50={np.percentile(both, 50):.2f}")
        lh = sum(1 for e in merged if e[2] == "left_wrist")
        rh = sum(1 for e in merged if e[2] == "right_wrist")
        msg = f"  proximity-minima catches: {len(merged)} (L={lh} R={rh})"
        if truth:
            msg += f"  truth={truth}  err={len(merged) - truth:+d}"
        print(msg)
        gaps = np.diff([e[0] for e in merged]) if len(merged) > 1 else []
        if len(gaps):
            print(f"  inter-event gap s: median={np.median(gaps):.2f} "
                  f"min={min(gaps):.2f} max={max(gaps):.2f}")
    return dict(fps=fps, n_frames=n, events=merged, series=series,
                truth=truth, meta=meta)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--rec", required=True, nargs="+")
    ap.add_argument("--prox", type=float, default=0.22,
                    help="ball-hand 3D distance threshold (m)")
    ap.add_argument("--gap", type=float, default=0.30,
                    help="min seconds between distinct catches")
    ap.add_argument("--arrive", type=float, default=0.35,
                    help="ball must have been this far (m) within 0.5s before")
    args = ap.parse_args(argv)
    for rec in args.rec:
        analyse(rec, prox_thr=args.prox, min_gap_s=args.gap,
                arrive_from=args.arrive)
    return 0


if __name__ == "__main__":
    sys.exit(main())
