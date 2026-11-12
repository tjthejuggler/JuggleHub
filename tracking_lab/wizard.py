"""Guided recording wizard: live viewer tells you what to juggle, you just follow along.

Run:  python3 -m tracking_lab.wizard

Controls (on screen):
  SPACE  start the current session (countdown, then it records and
         stops by itself and shows the next one)
  k      skip this session
  r      re-record the session you just finished (on the instruction card)
  q      quit

Everything else (naming, folders, camera choice, depth capture, meta.json) is automatic.
Output: tracking_lab/recordings/<ts>_<label>/{no_boxes,depth}/ + meta.json
"""
from __future__ import annotations

import datetime
import json
import os
import sys
import time

import cv2
import numpy as np

from .camera import DUP_INVALID_PCT, FrameGuard, configure_rgb, exposure_request_from_env

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_ROOT = os.path.join(HERE, "recordings")

# label -> (title, instruction lines, camera, seconds)
SESSIONS = [
    ("cascade3_mixed", "3 balls, DISTINCT colors, normal (not glowing)",
     ["Use 3 balls with clearly different colors.", "Juggle a normal 3-ball cascade.",
      "Whole body + full ball arc in frame."], "realsense", 60),
    ("cascade3_same", "3 IDENTICAL balls (same color, not glowing)",
     ["Use 3 balls that look exactly the same.", "Juggle a normal 3-ball cascade.",
      "Whole body + full ball arc in frame."], "realsense", 60),
    ("cascade3_glow_mixed_locked", "3 GLOWING balls, distinct colors (auto exposure)",
     ["Use 3 glowing balls with different colors, LEDs ON.",
      "Juggle a normal 3-ball cascade at a steady pace."], "realsense", 60),
    ("cascade3_glow_mixed", "3 GLOWING balls, DISTINCT colors",
     ["Use 3 glowing balls with different colors, LEDs ON.", "Juggle a normal 3-ball cascade."],
     "realsense", 60),
    ("cascade3_mixed_webcam", "3 balls, DISTINCT colors (WEBCAM only)",
     ["Use 3 balls with clearly different colors.", "Juggle a normal 3-ball cascade.",
      "This one records from the plain webcam (no depth)."], "webcam", 60),
    ("cascade3_same_webcam", "3 IDENTICAL balls (WEBCAM only)",
     ["Use 3 identical balls, not glowing.", "Juggle a normal 3-ball cascade.",
      "This one records from the plain webcam (no depth)."], "webcam", 60),
    ("drops3_mixed", "3 DISTINCT colors + deliberate drops",
     ["Juggle a 3-ball cascade.", "Drop a ball on purpose 3-4 times, pick it up and continue."],
     "realsense", 75),
    ("columns_shower", "Second pattern: columns or shower",
     ["Juggle any non-cascade pattern (columns, shower, ...).", "3 distinct colors."],
     "realsense", 60),
]

W, H, FPS = 640, 480, 30


class Cam:
    """Opens the requested source; realsense yields (color, depth), webcam (color, None)."""

    def __init__(self, source: str):
        self.source = source
        self.pipeline = None
        self.cap = None
        self.align = None
        self.depth_scale = None
        self.exposure = None          # RGB exposure state read back from the camera
        self.guard = FrameGuard()     # repeated-colour-frame counter
        if source == "realsense":
            import pyrealsense2 as rs
            self.rs = rs
            self.pipeline = rs.pipeline()
            cfg = rs.config()
            cfg.enable_stream(rs.stream.color, W, H, rs.format.bgr8, FPS)
            cfg.enable_stream(rs.stream.depth, W, H, rs.format.z16, FPS)
            profile = self.pipeline.start(cfg)
            self.align = rs.align(rs.stream.color)
            self.depth_scale = profile.get_device().first_depth_sensor().get_depth_scale()
            # EXPOSURE: always put the RGB sensor in a known state and read it
            # back (see camera.py for why). Default is auto exposure, written
            # explicitly. Manual: TRACKING_EXPOSURE_MS=<ms> [TRACKING_GAIN=<g>].
            # The old TRACKING_EXPOSURE_LOCK / TRACKING_LED_LOCK presets are
            # retired: the 2000 preset produced 5 unique colour fps.
            expo_ms, gain = exposure_request_from_env()
            self.exposure = configure_rgb(profile, expo_ms, gain, FPS)
        else:
            self.cap = cv2.VideoCapture(0)
            self.cap.set(cv2.CAP_PROP_FRAME_WIDTH, W)
            self.cap.set(cv2.CAP_PROP_FRAME_HEIGHT, H)
            self.cap.set(cv2.CAP_PROP_FPS, FPS)
            if not self.cap.isOpened():
                raise RuntimeError("cannot open webcam 0")

    def read(self):
        """-> (color_bgr, depth_u16_or_None); retries once on transient failure."""
        if self.pipeline is not None:
            for _ in range(2):
                try:
                    frames = self.align.process(self.pipeline.wait_for_frames(timeout_ms=2000))
                    color = np.asarray(frames.get_color_frame().get_data())
                    depth = np.asarray(frames.get_depth_frame().get_data())
                    self.guard.update(color)
                    return color, depth
                except Exception:
                    time.sleep(0.05)
            return None, None
        for _ in range(2):
            ok, color = self.cap.read()
            if ok:
                self.guard.update(color)
                return color, None
        return None, None

    def status_lines(self) -> list:
        """Overlay lines: exposure mode + a loud warning when the colour
        stream is repeating frames (so a broken take is visible while it is
        being recorded, not days later)."""
        lines = []
        if self.exposure is not None:
            if self.exposure.get("auto_exposure") == 1.0:
                mode = "exposure: AUTO"
            elif "manual_exposure_ms" in self.exposure:
                mode = (f"exposure: MANUAL {self.exposure['manual_exposure_ms']} ms "
                        f"(unit unconfirmed) gain {self.exposure.get('gain')}")
            else:
                mode = "exposure: UNKNOWN (could not read camera state)"
            lines.append((mode, 0.5, (200, 200, 200)))
        warn = self.guard.warning(FPS)
        if warn:
            lines.append((warn, 0.7, (0, 0, 255)))
        return lines

    def close(self):
        if self.pipeline is not None:
            try:
                self.pipeline.stop()
            except Exception:
                pass
        if self.cap is not None:
            self.cap.release()

    def switch(self, source: str):
        if source == self.source:
            return self
        self.close()
        return Cam(source)


def overlay(img, lines, recording, countdown, frame_count, elapsed, big=None):
    disp = img.copy()
    y = 26
    for text, scale, color in lines:
        cv2.putText(disp, text, (12, y), cv2.FONT_HERSHEY_SIMPLEX, scale, color, max(1, int(scale * 2)), cv2.LINE_AA)
        y += int(10 + 30 * scale)
    status = "REC" if recording else "READY"
    cv2.circle(disp, (W - 60, 26), 10, (0, 0, 255) if recording else (0, 200, 0), -1)
    cv2.putText(disp, status, (W - 44, 32), cv2.FONT_HERSHEY_SIMPLEX, 0.55, (255, 255, 255), 1, cv2.LINE_AA)
    if recording:
        remain = max(0, countdown - elapsed)
        done = min(1.0, elapsed / max(countdown, 0.01))
        cv2.rectangle(disp, (0, H - 8), (int(W * done), H), (0, 0, 255), -1)
        cv2.putText(disp, f"{remain:4.0f}s  {frame_count} frames", (W - 190, H - 16),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.55, (255, 255, 255), 1, cv2.LINE_AA)
    if big is not None:
        scale = 3.0 if len(big) <= 2 else 2.2
        (tw, th), _ = cv2.getTextSize(big, cv2.FONT_HERSHEY_SIMPLEX, scale, max(2, int(scale)))
        cv2.putText(disp, big, ((W - tw) // 2, (H + th) // 2), cv2.FONT_HERSHEY_SIMPLEX,
                    scale, (0, 255, 255), max(2, int(scale)), cv2.LINE_AA)
    return disp


def record_session(cam: Cam, label: str, seconds: float):
    ts = datetime.datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    out = os.path.join(OUT_ROOT, f"{ts}_{label}")
    rgb_dir = os.path.join(out, "no_boxes")
    os.makedirs(rgb_dir, exist_ok=True)
    depth_dir = os.path.join(out, "depth")
    has_depth = cam.source == "realsense"
    if has_depth:
        os.makedirs(depth_dir, exist_ok=True)
    meta = dict(label=label, source=cam.source, width=W, height=H, fps=FPS,
                depth_scale=cam.depth_scale, started=ts)

    n, stamps = 0, []
    # 3-second countdown before recording starts
    cd_end = time.time() + 3.0
    while time.time() < cd_end:
        color, _ = cam.read()
        if color is None:
            cv2.waitKey(30)
            continue
        remain = int(cd_end - time.time()) + 1
        lines = [(f"[{label}]  recording starts in...", 0.7, (0, 255, 255))]
        cv2.imshow("JuggleHub Recording Wizard",
                   overlay(color, lines, False, seconds, 0, 0, big=str(remain)))
        if cv2.waitKey(1) & 0xFF == ord("q"):
            cv2.destroyAllWindows()
            return None

    t0 = time.time()
    recording = True
    cam.guard.reset()          # count repeats over the recorded frames only
    while True:
        color, depth = cam.read()
        if color is None:
            cv2.waitKey(30)
            continue
        elapsed = time.time() - t0
        lines = [(f"[REC {label}]  recording...", 0.6, (0, 0, 255))]
        lines += cam.status_lines()
        disp = overlay(color, lines, recording, seconds, n, elapsed)

        if recording:
            cv2.imwrite(os.path.join(rgb_dir, f"frame_{n}.jpg"), color,
                        [cv2.IMWRITE_JPEG_QUALITY, 95])
            if depth is not None:
                cv2.imwrite(os.path.join(depth_dir, f"frame_{n}_depth.png"), depth)
            stamps.append(round(time.time() - t0, 4))
            n += 1

        cv2.imshow("JuggleHub Recording Wizard", disp)
        key = cv2.waitKey(1) & 0xFF
        if elapsed >= seconds:
            break
        if key == ord("q"):
            cv2.destroyAllWindows()
            meta.update(frames=n, duration_s=round(elapsed, 2), aborted=True)
            _write_meta(meta, out, stamps, cam)
            return None  # quit signal

    cv2.destroyAllWindows()
    meta["frames"] = n
    meta["duration_s"] = round(sum(stamps[-1:]) if stamps else 0.0, 2)
    meta["effective_fps"] = round(n / max(meta["duration_s"], 0.01), 1)
    _write_meta(meta, out, stamps, cam)
    return out


def _write_meta(meta, out, stamps, cam: Cam | None = None):
    meta["stamps"] = stamps
    if cam is not None:
        # effective_fps above is the LOOP rate; it stays ~30 even when the
        # colour stream repeats frames. These fields are the honest ones.
        meta["rgb_exposure"] = cam.exposure
        meta.update(cam.guard.meta())
        if cam.guard.dup_pct > DUP_INVALID_PCT:
            meta["invalid"] = "repeated colour frames"
            print(f"\n*** WARNING: {cam.guard.dup_pct:.0f}% of the colour frames in "
                  f"this take are repeats ({cam.guard.unique} unique of "
                  f"{cam.guard.total}). The recording is NOT usable for "
                  f"tracking evaluation. Check exposure settings and re-record. ***\n",
                  file=sys.stderr)
    with open(os.path.join(out, "meta.json"), "w") as f:
        json.dump(meta, f, indent=2)


def main(argv=None) -> int:
    os.makedirs(OUT_ROOT, exist_ok=True)
    done_labels = {d.split("_", 2)[-1] for d in os.listdir(OUT_ROOT)
                   if os.path.isdir(os.path.join(OUT_ROOT, d))}
    queue = [s for s in SESSIONS if s[0] not in done_labels]
    if not queue:
        queue = list(SESSIONS)
        print("All sessions already recorded; running through the full list again.")

    # --redo <label>: re-record one specific session (e.g. after config changes)
    if "--redo" in sys.argv:
        i = sys.argv.index("--redo")
        want = sys.argv[i + 1]
        queue = [s for s in SESSIONS if s[0] == want]
        if not queue:
            print(f"no session named {want!r}; options:",
                  [s[0] for s in SESSIONS])
            return 1
        print(f"REDO mode: re-recording only {want!r} "
              f"(new timestamped folder; old one is kept)")

    i = 0
    cam = None
    last_out = None
    try:
        while i < len(queue):
            label, title, notes, source, seconds = queue[i]
            if source == "realsense" and cam is None or (cam and cam.source != source):
                try:
                    cam = cam.switch(source) if cam else Cam(source)
                except Exception as e:
                    if source == "realsense":
                        print(f"WARNING: RealSense unavailable ({e}); "
                              f"falling back to webcam for this session.", file=sys.stderr)
                        source = "webcam"
                        cam = cam.switch(source) if cam else Cam(source)
                    else:
                        raise
            elif cam is None:
                cam = Cam(source)

            # preview / instruction loop
            recording_prompt = False
            proceed = None
            while True:
                color, _depth = cam.read()
                if color is None:
                    cv2.waitKey(30)
                    continue
                lines = [(f"{i + 1}/{len(queue)}  {title}", 0.7, (0, 255, 255))]
                for note in notes:
                    lines.append((note, 0.55, (255, 255, 255)))
                lines.append((f"camera: {source}", 0.5, (200, 200, 200)))
                lines += cam.status_lines()
                lines.append(("SPACE=start   k=skip   r=redo previous   q=quit", 0.55, (0, 255, 0)))
                cv2.imshow("JuggleHub Recording Wizard",
                           overlay(color, lines, False, seconds, 0, 0, big="READY"))
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

            print(f"Recording '{label}' for {seconds}s (auto-stop) ...")
            out = record_session(cam, label, seconds)
            if out is None:
                print("Quit during recording.")
                break
            last_out = out
            print(f"  saved -> {out}")
            # "saved" flash, then straight to the next instruction card
            t_end = time.time() + 2.0
            while time.time() < t_end:
                color, _ = cam.read()
                if color is None:
                    cv2.waitKey(30)
                    continue
                cv2.imshow("JuggleHub Recording Wizard",
                           overlay(color, [("Saved!", 0.7, (0, 255, 0))], False, 0, 0, 0,
                                   big="SAVED"))
                cv2.waitKey(1)
            i += 1
    finally:
        if cam is not None:
            cam.close()

    print("\nDone. Recordings in:", OUT_ROOT)
    if last_out:
        print("Last recording:", last_out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
