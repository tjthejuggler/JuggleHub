"""Verification wizard: juggle a named pattern until you've done the required
number of catches, then stop juggling and press SPACE to finish the recording.

No time limit — take as long as you need. Nothing is counted on screen while
you juggle: you count your catches silently, stop juggling at the target
number, and press SPACE to submit. Scoring happens afterwards:
  python3 -m tracking_lab --rec tracking_lab/recordings/<dir> --expected <required>

Controls: SPACE start, SPACE submit when done, R restart this take (drop /
miscount), k skip, q quit. All takes use the RealSense (auto-falls back to
webcam if unplugged); the active camera is shown on the instruction card.

Held-out takes (added 2026-11-12):
  python3 -m tracking_lab.verify_wizard --heldout h1
  python3 -m tracking_lab.verify_wizard --heldout led1 --only cascade60
`--heldout TAG` records the patterns again under the label <pattern>_<TAG>
and marks them `heldout` in meta.json. `python3 -m tracking_lab.evaluate`
picks those up automatically as the HELD-OUT set. Without the flag the wizard
only offers patterns that have no recording yet, which is none of them once
the first six exist. Never tune a tracker constant against a held-out take.
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

from .camera import DUP_INVALID_PCT
from .wizard import Cam, OUT_ROOT, overlay
from .detectors import BallDetector, MotionDetector, PoseDetector
from .tracker import JuggleTracker, TrackingConfig, _color_hist

# (label, title, instruction lines, required catches)
PATTERNS = [
    ("cascade60", "3-BALL CASCADE — 60 catches",
     ["Plain cascade, steady rhythm.", "Stop juggling after 60 catches, then press SPACE."], 60),
    ("box30", "BOX — 30 catches",
     ["Box pattern: alternate across-throws.", "30 catches total, then press SPACE."], 30),
    ("441_30", "441 — 30 catches",
     ["Siteswap 441: throw, throw, pass.", "30 catches total, then press SPACE."], 30),
    ("shower40", "3-BALL SHOWER — 40 catches",
     ["Shower: throw across + hand-back.", "40 catches total, then press SPACE."], 40),
    ("columns30", "COLUMNS — 30 catches",
     ["Columns: middle-left-right, straight up.", "30 catches total, then press SPACE."], 30),
    ("flash_10", "3-BALL FLASH × 10",
     ["Flash: throw all 3 (R-L-R), catch all 3, pause.", "10 flashes = 30 catches, then press SPACE."], 30),
]

W, H, FPS = 640, 480, 30


def _draw_tracks(img, tracker):
    from .__main__ import PALETTE
    for t in tracker.tracks:
        c = PALETTE[t.track_id % len(PALETTE)]
        x, y = int(t.x), int(t.y)
        cv2.circle(img, (x, y), 12, c, 2)
        if len(t.history) > 2:
            for _, px, py, _s in t.history[-30:]:
                cv2.circle(img, (int(px), int(py)), 1, c, -1)


def build_queue(done: set, heldout: str | None = None, only: list | None = None) -> list:
    """-> [(label, title, notes, required)] still to record.

    heldout=None: the original behaviour, patterns with no recording yet.
    heldout=TAG:  every (or `only` the named) pattern again, as <pattern>_<TAG>.
    """
    pats = [p for p in PATTERNS if not only or p[0] in only]
    if heldout:
        pats = [(f"{lab}_{heldout}", f"{title}  [held-out {heldout}]", notes, req)
                for lab, title, notes, req in pats]
    return [p for p in pats if p[0] not in done]


def record_session(cam: Cam, label: str, required: int, heldout: bool = False):
    ts = datetime.datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    out = os.path.join(OUT_ROOT, f"{ts}_{label}")
    rgb_dir = os.path.join(out, "no_boxes")
    os.makedirs(rgb_dir, exist_ok=True)
    depth_dir = os.path.join(out, "depth")
    has_depth = cam.source == "realsense"
    if has_depth:
        os.makedirs(depth_dir, exist_ok=True)
    meta = dict(label=label, source=cam.source, width=W, height=H, fps=FPS,
                depth_scale=cam.depth_scale, started=ts, required_catches=required,
                heldout=bool(heldout))

    while True:  # restartable take loop
        n, stamps = 0, []
        cd_end = time.time() + 3.0
        while time.time() < cd_end:
            color, _ = cam.read()
            if color is None:
                cv2.waitKey(30)
                continue
            remain = int(cd_end - time.time()) + 1
            lines = [(f"[{label}]  recording starts in...", 0.7, (0, 255, 255))]
            cv2.imshow("JuggleHub Verification Wizard",
                       overlay(color, lines, False, 0, 0, 0, big=str(remain)))
            if cv2.waitKey(1) & 0xFF == ord("q"):
                cv2.destroyAllWindows()
                return None
        t0 = time.time()
        restarted = False
        cam.guard.reset()      # count repeated colour frames over this take only
        while True:
            color, depth = cam.read()
            if color is None:
                cv2.waitKey(30)
                continue
            disp = overlay(color, [(f"[REC {label}]  juggle {required} catches, "
                                    "SPACE=submit", 0.7, (0, 0, 255)),
                                   ("R=restart take", 0.5, (200, 200, 200))]
                           + cam.status_lines(),
                           True, 0, n, time.time() - t0)

            cv2.imwrite(os.path.join(rgb_dir, f"frame_{n}.jpg"), color,
                        [cv2.IMWRITE_JPEG_QUALITY, 95])
            if depth is not None:
                cv2.imwrite(os.path.join(depth_dir, f"frame_{n}_depth.png"), depth)
            stamps.append(round(time.time() - t0, 4))
            n += 1
            cv2.imshow("JuggleHub Verification Wizard", disp)
            key = cv2.waitKey(1) & 0xFF
            if key == ord(" "):      # submitted
                break
            if key == ord("r"):      # discard the take, start over
                for k in range(n):
                    os.remove(os.path.join(rgb_dir, f"frame_{k}.jpg"))
                    if depth is not None:
                        os.remove(os.path.join(depth_dir, f"frame_{k}_depth.png"))
                restarted = True
                break
            if key == ord("q"):
                cv2.destroyAllWindows()
                meta.update(frames=n, aborted=True)
                with open(os.path.join(out, "meta.json"), "w") as f:
                    json.dump(meta, f, indent=2)
                return None
        if not restarted:
            break
    cv2.destroyAllWindows()
    meta["frames"] = n
    meta["duration_s"] = round(stamps[-1] if stamps else 0.0, 2)
    meta["effective_fps"] = round(n / max(meta["duration_s"], 0.01), 1)
    meta["stamps"] = stamps
    # effective_fps is the loop rate; these say whether the colour frames are real
    meta["rgb_exposure"] = cam.exposure
    meta.update(cam.guard.meta())
    if cam.guard.dup_pct > DUP_INVALID_PCT:
        meta["invalid"] = "repeated colour frames"
        print(f"\n*** WARNING: {cam.guard.dup_pct:.0f}% of the colour frames in this "
              f"take are repeats ({cam.guard.unique} unique of {cam.guard.total}). "
              f"Do NOT score it; check exposure and re-record. ***\n", file=sys.stderr)
    with open(os.path.join(out, "meta.json"), "w") as f:
        json.dump(meta, f, indent=2)
    return out


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Counted-catch recording wizard.")
    ap.add_argument("--heldout", metavar="TAG", default=None,
                    help="record the patterns again as <pattern>_<TAG>, marked "
                         "held-out (letters/digits only, e.g. h1, led1)")
    ap.add_argument("--only", nargs="+", metavar="PATTERN", default=None,
                    choices=[p[0] for p in PATTERNS],
                    help="restrict to these patterns")
    args = ap.parse_args(argv)
    if args.heldout and not args.heldout.isalnum():
        ap.error("--heldout TAG must be letters/digits only")

    os.makedirs(OUT_ROOT, exist_ok=True)
    # a pattern counts as done only if a session folder with a meta.json
    # exists: a crashed take (e.g. camera disconnect mid-recording) leaves a
    # partial folder without meta and must be offered again.
    done = set()
    for d in os.listdir(OUT_ROOT):
        p = os.path.join(OUT_ROOT, d)
        if os.path.isdir(p) and os.path.exists(os.path.join(p, "meta.json")):
            done.add(d.split("_", 2)[-1])
    queue = build_queue(done, args.heldout, args.only)
    if not queue:
        print("Nothing to record: every requested pattern already has a recording "
              "under that label. Use --heldout <new tag> for another set.")
        return 0

    cam = None
    try:
        i = 0
        while i < len(queue):
            label, title, notes, required = queue[i]
            source = "realsense"
            if cam is None or cam.source != source:
                try:
                    cam = cam.switch(source) if cam else Cam(source)
                except Exception as e:
                    print(f"WARNING: {e}; using webcam", file=sys.stderr)
                    source = "webcam"
                    cam = cam.switch(source) if cam else Cam(source)

            proceed = None
            while True:
                color, _ = cam.read()
                if color is None:
                    cv2.waitKey(30)
                    continue
                lines = [(f"{i + 1}/{len(queue)}  {title}", 0.7, (0, 255, 255))]
                for note in notes:
                    lines.append((note, 0.55, (255, 255, 255)))
                cam_name = "RealSense (depth + color)" if source == "realsense" else "webcam (no depth)"
                lines.append((f"camera: {cam_name}", 0.55, (200, 200, 200)))
                lines += cam.status_lines()
                lines.append(("no time limit - juggle until done, then SPACE",
                              0.55, (200, 200, 200)))
                lines.append(("SPACE=start   k=skip   q=quit", 0.55, (0, 255, 0)))
                cv2.imshow("JuggleHub Verification Wizard",
                           overlay(color, lines, False, 0, 0, 0,
                                   big=f"{required} catches"))
                key = cv2.waitKey(1) & 0xFF
                if key == ord(" "):
                    proceed = "record"
                    break
                if key == ord("k"):
                    proceed = "skip"
                    break
                if key == ord("r") and i > 0:
                    i -= 1
                    proceed = "skip"
                    break
                if key == ord("q"):
                    proceed = "quit"
                    break
            if proceed == "quit":
                break
            if proceed == "skip":
                i += 1
                continue

            print(f"Recording '{label}' — juggle {required} catches, SPACE to finish.")
            out = record_session(cam, label, required, heldout=bool(args.heldout))
            if out is None:
                print("Quit during recording.")
                break
            print(f"  saved -> {out}  (expected {required} catches)")
            t_end = time.time() + 2.0
            while time.time() < t_end:
                color, _ = cam.read()
                if color is None:
                    cv2.waitKey(30)
                    continue
                cv2.imshow("JuggleHub Verification Wizard",
                           overlay(color, [("Saved!", 0.7, (0, 255, 0))], False, 0, 0, 0,
                                   big="SAVED"))
                cv2.waitKey(1)
            i += 1
    finally:
        if cam is not None:
            cam.close()
    print("\nDone. Score each session against your hand count:")
    print("  python3 -m tracking_lab --rec tracking_lab/recordings/<dir> --expected <count>")
    if args.heldout:
        print("Held-out takes are scored together by:  python3 -m tracking_lab.evaluate")
    return 0


if __name__ == "__main__":
    sys.exit(main())
