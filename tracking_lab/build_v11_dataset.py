"""Build the V11 training dataset from tracking_lab recordings.

Sources frames from ALL recorded sessions (15+ videos), pre-labels them with
GLM-5.3-flash vision (bounding boxes for balls incl. LED streak tips), and
writes a YOLO-format dataset with train/val split. Frames are sampled evenly
per video; sessions are tagged so augmentation (blur/LED streak synthesis)
can oversample the failure classes in a later step.

Usage:
  python3 -m tracking_lab.build_v11_dataset --per-video 30   # ~450 frames
  python3 -m tracking_lab.build_v11_dataset --per-video 60   # ~900 frames

Output: .tmp/v11_dataset/{images,labels}/{train,val} + v11_data.yaml
Label classes: 0=ball (any state incl. LED streak tip), 1=held ball.
"""
from __future__ import annotations

import base64
import glob
import json
import os
import random
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
OUT = os.path.join(TMP, "v11_dataset")

PROMPT = """Find every juggling ball in this image and output YOLO-format
bounding boxes. A "ball" is: a matte sphere, a glowing LED sphere, OR the
BRIGHT END TIP of an LED light streak (juggling LEDs paint streaks in long
exposures; the ball is at the leading/trailing bright tip). Exclude people,
hands, TV screens, lights fixed to the ceiling/walls, and furniture.

Reply STRICT JSON, nothing else:
{"balls": [{"bbox": [x1, y1, x2, y2], "state": "free"|"held"}]}
Coordinates in pixels, ints. state=held means gripped in a hand."""


def ask_boxes(frame) -> list | None:
    ok, buf = cv2.imencode(".jpg", frame, [cv2.IMWRITE_JPEG_QUALITY, 90])
    b64 = base64.b64encode(buf).decode()
    body = json.dumps({
        "model": MODEL,
        "messages": [{"role": "user", "content": [
            {"type": "image_url", "image_url": {"url": f"data:image/jpeg;base64,{b64}"}},
            {"type": "text", "text": PROMPT}]}],
        "max_tokens": 3000,
        "temperature": 0.0,
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
                out = json.loads(txt)
                return out.get("balls", [])
        except urllib.error.HTTPError as e:
            print(f"    HTTP {e.code} retry {attempt+1}/3", flush=True)
            time.sleep(8 * (attempt + 1))
        except json.JSONDecodeError:
            print(f"    bad JSON retry {attempt+1}/3", flush=True)
            time.sleep(4)
        except Exception as e:
            print(f"    {e} retry {attempt+1}/3", flush=True)
            time.sleep(8)
    return None


def sessions():
    return sorted(glob.glob(os.path.join(REC_ROOT, "*")))


def main(argv=None) -> int:
    argv = sys.argv[1:] if argv is None else argv
    per_video = 30
    if "--per-video" in argv:
        i = argv.index("--per-video"); per_video = int(argv[i + 1])

    for sub in ("images/train", "images/val", "labels/train", "labels/val"):
        os.makedirs(os.path.join(OUT, sub), exist_ok=True)

    rng = random.Random(42)
    total_done = 0
    sess_dirs = sessions()
    print(f"{len(sess_dirs)} recordings found; sampling {per_video} frames each")

    for si, sess in enumerate(sess_dirs):
        name = os.path.basename(sess.rstrip("/"))
        files = sorted(glob.glob(os.path.join(sess, "no_boxes", "*.jpg")))
        if not files:
            continue
        picks = [files[i] for i in
                 range(0, len(files), max(1, len(files) // per_video))][:per_video]
        print(f"[{si+1}/{len(sess_dirs)}] {name}: {len(picks)} frames")
        for pi, fpath in enumerate(picks):
            img = cv2.imread(fpath)
            if img is None:
                continue
            h, w = img.shape[:2]
            boxes = ask_boxes(img)
            if boxes is None or len(boxes) == 0:
                continue
            split = "train" if rng.random() < 0.85 else "val"
            stem = f"{name}_f{pi:03d}"
            cv2.imwrite(os.path.join(OUT, "images", split, stem + ".jpg"),
                        img, [cv2.IMWRITE_JPEG_QUALITY, 92])
            lines = []
            for b in boxes:
                x1, y1, x2, y2 = b["bbox"]
                x1, y1 = max(0, int(x1)), max(0, int(y1))
                x2, y2 = min(w, int(x2)), min(h, int(y2))
                if x2 - x1 < 8 or y2 - y1 < 8:
                    continue
                cx, cy = (x1 + x2) / 2 / w, (y1 + y2) / 2 / h
                bw, bh = (x2 - x1) / w, (y2 - y1) / h
                cls = 1 if b.get("state") == "held" else 0
                lines.append(f"{cls} {cx:.6f} {cy:.6f} {bw:.6f} {bh:.6f}")
            with open(os.path.join(OUT, "labels", split, stem + ".txt"), "w") as f:
                f.write("\n".join(lines))
            total_done += 1
            if pi % 5 == 4:
                print(f"    {pi+1}/{len(picks)}", flush=True)
            time.sleep(0.3)  # gentle on the API

    yaml = (f"path: {OUT}\n"
            "train: images/train\nval: images/val\nnames:\n"
            "  0: ball\n  1: held_ball\n")
    with open(os.path.join(OUT, "v11_data.yaml"), "w") as f:
        f.write(yaml)
    print(f"\ndone: {total_done} labeled frames -> {OUT}")
    print("train with: yolo detect train data=" +
          os.path.join(OUT, "v11_data.yaml") +
          " model=yolo11n.pt epochs=120 imgsz=640")
    return 0


if __name__ == "__main__":
    sys.exit(main())
