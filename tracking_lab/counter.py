"""Catch-count player with click-to-annotate tracker feedback.

Run:  python3 -m tracking_lab.counter

Watch the rendered tracking video, count catches, and click on mistakes.
Clicks are mode-based — pick a mode with number keys 1-5, then click things:

  1  BAD STATE   — click a track circle whose state label is wrong
                   (e.g. empty hand labeled HELD). Records id+frame.
  2  PHANTOM     — click a track circle that shouldn't exist at all.
  3  MISSED BALL — click a real ball that has NO track on it.
  4  MISSED EVT  — click where a catch/throw happened that has no event.
  5  BAD EVT     — click an event banner that is wrong (phantom catch/throw).

  C / X   +1 / undo catch count
  SPACE   play/pause      LEFT/RIGHT   1 frame back/forward
  UP/DOWN speed 0.25x-4x
  ENTER   submit everything for this video, next video
  q       quit (saved progress kept)
"""
from __future__ import annotations

import glob
import json
import os
import subprocess
import sys

import cv2
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REC_ROOT = os.path.join(HERE, "recordings")
TMP = os.path.abspath(os.path.join(HERE, "..", ".tmp"))

QUEUE = ["box30", "441_30", "shower40", "columns30", "flash_10", "cascade60"]

MODES = {
    1: ("BAD STATE", "click track circle with WRONG state label"),
    2: ("PHANTOM", "click track that should not exist"),
    3: ("MISSED BALL", "click a real ball with NO track"),
    4: ("MISSED EVT", "click where an event was missed"),
    5: ("BAD EVT", "click a wrong event banner"),
}


def render_path(frag: str) -> str:
    return os.path.join(TMP, f"count_{frag}.mp4")


def tracks_path(frag: str) -> str:
    return render_path(frag).replace(".mp4", "_tracks.json")


def ensure_render(frag: str) -> str | None:
    out = render_path(frag)
    if os.path.exists(out) and os.path.exists(tracks_path(frag)):
        return out
    dirs = sorted(glob.glob(os.path.join(REC_ROOT, f"*{frag}")))
    if not dirs:
        print(f"no recording matching {frag}", file=sys.stderr)
        return None
    subprocess.run([sys.executable, "-m", "tracking_lab", "--rec", dirs[-1],
                    "--render", out, "--quiet"], check=True, capture_output=True)
    return out


class Player:
    def __init__(self, path: str, track_data: dict):
        self.cap = cv2.VideoCapture(path)
        self.fps = self.cap.get(cv2.CAP_PROP_FPS) or 30.0
        self.total = int(self.cap.get(cv2.CAP_PROP_FRAME_COUNT))
        self.pos = 0
        self.playing = False
        self.speed = 1.0
        self.count = 0
        self.mode = 1
        self.notes = []          # annotation records
        self.track_data = track_data

    def frame_info(self):
        """Track data for current frame (per_frame index == frame number)."""
        pf = self.track_data.get("per_frame", [])
        if 0 <= self.pos < len(pf):
            return pf[self.pos]
        return None

    def track_at(self, x, y):
        """Track id whose circle contains (x,y), else None."""
        info = self.frame_info()
        if not info:
            return None
        for tid, (tx, ty) in zip(info["ids"], info["pos"]):
            if (x - tx) ** 2 + (y - ty) ** 2 <= 14 ** 2:
                return tid
        return None

    def nearest_event(self, x, y):
        """Event near (x,y) in the current frame, else None."""
        best = None
        bd = 60 * 60
        for e in self.track_data.get("events", []):
            if abs(e.get("frame", -1) - self.pos) > 2:
                continue
            ex, ey = 20, 40          # event banner position in _draw
            d = (x - ex) ** 2 + (y - ey) ** 2
            if d < bd:
                best = e
                bd = d
        return best

    def seek(self, frame: int):
        self.pos = max(0, min(frame, self.total - 1))
        self.cap.set(cv2.CAP_PROP_POS_FRAMES, self.pos)

    def read(self):
        ok, frame = self.cap.read()
        if ok:
            self.pos += 1
        return ok, frame

    def hud(self, frame, label):
        fps_eff = self.fps * self.speed
        remain = (self.total - self.pos) / max(fps_eff, 0.1)
        bar_w = 560
        done = int(bar_w * self.pos / max(self.total, 1))
        cv2.rectangle(frame, (10, 10), (10 + bar_w, 30), (60, 60, 60), -1)
        cv2.rectangle(frame, (10, 10), (10 + done, 30), (90, 200, 90), -1)
        cv2.putText(frame, f"{label}  {self.pos}/{self.total}  "
                    f"{self.speed:.2f}x  -{remain:.0f}s  "
                    f"{'PAUSED' if not self.playing else 'play'}",
                    (12, 55), cv2.FONT_HERSHEY_SIMPLEX, 0.55,
                    (255, 255, 255), 1, cv2.LINE_AA)
        cv2.putText(frame, f"CATCHES: {self.count}   errors: {len(self.notes)}",
                    (12, 90), cv2.FONT_HERSHEY_SIMPLEX, 0.8,
                    (0, 255, 255), 2, cv2.LINE_AA)
        mname, mhelp = MODES[self.mode]
        cv2.putText(frame, f"MODE {self.mode}: {mname} - {mhelp}",
                    (12, 122), cv2.FONT_HERSHEY_SIMPLEX, 0.55,
                    (0, 200, 0), 1, cv2.LINE_AA)
        cv2.putText(frame, "1-5 mode  click=annotate  SPACE play  </> frame  "
                    "up/down speed  C catch  X undo  ENTER submit  q quit",
                    (12, 150), cv2.FONT_HERSHEY_SIMPLEX, 0.45,
                    (180, 180, 180), 1, cv2.LINE_AA)
        if getattr(self, "last_click", ""):
            cv2.putText(frame, self.last_click, (12, 178),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.55, (0, 0, 255), 2, cv2.LINE_AA)
        return frame


def on_mouse(event, x, y, flags, param):
    if event != cv2.EVENT_LBUTTONDOWN:
        return
    player, frag, errors_path = param
    info = player.frame_info()
    tid = player.track_at(x, y)
    mode = player.mode
    if mode in (1, 2) and tid is not None:
        st = info["states"][info["ids"].index(tid)] if info else "?"
        rec = dict(mode=MODES[mode][0], frame=player.pos, track=tid,
                   label=st, pos=[int(x), int(y)],
                   wrists=info.get("wrists", {}) if info else {})
        player.notes.append(rec)
        player.last_click = (f"{MODES[mode][0]}: track {tid} state={st} "
                             f"@ frame {player.pos}")
    elif mode == 3:
        rec = dict(mode=MODES[mode][0], frame=player.pos, pos=[int(x), int(y)],
                   nearby_tracks=info["ids"] if info else [])
        player.notes.append(rec)
        player.last_click = f"MISSED BALL @ frame {player.pos} ({x},{y})"
    elif mode == 4:
        rec = dict(mode=MODES[mode][0], frame=player.pos, pos=[int(x), int(y)])
        player.notes.append(rec)
        player.last_click = f"MISSED EVENT @ frame {player.pos} ({x},{y})"
    elif mode == 5:
        e = player.nearest_event(x, y)
        rec = dict(mode=MODES[mode][0], frame=player.pos, pos=[int(x), int(y)],
                   event=e)
        player.notes.append(rec)
        player.last_click = (f"BAD EVT: {e['event'] if e else '?'} "
                             f"t{e.get('track') if e else '?'} @ {player.pos}")


def main() -> int:
    todo = []
    for frag in QUEUE:
        cnt = render_path(frag).replace(".mp4", "_count.txt")
        if not os.path.exists(cnt):
            todo.append(frag)
    if not todo:
        print("All queued videos already have counts. Delete a "
              "*_count.txt in .tmp/ to redo one.")
        return 0
    print(f"{len(todo)} videos to count: {todo}")

    cv2.namedWindow("Catch Counter", cv2.WINDOW_NORMAL)
    cv2.resizeWindow("Catch Counter", 960, 720)

    for frag in todo:
        real = ensure_render(frag)
        if real is None:
            continue
        path = render_path(frag)
        if os.path.abspath(real) != os.path.abspath(path):
            os.replace(real, path)
        with open(tracks_path(frag)) as f:
            track_data = json.load(f)

        player = Player(path, track_data)
        param = (player, frag, None)
        cv2.setMouseCallback("Catch Counter", on_mouse, param)
        print(f"\n=== counting {frag} (track data: "
              f"{len(track_data.get('events', []))} events) ===")
        submit = False
        while True:
            if player.playing:
                ok, frame = player.read()
                if not ok:
                    player.playing = False
                    player.seek(player.total - 1)
                    ok, frame = player.cap.retrieve()
                cv2.imshow("Catch Counter", player.hud(frame, frag))
                key = cv2.waitKey(max(1, int(1000 / (player.fps * player.speed)))) & 0xFF
            else:
                player.cap.set(cv2.CAP_PROP_POS_FRAMES, max(0, player.pos - 1))
                ok, frame = player.cap.read()
                frame = frame if ok else np.zeros((480, 640, 3), np.uint8)
                cv2.imshow("Catch Counter", player.hud(frame, frag))
                key = cv2.waitKey(20) & 0xFF

            if key == ord(" "):
                player.playing = not player.playing
            elif key in (81, 2):         # LEFT
                player.seek(player.pos - 2)
                player.playing = False
            elif key in (83, 3):         # RIGHT
                player.seek(player.pos)
                player.playing = False
            elif key in (82, 0):         # UP
                player.speed = min(4.0, player.speed + 0.25)
            elif key in (84, 1):         # DOWN
                player.speed = max(0.25, player.speed - 0.25)
            elif key in (ord("c"), ord("C")):
                player.count += 1
            elif key in (ord("x"), ord("X")):
                player.count = max(0, player.count - 1)
            elif key in range(ord("1"), ord("5") + 1):
                player.mode = key - ord("0")
                player.last_click = ""
            elif key == 13:
                submit = True
                break
            elif key == ord("q"):
                print("quit — count NOT saved for this video")
                player.cap.release()
                cv2.destroyAllWindows()
                return 0
        player.cap.release()
        if submit:
            base = render_path(frag).replace(".mp4", "")
            with open(base + "_count.txt", "w") as f:
                f.write(str(player.count))
            with open(base + "_notes.json", "w") as f:
                json.dump(dict(video=frag, true_catches=player.count,
                               notes=player.notes), f, indent=2)
            print(f"saved {player.count} catches, {len(player.notes)} notes "
                  f"for {frag}")
        cv2.destroyAllWindows()

    print("\nAll counts + notes saved to .tmp/. Tell Roo they are done.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
