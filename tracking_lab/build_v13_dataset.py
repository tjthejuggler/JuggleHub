"""Build the V13 training dataset: v12_clean + shower-heavy frames.

WHY (T11d, 2026-11-13)
Every model trained on v12 (full 640 run, 416/448 fine-tunes) counted
SHOWERS worse than the deployed V8 while being par on cascades. The v12
data contains almost no shower footage, so the fast cross-hand flight
frames a shower produces were never supervised. V13 = v12_clean plus
GLM-labelled frames from the OLD shower recordings only.

CONTAMINATION RULE (deliberate, documented)
Training sessions: the four 2026-11-12/10 shower40 recordings and
columns_shower — all TUNING takes. §3 numbers for those specific takes
become training-contaminated for any v13-trained model. The 2026-11-13
shower50 takes (the current prox backtest set) are NOT used, so the
9-take backtest stays an honest judge.

LABEL QUALITY (lesson from v11: raw GLM boxes were poor — only 39%
overlapped a V8 detection)
Each sampled frame is labelled TWICE by GLM-5.3-flash (temp 0). A box is
kept only if it is stable across the two passes (IoU >= 0.5 with a box
from the other pass, same state). A frame is dropped entirely if the two
passes disagree on the number of balls by more than one, or if no box is
stable. Labels are cached in .tmp/v13_labels.json so the API stage can be
resumed. Shower frames go to TRAIN only, like the v11 GLM frames before.

Usage:
  python3 -m tracking_lab.build_v13_dataset --label            # API stage
  python3 -m tracking_lab.build_v13_dataset --build            # assemble
  python3 -m tracking_lab.build_v13_dataset --label --build    # both
  python3 -m tracking_lab.build_v13_dataset --check            # re-verify
Output: .tmp/v13_dataset/{train,val}/{images,labels} + data.yaml + manifest.json
"""
from __future__ import annotations

import argparse
import base64
import glob
import json
import os
import shutil
import sys
import time
import urllib.request

import cv2

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
TMP = os.path.join(REPO, ".tmp")
V12 = os.path.join(TMP, "v12_clean")
OUT = os.path.join(TMP, "v13_dataset")
CACHE = os.path.join(TMP, "v13_labels.json")
REC_ROOT = os.path.join(HERE, "recordings")
KEY_FILE = os.path.join(HERE, "zai_apikey.txt")
ENDPOINT = "https://api.z.ai/api/coding/paas/v4/chat/completions"
MODEL = "glm-5.3-flash"

# training sources: OLD shower footage only (TUNING takes; contamination is
# accepted and documented — see module docstring). The 2026-11-13 shower50
# takes must NOT appear here.
TRAIN_SESSIONS = [
    "2026-11-10_19-26-34_shower40",
    "2026-11-12_16-27-15_shower40_h1",
    "2026-11-12_17-03-12_shower40_h2",
    "2026-11-12_18-00-55_shower40_h3",
    "2026-11-10_12-33-55_columns_shower",
]
PER_SESSION = 80          # frames sampled per session
MIN_IOU = 0.5             # stability threshold between the two GLM passes
MAX_COUNT_DISAGREE = 1    # drop frame if passes differ by more than this
NAMES = ["ball", "ball_held"]

PROMPT = """Find every juggling ball in this image and output YOLO-format
bounding boxes. A "ball" is: a matte sphere, a glowing LED sphere, OR the
BRIGHT END TIP of an LED light streak (juggling LEDs paint streaks in long
exposures; the ball is at the leading/trailing bright tip). Balls crossing
between hands at speed are still balls — look for small spheres or bright
tips in mid-air, also when slightly motion-blurred. Exclude people, hands,
TV screens, lights fixed to the ceiling/walls, and furniture.

Reply STRICT JSON, nothing else:
{"balls": [{"bbox": [x1, y1, x2, y2], "state": "free"|"held"}]}
Coordinates in pixels, ints. state=held means gripped in a hand."""


# ---------------------------------------------------------------- API stage
def _b64(frame) -> str:
    ok, buf = cv2.imencode(".jpg", frame, [cv2.IMWRITE_JPEG_QUALITY, 90])
    return base64.b64encode(buf).decode()


def ask_boxes(frame) -> list | None:
    body = json.dumps({
        "model": MODEL,
        "messages": [{"role": "user", "content": [
            {"type": "image_url", "image_url": {"url": f"data:image/jpeg;base64,{_b64(frame)}"}},
            {"type": "text", "text": PROMPT}]}],
        "max_tokens": 3000,
        "temperature": 0.0,
        "thinking": {"type": "disabled"},
    }).encode()
    req = urllib.request.Request(ENDPOINT, data=body, headers={
        "Content-Type": "application/json",
        "Authorization": f"Bearer {open(KEY_FILE).read().strip()}"})
    for attempt in range(4):
        try:
            with urllib.request.urlopen(req, timeout=180) as r:
                d = json.loads(r.read())
                if d["choices"][0].get("finish_reason") != "stop":
                    time.sleep(5)
                    continue
                txt = (d["choices"][0]["message"].get("content") or "").strip()
                if "```" in txt:
                    txt = txt.split("```json")[-1].split("```")[0].strip()
                s, e = txt.find("{"), txt.rfind("}")
                if s != -1 and e > s:
                    out = json.loads(txt[s:e + 1])
                    return out.get("balls", [])
        except urllib.error.HTTPError as exc:
            print(f"    HTTP {exc.code} retry {attempt + 1}/4", flush=True)
            time.sleep(8 * (attempt + 1))
        except Exception as exc:
            print(f"    {exc} retry {attempt + 1}/4", flush=True)
            time.sleep(8)
    return None


def _iou(a, b) -> float:
    ax1, ay1, ax2, ay2 = a
    bx1, by1, bx2, by2 = b
    ix = max(0, min(ax2, bx2) - max(ax1, bx1))
    iy = max(0, min(ay2, by2) - max(ay1, by1))
    inter = ix * iy
    ua = (ax2 - ax1) * (ay2 - ay1) + (bx2 - bx1) * (by2 - by1) - inter
    return inter / ua if ua else 0.0


def label_stage() -> dict:
    """Two-pass GLM labelling with stability filter. Cached, resumable."""
    cache = {}
    if os.path.exists(CACHE):
        cache = json.load(open(CACHE))
    done = len(cache)
    for sess in TRAIN_SESSIONS:
        files = sorted(glob.glob(os.path.join(REC_ROOT, sess, "no_boxes", "*.jpg")))
        if not files:
            print(f"{sess}: no frames found, skipping")
            continue
        picks = [files[i] for i in
                 range(0, len(files), max(1, len(files) // PER_SESSION))][:PER_SESSION]
        todo = [f for f in picks if f not in cache]
        print(f"{os.path.basename(sess)}: {len(picks)} sampled, {len(todo)} to label")
        for i, fpath in enumerate(todo):
            img = cv2.imread(fpath)
            if img is None:
                cache[fpath] = None
                continue
            p1, p2 = ask_boxes(img), ask_boxes(img)
            if p1 is None or p2 is None:
                print(f"  [{i + 1}/{len(todo)}] API failed, frame skipped", flush=True)
                cache[fpath] = None
                continue
            n1, n2 = len(p1), len(p2)
            kept, used2 = [], set()
            for b1 in p1:
                for j, b2 in enumerate(p2):
                    if j in used2:
                        continue
                    if _iou(b1["bbox"], b2["bbox"]) >= MIN_IOU:
                        used2.add(j)
                        state = "held" if (b1.get("state") == "held"
                                           and b2.get("state") == "held") else "free"
                        kept.append({"bbox": b1["bbox"], "state": state})
                        break
            ok_frame = abs(n1 - n2) <= MAX_COUNT_DISAGREE and \
                len(kept) >= max(1, min(n1, n2) - 1)
            cache[fpath] = dict(passes=[n1, n2], boxes=kept) if ok_frame else None
            if (i + 1) % 10 == 0:
                print(f"  [{i + 1}/{len(todo)}] kept so far "
                      f"{sum(1 for v in cache.values() if v)}/{len(cache)}", flush=True)
                json.dump(cache, open(CACHE, "w"))          # checkpoint
        json.dump(cache, open(CACHE, "w"))
    kept = sum(1 for v in cache.values() if v)
    print(f"label stage done: {kept} usable frames of {len(cache)} sampled "
          f"({done} were already cached)")
    return cache


# ------------------------------------------------------------- build stage
def _place(img: str, stem: str, rows: list) -> None:
    dst_img = os.path.join(OUT, "train", "images", stem + ".jpg")
    try:
        os.link(img, dst_img)
    except OSError:
        shutil.copy2(img, dst_img)
    h, w = cv2.imread(img).shape[:2]
    with open(os.path.join(OUT, "train", "labels", stem + ".txt"), "w") as f:
        for cls, x, y, bw, bh in rows:
            f.write(f"{cls} {x:.6f} {y:.6f} {bw:.6f} {bh:.6f}\n")


def build_stage() -> int:
    from .build_v12_dataset import verify, _report  # same checks as v12
    if not os.path.isdir(V12):
        print(f"v12 base missing: {V12}\nrun build_v12_dataset first")
        return 1
    cache = json.load(open(CACHE)) if os.path.exists(CACHE) else {}
    if os.path.exists(OUT):
        shutil.rmtree(OUT)
    shutil.copytree(V12, OUT, copy_function=os.link)      # base = v12_clean
    added = dropped = 0
    for fpath, res in sorted(cache.items()):
        if not res:
            dropped += 1
            continue
        sess = os.path.basename(os.path.dirname(os.path.dirname(fpath)))
        if sess not in TRAIN_SESSIONS:
            continue
        stem = "sh_" + os.path.basename(sess) + "_f" + \
            os.path.splitext(os.path.basename(fpath))[0]
        rows = []
        for b in res["boxes"]:
            x1, y1, x2, y2 = b["bbox"]
            cls = 1 if b["state"] == "held" else 0
            rows.append((cls, (x1 + x2) / 2 / 640, (y1 + y2) / 2 / 480,
                         (x2 - x1) / 640, (y2 - y1) / 480))
        _place(fpath, stem, rows)
        added += 1
    with open(os.path.join(OUT, "data.yaml"), "w") as f:
        f.write("train: train/images\nval: val/images\n"
                f"nc: {len(NAMES)}\nnames:\n" + "".join(f"- {n}\n" for n in NAMES))
    problems, stats = verify(OUT, len(NAMES))
    with open(os.path.join(OUT, "manifest.json"), "w") as f:
        json.dump(dict(base="v12_clean", nc=len(NAMES), names=NAMES,
                       shower_train_frames=added, shower_frames_dropped=dropped,
                       label_stability=dict(min_iou=MIN_IOU,
                                            max_count_disagree=MAX_COUNT_DISAGREE,
                                            passes=2),
                       train_sessions=TRAIN_SESSIONS,
                       contamination_note=("shower40/columns_shower takes are TUNING "
                                           "sessions; their future per-model numbers "
                                           "are training-contaminated. 2026-11-13 "
                                           "shower50 takes are NOT in training."),
                       stats=stats, problems=problems), f, indent=1)
    print(f"V13 -> {OUT}  (+{added} shower frames, {dropped} dropped as unstable)")
    return _report(problems, stats)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Build V13 (see module docstring).")
    ap.add_argument("--label", action="store_true", help="run the GLM labelling stage")
    ap.add_argument("--build", action="store_true", help="assemble the dataset")
    ap.add_argument("--check", action="store_true", help="re-verify an existing build")
    args = ap.parse_args(argv)
    if args.check:
        from .build_v12_dataset import verify, _report
        return _report(*verify(OUT, len(NAMES)))
    if not (args.label or args.build):
        ap.print_help()
        return 1
    if args.label:
        label_stage()
    return build_stage() if args.build else 0


if __name__ == "__main__":
    sys.exit(main())
