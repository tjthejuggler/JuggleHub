"""Colab cell: fine-tune the deployed ball model AT 416 px (resolution-matched
training), then export to OpenVINO.

WHY: the plain 416 re-export of our V8 weights is fast (17.6 ms vs 24.7 on the
local CPU, measured 2026-11-13) but inaccurate END-TO-END because the weights
were trained at 640 — small far balls and the in-flight/held split degrade.
Fine-tuning at the target resolution lets the model adapt; accuracy must be
re-judged locally (12-take prox backtest) before adoption. Expected CPU win if
accuracy holds: ~7 ms/frame on the ball net.

BEFORE RUNNING (on your machine, from the repo root):
  1. Upload to Colab (folder icon -> upload):
     - .tmp/v11_dataset.zip        (47 MB: 470 train / 89 val, classes ball/held_ball)
     - engine/models/zipped/V8_balls_held/best.pt   (the deployed V8 weights)
  2. Runtime -> Change runtime type -> T4 GPU (or better).

Then copy this ENTIRE file into ONE Colab cell and run. Downloads:
  v8_416ft.zip  (contains best.pt + OpenVINO export for the 416 fine-tune)
  v8_448ft.zip  (same for 448, the safer-accuracy fallback; ~19-20 ms expected)
"""

# ---- cell 1: setup + unzip ------------------------------------------------
!pip install -q ultralytics

import zipfile, os, yaml, shutil

with zipfile.ZipFile("/content/v11_dataset.zip") as z:
    z.extractall("/content")

# fix the yaml path (it was written with the local absolute path)
yaml_path = "/content/v11_dataset/v11_data.yaml"
txt = open(yaml_path).read().replace(
    "path: /home/twain/Projects/JuggleHub/.tmp/v11_dataset",
    "path: /content/v11_dataset")
open(yaml_path, "w").write(txt)
print(open(yaml_path).read())   # must show path: /content/v11_dataset

TEACHER = "/content/best.pt"    # the V8 weights you uploaded
assert os.path.isfile(TEACHER), "upload engine/models/zipped/V8_balls_held/best.pt first!"

# ---- cell 2: train at 416 and 448 ------------------------------------------
from ultralytics import YOLO

# Same augmentation policy as the v11 training cell (proven for our data):
# strong value/saturation aug (LED glow, dim room, ball colours), horizontal
# flip, no rotation (gravity matters), mosaic with late close-out.
AUG = dict(hsv_v=0.6, hsv_s=0.7, degrees=0, flipud=0.0, fliplr=0.5,
           mosaic=1.0, close_mosaic=10)

for size, epochs, lr in ((416, 60, 0.004), (448, 60, 0.004)):
    run = f"v8_{size}ft"
    model = YOLO(TEACHER)                      # fine-tune from deployed V8
    model.train(data=yaml_path, epochs=epochs, imgsz=size, batch=32,
                patience=20, lr0=lr, project=run, name="run1", **AUG)
    m = YOLO(f"{run}/run1/weights/best.pt").val(data=yaml_path, imgsz=size)
    print(f"[{run}] mAP50 {m.box.map50:.3f}  mAP50-95 {m.box.map:.3f}")

# ---- cell 3: export both to OpenVINO and package ---------------------------
from google.colab import files as gfiles

for size in (416, 448):
    run = f"v8_{size}ft"
    YOLO(f"{run}/run1/weights/best.pt").export(format="openvino", imgsz=size)
    shutil.make_archive(run, "zip", f"{run}/run1/weights")
    print(f"downloading {run}.zip ...")
    gfiles.download(f"{run}.zip")

print("DONE. On your machine: unzip into .tmp/quant/<name>/ and tell Roo - "
      "the local 12-take prox backtest decides adoption, never the mAP alone.")
