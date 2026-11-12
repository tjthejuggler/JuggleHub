"""Exposure experiment for the RealSense D455.

Compares several RGB exposure settings by recording a short clip at each one
while you juggle, then reporting per setting: unique colour frames per second,
image brightness, saturated pixels and V8 detections per frame.

Usage:
  python3 -m tracking_lab.exposure_test            # sweep + report
  python3 -m tracking_lab.exposure_test --seconds 15

IMPORTANT: keep juggling continuously while it sweeps. Each setting records
`--seconds` seconds; between settings there is a 3 s pause.

HISTORY (2026-11-12 audit) — the first run of this test was invalid.
It passed 8000 / 4000 / 2000 to the RGB exposure option, labelled
"8ms / 4ms / 2ms", and declared "2ms_g16" the winner by detections per frame.
Re-checking the saved clips showed those settings delivered 1.3 / 2.6 / 5.1
unique colour frames per second (each image repeated 24 / 12 / 6 times) and
were heavily overexposed; only "auto" was a real 30 fps stream. The winner was
picked by counting detections on repeated, motion-smeared frames. That result
must not be used. The numbers above are measured; the cause (the RGB exposure
unit being 100 us, not 1 us) is inferred and is still unconfirmed on the
device — see camera.py.

This version states exposure in milliseconds via camera.configure_rgb, counts
repeated frames while recording, and restores auto exposure on exit. It has
NOT been run on a camera yet. The gains below are untested starting points
(gain doubles as exposure halves), not known-good values.

Settings swept (exposure_ms, gain): see SETTINGS.
Results: .tmp/exposure_test_results.json
"""
from __future__ import annotations

import json
import os
import sys
import time

import cv2
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
TMP = os.path.abspath(os.path.join(HERE, "..", ".tmp"))
KEY_FILE = os.path.join(HERE, "zai_apikey.txt")

# (name, exposure in REAL milliseconds or None for auto, gain)
SETTINGS = [
    ("auto", None, None),
    ("16ms_g16", 16.0, 16.0),
    ("8ms_g32", 8.0, 32.0),
    ("4ms_g64", 4.0, 64.0),
    ("2ms_g128", 2.0, 128.0),
]

W, H, FPS = 640, 480, 30


import pyrealsense2 as rs  # noqa: E402  (may fail headless)

from .camera import DUP_INVALID_PCT, FrameGuard, configure_rgb  # noqa: E402


def set_exposure(profile, exposure_ms, gain) -> dict:
    """Apply a setting through the one shared code path; -> read-back state."""
    return configure_rgb(profile, exposure_ms, gain, FPS)


def recall_stats(out_dir, det):
    import glob
    import re
    files = sorted(glob.glob(os.path.join(out_dir, "frame_*.jpg")),
                   key=lambda p: int(re.search(r"frame_(\d+)", p).group(1)))
    counts, confs, gray, sat = [], [], [], []
    for f in files:
        img = cv2.imread(f)
        dets = det.detect(img)
        counts.append(len(dets))
        confs.extend([s for _b, s, _c in dets])
        g = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
        gray.append(float(g.mean()))
        sat.append(int((g >= 250).sum()))
    return dict(frames=len(files),
                mean_dets=float(np.mean(counts)) if counts else 0.0,
                frames_with_3=float(np.mean([1 if c >= 3 else 0 for c in counts])) if counts else 0.0,
                mean_conf=float(np.mean(confs)) if confs else 0.0,
                mean_gray=float(np.mean(gray)) if gray else 0.0,
                mean_saturated_px=float(np.mean(sat)) if sat else 0.0)


def main(argv=None) -> int:
    args = sys.argv[1:]
    seconds = 12
    if "--seconds" in args:
        seconds = float(args[args.index("--seconds") + 1])

    det_mod = __import__("tracking_lab.detectors", fromlist=["BallDetector"])
    det = det_mod.BallDetector()

    results = {}
    pipe = rs.pipeline()
    cfg = rs.config()
    cfg.enable_stream(rs.stream.color, W, H, rs.format.bgr8, FPS)
    cfg.enable_stream(rs.stream.depth, W, H, rs.format.z16, FPS)
    profile = pipe.start(cfg)
    align = rs.align(rs.stream.color)

    cv2.namedWindow("Exposure Test", cv2.WINDOW_NORMAL)
    cv2.resizeWindow("Exposure Test", 960, 720)

    try:
        return _sweep(pipe, profile, align, det, seconds, results)
    finally:
        # never leave the camera in manual exposure: later recordings would
        # silently inherit it
        try:
            set_exposure(profile, None, None)
        except Exception as e:
            print(f"WARNING: could not restore auto exposure: {e}", file=sys.stderr)
        try:
            pipe.stop()
        except Exception:
            pass  # already stopped
        cv2.destroyAllWindows()


def _sweep(pipe, profile, align, det, seconds, results) -> int:
    print("Keep juggling continuously! Sweeping exposure settings...")
    for name, expo, gain in SETTINGS:
        state = set_exposure(profile, expo, gain)
        ok = not state["errors"]
        print(f"\n--- {name} (applied={ok}) — juggle now, {seconds}s ---")
        # 3 s visible countdown before capture starts
        cd_end = time.time() + 3.0
        while time.time() < cd_end:
            frames = align.process(pipe.wait_for_frames(timeout_ms=4000))
            color = np.asanyarray(frames.get_color_frame().get_data())
            remain = int(cd_end - time.time()) + 1
            cv2.putText(color, f"NEXT: {name}   starting in {remain}...",
                        (12, 40), cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 255, 255), 2)
            cv2.putText(color, "keep juggling!", (12, 74),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)
            cv2.imshow("Exposure Test", color)
            if cv2.waitKey(1) & 0xFF == ord("q"):
                print("aborted")
                return 0

        out_dir = os.path.join(TMP, f"exposure_{name}")
        os.makedirs(out_dir, exist_ok=True)
        for old in os.listdir(out_dir):          # no stale frames from earlier runs
            if old.startswith("frame_"):
                os.remove(os.path.join(out_dir, old))
        n = 0
        guard = FrameGuard()
        t0 = time.time()
        while time.time() - t0 < seconds:
            frames = align.process(pipe.wait_for_frames(timeout_ms=4000))
            color = np.asanyarray(frames.get_color_frame().get_data())
            guard.update(color)
            cv2.imwrite(os.path.join(out_dir, f"frame_{n}.jpg"), color,
                        [cv2.IMWRITE_JPEG_QUALITY, 95])
            n += 1
            elapsed = time.time() - t0
            # draw on a COPY: the old code drew on the camera's buffer, so a
            # re-delivered frame was saved with the previous HUD burnt in
            disp = color.copy()
            cv2.putText(disp, f"REC {name}  {elapsed:.0f}/{seconds:.0f}s",
                        (12, 40), cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 0, 255), 2)
            cv2.circle(disp, (W - 30, 30), 10, (0, 0, 255), -1)
            warn = guard.warning(FPS)
            if warn:
                cv2.putText(disp, warn, (12, 74), cv2.FONT_HERSHEY_SIMPLEX, 0.6,
                            (0, 0, 255), 2)
            cv2.imshow("Exposure Test", disp)
            if cv2.waitKey(1) & 0xFF == ord("q"):
                print("aborted")
                return 0
        duration = max(time.time() - t0, 0.01)
        stats = recall_stats(out_dir, det)
        stats["setting"] = name
        stats["applied"] = ok
        stats["rgb_exposure"] = state
        stats.update(guard.meta())
        stats["unique_color_fps"] = round(guard.unique / duration, 1)
        # a setting that repeats frames is disqualified, whatever it detects
        stats["valid"] = guard.dup_pct <= DUP_INVALID_PCT
        results[name] = stats
        # show the result on-screen for 2 s
        t_end = time.time() + 2.0
        while time.time() < t_end:
            frames = align.process(pipe.wait_for_frames(timeout_ms=4000))
            color = np.asanyarray(frames.get_color_frame().get_data())
            cv2.putText(color, f"{name}: dets/frame={stats['mean_dets']:.2f} "
                        f"3balls={100*stats['frames_with_3']:.0f}%",
                        (12, 40), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)
            cv2.imshow("Exposure Test", color)
            cv2.waitKey(1)
        print(f"  {n} frames; unique colour fps={stats['unique_color_fps']} "
              f"({stats['color_duplicate_pct']}% repeats"
              f"{'' if stats['valid'] else ' -> INVALID'}); "
              f"gray={stats['mean_gray']:.0f} saturated_px={stats['mean_saturated_px']:.0f}; "
              f"dets/frame={stats['mean_dets']:.2f}; "
              f">=3 balls: {100*stats['frames_with_3']:.0f}%; "
              f"conf={stats['mean_conf']:.2f}")

    out = os.path.join(TMP, "exposure_test_results.json")
    with open(out, "w") as f:
        json.dump(results, f, indent=2)
    print("\nsaved ->", out)
    valid = {k: v for k, v in results.items() if v["valid"]}
    invalid = [k for k, v in results.items() if not v["valid"]]
    if invalid:
        print("DISQUALIFIED (repeated colour frames):", ", ".join(invalid))
    if valid:
        best = max(valid.items(), key=lambda kv: kv[1]["frames_with_3"])
        print("Highest share of frames with >=3 detections among valid settings:",
              best[0])
    print("\nThis is a 12 s detection-count comparison, not a tracking result. "
          "Before adopting a manual setting, record a counted session with it "
          "and score it with: python3 -m tracking_lab.evaluate")
    return 0


if __name__ == "__main__":
    sys.exit(main())
