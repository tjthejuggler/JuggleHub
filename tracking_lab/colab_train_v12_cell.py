# V12 training cell for Colab. Plain Python: paste the whole file into one cell.
#
# Upload .tmp/v12_clean.zip (about 0.45 GB) to /content/ first. It is built by
#   python3 -m tracking_lab.build_v12_dataset
#   cd .tmp && zip -q -r v12_clean.zip v12_clean
# Do NOT upload the older v12_dataset.zip (1.88 GB): its yaml declares one class
# while the labels use two, so Ultralytics drops about two thirds of its images,
# and its val split shares recordings and identical files with train.
#
# How to read the result:
#   * val here is split by recording, so no val frame comes from a recording
#     the model trained on. Earlier models (V8..V11) were validated on frames
#     from recordings that were also in train. Their mAP numbers are therefore
#     not comparable with the one printed below; expect this one to be lower.
#   * A higher mAP is not the goal. The goal is catch/throw counts. After
#     downloading the exported model, score it on the counted sessions:
#       TRACKING_BALL_MODEL=<path to best.xml> python3 -m tracking_lab.evaluate
import collections
import glob
import os
import subprocess
import sys
import zipfile

ZIP = "/content/v12_clean.zip"
DS = "/content/v12_clean"
YAML = os.path.join(DS, "data.yaml")

# 1. unpack
if not os.path.exists(YAML):
    if not os.path.exists(ZIP):
        raise SystemExit(f"{ZIP} not found - upload v12_clean.zip to /content/ first")
    with zipfile.ZipFile(ZIP) as z:
        z.extractall("/content/")

# 2. refuse to train if the labels do not match the yaml (the fault that made
#    the first V12 dataset unusable)
with open(YAML) as f:
    nc = next(int(ln.split(":")[1]) for ln in f if ln.startswith("nc:"))
for split in ("train", "val"):
    n_img = len(os.listdir(os.path.join(DS, split, "images")))
    labels = glob.glob(os.path.join(DS, split, "labels", "*.txt"))
    classes = collections.Counter()
    for p in labels:
        with open(p) as f:
            classes.update(int(float(ln.split()[0])) for ln in f if ln.strip())
    print(f"{split}: {n_img} images, {len(labels)} label files, boxes per class {dict(sorted(classes.items()))}")
    if n_img != len(labels) or any(c >= nc for c in classes):
        raise SystemExit(f"{split}: labels do not match data.yaml (nc={nc}) - rebuild the dataset")

# 3. train (same hyper-parameters as the V11 run, so the dataset is the only change)
subprocess.run([sys.executable, "-m", "pip", "-q", "install", "ultralytics"], check=True)
from ultralytics import YOLO

model = YOLO("yolo11n.pt")
model.train(
    data=YAML,
    epochs=120,
    imgsz=640,
    batch=32,
    patience=20,
    hsv_v=0.6, hsv_s=0.7,
    degrees=0, flipud=0.0, fliplr=0.5,
    mosaic=1.0, close_mosaic=10,
    project="v12", name="clean",
)
# Check the "train: ... images, ... backgrounds, ... corrupt" line Ultralytics
# printed above: it must show 0 corrupt.

# 4. validate + export for the OpenVINO pipeline
best = str(model.trainer.best)
metrics = YOLO(best).val(data=YAML)
print("mAP50:", metrics.box.map50, " mAP50-95:", metrics.box.map)
for i, name in metrics.names.items():
    if i in metrics.box.ap_class_index:
        j = list(metrics.box.ap_class_index).index(i)
        print(f"  {name}: mAP50 {metrics.box.ap50[j]:.3f}")
out = YOLO(best).export(format="openvino", imgsz=640)
print("DONE. download:", best, "and the folder", out)
