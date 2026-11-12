"""Live reference recorder: aligned RGB + depth (RealSense) or webcam only.

Records the frames the iteration loop needs — same layout as the engine's
1_raw_recordings sessions so tracking_lab can consume either:

  python3 -m tracking_lab.record --source realsense --seconds 60 --label 3ball_samecolor
  python3 -m tracking_lab.record --source webcam --seconds 60 --label webcam_test

Output: tracking_lab/recordings/<ts>_<label>/{no_boxes,depth}/ + meta.json
"""
from __future__ import annotations

import argparse
import datetime
import json
import os
import sys
import time

import cv2
import numpy as np

from .camera import DUP_INVALID_PCT, FrameGuard, configure_rgb, exposure_request_from_env


def record(source: str, seconds: float, label: str, out_root: str, width: int = 640,
           height: int = 480, fps: int = 30):
    ts = datetime.datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    out = os.path.join(out_root, f"{ts}_{label}")
    rgb_dir = os.path.join(out, "no_boxes")
    os.makedirs(rgb_dir, exist_ok=True)
    meta = dict(source=source, label=label, width=width, height=height,
                fps=fps, started=ts)

    if source == "realsense":
        import pyrealsense2 as rs
        pipeline, config = rs.pipeline(), rs.config()
        config.enable_stream(rs.stream.color, width, height, rs.format.bgr8, fps)
        config.enable_stream(rs.stream.depth, width, height, rs.format.z16, fps)
        profile = pipeline.start(config)
        align = rs.align(rs.stream.color)
        depth_scale = profile.get_device().first_depth_sensor().get_depth_scale()
        # Exposure: auto by default, written explicitly and read back. This
        # used to force exposure=2000/gain=16 unconditionally ("2ms/gain16,
        # verified best"); that setting yields 5 unique colour fps on saved
        # clips — see camera.py. Manual: TRACKING_EXPOSURE_MS / TRACKING_GAIN.
        expo_ms, gain = exposure_request_from_env()
        meta["rgb_exposure"] = configure_rgb(profile, expo_ms, gain, fps)
        meta["depth_scale"] = depth_scale
        depth_dir = os.path.join(out, "depth")
        os.makedirs(depth_dir, exist_ok=True)
    else:
        cap = cv2.VideoCapture(0)
        cap.set(cv2.CAP_PROP_FRAME_WIDTH, width)
        cap.set(cv2.CAP_PROP_FRAME_HEIGHT, height)
        cap.set(cv2.CAP_PROP_FPS, fps)
        if not cap.isOpened():
            print("ERROR: cannot open webcam 0", file=sys.stderr)
            return 1
        pipeline = None

    n = 0
    t0 = time.time()
    stamps = []
    guard = FrameGuard()
    try:
        while time.time() - t0 < seconds:
            if pipeline is not None:
                frames = pipeline.wait_for_frames(timeout_ms=2000)
                frames = align.process(frames)
                color = np.asarray(frames.get_color_frame().get_data())
                depth = np.asarray(frames.get_depth_frame().get_data()).astype(np.uint16)
                cv2.imwrite(os.path.join(depth_dir, f"frame_{n}_depth.png"), depth)
            else:
                ok, color = cap.read()
                if not ok:
                    break
            stamps.append(time.time() - t0)
            guard.update(color)
            cv2.imwrite(os.path.join(rgb_dir, f"frame_{n}.jpg"), color,
                        [cv2.IMWRITE_JPEG_QUALITY, 95])
            n += 1
            if n % 150 == 0:
                warn = guard.warning(fps)
                print(f"  {n} frames, {time.time()-t0:.0f}s"
                      + (f"   *** {warn} ***" if warn else ""), flush=True)
    except KeyboardInterrupt:
        pass
    finally:
        if pipeline is not None:
            pipeline.stop()
        else:
            cap.release()

    meta["frames"] = n
    meta["duration_s"] = round(time.time() - t0, 2)
    meta["effective_fps"] = round(n / max(meta["duration_s"], 0.01), 1)
    meta["stamps"] = [round(s, 4) for s in stamps]
    meta.update(guard.meta())
    if guard.dup_pct > DUP_INVALID_PCT:
        meta["invalid"] = "repeated colour frames"
    with open(os.path.join(out, "meta.json"), "w") as f:
        json.dump(meta, f, indent=2)
    print(f"Recorded {n} frames ({meta['effective_fps']} loop fps, "
          f"{guard.unique} unique colour frames) -> {out}")
    if guard.dup_pct > DUP_INVALID_PCT:
        print(f"*** WARNING: {guard.dup_pct:.0f}% of the colour frames are repeats. "
              f"This recording is NOT usable; check exposure and re-record. ***",
              file=sys.stderr)
    return 0


def main(argv=None) -> int:
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description="Reference recording capture")
    ap.add_argument("--source", choices=["realsense", "webcam"], default="realsense")
    ap.add_argument("--seconds", type=float, default=60)
    ap.add_argument("--label", default="session")
    ap.add_argument("--out", default=os.path.join(here, "recordings"))
    ap.add_argument("--width", type=int, default=640)
    ap.add_argument("--height", type=int, default=480)
    ap.add_argument("--fps", type=int, default=30)
    args = ap.parse_args(argv)
    return record(args.source, args.seconds, args.label, args.out,
                  args.width, args.height, args.fps)


if __name__ == "__main__":
    sys.exit(main())
