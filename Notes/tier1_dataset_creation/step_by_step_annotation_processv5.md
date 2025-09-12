
### **The Master Protocol: A Step-by-Step Guide to Dataset Creation (v5.0)**

**Objective:** To create a comprehensive, high-quality dataset for a YOLO model that can robustly detect juggling balls and hands in real-time under various indoor lighting conditions and states. The protocol is designed to yield a model with the highest possible coordinate accuracy for the JuggleHub real-time engine.

**Guiding Philosophy: Train for Quality, Augment for Reality**
The core strategy of this protocol is built on a crucial distinction. We will dedicate our manual effort to capturing and labeling a "golden dataset" of the cleanest, sharpest, most pristine images possible. This provides the model with an unambiguous ground truth. We will then, during the automated training phase, use data augmentation to artificially teach the model how to handle the minor, real-world imperfections it will encounter during high-FPS inference, such as motion blur and noise. This two-stage approach is the key to achieving state-of-the-art performance.

**Classes to be Labeled:**
1.  **`led_on`**: A ball that is actively and visibly glowing from its own internal light source.
2.  **`led_off`**: Any non-glowing ball, including a standard ball or an LED ball that is switched off.
3.  **`dropped_ball`**: Any type of ball that is on the floor *after an uncontrolled drop*. This class represents an event state, is the highest priority, and overrides the other two ball classes.
4.  **`hand`**: A single class for both the left and right hand. The object detector's job is simply to find the hand's location. The more complex task of determining whether it is the *left* or *right* hand will be handled by the downstream analysis pipeline, which can use context like trajectory history and pattern structure to make an intelligent assignment. This separation of concerns creates a more robust system.

**Core Principles:**
*   **Manual Control is Mandatory:** All captures will use manual camera settings. Auto-exposure, auto-gain, and auto-white-balance are forbidden to ensure consistent and repeatable data collection.
*   **IR Projector ON:** The RealSense depth projector must be enabled for all captures to ensure the training data perfectly matches the inference conditions, which require the projector for accurate depth measurements.
*   **Consistency is Key:** The rules defined here are not guidelines; they are absolute. Following them without deviation is what separates a decent model from a high-performance one.

---

### **Phase 1: Pre-Flight Checklist (Do This Once Before Starting)**

1.  **Prepare Your Wardrobe:** Create at least 5-6 distinct "outfits." An outfit change can be as simple as changing your shirt. The goal is to vary color, pattern (solids, stripes, graphics), and brightness. Lay them out so you can change quickly between sessions and checkpoints.
2.  **Identify Filming Locations:** Select 4-5 different indoor locations with unique lighting characteristics. Examples:
    *   **The Office:** Cool, overhead fluorescent lighting.
    *   **The Living Room:** Warm, off-center lamp lighting.
    *   **The Sun Room / Near Window:** Bright, natural daylight.
    *   **The Hallway:** Dim, single-source lighting.
    *   **The Basement:** Unfinished, potentially mixed or poor lighting.
3.  **Prepare "Hard Negative" Props:** Gather a few common objects that could be mistaken for balls. Place them in your filming locations. Examples: oranges, doorknobs, decorative spheres, rolled-up socks. These are crucial for teaching the model what *not* to detect, thereby reducing false positives.
4.  **Camera Setup:**
    *   Clean the camera lens with a microfiber cloth. A single smudge can degrade hundreds of images.
    *   Mount the camera on a stable tripod. This is crucial for capturing controlled blur and perfectly sharp baseline images by eliminating camera shake.

---

### **Phase 2: The Data Capture Sessions (Repeat for Each Location)**

This is the main loop. You will execute this entire protocol, from calibration to negative examples, in *each* of your chosen locations.

#### **SESSION 1: The Office (Cool, Overhead Lighting)**

**Step 1.1: Calibrate All Office Camera Settings**
1.  Launch the RealSense Viewer. Disable all "auto" controls (Exposure, Gain, White Balance). Ensure the IR Projector is ON.
2.  **Calibrate "Sharp/Nominal" Profile (for `led_off` & `hand`):**
    *   Hold a non-glowing ball. Juggle or move it at a realistic speed.
    *   Set `Exposure` to the lowest possible value that completely **freezes motion** and eliminates all blur. This is your most critical setting. Note this value as **Office_Sharp_Exposure**.
    *   Now, adjust `Gain` until the scene is well-lit but not noisy. Note this as **Office_Nominal_Gain**.
    *   Briefly toggle Auto `White Balance`, note the Kelvin value, then manually set it. Note this as **Office_WB**.
3.  **Calibrate "LED/Glow" Profile (for `led_on`):**
    *   Turn on an LED ball in the dimly lit or dark office.
    *   The goal is to see the ball's color and shape, not a washed-out white blob.
    *   Set a **very fast `Exposure`** (e.g., 500-2000µs). This is the key. Adjust until the glow is not overexposed. Note this as **Office_LED_Exposure**.
    *   Keep `Gain` as low as possible. The background will be dark; this is correct and desirable. Note this as **Office_LED_Gain**.
4.  **Calibrate "Intentional Blur" Profile:**
    *   Set a slow, fixed `Exposure` (e.g., 16667µs for 1/60s). Note this as **Office_Blur_Exposure**.
    *   Adjust `Gain` to get a usable image brightness. This profile provides the "dose of reality" for our model, complementing the artificial blur added during augmentation.

**Step 1.2: Capture `led_off` & `hand` Data (Total: ~350 Images)**
1.  **Attire:** Put on **Outfit #1**.
2.  **Settings:** Use the **"Sharp/Nominal" Profile** (`Office_Sharp_Exposure`, `Office_Nominal_Gain`, `Office_WB`).
3.  **Capture Normal Poses (1-150):**
    *   Capture the `led_off` ball in a wide variety of positions: close to the camera, far away, in corners, on the desk. Vary the camera angle.
    *   Capture interactions: ball in hand, next to a keyboard, partially blocked by a mug.
4.  **Capture Challenging Poses (151-250):**
    *   **Occlusion:** Systematically capture images where the ball and your hands are 30-70% hidden by each other, your arm, a desk leg, or a monitor.
    *   **Truncation:** Capture images where the ball and your hands are partially cut off by the edges of the frame.
    *   **Reflections:** Capture images where the ball's reflection is clearly visible in a monitor screen or window.
5.  **Settings:** Switch to the **"Intentional Blur" Profile**.
6.  **Capture Motion Blur (251-350):**
    *   Toss, roll, and drop the ball. Capture clear blur streaks of both balls and hands against a sharp background. Get blur in all directions (horizontal, vertical, diagonal).

**Step 1.3: Capture `led_on` Data (Total: ~350 Images)**
1.  **Attire:** Change your shirt. You are now in **Outfit #1.1**.
2.  **Settings:** Use the **"LED/Glow" Profile** (`Office_LED_Exposure`, `Office_LED_Gain`, `Office_WB`).
3.  **Capture Normal Poses (1-150):**
    *   Turn off the main office lights so the room is dim or dark.
    *   Capture the `led_on` ball in various positions: held, stationary on the floor/desk, near decoy lights (monitor LEDs, phone screens).
4.  **Capture Challenging Poses (151-250):**
    *   Capture occlusion where the glowing ball is partially hidden by your (now dimly lit) hand.
    *   Capture truncation at the frame edge.
    *   Capture two `led_on` balls close together, sometimes overlapping, sometimes merging into a single blob of light.
5.  **Settings:** Switch to the **"Intentional Blur" Profile**.
6.  **Capture Motion Blur (251-350):**
    *   Toss the ball to create streaks of light. Capture this with both the main lights ON and OFF to get different background contrasts.

**Step 1.4: Capture `dropped_ball` Data (Total: ~200 Images)**
1.  **Attire:** Change into **Outfit #2**.
2.  **Settings:** Use the **"Sharp/Nominal" Profile**. The goal is to clearly see the ball on the floor.
3.  **Execute Intentional Drops (1-200):**
    *   This is a dedicated session. **Actively drop the balls.**
    *   Capture frames of `led_off` balls as they hit the floor, while they are bouncing, and after they have settled.
    *   Capture frames of `led_on` balls under the same conditions (hitting, bouncing, settled).
    *   Ensure they land on different surfaces if available (e.g., carpet vs. chair mat).
    *   Capture them rolling to a stop, including under desks or against walls.

**Step 1.5: Capture Hard Negative Data (Total: ~50 Images)**
1.  **Remove All Juggling Balls from the Scene.**
2.  **Capture cluttered scenes (1-25):** Take photos of the environment with the confusing "hard negative" props (oranges, doorknobs) visible.
3.  **Capture empty scenes (26-50):** Take photos of the juggling area from various angles with nothing of interest present.

**Session 1 Complete. You have ~950 images from this location.**

---

#### **SESSIONS 2, 3, 4, & 5: Living Room, Sun Room, Hallway, etc.**

**For each new location, you must repeat the *entire protocol* from Session 1.**

1.  **Start with a new, unique outfit.** Do not reuse an outfit from a previous session.
2.  **Perform Step 1.1: Calibrate All Settings.** The lighting will be different, so you *must* find new values for `[Location]_Sharp_Exposure`, `[Location]_Nominal_Gain`, `[Location]_WB`, etc. Write them down.
3.  **Perform Step 1.2: Capture `led_off` & `hand` Data** (~350 images), following the attire change checkpoint.
4.  **Perform Step 1.3: Capture `led_on` Data** (~350 images), following the attire change checkpoint.
5.  **Perform Step 1.4: Capture `dropped_ball` Data** (~200 images), with a fresh outfit.
6.  **Perform Step 1.5: Capture Hard Negative Data** (~50 images).

After completing 5 full sessions, you will have a comprehensive dataset of approximately 4,750 images, systematically covering a vast range of conditions.

---

### **Phase 3: The Labeling Protocol - The Rules of Truth**

Your labels are the answer key for the model. Their quality and consistency are more important than the quantity of images.

#### **The Labeling Decision Process**

For any object of interest in any frame, follow these exact decision flows:

**Decision Flowchart for Balls:**
1.  **Is the object a ball, and is it on the floor after an uncontrolled drop?**
    *   **YES:** Label it **`dropped_ball`**. Stop here. The process is complete for this object.
    *   **NO:** Proceed to the next question.
2.  **Is the ball visibly glowing from its own internal light source?**
    *   **YES:** Label it as **`led_on`**.
    *   **NO:** Label it as **`led_off`**.

**Decision Process for Hands:**
1.  **Is this object a human hand?**
    *   **YES:** Label it as **`hand`** according to the specific rules below.

#### **Detailed Labeling Rules for All Objects:**

*   **DO Label Snugly:** For sharp objects, zoom in. The bounding box edges should be tangent to the object's pixels. Minimize background inside the box.
*   **DO Follow the Motion Blur Rule:** The box must be the **actual size of the object** (ball or hand), not the size of the blurred streak. Consistently place the box on the **leading edge** of the blur (the side furthest along the direction of motion). **Why?** This provides the most accurate, up-to-date position of the object at the end of the camera's exposure, which is exactly what your Kalman filter needs to make the best possible prediction for the next frame.
*   **DO Infer for Occlusion:** If an object is partially hidden, draw the bounding box around the **inferred full shape** of the object, as if you could see through the obstruction. **Why?** This teaches the model object permanence. For hands and balls, this overlap is a critical feature your downstream logic will use to detect `THROW` and `CATCH` events.
*   **DO Crop for Truncation:** If an object is cut off by the frame edge, draw the box to cover **only the visible portion**, stopping exactly at the image border.
*   **DO Create Empty Labels for Negatives:** For all images from the "Hard Negative" captures, create a corresponding **completely empty** label file. This is how the model learns what to ignore.
*   **DON'T Label Reflections:** Never draw a bounding box around a reflection in a monitor, window, or shiny floor. It is part of the background. Including images with reflections but leaving them unlabeled is a powerful form of "hard negative mining" that makes your model more robust.
*   **DON'T Label Unidentifiable Fragments:** If less than ~25% of an object is visible (due to occlusion or truncation) and it's no longer clearly identifiable, do not label it. This prevents adding noisy, ambiguous data.
*   **DON'T Create New Classes:** Never invent a "Not a Ball" class for hard negatives. Their power comes from being unlabeled parts of the background.

#### **Specific, Critical Rules for Balls:**

*   **DO Label the Ball, Not the Glow:** For `led_on` balls, the bounding box should cover the physical ball itself, not the fuzzy aura or glow around it. Find the core shape and label it.
*   **DO Label Merged Blobs as One:** If two `led_on` balls merge into a single, visually indistinguishable blob of light, draw **one single `led_on` bounding box** around the entire merged shape. **Why?** You must label what the camera sees, not what you know. This provides an honest signal to the `ByteTrack` tracker, which is designed to re-identify the individual balls once they become visually distinct again.
*   **DON'T Guess Inside Merged Blobs:** Do not use your real-world knowledge to draw two boxes inside a single visual blob. This will teach the model to hallucinate features that aren't there.
*   **DON'T Label Scenery:** **Crucial Rule.** Never label a ball sitting on a shelf or table *before* juggling as `led_off`. The `dropped_ball` class is an *event*, not a location. A ball sitting on the floor *before* juggling is a background object, not a `dropped_ball`.

#### **Specific, Critical Rules for Labeling Hands:**

*   **DO Model the Functional Area:** The bounding box should encompass the entire hand unit, from just above the wrist crease to the tips of the fingers. This includes the palm and all fingers, as this entire area is used for interaction.
*   **DO Expect Varied Box Shapes:** A hand's bounding box will change shape dramatically with its pose (open hand, fist, pointing). This is correct and teaches the model a robust, pose-invariant concept of a hand.
*   **DO Overlap with Balls During Catches:** When a hand is holding a ball, correctly labeling the inferred full shapes of both will result in overlapping bounding boxes. This overlap is the primary signal your event-detection pipeline will use to identify throws and catches.
*   **DO Handle Overlapping Hands:** If hands are clasped or one is in front of the other, if you can see a visual boundary, label them as two separate, overlapping `hand` boxes. If they are visually merged, label the entire shape as a single `hand`.

---

### **Phase 4: Curation and Final Dataset Assembly**

1.  **Review Your Old "Auto" Dataset (Optional):**
    *   Go through any old images captured with auto-settings.
    *   For each image, ask: "Is it perfectly sharp, in focus, and well-lit?"
    *   If YES, you may keep it and label it according to this protocol.
    *   If NO (any blur, noise, softness), **DELETE IT PERMANENTLY.** It is a liability that will teach the model incorrect labels.
2.  **Check for Balance:** Once all labeling is complete, do a rough count of your labeled instances. Your goal is a distribution that looks something like this:
    *   `led_off`: ~40-45% of total instances.
    *   `led_on`: ~40-45% of total instances.
    *   `dropped_ball`: ~10-20% of total instances.
    *   `hand`: Ensure hands are labeled consistently in all relevant (well-lit) images.
    *   If the `dropped_ball` count is too low, go back and shoot another "Intentional Drops" session. This is non-negotiable for a robust model.

---

### **Phase 5: The Data Augmentation Strategy - Teaching Reality**

This final phase happens automatically during training, but understanding it is key to knowing *why* our data collection protocol is designed the way it is.

**The Role of Augmented Motion Blur:**
We will use an augmentation that applies a realistic, **elongated linear motion blur** to the sharp objects in our dataset. This is not a generic "fuzzy edges" blur (Gaussian blur); it is a directional streak that simulates an object moving while the camera shutter is open. In the high-speed context of your application (60+ FPS), a juggling ball's motion over a single frame is nearly perfectly linear, making this a very high-fidelity simulation.

**Ground Truth and Augmentation: A Critical Distinction**
This directly addresses the question of how to label blur. The process is simple and unambiguous:
1.  **You Label the Ground Truth:** You only ever label the **perfectly sharp** images from your "Sharp/Nominal" and "LED/Glow" profiles. The bounding box you draw is the permanent, absolute ground truth.
2.  **The Pipeline Augments:** During training, the data loader takes a sharp image and its label. It then applies the artificial motion blur *only to the pixels inside the object's bounding box*.
3.  **The Model Learns:** The model is then shown the newly generated **blurry image** but is tasked with predicting the **original, unchanged, sharp bounding box**.
This process teaches the model to effectively "see through" the blur and find the object's true center of mass.

**The Two-Pronged Strategy for Robustness:**
Our entire protocol creates a powerful synergy between real and artificial data:
*   **Augmented Data provides BREADTH:** By augmenting thousands of sharp images, we teach the model the *general concept* of motion blur across every possible background, lighting condition, and object position. This provides immense scale and variety.
*   **Real Blurred Data provides DEPTH:** The small, dedicated set of images from the "Intentional Blur" profile provides the final "dose of reality." These images contain all the subtle physical nuances (slight trajectory curves, rotational texture) that the simulation might miss. This grounds the model's general knowledge in real-world physics.

By following this complete protocol, you are not just creating a dataset; you are engineering the inputs for a high-performance computer vision system. This significant upfront investment will directly translate into a model with the superior accuracy, reliability, and contextual awareness your JuggleHub project demands.
