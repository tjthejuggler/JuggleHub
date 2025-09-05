Of course. This is the perfect time to consolidate everything into a single, clear, step-by-step master guide.

Here is the complete workflow, from turning on your camera to having a fully prepared `V1_generalist` dataset ready for training.

---

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

#### **Phase 4: Final Dataset Assembly (The Last Step Before Training)**

This final, automated step takes all your converted annotations and splits them into the final `train/` and `valid/` folders.

1.  **Create the Dataset Preparation Script:**
    *   The LLM programmer will now provide you with the `scripts/prepare_dataset.py` script. This script will be designed to read from your `data/annotation_sessions/` folders, gather all the image/text pairs, shuffle them, and split them 80/20 into a new training-ready folder.

2.  **Run the Preparation Script:**
    *   From your `JuggleHub` root directory, you will run a command like this:

    **Example Command:**
    ```bash
    python scripts/prepare_dataset.py \
      --output data/3_training_datasets/V1_generalist \
      data/annotation_sessions/normal_balls_daylight_session \
      data/annotation_sessions/normal_balls_lowlight_session \
      data/annotation_sessions/led_balls_red_session
    ```
    *   **Result:** The script will create the `V1_generalist` folder. Inside, you will find the `train/` and `valid/` folders, both containing `images/` and `labels/` subdirectories. The data will be randomly shuffled and correctly split.

You have now successfully completed the entire data preparation pipeline. The `data/3_training_datasets/V1_generalist` folder is a high-quality, professional-grade dataset, perfectly structured and ready to be fed into the training script.

------------

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