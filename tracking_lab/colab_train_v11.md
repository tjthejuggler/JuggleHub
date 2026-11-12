# V11 Training on Google Colab

Train the V11 ball detector on Google Colab (GPU) using the dataset built by
`tracking_lab/build_v11_dataset.py`. When the dataset build finishes locally,
zip it and upload.

## Step 1 — zip the dataset locally (after the build completes)

```bash
cd /home/twain/Projects/JuggleHub/.tmp
zip -r v11_dataset.zip v11_dataset/
```

## Step 2 — Colab notebook (new notebook, GPU runtime: Runtime → Change runtime type → T4 GPU)

Paste these cells in order:

### Cell 1: upload the zip
```python
from google.colab import files
uploaded = files.upload()   # pick v11_dataset.zip
```

### Cell 2: unzip + install ultralytics
```python
!unzip -q v11_dataset.zip -d /content/
!pip -q install ultralytics
```

### Cell 3: train (~1-1.5 h on T4)
```python
# fix the yaml path first (it was written with Roo's local absolute path)
yaml_path = "/content/v11_dataset/v11_data.yaml"
txt = open(yaml_path).read()
txt = txt.replace("path: /home/twain/Projects/JuggleHub/.tmp/v11_dataset",
                  "path: /content/v11_dataset")
open(yaml_path, "w").write(txt)
print(txt)   # should now show: path: /content/v11_dataset

from ultralytics import YOLO
model = YOLO("yolo11n.pt")   # COCO-pretrained nano; transfer to our 2 classes
model.train(
    data=yaml_path,
    epochs=150,
    imgsz=640,
    batch=32,
    patience=25,          # early-stop if no improvement
    # augmentation tuned for our failure modes:
    hsv_v=0.6,            # strong brightness variation (LED glow / dim room)
    hsv_s=0.7,            # saturation (white vs colored balls)
    degrees=0,            # no rotation: gravity matters
    flipud=0.0, fliplr=0.5,
    mosaic=1.0, close_mosaic=10,
)
# NOTE: 'blur' is not a built-in YOLO arg (ultralytics rejects it). Motion
# blur robustness comes instead from the dataset itself: the training frames
# are sourced from real recordings that already contain blur + LED streaks.
```

### Cell 4: validate on the val split
```python
metrics = model.val()
print(metrics.box.map50, metrics.box.map)
```

### Cell 5: download the weights
```python
from google.colab import files
files.download("runs/detect/train/weights/best.pt")
```

## Step 3 — local deployment (give the downloaded best.pt to Roo)

Put `best.pt` in the repo root; Roo will:
1. export to OpenVINO (`yolo export format=openvino`),
2. point `tracking_lab/detectors.py` at the new model behind a flag,
3. run the 6-video regression suite + glowing-session count vs your 216,
4. keep V11 only if it beats V8's numbers, else revert (scorecard protocol).

## Notes

- The dataset uses GLM-5.3-flash pre-labels (2 classes: 0=ball free, 1=held
  ball). Pre-label noise is expected; YOLO tolerates a few percent box noise.
  If mAP comes out low, first fix: more frames (--per-video 80) rather than
  longer training.
- Exposure: recordings now include both auto and 2ms-locked frames; V11 sees
  both brightness regimes.
  **Corrected 2026-11-12:** the "2ms-locked" recordings are not a second
  brightness regime. They are 5 fps streams with each colour frame repeated six
  times (see `camera.py`). 80 of V11's 559 frames come from them, and 240 come
  from the six regression videos, so V11's suite scores are not independent.
  For new training use `build_v12_dataset.py` and `colab_train_v12_cell.py`.
