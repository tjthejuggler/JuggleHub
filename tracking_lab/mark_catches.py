"""Ground-truth catch marker (T3 Part A).

Step through a recording frame by frame and mark every true catch, so the
tracker can be scored per event (missed / phantom / wrong-side) instead of
only by totals.

Definition of a catch (keep this consistent above all else): the frame where
the ball FIRST STOPS in the receiving hand. Do not mark the frame where the
hand closes or where the arc peaks — first stop.

Run:  python3 -m tracking_lab.mark_catches <suffix>    e.g. box30, cascade60_h1

Keys: SPACE play/pause   LEFT/RIGHT step one frame   UP/DOWN playback speed
      l = catch, hand on the LEFT side of the PICTURE at this frame
      r = catch, hand on the RIGHT side of the picture
      x undo last mark   ENTER save & exit   q quit without saving

Notes:
  * sides are PICTURE sides, not hand names — whether the pose model's
    left_wrist matches the viewer's left has not been verified; the scorer
    uses the same picture-side convention.
  * uppercase R shares key code 82 with the GTK UP-arrow, so the R key is
    only recognised lowercase. Press 'r'.
  * if a mark file already exists for the session it is loaded first, so a
    session can be resumed or corrected.
  * a tracker report may show nothing about ground truth here: the raw frame
    is shown on purpose, so marking is not influenced by tracker output.
"""
from __future__ import annotations

import argparse
import json
import os
import sys

import cv2

from .evaluate import find_rec
from .recording import RecordingSession

HERE = os.path.dirname(os.path.abspath(__file__))
GT_DIR = os.path.join(HERE, "ground_truth")

DEFINITION = "catch = frame where the ball FIRST STOPS in the hand"
HELP_LINE = ("SPACE play/pause  LEFT/RIGHT step  UP/DOWN speed  "
             "l=left catch  r=right catch  x undo  ENTER save  q quit")
# left-to-right on screen for the viewer
SIDES = [("l", "L", "LEFT side"), ("r", "R", "RIGHT side")]


def gt_path(suffix: str) -> str:
    return os.path.join(GT_DIR, f"{suffix}_catches.json")


def load_marks(suffix: str) -> list:
    p = gt_path(suffix)
    if not os.path.exists(p):
        return []
    with open(p) as f:
        return json.load(f).get("catches", [])


def save_marks(suffix: str, session: str, marks: list) -> str:
    os.makedirs(GT_DIR, exist_ok=True)
    ordered = sorted(marks, key=lambda m: (m["frame"], m["side"]))
    with open(gt_path(suffix), "w") as f:
        json.dump({"session": session, "catches": ordered}, f, indent=1)
    return gt_path(suffix)


def _hud(frame, sess, i, marks, speed, playing, restored):
    h, w = frame.shape[:2]
    scale = 0.55 if w <= 1000 else 0.8
    y = 34

    def put(text, color, s=None):
        nonlocal y
        cv2.putText(frame, text, (12, y), cv2.FONT_HERSHEY_SIMPLEX,
                    s or scale, color, 2, cv2.LINE_AA)
        y += int(28 * (s or scale) / 0.6)

    idx = sess[i].idx
    put(f"{sess.name}   frame {idx}   ({i + 1}/{len(sess)})   "
        f"{'PLAY' if playing else 'PAUSED'} x{speed:.2f}", (255, 255, 255),
        scale + 0.1)
    put(DEFINITION, (0, 255, 255))
    last = " ".join(f"{m['frame']}{m['side']}" for m in marks[-5:])
    put(f"marked catches: {len(marks)}   last: {last}",
        (0, 255, 0) if marks else (160, 160, 160))
    if restored:
        put(f"loaded {restored} marks from existing file", (0, 200, 255))
    put(HELP_LINE, (180, 180, 180), scale - 0.1)
    cv2.putText(frame, str(idx), (w - 160, h - 16), cv2.FONT_HERSHEY_SIMPLEX,
                1.0, (0, 255, 255), 2, cv2.LINE_AA)
    return frame


def run(suffix: str) -> int:
    rec = find_rec(suffix)
    if rec is None:
        print(f"no recording matching *_{suffix}", file=sys.stderr)
        return 2
    sess = RecordingSession(rec, load_depth=False)
    marks = load_marks(suffix)
    restored = len(marks)

    speed = 1.0
    i = 0
    playing = False
    win = "Mark Catches"
    cv2.namedWindow(win, cv2.WINDOW_NORMAL)
    cv2.resizeWindow(win, 1100, 720)
    print(f"{sess.name}: {len(sess)} frames, {restored} marks restored. "
          f"{DEFINITION}.")

    while True:
        fr = sess[i]
        cv2.imshow(win, _hud(fr.rgb, sess, i, marks, speed, playing, restored))
        key = cv2.waitKey(max(1, int(1000 / (sess.fps * speed)))
                          if playing else 30) & 0xFF

        if key == ord(" "):
            playing = not playing
        elif key in (81, 2):                       # LEFT
            i = max(0, i - 1)
            playing = False
        elif key in (83, 3):                       # RIGHT
            i = min(len(sess) - 1, i + 1)
            playing = False
        elif key in (82, 0):                       # UP
            speed = min(4.0, speed + 0.25)
        elif key in (84, 1):                       # DOWN
            speed = max(0.25, speed - 0.25)
        elif key in (108, 76):                     # l / L
            marks.append({"frame": sess[i].idx, "side": "L"})
            playing = False
        elif key == 114:                           # r only (82 == GTK UP)
            marks.append({"frame": sess[i].idx, "side": "R"})
            playing = False
        elif key in (120, 88):                     # x / X
            if marks:
                marks.pop()
        elif key == 13:                            # ENTER
            path = save_marks(suffix, sess.name, marks)
            print(f"saved {len(marks)} catches -> {path}")
            break
        elif key in (113, 81 ^ 0x80):              # q / Q
            print("quit — marks NOT saved")
            break
        elif playing:
            i = min(len(sess) - 1, i + 1)
            if i >= len(sess) - 1:
                playing = False

    cv2.destroyAllWindows()
    return 0


def _selftest() -> int:
    """Headless check: session loads, idx extraction works, marks round-trip
    through save/load with identical frames. No GUI."""
    rec = find_rec("box30")
    assert rec, "box30 recording not found"
    sess = RecordingSession(rec, load_depth=False)
    f0, f100 = sess[0].idx, sess[100].idx
    assert f0 == 0 and f100 == 100, f"idx wrong: {f0}, {f100}"

    suffix = "box30_SELFTEST"
    try:
        marks = [{"frame": f0 + 3, "side": "L"}, {"frame": f100 - 4,
                  "side": "R"}, {"frame": f100, "side": "L"}]
        path = save_marks(suffix, sess.name, marks)
        got = load_marks(suffix)
        assert got == sorted(marks, key=lambda m: (m["frame"], m["side"])), \
            f"round-trip mismatch: {got}"
        print(f"selftest OK: session {len(sess)} frames, idx {f0}/{f100}, "
              f"3 marks round-tripped via {path}")
    finally:
        if os.path.exists(gt_path(suffix)):
            os.remove(gt_path(suffix))
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("suffix", nargs="?", help="session suffix, e.g. box30")
    ap.add_argument("--selftest", action="store_true",
                    help="headless round-trip test, no GUI")
    a = ap.parse_args()
    if a.selftest:
        return _selftest()
    if not a.suffix:
        ap.error("session suffix required (e.g. box30)")
    return run(a.suffix)


if __name__ == "__main__":
    sys.exit(main())
