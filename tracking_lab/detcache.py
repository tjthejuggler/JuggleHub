"""Detection cache: run pose + ball + motion ONCE per recording, replay forever.

Perception costs ~55 ms/frame; the tracker itself costs well under 1 ms. Caching
the perception output turns a ~4 minute regression run into ~2 seconds, which
makes it practical to sweep tracker constants, run ablations, and compute
event-level diagnostics on every change.

The cache is keyed on the detector model files + confidence, so switching
models (TRACKING_MODEL_V11=1) or retraining automatically rebuilds it.

Usage:
  python3 -m tracking_lab.detcache --rec tracking_lab/recordings/<session>
"""
from __future__ import annotations

import hashlib
import os
import pickle
import sys

from . import detectors
from .perception import make_depth_fn, perceive
from .recording import RecordingSession
from .tracker import JuggleTracker, TrackingConfig, _color_hist

CACHE_VERSION = 1
CACHE_DIR = os.path.join(detectors.REPO, ".tmp", "detcache")


def _model_tag() -> str:
    parts = []
    for p in (detectors.BALL_MODEL, detectors.POSE_MODEL):
        binf = os.path.splitext(p)[0] + ".bin"
        st = os.stat(binf if os.path.exists(binf) else p)
        parts.append(f"{os.path.abspath(p)}:{st.st_size}:{int(st.st_mtime)}")
    return hashlib.md5("|".join(parts).encode()).hexdigest()[:10]


def cache_path(rec_path: str, conf: float = 0.25) -> str:
    name = os.path.basename(rec_path.rstrip("/"))
    return os.path.join(CACHE_DIR,
                        f"{name}__{_model_tag()}_c{conf:.2f}_v{CACHE_VERSION}.pkl")


def build(rec_path: str, conf: float = 0.25, use_depth: bool = True,
          verbose: bool = False) -> dict:
    sess = RecordingSession(rec_path, load_depth=use_depth)
    det = detectors.BallDetector(conf=conf)
    pose = detectors.PoseDetector()
    motion = detectors.MotionDetector()
    frames = []
    for i in range(len(sess)):
        fr = sess[i]
        wrists, face_pt, dets_raw, _n = perceive(fr.rgb, det, pose, motion)
        depth_fn = make_depth_fn(fr.depth, fr.rgb.shape[:2])
        dets = []
        for box, score, cls in dets_raw:
            z = None
            if depth_fn is not None:
                z = depth_fn((box[0] + box[2]) / 2, (box[1] + box[3]) / 2)
            dets.append((box, score, cls, _color_hist(fr.rgb, box), z))
        frames.append(dict(idx=fr.idx, wrists=wrists, face_pt=face_pt, dets=dets))
        if verbose and i % 100 == 0:
            print(f"  [{i}/{len(sess)}] {sess.name}", flush=True)
    return dict(name=sess.name, fps=sess.fps, has_depth=sess.load_depth,
                model=detectors.BALL_MODEL, conf=conf, frames=frames)


def load(rec_path: str, conf: float = 0.25, rebuild: bool = False,
         verbose: bool = False) -> dict:
    path = cache_path(rec_path, conf)
    if os.path.exists(path) and not rebuild:
        with open(path, "rb") as f:
            return pickle.load(f)
    cache = build(rec_path, conf=conf, verbose=verbose)
    os.makedirs(CACHE_DIR, exist_ok=True)
    with open(path, "wb") as f:
        pickle.dump(cache, f, protocol=pickle.HIGHEST_PROTOCOL)
    return cache


def replay(cache: dict, cfg: TrackingConfig | None = None,
           balls: int = 3) -> JuggleTracker:
    """Run the tracker over cached perception. Identical to the live runner.

    pose_every_n > 1 simulates running the pose network every n-th frame
    and holding the last wrists in between (T6 option 1): the cache stores
    per-frame pose output, so holding forward reproduces a throttled live
    runner exactly for otherwise-deterministic pose. Changes detections ->
    counts; judge on per-event marks + held-out, never on speed alone."""
    cfg = cfg or TrackingConfig()
    cfg.fps = cache["fps"]
    cfg.n_balls = balls
    tracker = JuggleTracker(cfg)
    every_n = max(1, int(getattr(cfg, "pose_every_n", 1)))
    last_wrists = {}
    for k, f in enumerate(cache["frames"]):
        if every_n == 1 or k % every_n == 0:
            last_wrists = f["wrists"]
        dets = [(b, s, c, h) for b, s, c, h, _z in f["dets"]]
        depth_fn = None
        if cache["has_depth"]:
            zmap = {((b[0] + b[2]) / 2, (b[1] + b[3]) / 2): z
                    for b, _s, _c, _h, z in f["dets"]}
            depth_fn = lambda x, y, _m=zmap: _m.get((x, y))  # noqa: E731
        tracker.step(f["idx"], dets, last_wrists, depth_at=depth_fn,
                     face_pt=f["face_pt"])
    return tracker


def main(argv=None) -> int:
    import argparse
    ap = argparse.ArgumentParser(description="Build the detection cache")
    ap.add_argument("--rec", required=True, nargs="+")
    ap.add_argument("--conf", type=float, default=0.25)
    ap.add_argument("--rebuild", action="store_true")
    args = ap.parse_args(argv)
    for rec in args.rec:
        c = load(rec, conf=args.conf, rebuild=args.rebuild, verbose=True)
        print(f"{c['name']}: {len(c['frames'])} frames cached -> "
              f"{cache_path(rec, args.conf)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
