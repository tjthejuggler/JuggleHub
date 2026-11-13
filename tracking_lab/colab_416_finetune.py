"""Colab cell: train on the V12-CLEAN dataset (never yet trained) —
(a) a full 640 run that finally tests whether v12 beats the deployed V8,
(b) 416 and 448 resolution-matched fine-tunes for the lighter/faster model.

WHY 416: the plain 416 re-export of the V8 weights is fast (17.6 ms vs 24.7 on
the local CPU, measured 2026-11-13) but inaccurate end-to-end because the
weights were trained at 640. Fine-tuning AT the target resolution lets the
model adapt. Adoption is decided locally by the 12-take prox backtest, never
by mAP alone.

BEFORE RUNNING (on your machine, from the repo root):
  1. Upload to Colab (folder icon -> upload):
     - .tmp/v12_clean.zip  (415 MB: 4103 train / 887 val, classes ball/ball_held)
     - engine/models/zipped/V8_balls_held/best.pt  (deployed V8 weights)
  2. Runtime -> Change runtime type -> T4 GPU (or better). Full session ~2-3 h.

Copy this ENTIRE file into ONE Colab cell and run. It auto-downloads:
  v12_640.zip, v8v12_416ft.zip, v8v12_448ft.zip
"""

# ---- setup: unzip + verify ------------------------------------------------
!pip install -q ultralytics

import zipfile, os

with zipfile.ZipFile("/content/v12_clean.zip") as z:
    z.extractall("/content")

yaml_path = "/content/v12_clean/data.yaml"
print(open(yaml_path).read())   # relative paths (train/images, val/images)
assert os.path.isdir("/content/v12_clean/train/images")
n_tr = len(os.listdir("/content/v12_clean/train/images"))
n_va = len(os.listdir("/content/v12_clean/val/images"))
print(f"v12_clean: {n_tr} train / {n_va} val images")   # expect 4103 / 887

TEACHER = "/content/best.pt"    # the V8 weights you uploaded
assert os.path.isfile(TEACHER), "upload engine/models/zipped/V8_balls_held/best.pt first!"

# ---- train ------------------------------------------------------------------
from ultralytics import YOLO

# Proven augmentation policy for our data: strong value/sat aug (LED glow,
# dim room, ball colours), h-flip, no rotation (gravity matters), mosaic.
AUG = dict(hsv_v=0.6, hsv_s=0.7, degrees=0, flipud=0.0, fliplr=0.5,
           mosaic=1.0, close_mosaic=10)

# (a) THE V12 TEST: full 640 training from COCO-pretrained nano. If this
#     beats V8 in the local backtest, v12 becomes the new default weights.
m = YOLO("yolo11n.pt")
m.train(data=yaml_path, epochs=150, imgsz=640, batch=32, patience=25,
        project="v12_640", name="run1", **AUG)

# (b)/(c) RESOLUTION-MATCHED FINE-TUNES from the deployed V8 weights: the
#     lighter-model candidates. Low LR: adapt, don't forget.
for size in (416, 448):
    run = f"v8v12_{size}ft"
    model = YOLO(TEACHER)
    model.train(data=yaml_path, epochs=60, imgsz=size, batch=32,
                patience=20, lr0=0.004, project=run, name="run1", **AUG)

# ---- validate + export + download --------------------------------------------
# Ultralytics may save under <project>/<name> OR runs/detect/<project>/<name>
# depending on cwd/version — locate each run's weights dir by glob instead of
# assuming the path.
from google.colab import files as gfiles
import glob, shutil

def find_weights(name):
    for pat in (f"/content/{name}/run1/weights", f"{name}/run1/weights",
                f"/content/runs/detect/{name}/run1/weights",
                f"runs/detect/{name}/run1/weights"):
        if os.path.isfile(os.path.join(pat, "best.pt")):
            return pat
    raise FileNotFoundError(f"best.pt for {name} not found under /content")

for name, size in (("v12_640", 640), ("v8v12_416ft", 416), ("v8v12_448ft", 448)):
    wdir = find_weights(name)
    print(f"[{name}] weights at {wdir}")
    met = YOLO(os.path.join(wdir, "best.pt")).val(data=yaml_path, imgsz=size)
    print(f"[{name}] mAP50 {met.box.map50:.3f}  mAP50-95 {met.box.map:.3f}")
    YOLO(os.path.join(wdir, "best.pt")).export(format="openvino", imgsz=size)
    shutil.make_archive(name, "zip", wdir)
    print(f"downloading {name}.zip ...")
    gfiles.download(f"{name}.zip")

print("DONE. Put the three zips in the repo (e.g. Downloads) and tell Roo: "
      "local 12-take prox backtest + section-3 suite decide adoption.")
