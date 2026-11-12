"""Auto-annotate verification videos with a vision model (GLM-5.3-flash via
the Z.ai coding-plan endpoint). Replaces the manual click player.

For each queued video the script:
  1. samples frames evenly (default every 0.5 s)
  2. sends each frame (with the tracker overlay) to the model asking:
       - how many real balls are visible
       - for each track circle (#id): does it sit on a real ball, and is the
         HELD/IN_FLIGHT/COAST label plausible?
       - was a catch/throw happening at this frame
  3. aggregates per-video: true catch count estimate, list of bad-state /
     phantom / missed-ball frames with track ids
  4. writes <render>_auto.json next to the render for the tuner

Usage:
  python3 -m tracking_lab.auto_annotate                 # all queued videos
  python3 -m tracking_lab.auto_annotate --only box30    # one video
  python3 -m tracking_lab.auto_annotate --step 15       # denser sampling
"""
from __future__ import annotations

import base64
import glob
import json
import os
import sys
import time
import urllib.request

import cv2

HERE = os.path.dirname(os.path.abspath(__file__))
REC_ROOT = os.path.join(HERE, "recordings")
TMP = os.path.abspath(os.path.join(HERE, "..", ".tmp"))
KEY_FILE = os.path.join(HERE, "zai_apikey.txt")
ENDPOINT = "https://api.z.ai/api/coding/paas/v4/chat/completions"
MODEL = "glm-5.3-flash"

QUEUE = ["box30", "441_30", "shower40", "columns30", "flash_10", "cascade60"]

PROMPT = """You are auditing a juggling-ball tracker. The image shows a video frame with the tracker's overlay: colored circles labeled like "#1 HELD" or "#2 IN_F" mark tracked balls, small gray rectangles are raw detections, red circles are wrist positions.

Look carefully and answer in STRICT JSON (no markdown, no commentary):
{
 "real_balls": <int, number of actual juggling balls visible>,
 "tracks": [
   {"id": <track id int>, "on_real_ball": <true/false>,
    "state_plausible": <true/false>,
    "problem": "<one short phrase or empty>"}
 ],
 "event_now": "<none|catch|throw|carry>",
 "notes": "<anything odd, short>"
}
Rules: an empty hand with a HELD circle on it = on_real_ball:false, state_plausible:false, problem "held on empty hand". A circle on a ceiling light or background object = problem "phantom". A visible ball with no circle = add to notes "missed ball near (x,y)". Judge labels only for circles clearly visible; ignore half-cut ones."""


def b64_image(frame) -> str:
    ok, buf = cv2.imencode(".jpg", frame, [cv2.IMWRITE_JPEG_QUALITY, 90])
    return base64.b64encode(buf).decode()


def ask_model(frame) -> dict | None:
    img = b64_image(frame)
    body = json.dumps({
        "model": MODEL,
        "messages": [{"role": "user", "content": [
            {"type": "image_url", "image_url": {"url": f"data:image/jpeg;base64,{img}"}},
            {"type": "text", "text": PROMPT}]}],
        "max_tokens": 4096,   # reasoning tokens come out of this budget first
        "temperature": 0.1,
    }).encode()
    req = urllib.request.Request(ENDPOINT, data=body, headers={
        "Content-Type": "application/json", "Authorization": f"Bearer {open(KEY_FILE).read().strip()}"})
    for attempt in range(4):
        try:
            with urllib.request.urlopen(req, timeout=180) as r:
                d = json.loads(r.read())
                txt = (d["choices"][0]["message"].get("content") or "").strip()
                # robust extraction: prefer ```json block, then first {...}
                if "```" in txt:
                    txt = txt.split("```json")[-1].split("```")[0].strip()
                start, end = txt.find("{"), txt.rfind("}")
                if start != -1 and end > start:
                    txt = txt[start:end + 1]
                return json.loads(txt)
        except urllib.error.HTTPError as e:
            print(f"    HTTP {e.code}: {e.read()[:120]}; retry {attempt+1}/4")
            time.sleep(10 * (attempt + 1))
        except json.JSONDecodeError:
            print(f"    non-JSON model output: {txt[:120]!r}; retry")
            time.sleep(5)
        except Exception as e:
            print(f"    {e}; retry {attempt+1}/4")
            time.sleep(10)
    return None


def annotate_video(frag: str, step: int) -> dict | None:
    vpath = os.path.join(TMP, f"count_{frag}.mp4")
    tpath = vpath.replace(".mp4", "_tracks.json")
    if not (os.path.exists(vpath) and os.path.exists(tpath)):
        real = None
        dirs = sorted(glob.glob(os.path.join(REC_ROOT, f"*{frag}")))
        if dirs:
            subprocess_run(dirs[-1], vpath)
            real = vpath
        if real is None:
            print(f"no recording for {frag}")
            return None
    cap = cv2.VideoCapture(vpath)
    total = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    fps = cap.get(cv2.CAP_PROP_FPS) or 30.0
    track_data = json.load(open(tpath))
    pf = track_data.get("per_frame", [])

    results = []
    for fidx in range(30, total - 5, step):
        cap.set(cv2.CAP_PROP_POS_FRAMES, fidx)
        ok, frame = cap.read()
        if not ok:
            continue
        t0 = time.time()
        out = ask_model(frame)
        if out is None:
            print(f"  [{frag}] frame {fidx}: FAILED")
            continue
        info = pf[fidx] if fidx < len(pf) else {}
        results.append(dict(frame=fidx, model=out,
                            tracker=dict(ids=info.get("ids", []),
                                         states=info.get("states", []),
                                         pos=info.get("pos", []),
                                         wrists=info.get("wrists", {}))))
        nb = out.get("real_balls", "?")
        probs = [t.get("problem") for t in out.get("tracks", []) if t.get("problem")]
        print(f"  [{frag}] frame {fidx}: balls={nb} problems={probs or '-'} ({time.time()-t0:.0f}s)")
    cap.release()

    # aggregate: catch/throw moments + error list
    events_seen = [r["frame"] for r in results if str(r["model"].get("event_now", "")).lower() in ("catch", "throw", "carry")]
    bad = []
    for r in results:
        for t in r["model"].get("tracks", []):
            if t.get("problem") or t.get("on_real_ball") is False:
                bad.append(dict(frame=r["frame"], track=t.get("id"),
                                problem=t.get("problem", "not on ball")))
    return dict(video=frag, sampled=len(results),
                event_frames=events_seen,
                bad_tracks=bad,
                samples=results)


def subprocess_run(rec_dir, vpath):
    subprocess.run([sys.executable, "-m", "tracking_lab", "--rec", rec_dir,
                    "--render", vpath, "--quiet"], check=True, capture_output=True)


def _sample_frames(frag: str, step: int):
    """-> [(fidx, b64img, tracker_info)] or None."""
    import json as _json
    vpath = os.path.join(TMP, f"count_{frag}.mp4")
    tpath = vpath.replace(".mp4", "_tracks.json")
    if not (os.path.exists(vpath) and os.path.exists(tpath)):
        dirs = sorted(glob.glob(os.path.join(REC_ROOT, f"*{frag}")))
        if not dirs:
            return None
        subprocess_run(dirs[-1], vpath)
    cap = cv2.VideoCapture(vpath)
    total = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    track_data = _json.load(open(tpath))
    pf = track_data.get("per_frame", [])
    out = []
    for fidx in range(30, total - 5, step):
        cap.set(cv2.CAP_PROP_POS_FRAMES, fidx)
        ok, frame = cap.read()
        if not ok:
            continue
        info = pf[fidx] if fidx < len(pf) else {}
        out.append((fidx, b64_image(frame),
                    dict(ids=info.get("ids", []), states=info.get("states", []),
                         pos=info.get("pos", []), wrists=info.get("wrists", {}))))
    cap.release()
    return out


def _one_request(args_tuple):
    frag, fidx, img, tracker = args_tuple
    body = json.dumps({
        "model": MODEL,
        "messages": [{"role": "user", "content": [
            {"type": "image_url", "image_url": {"url": f"data:image/jpeg;base64,{img}"}},
            {"type": "text", "text": PROMPT}]}],
        "max_tokens": 4096,
        "temperature": 0.1,
        "thinking": {"type": "disabled"},   # skip reasoning: faster, no truncation
    }).encode()
    req = urllib.request.Request(ENDPOINT, data=body, headers={
        "Content-Type": "application/json",
        "Authorization": f"Bearer {open(KEY_FILE).read().strip()}"})
    for attempt in range(5):
        try:
            with urllib.request.urlopen(req, timeout=180) as r:
                d = json.loads(r.read())
                msg = d["choices"][0]["message"]
                finish = d["choices"][0].get("finish_reason")
                txt = (msg.get("content") or "").strip()
                if "```" in txt:
                    txt = txt.split("```json")[-1].split("```")[0].strip()
                s, e = txt.find("{"), txt.rfind("}")
                if s != -1 and e > s:
                    txt = txt[s:e + 1]
                out = json.loads(txt)
                if finish != "stop":
                    print(f"  {frag} f{fidx}: finish={finish} (retry "
                          f"{attempt+1}/5)", flush=True)
                    time.sleep(5)
                    continue
                return (frag, fidx, tracker, out)
        except urllib.error.HTTPError as e:
            print(f"  {frag} f{fidx}: HTTP {e.code} (retry {attempt+1}/5)",
                  flush=True)
            time.sleep(8 * (attempt + 1))
        except json.JSONDecodeError:
            print(f"  {frag} f{fidx}: bad JSON finish={finish} "
                  f"content={txt[:150]!r} (retry {attempt+1}/5)", flush=True)
            time.sleep(4)
        except Exception as e:
            print(f"  {frag} f{fidx}: {e} (retry {attempt+1}/5)", flush=True)
            time.sleep(8)
    return (frag, fidx, tracker, None)


def main(argv=None) -> int:
    from concurrent.futures import ThreadPoolExecutor
    step = 15
    workers = 3
    only = None
    args = sys.argv[1:]
    if "--step" in args:
        i = args.index("--step"); step = int(args[i + 1]); args = args[:i] + args[i + 2:]
    if "--workers" in args:
        i = args.index("--workers"); workers = int(args[i + 1]); args = args[:i] + args[i + 2:]
    if "--only" in args:
        i = args.index("--only"); only = args[i + 1]; args = args[:i] + args[i + 2:]

    queue = [only] if only else QUEUE
    work, exp_map = [], {}
    for frag in queue:
        out_path = os.path.join(TMP, f"count_{frag}_auto.json")
        if os.path.exists(out_path):
            print(f"{frag}: already annotated, skipping")
            continue
        samples = _sample_frames(frag, step)
        if samples is None:
            print(f"{frag}: no recording found")
            continue
        exp_map[frag] = {"box30": 30, "441_30": 30, "shower40": 40,
                         "columns30": 30, "flash_10": 30,
                         "cascade60": 60}.get(frag, 0)
        work.extend([(frag, f, img, tr) for f, img, tr in samples])
        print(f"{frag}: {len(samples)} frames queued (expect ~{exp_map[frag]} catches)")

    print(f"total {len(work)} requests, {workers} parallel workers", flush=True)
    results = {}
    t0 = time.time()
    with ThreadPoolExecutor(max_workers=workers) as ex:
        for n_done, (frag, fidx, tracker, out) in enumerate(
                ex.map(_one_request, work), 1):
            results.setdefault(frag, []).append((fidx, tracker, out))
            if n_done % 10 == 0 or n_done == len(work):
                rate = n_done / max(time.time() - t0, 1)
                print(f"  {n_done}/{len(work)} ({rate:.2f}/s, "
                      f"eta {int((len(work) - n_done) / max(rate, 0.01))} s)",
                      flush=True)

    for frag, items in results.items():
        items.sort(key=lambda r: r[0])
        event_frames = [f for f, _tr, out in items
                        if out and str(out.get("event_now", "")).lower()
                        in ("catch", "throw", "carry")]
        bad = []
        for f, _tr, out in items:
            if not out:
                continue
            for t in out.get("tracks", []):
                if t.get("problem") or t.get("on_real_ball") is False:
                    bad.append(dict(frame=f, track=t.get("id"),
                                    problem=t.get("problem", "not on ball")))
        res = dict(video=frag, expected_catches=exp_map.get(frag, 0),
                   sampled=len(items), event_frames=event_frames,
                   bad_tracks=bad,
                   samples=[dict(frame=f, model=out, tracker=tr)
                            for f, tr, out in items])
        out_path = os.path.join(TMP, f"count_{frag}_auto.json")
        with open(out_path, "w") as f:
            json.dump(res, f, indent=2)
        print(f"{frag}: saved {len(items)} samples, {len(bad)} flagged, "
              f"{len(event_frames)} event frames -> {out_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
