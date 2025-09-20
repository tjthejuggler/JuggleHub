### **The Complete Guide to Filming and Labeling the JuggleHub Dataset (v3.0)**

This is the definitive guide for creating and labeling a high-quality juggling dataset. The core strategy is built on a methodical approach to data collection and a strict set of labeling rules. Following this guide precisely will result in a robust and reliable AI model.

The strategy is built on three key decisions:
1.  **One Unified Model:** We will train a single AI model to handle all conditions.
2.  **A Controlled, Multi-Profile Filming Strategy:** We will use meticulously controlled camera settings—not just "auto" vs. "manual"—to create a dataset that is both diverse and high-quality.
3.  **A Three-Class System:** We will use three specific classes to give the model the context it needs to identify the state of each ball.

---

### **The Three-Class System: The Foundation of Our Dataset**

This is what you will be labeling. Understanding these classes is the most critical step.

1.  **`led_on`**: This class is exclusively for a ball that is **visibly glowing**.
2.  **`led_off`**: This class is for any ball that is **not glowing**. This single class correctly covers both a normal, non-LED juggling ball *and* an LED ball that is powered off.
3.  **`dropped_ball`**: This class is for **any type of ball** (glowing or not) that is on the **floor** after an uncontrolled drop. This class overrides the other two; if a ball is on the floor, it is always a `dropped_ball`.

---

### **Part 1: Filming the Dataset - A Scientific Approach to Data Capture**

#### **The Core Philosophy: Train for Quality, Deploy for Speed**

It is tempting to collect data using the exact same settings (e.g., 60 FPS) you'll use in the final app. However, the professional approach separates the goals of data collection and deployment.

*   **Phase 1: Training Data Collection (Goal: Maximum Quality):** Your single most important goal is to teach the model what the world looks like as accurately as possible. For this, you should use settings that produce the cleanest, sharpest, lowest-noise images. Real-time performance does not matter here. We create a "golden dataset" of pristine images.
*   **Phase 2: Real-Time Inference (Goal: Maximum Speed):** In your live app, your goal is to minimize latency. You will use settings that meet your speed requirements (e.g., 60 FPS or 90 FPS), even if it introduces some motion blur or noise.

The model trained on the pristine data will be robust enough to handle the slightly degraded inference images. To perfect this, a technique called **Data Augmentation** is used during training, where we artificially add motion blur and noise to our clean images. This explicitly teaches the model to be robust to the exact conditions it will see during real-time inference.

| Phase | **Data Collection & Training** | **Real-Time Inference** |
| :--- | :--- | :--- |
| **Goal** | Highest possible image quality. | Lowest possible latency. |
| **FPS** | **30 FPS** | **60 FPS** (or higher) |
| **Exposure** | **As low as needed** (e.g., < 159 µs) to freeze motion. | **As low as possible** (e.g., 160 µs) for the FPS setting. |
| **Gain** | **As low as possible** to minimize noise. | **As high as needed** for a "good enough" image. |
| **Key Action**| Create a pristine dataset. Use **data augmentation** during training. | Deploy the robust model for fast, real-time decisions. |

#### **Critical Camera Configuration:**
*   **Turn IR Projector ON:** The RealSense projector is required for good depth data during inference, so it **must be on** during all training data collection to ensure consistency.
*   **Use Manual Settings for ALL Captures:** Disable auto-exposure, auto-gain, and auto-white-balance for all filming sessions. This gives us complete control and repeatability.

#### **Filming Strategy: The Multi-Profile Method**

For each distinct filming environment, we will capture several "sets" of data. This multi-profile method is designed for the **data collection phase** to create our "golden dataset".

*   **Profile 1: "Sharp/Nominal" (For `led_off` balls):**
    *   **Goal:** Capture a large volume of crisp, blur-free images. This is the foundation of your dataset and allows for perfect labeling.
    *   **Settings:**
        1.  **Set FPS to 30:** This gives you the timing budget for very fast shutter speeds.
        2.  **Set Exposure:** Set a fast shutter speed (e.g., start at 4000µs and go lower, even below 159µs if possible) until all motion blur is eliminated.
        3.  **Set Gain:** After setting exposure, increase the gain until the scene is well-lit, but keep it as low as possible.
        4.  **Set White Balance:** Briefly toggle "auto" to find a good value, then lock it in manually.
    *   **Use Case:** This profile is for capturing **`led_off`** and **`dropped_ball`** classes.

*   **Profile 2: "LED/Glow" (For `led_on` balls):**
    *   **Goal:** Capture the true color and shape of glowing balls without them appearing as washed-out white blobs.
    *   **Settings:**
        1.  **Set Exposure:** Use a **very fast** shutter speed (e.g., 500-2000µs). This is the key to preventing overexposure from the LEDs.
        2.  **Set Gain:** Keep the gain relatively low. The scene will be dark, but the glowing balls will be perfectly exposed.
    *   **Use Case:** Exclusively for capturing the **`led_on`** class.

*   **Profile 3: "Intentional Blur" (For both `led_on` and `led_off`):**
    *   **Goal:** To teach the model what severe motion blur looks like in a controlled, consistent way. This is different from the minor blur encountered during high-FPS inference.
    *   **Settings:**
        1.  **Set Exposure:** Use a **slower**, fixed shutter speed (e.g., 16667µs or 1/60s).
        2.  **Set Gain:** Adjust gain to get a usable image brightness at this slower shutter.
    *   **Use Case:** Capture a dedicated portion (~15%) of your data with this profile to create consistent blur examples.

#### **What to Film (The Comprehensive Shot List):**

*   **Vary Environments (Critical):** Record in at least 4-5 diverse locations: a brightly sunlit room, a room with only warm lamp light, an office with cool overhead light, outdoors in shade, etc.
*   **Vary Yourself:** Do not be a constant in the scene. Systematically change your shirt (patterns, solids, light, dark), pants, and even have a friend with a different build or skin tone juggle for some clips.
*   **Vary Backgrounds:** Film against complex backgrounds (bookshelves, patterned walls) and simple ones (plain walls). Intentionally include clutter.
*   **Capture All Angles:** Film from low angles, high angles, and straight on.
*   **Capture All Distances:** Get footage where the balls are very close and large, and very far away and small.
*   **Film Challenging Cases Systematically:**
    *   **Occlusion:** Intentionally film balls partially hidden by your hands, arms, and other objects.
    *   **Truncation:** Film so that balls are frequently cut off by the edges of the frame.
    *   **Drops:** Dedicate sessions to intentionally dropping balls. Capture them falling, bouncing, and settling on the floor.
*   **Create "Hard Negatives":**
    *   Place confusing, ball-like objects (oranges, doorknobs, decorations) in the background while juggling.
    *   Record short clips of the scene containing *only* these confusing objects, with no juggling balls present. These will be your "negative" images.

---

### **Part 2: Labeling the Dataset - Precision and Consistency**

Your labels are the "answer key" for the model. Their quality is more important than anything else. **Our mantra is "Consistently Snug, Not Obsessively Perfect."**

#### **The Labeling Litmus Test: Your Step-by-Step Guide**
For any ball in any frame, follow this exact decision process:

1.  **Is the ball on the floor after a drop?**
    *   **YES:** Label it as **`dropped_ball`**. Stop here.
    *   **NO:** Proceed to the next question.

2.  **Is the ball visibly glowing?**
    *   **YES:** Label it as **`led_on`**.
    *   **NO:** Label it as **`led_off`**.

#### **Detailed Labeling Rules (DOs):**

*   **Bounding Box Precision:** For sharp objects, **zoom in**. Align the four sides of the box to be tangent to the object's edges. The box should be a "snug fit" with minimal background inside. Do not obsess over a single pixel, but do not be sloppy.
*   **Labeling Motion Blur (CRITICAL RULE):**
    *   The bounding box must be the **actual size of the ball**, not the size of the blurred streak.
    *   Consistently place the box on the **leading edge** of the blur (the side furthest along the direction of motion). Some of the blur trail will correctly be outside the box.
*   **Partially Occluded Balls:** If a ball is hidden behind your hand, draw the bounding box around the **inferred full shape** of the ball, as if you could see through the obstruction.
*   **Truncated Balls (Off-Screen):** Draw the bounding box to cover **only the visible portion**, stopping exactly at the image border.
*   **Merged LED Balls:** If two `led_on` balls merge into one indistinguishable blob of light, draw **one single bounding box** around the entire merged shape. However, if you can see any visual boundary between them, label them as two separate, overlapping objects.
*   **Negative Images:** For frames containing only "hard negative" background objects (or no objects at all), create a corresponding **completely empty** label file. This is crucial.

#### **Critical "DON'T" Rules:**

*   **DO NOT Label Stationary Scenery:** **Never** label a ball that is sitting on a **table, shelf, or in a box** and was not part of the active juggling pattern. These are background objects. Labeling them `dropped_ball` will break your drop detection.
*   **DO NOT Label Reflections:** Never draw a bounding box around a reflection in a mirror or window.
*   **DO NOT Guess with Merged Blobs:** Do not use your knowledge to draw two separate boxes inside a single, visually merged blob of light. Label what you see.
*   **DO NOT Label Tiny Fragments:** If so little of a ball is visible (e.g., less than 25%) that it's unidentifiable, do not label it.
*   **DO NOT Create New Classes:** Never create a "Not a Ball" class for confusing objects. The model learns to ignore them by having them be unlabeled parts of the background in your labeled images.
*   **DO NOT Use Your Old "Auto" Data (Unless Curated):** Discard any blurry images captured with auto-settings. You may keep the perfectly sharp ones, but it is safer and cleaner to start fresh with your new, controlled methodology.
*   **DO NOT Label Stationary Scenery**: Never label a ball that is sitting on a table, shelf, or in a box and was not part of the active juggling pattern. The dropped_ball class is exclusively for a ball that has left a juggling pattern in an uncontrolled way. Labeling scenery as a dropped_ball will fundamentally break your drop detection logic.

### **Part 3: Dataset Balance - Ensuring a Smart Model**

Your final dataset should follow these guidelines to prevent the model from becoming biased.

*   **Balance `led_on` vs. `led_off`:** Aim for a roughly equal number of images featuring each class. A 50/50 or 60/40 split is ideal. This ensures the model is equally skilled at recognizing balls in both camera modes.
*   **Over-Sample the Rare Event** (dropped_ball): Drops are rare but critically important. The dropped_ball class should represent 10% to 20% of your total labeled instances (not images). This means you must dedicate specific filming sessions to intentionally dropping balls. Capture them falling, bouncing on different surfaces (carpet vs. wood), rolling under furniture, and coming to a rest. This is the only way to teach the model what failure looks like.

#### Condensed list of advice for when filming and labelling a dataset

Below is a condensed set of guidelines for creating and labeling a juggling dataset, based on the advice provided.

### **Filming for the Dataset: What to Do and Not Do**

#### **DO:**
*   **Vary Conditions:** Record clips in diverse environments, such as in both daylight and low-light.
*   **Include Challenging Cases:** Actively film scenarios with reflections (e.g., in mirrors, windows), backlighting, and balls partially cut off by the edge of the frame (truncation).
*   **Create "Hard Negatives":** Intentionally place confusing, ball-like objects (e.g., a ceramic apple, doorknobs, oranges) in the background while you juggle.
*   **Capture "Negative" Images:** Record clips of the scene containing only the confusing background objects, with no juggling balls present.
*   **Maintain Balance:** Aim for a dataset where approximately 80% of the images are in standard, clear conditions, and 20% include the challenging cases (reflections, occlusions, truncations, etc.).

#### **DON'T:**
*   **Avoid Reflections:** Do not shy away from filming in front of reflective surfaces; they are crucial for teaching the model what to ignore.
*   **Avoid Confusing Objects:** Do not remove objects that look like balls. Including them makes the final model more reliable by preventing false positives.

---

### **Labeling the Dataset: What to Do and Not Do**

#### **DO:**
*   **Label What the Camera Sees, Not What You Know**: If two led_on balls merge into one indistinguishable blob of light due to motion blur and overexposure, you must draw one single bounding box around the entire merged shape and label it led_on. Do not use your real-world knowledge to draw two separate boxes inside the blob. This will confuse the model. Trust your tracker (ByteTrack) to re-identify the individual balls once they separate and become visually distinct again in a later frame.
*   **Partially Occluded Balls:** If a ball is hidden behind your hand or another ball, draw the bounding box around the **inferred full shape** of the ball, including the hidden part.
*   **Truncated Balls (Off-Screen):** If a ball is cut off by the edge of the frame, draw the bounding box to cover **only the visible portion**, stopping exactly at the image border.
*   **Merged LED Balls:** If two glowing balls merge into one indistinguishable blob of light, draw **one single bounding box** around the entire merged shape. However, if you can see any visual boundary between them, label them as two separate, overlapping objects.
*   **Be Precise with Confusing Objects:** In images where both juggling balls and "hard negative" objects (like the ceramic apple) are present, **only label the actual juggling balls**.
*   **Handle Negative Images:** For frames that contain only the confusing objects and no juggling balls, create a corresponding **completely empty** label file.

#### **DON'T:**
*   **Don't Label Reflections:** Never draw a bounding box around a reflection. It must be treated as part of the background.
*   **Don't Guess with Merged Blobs:** Do not use your knowledge to draw two separate boxes inside a single, visually merged blob of light.
*   **Don't Label Tiny Fragments:** If so little of a ball is visible (e.g., less than 25%) that it's no longer identifiable as a ball, do not label it.
*   **Don't Create "Not a Ball" Labels:** Never create a new class for confusing objects. The model learns to ignore them by having them unlabeled in the background or present in negative images.
*   
-------------------

### **The Complete Data Preparation Workflow: From Recording to Training-Ready Dataset**

This guide will walk you through the entire process. Follow these steps in order.

#### **Phase 1: Setup (Do This Once)**

1.  **Create Master Directories:** In your `JuggleHub` project, create the master directories that will hold all your data.
    ```bash
    # Make sure you are in the JuggleHub project root
    mkdir -p data/1_raw_recordings
    mkdir -p data/2_tagged_and_annotated
    mkdir -p data/3_training_datasets
    mkdir -p data/annotation_sessions
    ```

2.  **Create Class Definition File:** Create the `classes.txt` file that defines what you will be labeling.
    ```bash
    # This command creates the file and adds "ball" to it
    echo "ball" > data/classes.txt
    ```

3.  **Download VIA Tool:** Download the `via.html` file from the [VIA Homepage](https://www.robots.ox.ac.uk/~vgg/software/via/) and save it somewhere convenient.

#### **Phase 2: Data Collection & Annotation (Repeat for Each Category)**

This is the main loop of your work. You will repeat these steps for each category of data you want to collect (e.g., `normal_balls/day_light`, `led_balls/color_red`, etc.).

1.  **Record Raw Video Clips:**
    *   Run the JuggleHub application (engine + UI).
    *   Use the "Record 5s Clip" button to capture video clips of the specific scenario you want to document.
    *   **Result:** New timestamped folders containing raw image frames will be automatically created in your `data/1_raw_recordings/` directory.

2.  **Prepare an Annotation Batch:**
    *   Create a new, specific category folder inside `data/2_tagged_and_annotated/`. For example:
        ```bash
        mkdir -p data/2_tagged_and_annotated/normal_balls/day_light
        ```
    *   Go through your `1_raw_recordings/` folders and **copy** the best, most representative frames for this category into the new folder you just created.

3.  **Set Up the VIA Annotation Session:**
    *   Create a dedicated session folder. For example:
        ```bash
        mkdir -p data/annotation_sessions/normal_balls_daylight_session/images
        ```
    *   Copy the `via.html` file into `data/annotation_sessions/normal_balls_daylight_session/`.
    *   Copy the batch of images you just prepared from `2_tagged_and_annotated/` into the `images` subfolder of your session directory.

4.  **Annotate the Batch:**
    *   Open the `via.html` file for your current session.
    *   **Load Images:** Go to "Project" -> "Add" -> "Add files from local directory" and select your session's `images` folder.
    *   **Define Attribute:** In the left "Attributes" panel, click `Region Attributes`, type `name` in the box, and press Enter.
    *   **Draw and Label:** Use the rectangle tool to draw boxes around all the balls. For the first ball, type `ball` as the `name`. For all subsequent balls, you can just select `ball` from the dropdown.
    *   **Save Project:** When the batch is complete, go to **"Project" -> "Save"**. This will download `via_project.json`. Move this file into your session's root directory (e.g., `data/annotation_sessions/normal_balls_daylight_session/`).

5.  **Repeat:** Go back to Step 1 and repeat this process for the next category until you have annotated all the batches you want to include in your V1 dataset.

#### **Phase 3: Conversion & Assembly (Do This After Annotating)**

Once you have several completed annotation sessions and their corresponding `via_project.json` files, you need to convert them into the YOLO format.

1.  **Run the Conversion Script:**
    *   Open a terminal and activate your JuggleHub `venv`.
    *   Navigate to the root of your `JuggleHub` project.
    *   Run the `convert_via_to_yolo.py` script, providing the paths to **all the JSON project files** you want to include in your dataset.

    **Example Command:**
    ```bash
    python scripts/convert_via_to_yolo.py \
      data/annotation_sessions/normal_balls_daylight_session/via_project.json \
      data/annotation_sessions/normal_balls_lowlight_session/via_project.json \
      data/annotation_sessions/led_balls_red_session/via_project.json
    ```
    *   **Result:** The script will run and create a `.txt` file next to every single image you annotated inside the `images` subfolders of your session directories. Your data is now in the correct YOLO format.

#### **Step A2: Assemble the Dataset**
1.  Run your `prepare_dataset.py` script to split your data into `train/` and `valid/` sets.
    *   **Example Command:**
    ```bash
    python scripts/prepare_dataset.py \
        --dataset-name V2_with_hands \
        --source-dir data/annotation_sessions \
        --output-dir data/3_training_datasets \
        --tags session_name_1 session_name_2
    ```
    *   **Result:** A new folder like `data/3_training_datasets/V2_with_hands/` is created.

#### **Step A3: Create the Perfect `dataset.yaml` (The Most Important New Step)**
This is where we prevent the label mismatch error at its source. We will create the `dataset.yaml` file locally.

3.  **Run the create_yaml.py Script:** From your terminal, run this script, providing the path to your newly assembled dataset and your full list of class names in the correct order.
    ```bash
    python scripts/create_yaml.py data/3_training_datasets/V2_with_hands led_on led_off dropped_ball hand
    ```
    *   **Result:** A perfect `dataset.yaml` file is now inside your `V2_with_hands` folder. The dataset is now self-contained and correct.

#### **Step A4: Prepare Your Assets for Upload**
1.  **Compress the Dataset:** Navigate to the `3_training_datasets` directory and zip your final dataset folder.
    ```bash
    cd data/3_training_datasets/
    zip -r V2_with_hands.zip V2_with_hands/
    ```
2.  **Locate Your Custom Model:** Find your `yolo11n.pt` file. You will need to upload this alongside your dataset.

---

### **Phase B: Cloud Setup (Google Drive)**

1.  **Organize Google Drive:**
    *   In your Google Drive, ensure you have a `JuggleHub` folder.
    *   Inside `JuggleHub`, create two folders: `datasets` and `models`.
2.  **Upload Your Assets:**
    *   Upload `V2_with_hands.zip` to the `JuggleHub/datasets/` folder.
    *   Upload `yolo11n.pt` to the `JuggleHub/models/` folder.

---

### **Phase C: Colab Execution (The Smooth & Repeatable Workflow)**

This is your new template for all future training notebooks.

#### **Step C1: Initial Setup (Boilerplate)**
Run this cell first to connect to your GPU and Google Drive.
```python
# 1. Verify GPU is active
!nvidia-smi

# 2. Mount Google Drive
from google.colab import drive
drive.mount('/content/drive')

# 3. Define project paths
# (Adjust if you used a different folder structure on Drive)
GDRIVE_PROJECT_PATH = "/content/drive/MyDrive/JuggleHub"
DATASET_NAME = "V2_with_hands" # <-- CHANGE THIS FOR EACH NEW DATASET
MODEL_NAME = "yolo11n.pt"      # <-- Your custom model name
```

#### **Step C2: Transfer Assets to Colab**
This copies your dataset and custom model from Drive to the fast local storage of the Colab machine.
```python
# 1. Copy the dataset zip file from Drive
!cp "{GDRIVE_PROJECT_PATH}/datasets/{DATASET_NAME}.zip" /content/

# 2. Unzip the dataset
!unzip -q /content/{DATASET_NAME}.zip -d /content/

# 3. Copy your custom model file from Drive
!cp "{GDRIVE_PROJECT_PATH}/models/{MODEL_NAME}" /content/

print("✅ Dataset and custom model are ready in the Colab environment.")
```

#### **Step C3: Update YAML and Clean Cache (The "No More Errors" Step)**
This block performs two critical actions: it updates the path in your YAML file and proactively deletes any old cache files.
```python
import yaml
import os

# --- 1. Update the YAML path ---
yaml_path = f"/content/{DATASET_NAME}/dataset.yaml"
colab_dataset_path = f"/content/{DATASET_NAME}"

with open(yaml_path, 'r') as f:
    data = yaml.safe_load(f)
data['path'] = colab_dataset_path
with open(yaml_path, 'w') as f:
    yaml.dump(data, f, sort_keys=False)
print("✅ dataset.yaml path updated for Colab.")
!echo "--- Current YAML contents: ---"
!cat {yaml_path}

# --- 2. Proactively delete stale cache files ---
train_cache = f"/content/{DATASET_NAME}/train/labels.cache"
val_cache = f"/content/{DATASET_NAME}/valid/labels.cache"
if os.path.exists(train_cache):
    os.remove(train_cache)
    print("🗑️ Deleted stale training cache.")
if os.path.exists(val_cache):
    os.remove(val_cache)
    print("🗑️ Deleted stale validation cache.")
```

#### **Step C4: Install Dependencies & Run Training**
Finally, you're ready to train.
```python
# 1. Install Ultralytics
!pip install -q ultralytics

# 2. Run the Training
from ultralytics import YOLO

# Load your CUSTOM model from the local path in Colab
model = YOLO(f"/content/{MODEL_NAME}")

# Train the model
results = model.train(
    data=f"/content/{DATASET_NAME}/dataset.yaml",
    epochs=100,
    imgsz=640,
    project='JuggleHub_Training_Results', # All results will save here
    name=f'run_{DATASET_NAME}'           # A specific sub-folder for this run
)
```

### **Why This New Workflow is Better**

*   **Correct From the Start:** By creating the `dataset.yaml` locally with a script (**Step A3**), you eliminate any chance of a class mismatch. The dataset is guaranteed to be correct before you even upload it.
*   **Explicit Model Handling:** The workflow recognizes that `yolo11n.pt` is a special file you provide. It makes uploading it and loading it from a specific path (**Step C2 & C4**) a required step, preventing `FileNotFoundError`.
*   **Proactive Cache Cleaning:** The "Cache Buster" (**Step C3**) automatically deletes old cache files before every training run. This completely prevents the stale cache error, even if you have to stop and restart a training session.

This comprehensive process makes your workflow robust, repeatable, and far less prone to the common errors of dataset preparation.

Excellent point. You're absolutely right to ask for the second half of the experiment.

The workflow for the augmented run is nearly identical, with one key addition: injecting the custom augmentation code before training.

Here is the exact, step-by-step process for the **augmented fine-tuning run**.

---

### **Augmented Fine-Tuning Workflow (Complete)**

You will perform this *after* your baseline training run is complete. You can do this in the same Colab notebook.

### **Phase D: Augmented Training (Building on the Previous Setup)**

Assume you have just completed **Phases A, B, and C** from the previous guide. Your dataset and custom `yolo11n.pt` model are already in the Colab environment.

#### **Step D1: Define and Register the Custom Augmentation**

This is the new and most important step for this run. We define our custom blur function and the "callback" function that will inject it into the training loop.

**Copy this entire block into a new Colab code cell and run it.**

```python
import torch
import cv2
import numpy as np
import random
from ultralytics import YOLO

# 1. THE CUSTOM AUGMENTATION FUNCTION
# This is the code that performs the actual image manipulation.
def apply_object_motion_blur(image, bboxes, blur_limit=(3, 15), p=0.5):
    """Applies motion blur to objects within their bounding boxes."""
    if random.random() > p:
        return image # Apply augmentation with a probability of p
    
    output_image = image.copy()
    for bbox in bboxes:
        x_min, y_min, x_max, y_max = [int(v) for v in bbox]
        if x_min >= x_max or y_min >= y_max:
            continue
        
        object_crop = output_image[y_min:y_max, x_min:x_max]
        if object_crop.size == 0: continue # Skip if crop is empty
        
        kernel_size = random.randint(blur_limit[0], blur_limit[1])
        if kernel_size % 2 == 0:
            kernel_size += 1 # Ensure kernel size is odd
            
        kernel = np.zeros((kernel_size, kernel_size))
        
        # Randomly choose horizontal or vertical blur
        if random.random() > 0.5:
            center = (kernel_size - 1) // 2
            kernel[center, :] = np.ones(kernel_size)
        else:
            center = (kernel_size - 1) // 2
            kernel[:, center] = np.ones(kernel_size)
            
        kernel = kernel / kernel_size # Normalize
        
        blurred_crop = cv2.filter2D(object_crop, -1, kernel)
        output_image[y_min:y_max, x_min:x_max] = blurred_crop
        
    return output_image


# 2. THE CALLBACK FUNCTION (THE "HOOK")
# This function is the bridge between the YOLO trainer and our custom code.
# It runs automatically on every single batch of training data.
def on_train_batch_start(trainer):
    """Callback to apply custom augmentation at the start of each training batch."""
    batch = trainer.batch
    images = batch['img']
    labels = batch['bboxes']
    device = trainer.device

    processed_images = []
    # Loop through each image in the batch
    for i in range(len(images)):
        image_tensor = images[i]
        
        # Get the labels corresponding to the current image
        bboxes_for_image = labels[labels[:, 0] == i]

        # Convert tensor image to OpenCV format (HWC, BGR, uint8)
        img_np = image_tensor.permute(1, 2, 0).cpu().numpy()
        img_np = (img_np * 255).astype(np.uint8)
        img_bgr = cv2.cvtColor(img_np, cv2.COLOR_RGB2BGR)

        # Convert normalized YOLO bboxes [class_id, cx, cy, w, h] to pixel [xmin, ymin, xmax, ymax]
        h, w, _ = img_bgr.shape
        pixel_bboxes = []
        for bbox in bboxes_for_image:
            _, cx, cy, bw, bh = bbox
            x_min = (cx - bw / 2) * w
            y_min = (cy - bh / 2) * h
            x_max = (cx + bw / 2) * w
            y_max = (cy + bh / 2) * h
            pixel_bboxes.append([x_min, y_min, x_max, y_max])
        
        # Apply our custom blur augmentation
        # We target a 60% probability to ensure the model still sees plenty of non-blurred examples.
        blurred_img_bgr = apply_object_motion_blur(img_bgr, pixel_bboxes, p=0.6)

        # Convert back to tensor format (CHW, RGB, float)
        img_rgb = cv2.cvtColor(blurred_img_bgr, cv2.COLOR_BGR2RGB)
        img_tensor_back = torch.from_numpy(img_rgb).float() / 255.0
        img_tensor_back = img_tensor_back.permute(2, 0, 1)

        processed_images.append(img_tensor_back)
    
    # Replace the original images in the batch with our new, augmented images
    trainer.batch['img'] = torch.stack(processed_images).to(device)

print("✅ Custom augmentation functions defined and ready.")
```

#### **Step D2: Run the Augmented Training**

Now we perform the training, but with two key differences:
1.  We load a **fresh copy** of your `yolo11n.pt` model to ensure a fair comparison with the baseline.
2.  We **add the callback** before starting the training.
3.  We give it a **different name** to keep the results separate.

**Run this code in the final cell.**

```python
# 1. Set the names from your setup cell again for clarity
DATASET_NAME = "V2_with_hands"
MODEL_NAME = "yolo11n.pt"

# 2. Load a FRESH, UNTRAINED copy of your custom model
# This ensures we are starting from the same point as the baseline run.
model_augmented = YOLO(f"/content/{MODEL_NAME}")

# 3. ATTACH THE HOOK: This is the magic step.
# We are telling the trainer to run our on_train_batch_start function on every batch.
model_augmented.add_callback("on_train_batch_start", on_train_batch_start)

print(f"🚀 Starting augmented training run for '{DATASET_NAME}'...")

# 4. Train the model
results_augmented = model_augmented.train(
    data=f"/content/{DATASET_NAME}/dataset.yaml",
    epochs=100,
    imgsz=640,
    project='JuggleHub_Training_Results',
    name=f'run_{DATASET_NAME}_augmented_blur' # <-- A new, descriptive name!
)

print("🎉 Augmented training complete!")
```

### **Phase E: Analysis and Saving**

After both the baseline and augmented runs are complete, your `JuggleHub_Training_Results` folder in Colab will contain two subfolders, for example:
*   `run_V2_with_hands`
*   `run_V2_with_hands_augmented_blur`

#### **Step E1: Save Everything to Google Drive**

Run this command to copy all your results back to Google Drive for permanent storage.

```python
# Copy the entire training results folder to your Google Drive
!cp -r /content/JuggleHub_Training_Results "{GDRIVE_PROJECT_PATH}/"
print(f"✅ All training results have been saved to '{GDRIVE_PROJECT_PATH}/JuggleHub_Training_Results'")
```

#### **Step E2: Compare the Models**

1.  **Quantitative Analysis:** In your Google Drive, navigate to the results folders. Open the `results.png` file in each. Compare the graphs, especially the **mAP50-95** score. The goal is for the augmented run to have a higher (or at least more stable) mAP score.
2.  **Qualitative Analysis:** Download the `best.pt` file from the `weights/` subfolder of each run. Use these two models on your local machine to perform inference on test videos that were *not* part of your training set, especially videos with lots of motion. Visually determine which model performs better in the real-world scenarios you care about.

This complete, two-part workflow allows you to scientifically prove the value of your custom augmentation strategy.

This is a fantastic `README.md`. It's comprehensive, well-structured, and clearly documents a very sophisticated project. Thank you for providing it.

Based on this, I have everything I need. The key takeaways are:
1.  **Engine:** Your C++ engine is the core of your real-time system.
2.  **Model Format:** The engine consumes models in the **OpenVINO Intermediate Representation (IR)** format (`.xml` and `.bin`).
3.  **Model Location:** The engine expects these files to be in `engine/models/`.
4.  **Model Name:** The engine is hard-coded (or configured) to look for a model named `yolo11n.xml` and `yolo11n.bin`.

This means our goal is to take the `best.pt` file produced by Colab and convert it into the OpenVINO IR format that your C++ engine can understand.

---

### **The Exact Workflow: From Colab to Your C++ Engine**

Here is the precise, step-by-step process.

### **Phase 1: Download Your Trained Model from Colab**

Your non-augmented training run is almost done. Once it finishes, a `best.pt` file will be created.

1.  **Locate the File in Colab:**
    *   In the Colab file browser (left sidebar), navigate to the results folder:
        `JuggleHub_Training_Results` -> `baseline_4_class_run` -> `weights/`
    *   Inside this folder, you will find `best.pt`.

2.  **Download to Your Local Machine:**
    *   Right-click on `best.pt` and select "Download".
    *   Save this file somewhere convenient on your local computer, for example, your `~/Downloads` folder.

### **Phase 2: Convert the Model to OpenVINO Format**

This is the critical conversion step. We will use the `yolo` command-line tool, which has a built-in exporter for OpenVINO.

1.  **Open a Terminal on Your Local Machine:** Make sure it's a terminal where your JuggleHub project's Python virtual environment is activated and your OpenVINO environment is sourced.
    ```bash
    # Navigate to your JuggleHub project root
    cd ~/Projects/JuggleHub

    # Activate your Python virtual environment
    source ./hub/.venv/bin/activate

    # Source the OpenVINO environment variables
    source /opt/intel/openvino_2025.2.0/setupvars.sh
    ```

2.  **Run the Export Command:**
    Execute the following command. This tells YOLO to take your downloaded `.pt` file and export it to the OpenVINO format.

    ```bash
    yolo export model=~/Downloads/best.pt format=openvino imgsz=640
    ```
    *   `model=~/Downloads/best.pt`: The path to the model you just downloaded.
    *   `format=openvino`: The target format for conversion.
    *   `imgsz=640`: It's crucial to specify the same image size you trained with (640x640).

3.  **Verify the Output:**
    *   The command will create a new directory named `best_openvino_model/`.
    *   Inside this directory, you will find several files, including the two we need: `best.xml` and `best.bin`.

### **Phase 3: Deploy the New Model to Your Engine**

Now, we replace the old model files in your engine with the new, fine-tuned ones.

1.  **Navigate to Your Engine's Model Directory:**
    ```bash
    cd ~/Projects/JuggleHub/engine/models
    ```

2.  **Backup the Old Model (IMPORTANT):**
    Never delete the original model. Just in case something goes wrong, you'll want to be able to revert.
    ```bash
    # Create a backup directory if it doesn't exist
    mkdir -p old_models

    # Move the existing model files to the backup folder
    mv yolo11n.xml yolo11n.bin metadata.yaml old_models/
    ```

3.  **Copy and Rename the New Model:**
    Copy the `best.xml` and `best.bin` files from the export directory and rename them to what your engine expects (`yolo11n.xml` and `yolo11n.bin`).

    ```bash
    # Copy and rename the .xml file
    cp ~/Downloads/best_openvino_model/best.xml ./yolo11n.xml

    # Copy and rename the .bin file
    cp ~/Downloads/best_openvino_model/best.bin ./yolo11n.bin
    ```

Your fine-tuned model is now deployed.

### **Phase 4: Run and Test Your Engine**

You are now ready to see the results of your hard work.

1.  **Run Your System:** Use your standard script to launch the hub and engine. It's a good idea to select a specific device for testing.
    ```bash
    # Example: Run using the GPU for good performance
    ./scripts/run_hub.sh --use-venv --device GPU
    ```

2.  **Evaluate Performance:**
    *   **Does it run?** The first check is to ensure the engine loads the new model without crashing.
    *   **What does it detect?** Point the camera at your juggling balls (both LED and non-LED) and your hands. The UI should now draw bounding boxes for all four classes.
    *   **How accurate is it?** Check the confidence scores. Are they high for the correct objects?
    *   **Are there false positives?** Does it incorrectly identify background objects as balls or hands?
    *   **How does it handle `dropped_ball`?** Drop a ball and see if it is correctly re-classified as `dropped_ball` once it hits the floor.

By following this four-phase process, you have successfully completed the full "train-convert-deploy-test" cycle. You can repeat this exact same process for your augmented model (`run...augmented_blur/weights/best.pt`) to compare its real-world performance directly against this baseline.






before 2025-09-20 10:04:00:

These are two of the most important and challenging edge cases in object detection labeling. Getting this right is what separates a decent model from a great one.

The guiding principle for both scenarios is this: **You must label what is visually present and reasonably inferable in the image, not what you know to be true from context.** You are teaching a computer vision model, so you must label based on *vision*.

Here is a detailed breakdown of how to handle each case.

---

### Case 1: Partially Occluded Balls

This is when a ball is partially hidden by your hand, body, or another ball, but its identity as a ball is still clear.

**The Rule: Always label the full, inferred bounding box of the ball.**

Imagine the ball is a perfect circle. Even if you can only see 70% of it, you must draw a bounding box that covers the full 100% of its area, including the part that is hidden.

**Why this is the correct approach:**

*   **Teaches Object Permanence:** You are teaching the model what a "whole ball" looks like, even when it's peeking out from behind an obstacle. The model learns to recognize the object from its visible parts and correctly estimate its full size.
*   **Prevents Mis-learning:** If you only labeled the visible crescent, the model would learn that "crescent shapes" are balls. This would lead to inaccurate detections and a poorly generalized model.
*   **Maintains Stable Dimensions:** The tracking algorithm (`ByteTrack`) relies on the size and position of the bounding box to maintain a consistent track from frame to frame. If the box for a single ball suddenly changed from a full square to a tiny sliver and back, the tracker could lose the object and assign a new ID.

**Practical Guidelines for Occlusion:**

*   **DO:** Draw a tight box around the *inferred full shape* of the ball.
*   **DON'T:** Draw a box *only* around the visible part.
*   **The ~25% Rule:** If so little of the ball is visible (e.g., less than 25%) that you, as a human, would have to guess if it's even a ball, then **do not label it.** If it's not visually identifiable, forcing a label will only add noise to your dataset.

---

### Case 2: Merged LED Balls (The "Blob of Light")

This is a different and more difficult problem. It's not occlusion; it's a loss of visual information where two objects merge into one. You know there are two balls, but the camera sees one bright blob.

**The Rule: If you cannot visually distinguish the boundary between the balls, you must label the entire blob as a single object.**

Do not use your prior knowledge to "guess" where the two balls are within the blob. This is the most critical mistake you can make in labeling.

**Why this is the correct approach:**

*   **You Must Label Reality:** The camera sensor is overexposed and sees a single entity. Your training data *must* reflect this physical reality. You are training the model to interpret what the camera sees.
*   **Prevents Model "Hallucination":** If you were to draw two boxes inside the single blob, you would be teaching the model to invent features that aren't there. This is extremely dangerous, as the model might learn to "hallucinate" two balls even when there is only one, leading to persistent false positives.
*   **Trust the Tracker:** This is where the tracking algorithm (`ByteTrack`) shines. It is designed for this exact scenario. Its logic will be:
    *   **Frame N:** Sees two distinct balls with ID 1 and ID 2.
    *   **Frame N+1:** Sees a single, large blob. It might temporarily lose track or assign the blob a new ID (e.g., ID 3).
    *   **Frame N+2:** Sees two distinct balls again as they separate. The tracker's algorithm is smart enough to look at their velocity and position and say, "Ah, these two new detections are almost certainly the original ID 1 and ID 2 that disappeared last frame," and it will correctly re-associate them.
    *   By labeling the blob as one object, you are providing a clean, honest signal to the tracker, allowing it to do its job correctly.

**Practical Guidelines for Merged Blobs:**

*   **DO:** If you can see any visual cue of two separate circles (even if they are touching and overlapping), label them with two separate, overlapping bounding boxes.
*   **DO:** If they are completely merged into a single, indistinguishable shape of light with no clear boundary between them, draw **one single bounding box** around the entire merged shape.
*   **DON'T:** Ever use your real-world knowledge to draw two boxes where the image only shows one object. This will confuse your model and damage its reliability.

By following these rules, you will create a robust, reliable model that understands how to handle the messy reality of object tracking.

----------

This is an excellent and subtle question that directly impacts the reliability of your model in real-world scenarios.

My strong recommendation is a two-part strategy:
1.  **Absolutely include images with reflections** in your dataset.
2.  **NEVER label the reflected ball.**

Treat the reflection as a sophisticated part of the background. By including it but not labeling it, you are actively teaching your model to be smarter.

---

### The Golden Rule for Reflections: A reflection is part of the background, not an object to be detected.

Here is a detailed explanation of why this is the correct and most robust strategy for your specific project.

### Why This is the Correct Strategy

1.  **It Teaches the Model to Ignore False Positives (Negative Mining):**
    *   A reflection looks very much like a real ball. If your model never sees a reflection in its training data, the first time it encounters one in the real world, it will almost certainly identify it as a ball. This will result in a "false positive" detection.
    *   By including images with reflections and deliberately **not labeling them**, you are providing a powerful lesson for the model. You are showing it an image and implicitly saying: "See this thing that looks like a ball? It's not. Learn to tell the difference and ignore it." This process is called "hard negative mining," and it is one of the most effective ways to reduce false positives and make a model more reliable.

2.  **It Prevents Catastrophic 3D Projection Errors (CRITICAL for Your Project):**
    *   This is the most important reason for your specific use case. Your engine's pipeline is: **2D Detection -> 3D Projection.**
    *   A depth camera like the D455 **cannot get correct depth data from a mirror.** The camera's infrared pattern will either reflect off the mirror's surface, giving you the distance to the mirror, or it will measure the *apparent distance* to the reflected object (e.g., `distance to mirror + distance from mirror to ball`).
    *   **If you were to label a reflection,** the model would detect it in 2D. Then, the engine would grab the corresponding depth data for that pixel, which would be completely wrong. This would result in a "ghost" ball being projected into your 3D space at a nonsensical `(x, y, z)` coordinate. This phantom data point would corrupt any trajectory analysis, pattern recognition, or speed calculations you try to perform. It would introduce extreme, difficult-to-filter noise.

3.  **It Avoids Tracker Confusion:**
    *   The `ByteTrack` algorithm tracks objects based on their motion from frame to frame. A real ball and its reflection move in perfect mirrored synchrony. This could potentially confuse the tracker's logic, leading to unstable track IDs or other unpredictable behavior. By ensuring the reflection is never detected in the first place, you keep the input to the tracker clean and reliable.

---

### What Would Happen if You Labeled the Reflection?

Let's walk through the failure case:
1.  You label both the real ball and its reflection.
2.  The model learns that both are valid targets.
3.  In production, you juggle one ball in front of a mirror.
4.  The model correctly detects **two** balls in the 2D image.
5.  The engine tries to project them into 3D:
    *   **Real Ball:** Gets correct depth data and is placed accurately in 3D space.
    *   **Reflected Ball:** Gets garbage depth data and is placed at a completely wrong position in 3D space.
6.  Your final output now shows two balls, one of which is teleporting wildly around the 3D scene, making all of your analysis data unusable.

### Practical Strategy for Your Dataset

1.  **Embrace Reflections:** Do not avoid them. Actively seek to create training data that includes them. This makes your model stronger.
2.  **Use Variety:** Capture data with different types of reflective surfaces if possible:
    *   A full-sized mirror.
    *   A window at night (which acts like a dark mirror).
    *   Even a polished floor or a glossy tabletop can create reflections.
3.  **Be Meticulous:** When labeling, identify the real, physical balls and label their full, inferred shape. Then, identify any reflections and **leave them completely unlabeled.**
4.  **Follow the 80/20 Guideline:** Just like with other challenging conditions, make sure your dataset has a strong foundation. Aim for ~80% of your images to be in "standard" conditions and ~20% to include challenging cases like reflections, backlighting, and heavy occlusions.

By following this strategy, you will build a sophisticated and robust model that not only finds what it's looking for but, just as importantly, knows what to ignore.

---------------

This is a fantastic question that addresses another critical edge case in labeling. The answer is different from the occlusion case, and it's vital to get it right.

My recommendation is: **Yes, you absolutely should include images where a ball is partially off-screen. You must label these by drawing a bounding box that covers *only the visible portion* of the ball.**

Do not try to guess or infer the full shape of the ball off-screen. The bounding box must stop at the edge of the image.

---

### The Critical Distinction: Occlusion vs. Truncation

To understand why the labeling rule is different here, we need to be very clear about two concepts:

1.  **Occlusion (Discussed Previously):** A ball is hidden *behind another object* within the image frame. The full ball exists, but some pixels are blocked by your hand, another ball, etc.
    *   **Rule:** Label the **inferred full shape** of the ball.
    *   **Reason:** You are teaching the model "object permanence" and what a whole object looks like even when partially hidden.

2.  **Truncation (This Case):** A ball is cut off *by the edge of the image frame itself*. The pixels for the rest of the ball were never captured by the camera in this frame.
    *   **Rule:** Label **only the visible part** of the ball. The bounding box must not go outside the image boundaries.
    *   **Reason:** You must train the model on the visual evidence that is actually present in the image.

### Why You Must Label Only the Visible Part for Truncated Objects

1.  **The Model Learns from Pixels:** A machine learning model learns by correlating a bounding box with the pixels *inside* that box. If you were to draw a box for the "full ball" that extends off-screen, a portion of that box would contain no pixels from the image at all. This would be meaningless, noisy data that would confuse the model. It would be trying to learn from nothing.

2.  **It Teaches Feature Recognition from Partial Evidence:** By labeling just the visible half-circle of a ball at the edge of the frame, you are teaching the model a valuable lesson: "Even if you only see this specific curved shape and texture, you can be confident that it's part of a juggling ball." This makes the model much more robust at detecting balls as they enter and leave the camera's field of view.

3.  **It Prevents Downstream Errors:** A correct bounding box (even a partial one) provides a valid center point for the visible part, which can be used to query the depth sensor. While not as accurate as the center of a full ball, it's still useful data. An incorrectly inferred box would provide a completely wrong center point, leading to bad depth data and a "ghost" object in your 3D space.

---

### Practical Strategy and Guidelines

1.  **Include Truncated Images:** As with other edge cases, these are crucial for robustness. A juggling pattern will naturally have balls entering and exiting the frame, so your model must be trained to handle this common scenario.

2.  **Label What You See:** When an object is cut off by the frame edge, draw your bounding box from the inside of the object right up to the very edge of the image. The final box will be rectangular, but one or two of its sides will be perfectly aligned with the image boundary.

3.  **Maintain Dataset Balance:** Truncated objects, like other "challenging" cases (occlusions, reflections, etc.), should represent a minority of your training examples. Follow the **80/20 guideline**: ensure about 80% of your labeled objects are fully visible and well-defined, and reserve about 20% for these combined edge cases. This ensures the model first learns the "ideal" representation of a ball before it learns how to handle the tricky situations.

By following this rule, you will train a model that doesn't get "surprised" when objects enter the scene, leading to faster, more confident detections from the very first frame an object becomes visible.

--------------

This is an excellent question that gets to the core of reducing false positives. You've identified a classic "hard negative" example, and how you handle it is critical.

The answer is a combination of two strategies we've discussed, used together for maximum effect:

1.  **Primary Strategy: Use Negative Images.** The most powerful way to teach the model to ignore the ceramic apple is to include images *of the ceramic apple in the scene* in your set of negative images (the ones with empty label files).
2.  **Secondary Strategy: Label Meticulously.** In any images where both the ceramic apple and *actual juggling balls* are present, you must be extremely precise and **only label the juggling balls, leaving the apple unlabeled.**

You should **never** create a new class label for the apple (e.g., "not_a_ball").

---

### Why This Two-Part Strategy is Correct

1.  **It Forces the Model to Learn Discriminating Features:**
    *   Let's say the ceramic apple and a red juggling ball are both red, round, and of a similar size. By providing an image with both and only labeling the juggling ball, you force the model to look for more subtle features.
    *   It might learn that the juggling ball has a matte texture, while the apple is glossy. It might learn that the juggling ball has a specific seam or logo that the apple lacks. It learns the "essential features" of a juggling ball beyond just "round and red."

2.  **It Directly Minimizes False Positives:**
    *   By including the apple in a **negative image** (with an empty label file), you are explicitly telling the model: "When you see an image containing only this object, the correct output is *nothing*."
    *   This is the most direct way to train the model to suppress a detection for that specific object. When it sees the apple, its internal confidence score for "ball" will be very low, and it won't produce a bounding box.

3.  **It Avoids Unnecessary Complexity:**
    *   Your goal is to detect one thing: a juggling ball. This is a **single-class object detection** problem. If you were to add a new class label like "apple" or "not_a_ball," you would be turning it into a multi-class problem.
    *   This adds unnecessary complexity to your model and your labeling process. It's computationally more efficient and just as effective to teach the model that the apple is simply part of the "background" (i.e., anything that isn't a juggling ball).

---

### The Practical Workflow

1.  **Stage Your Scene:** Intentionally place the ceramic apple (and any other "confusing" round-ish objects like oranges, doorknobs, or lamps) in the background of your juggling area.

2.  **Capture Positive Images:** Record yourself juggling with the confusing objects in the background. When you label these images, **be ruthlessly precise**. Only the real juggling balls get a bounding box. The apple, the doorknob, etc., are left unlabeled.

3.  **Capture Negative Images:** After that, record some video of *just the scene* with the confusing objects, but without you or any juggling balls. Extract frames from this video.

4.  **Create Empty Label Files:** For all the frames from Step 3, create a corresponding `.txt` label file that is **completely empty.**

**The Result:**

By the end of this process, your model will have learned from multiple perspectives:
*   "This is what a ball looks like when an apple is also in the scene." (from the positive images)
*   "This is what an apple looks like by itself, and the correct thing to do is detect nothing." (from the negative images)

This dual approach is far more effective than just one method alone. It makes your model not only good at recognizing juggling balls but also exceptionally good at rejecting the specific things that it's most likely to be confused by in its operational environment.