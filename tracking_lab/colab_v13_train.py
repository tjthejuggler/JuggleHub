"""Colab cell: train on the V13 dataset (v12_clean + 333 shower-heavy frames).

WHY V13 (T11d verdict, 2026-11-13): every v12-trained model was par on
cascades but LOST 2-10 catches per shower take vs the deployed V8 — v12 had
no shower footage. V13 adds two-pass-stability-filtered GLM labels from the
five OLD shower recordings (shower40 x4 + columns_shower). The 2026-11-13
shower50 takes are NOT in training, so the local 9-take prox backtest stays
an honest judge.

Two runs, both judged ONLY by the local backtest (never by val mAP):
  (a) v8v13_640ft — fine-tune from the deployed V8 best.pt at 640. This is
      the candidate with the best prior: keeps everything V8 knows, adds
      shower supervision.
  (b) v13_640     — full train from COCO yolo11n.pt. Control arm.

BEFORE RUNNING (on your machine):
  1. Upload to Colab (folder icon -> upload):
     - .tmp/v13_dataset.zip  (441 MB: 4436 train / 887 val)
     - engine/models/zipped/V8_balls_held/best.pt  (deployed V8 weights)
  2. Runtime -> Change runtime type -> T4 GPU (or better). ~2-3 h total.

Copy this ENTIRE file into ONE Colab cell and run. It auto-downloads:
  v8v13_640ft.zip, v13_640.zip
"""

# ---- setup -------------------------------------------------------------------
!pip install -q ultralytics

import zipfile, os

with zipfile.ZipFile("/content/v13_dataset.zip") as z:
    z.extractall("/content")

yaml_path = "/content/v13_dataset/data.yaml"
print(open(yaml_path).read())
assert os.path.isdir("/content/v13_dataset/train/images")
n_tr = len(os.listdir("/content/v13_dataset/train/images"))
n_va = len(os.listdir("/content/v13_dataset/val/images"))
print(f"v13: {n_tr} train / {n_va} val images")   # expect 4436 / 887

TEACHER = "/content/best.pt"    # the V8 weights you uploaded
assert os.path.isfile(TEACHER), "upload engine/models/zipped/V8_balls_held/best.pt first!"

# ---- train -------------------------------------------------------------------
from ultralytics import YOLO

AUG = dict(hsv_v=0.6, hsv_s=0.7, degrees=0, flipud=0.0, fliplr=0.5,
           mosaic=1.0, close_mosaic=10)

# (a) THE CANDIDATE: fine-tune the deployed V8 on v13. Low LR: adapt, forget
#     nothing. The shower frames are a small fraction, so 60 epochs is enough.
model = YOLO(TEACHER)
model.train(data=yaml_path, epochs=60, imgsz=640, batch=32, patience=20,
            lr0=0.004, project="v8v13_640ft", name="run1", **AUG)

# (b) CONTROL ARM: full train from COCO-pretrained nano on the same data.
m = YOLO("yolo11n.pt")
m.train(data=yaml_path, epochs=150, imgsz=640, batch=32, patience=25,
        project="v13_640", name="run1", **AUG)

# ---- validate + export + download ---------------------------------------------
from google.colab import files as gfiles
import glob, shutil

def find_weights(name):
    for pat in (f"/content/{name}/run1/weights", f"{name}/run1/weights",
                f"/content/runs/detect/{name}/run1/weights",
                f"runs/detect/{name}/run1/weights"):
        if os.path.isfile(os.path.join(pat, "best.pt")):
            return pat
    raise FileNotFoundError(f"best.pt for {name} not found under /content")

for name in ("v8v13_640ft", "v13_640"):
    wdir = find_weights(name)
    print(f"[{name}] weights at {wdir}")
    best = os.path.join(wdir, "best.pt")
    met = YOLO(best).val(data=yaml_path, imgsz=640)
    print(f"[{name}] mAP50 {met.box.map50:.3f}  mAP50-95 {met.box.map:.3f}")
    YOLO(best).export(format="openvino", imgsz=640)
    shutil.make_archive(name, "zip", wdir)
    print(f"downloading {name}.zip ...")
    gfiles.download(f"{name}.zip")

print("DONE. Bring v8v13_640ft.zip and v13_640.zip to the PC; Roo validates "
      "with the 9-take prox backtest (adoption only if error does not grow).")
