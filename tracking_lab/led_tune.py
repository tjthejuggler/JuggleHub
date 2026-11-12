"""AI-guided LED camera-settings tuner.

You juggle; every ~3 s a frame (tracker overlay included) is sent to GLM-5.3-
flash, which judges tracking quality on THIS frame:
  - how many balls are actually visible
  - how many have correct track circles
  - are there white streaks / phantom circles
It returns a quality score; the tuner uses those scores to hill-climb over
exposure/gain settings: it tries neighbors of the current best and moves to
whatever the model rates highest. At the end, the best setting is reported
and saved to .tmp/led_tune_results.json.

  python3 -m tracking_lab.led_tune [--budget 20]   # ~20 LLM samples total

You don't need to do anything but juggle a steady cascade.

HISTORY (2026-11-12 audit) — the earlier run's conclusions are not reliable.
The old SETTINGS passed 100..350 to the RGB exposure option, named
"100us".."350us", with the comment "auto runs ~400us in this room". Two
problems:
  * If the RGB exposure unit is 100 us (inferred from repeat-frame rates on
    other clips, NOT confirmed on the device — see camera.py), those values
    were 10..35 ms, i.e. the same length as or longer than auto, not shorter.
    This tuner saved no frames, so that cannot be checked after the fact.
  * Whatever the unit, the result file holds 1-2 LLM judgments per setting
    (.tmp/led_tune_results.json). That is too few to rank settings, and the
    "400us/gain64 optimum" quoted elsewhere is not in that file at all.
Settings are now stated in real milliseconds via camera.configure_rgb, auto
exposure is restored on exit, and repeated colour frames are flagged on
screen. NOT yet run on a camera; the gains are untested starting points.
"""
from __future__ import annotations

import base64
import json
import os
import sys
import time
import urllib.request

import cv2
import numpy as np

import pyrealsense2 as rs

from .camera import FrameGuard, configure_rgb

HERE = os.path.dirname(os.path.abspath(__file__))
TMP = os.path.abspath(os.path.join(HERE, "..", ".tmp"))
KEY_FILE = os.path.join(HERE, "zai_apikey.txt")
ENDPOINT = "https://api.z.ai/api/coding/paas/v4/chat/completions"
MODEL = "glm-5.3-flash"

W, H, FPS = 640, 480, 30

# hill-climb neighborhood: name -> (exposure in REAL milliseconds, gain)
# Shorter exposure = shorter LED trails; gain rises to compensate brightness.
# Untested starting points (see module docstring).
SETTINGS = {
    "auto":      (None, None),
    "8ms_g16":   (8.0, 16),
    "4ms_g32":   (4.0, 32),
    "2ms_g64":   (2.0, 64),
    "1ms_g128":  (1.0, 128),
    "0.5ms_g128": (0.5, 128),
}

PROMPT = """You are judging a juggling-ball tracker's overlay on this frame.
The overlay: colored circles labeled like "#1 HELD" track balls; red circles
mark wrists. The balls are glowing LEDs (bright colored dots; at bad camera
settings they smear into long white streaks).

Reply in STRICT JSON only:
{
 "visible_balls": <int, actual glowing balls you can see>,
 "correctly_tracked": <int, balls with a circle correctly on them>,
 "phantom_circles": <int, circles on empty hands/face/background>,
 "streaks": <true if bright white light-streaks are present>,
 "quality": <0-10 score: 10 = every visible ball correctly circled, no
             streaks, no phantom circles; 0 = tracking totally failed>
}
Be strict: missing a visible ball or any phantom circle costs points."""


def ask_model(frame) -> dict | None:
    ok, buf = cv2.imencode(".jpg", frame, [cv2.IMWRITE_JPEG_QUALITY, 85])
    b64 = base64.b64encode(buf).decode()
    body = json.dumps({
        "model": MODEL,
        "messages": [{"role": "user", "content": [
            {"type": "image_url", "image_url": {"url": f"data:image/jpeg;base64,{b64}"}},
            {"type": "text", "text": PROMPT}]}],
        "max_tokens": 2000,
        "temperature": 0.1,
    }).encode()
    req = urllib.request.Request(ENDPOINT, data=body, headers={
        "Content-Type": "application/json",
        "Authorization": f"Bearer {open(KEY_FILE).read().strip()}"})
    for attempt in range(3):
        try:
            with urllib.request.urlopen(req, timeout=180) as r:
                d = json.loads(r.read())
                txt = (d["choices"][0]["message"].get("content") or "").strip()
                if "```" in txt:
                    txt = txt.split("```json")[-1].split("```")[0].strip()
                s, e = txt.find("{"), txt.rfind("}")
                if s != -1 and e > s:
                    txt = txt[s:e + 1]
                return json.loads(txt)
        except urllib.error.HTTPError as e:
            print(f"    HTTP {e.code} retry {attempt+1}/3", flush=True)
            time.sleep(6 * (attempt + 1))
        except json.JSONDecodeError:
            print(f"    bad JSON retry {attempt+1}/3", flush=True)
            time.sleep(3)
        except Exception as e:
            print(f"    {e} retry {attempt+1}/3", flush=True)
            time.sleep(6)
    return None


def apply_setting(profile, name):
    expo_ms, gain = SETTINGS[name]
    state = configure_rgb(profile, expo_ms, gain, FPS)
    return not state["errors"]


def main() -> int:
    budget = 20
    args = sys.argv[1:]
    if "--budget" in args:
        budget = int(args[args.index("--budget") + 1])

    pipe = rs.pipeline()
    cfg = rs.config()
    cfg.enable_stream(rs.stream.color, W, H, rs.format.bgr8, FPS)
    profile = pipe.start(cfg)

    cv2.namedWindow("AI LED Tuner", cv2.WINDOW_NORMAL)
    cv2.resizeWindow("AI LED Tuner", 960, 720)

    scores = {}           # name -> list of model quality scores
    current = "auto"
    apply_setting(profile, current)
    tried_since_switch = 0

    print(f"AI-guided LED tuner: {budget} LLM judgments. Just juggle a "
          "steady cascade — the AI steers the settings itself.")

    # Only the colour stream is enabled here, so a too-long exposure shows up
    # as a LOW ARRIVAL RATE rather than as repeated frames. Track both.
    guard = FrameGuard()
    arrivals: list = []       # arrival times of the last ~30 colour frames
    rates: dict = {}          # setting -> lowest colour fps seen

    try:
        from concurrent.futures import ThreadPoolExecutor
        pool = ThreadPoolExecutor(max_workers=2)
        n = 0
        pending = None          # (current, future) of the in-flight judgment
        last_click = ""
        last_judgment_t = 0.0   # fire the first judgment immediately
        while n < budget:
            # async judgment: fire when no request is in flight and 3 s passed
            if pending is None and time.time() - last_judgment_t > 3.0:
                frameset = pipe.wait_for_frames(timeout_ms=4000)
                color = np.asanyarray(frameset.get_color_frame().get_data())
                img = color.copy()
                pending = (current, color.copy(),
                           pool.submit(ask_model, img))
                n += 1
                print(f"[{n}/{budget}] judging {current}... (video stays live)",
                      flush=True)

            frameset = pipe.poll_for_frames()
            got_frame = bool(frameset)
            color_fps = None
            if got_frame:
                color = np.asanyarray(frameset.get_color_frame().get_data())
                if guard.update(color):
                    arrivals.append(time.time())
                    del arrivals[:-30]
                if len(arrivals) >= 15:
                    color_fps = (len(arrivals) - 1) / max(
                        arrivals[-1] - arrivals[0], 1e-6)
                    rates[current] = min(rates.get(current, 1e9), color_fps)

            # collect a finished judgment if any
            if pending is not None and pending[2].done():
                pcur, _img, fut = pending
                out = fut.result()
                q = None
                if out:
                    try:
                        q = float(out.get("quality"))
                    except (TypeError, ValueError):
                        q = None
                if q is not None:
                    scores.setdefault(pcur, []).append(q)
                    last_click = (f"AI: {out.get('visible_balls')} balls, "
                                  f"{out.get('correctly_tracked')} tracked, "
                                  f"quality {q}/10  [{pcur}]")
                    print(f"    quality={q}/10  "
                          f"visible={out.get('visible_balls')} "
                          f"tracked={out.get('correctly_tracked')} "
                          f"streaks={out.get('streaks')}", flush=True)
                else:
                    print("    no score; will retry", flush=True)
                pending = None
                last_judgment_t = time.time()

                # hill-climb right after a judgment lands
                tried = {s: float(np.mean(v)) for s, v in scores.items() if v}
                untried = [s for s in SETTINGS if s not in scores]
                best = max(tried, key=tried.get) if tried else current
                if tried.get(current, 999) < 7 and len(
                        scores.get(current, [])) >= 2:
                    if untried:
                        current = untried[0]
                    elif best != current:
                        current = best
                    apply_setting(profile, current)
                    arrivals.clear()      # rate is per setting
                    print(f"  switching to {current}", flush=True)

            if got_frame:
                lines = [f"setting: {current}   judgments {n}/{budget}"]
                if color_fps is not None and color_fps < 0.85 * FPS:
                    lines.append(f"BAD STREAM: colour at {color_fps:.0f} fps "
                                 f"(want {FPS})")
                if current in scores and scores[current]:
                    lines.append(f"avg quality here: "
                                 f"{np.mean(scores[current]):.1f}/10")
                if pending is not None:
                    lines.append("AI judging...")
                if last_click:
                    lines.append(last_click)
                for li, line in enumerate(lines):
                    cv2.putText(color, line, (12, 40 + li * 34),
                                cv2.FONT_HERSHEY_SIMPLEX, 0.8,
                                (0, 255, 255) if li == 0 else
                                ((0, 255, 0) if li == len(lines) - 1 else
                                 (255, 255, 255)), 2)
                cv2.imshow("AI LED Tuner", color)
            if cv2.waitKey(1) & 0xFF == ord("q"):
                raise KeyboardInterrupt
            time.sleep(0.01)

    except KeyboardInterrupt:
        print("\nstopped by user")
    finally:
        # never leave the camera in manual exposure: later recordings would
        # silently inherit it
        try:
            configure_rgb(profile, None, None, FPS)
        except Exception as e:
            print(f"WARNING: could not restore auto exposure: {e}", file=sys.stderr)
        pipe.stop()
        cv2.destroyAllWindows()

    summary = {k: dict(mean=float(np.mean(v)), samples=len(v),
                       min_color_fps=(round(rates[k], 1) if k in rates else None))
               for k, v in scores.items()}
    out = os.path.join(TMP, "led_tune_results.json")
    with open(out, "w") as f:
        json.dump(summary, f, indent=2)
    slow = [k for k, v in summary.items()
            if v["min_color_fps"] is not None and v["min_color_fps"] < 0.85 * FPS]
    if slow:
        print("DISQUALIFIED (colour stream below frame rate):", ", ".join(slow))
    usable = {k: v for k, v in summary.items() if k not in slow}
    if usable:
        best = max(usable.items(), key=lambda kv: kv[1]["mean"])
        print(f"\nHighest mean score: {best[0]} (quality {best[1]['mean']:.1f}/10, "
              f"{best[1]['samples']} judgments)")
        if best[1]["samples"] < 5:
            print("  NOTE: fewer than 5 judgments for that setting — this is "
                  "not enough to call it better than the others.")
    print("saved ->", out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
