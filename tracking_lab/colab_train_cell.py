# V11 training cell for Colab - copy the ENTIRE contents of this file
# into a Colab cell and run it. Requires v11_dataset.zip uploaded first.

# fix the yaml path (it was written with Roo's local absolute path)
yaml_path = "/content/v11_dataset/v11_data.yaml"
txt = open(yaml_path).read()
txt = txt.replace("path: /home/twain/Projects/JuggleHub/.tmp/v11_dataset",
                  "path: /content/v11_dataset")
open(yaml_path, "w").write(txt)
print(txt)   # should show: path: /content/v11_dataset

from ultralytics import YOLO
model = YOLO("yolo11n.pt")   # COCO-pretrained nano; transfer to 2 classes
model.train(
    data=yaml_path,
    epochs=150,
    imgsz=640,
    batch=32,
    patience=25,          # early-stop if no improvement
    hsv_v=0.6,            # brightness variation (LED glow / dim room)
    hsv_s=0.7,            # saturation (white vs colored balls)
    degrees=0,            # no rotation: gravity matters
    flipud=0.0, fliplr=0.5,
    mosaic=1.0, close_mosaic=10,
    project="v11", name="run1",
)
metrics = YOLO("v11/run1/weights/best.pt").val(
    data=yaml_path)
print("mAP50:", metrics.box.map50, " mAP:", metrics.box.map)
print("DONE - weights at: v11/run1/weights/best.pt  (download this file)")
