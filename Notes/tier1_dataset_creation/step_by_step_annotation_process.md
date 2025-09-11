Of course. This is the right way to do it: with a single, definitive protocol that leaves no room for ambiguity. Based on all of our discussions and your project's specific needs, here is the complete, unabridged Master Protocol. This integrates the three-class system, the multi-profile filming strategy, and the detailed rules for handling every edge case.

Replace your old guide with this. Follow it precisely.

---

### **The Master Protocol: A Step-by-Step Guide to Dataset Creation (v4.0)**

**Objective:** To create a comprehensive, high-quality dataset for a YOLO model that can robustly detect juggling balls in real-time under various indoor lighting conditions and states.

**Classes to be Labeled:**
1.  **`led_on`**: A ball that is actively and visibly glowing.
2.  **`led_off`**: Any non-glowing ball, including a standard ball or an LED ball that is switched off.
3.  **`dropped_ball`**: Any type of ball that is on the floor *after an uncontrolled drop*. This class is the highest priority and overrides the other two.

**Core Principles:**
*   **Manual Control is Mandatory:** All captures will use manual camera settings. Auto-exposure, auto-gain, and auto-white-balance are forbidden.
*   **IR Projector ON:** The RealSense depth projector must be enabled for all captures to ensure the training data matches the inference conditions.
*   **Consistency is Key:** The rules defined here are not guidelines; they are absolute. Following them without deviation is what creates a high-performance model.

---

### **Phase 1: Pre-Flight Checklist (Do This Once Before Starting)**

1.  **Prepare Your Wardrobe:** Create at least 5-6 distinct "outfits." An outfit change can be as simple as changing your shirt. The goal is to vary color, pattern (solids, stripes, graphics), and brightness. Lay them out so you can change quickly between sessions and checkpoints.
2.  **Identify Filming Locations:** Select 4-5 different indoor locations with unique lighting characteristics. Examples:
    *   **The Office:** Cool, overhead fluorescent lighting.
    *   **The Living Room:** Warm, off-center lamp lighting.
    *   **The Sun Room / Near Window:** Bright, natural daylight.
    *   **The Hallway:** Dim, single-source lighting.
    *   **The Basement:** Unfinished, potentially mixed or poor lighting.
3.  **Prepare "Hard Negative" Props:** Gather a few common objects that could be mistaken for balls. Place them in your filming locations. Examples: oranges, doorknobs, decorative spheres, rolled-up socks.
4.  **Camera Setup:**
    *   Clean the camera lens with a microfiber cloth.
    *   Mount the camera on a stable tripod. This is crucial for capturing controlled blur and sharp baseline images.

---

### **Phase 2: The Data Capture Sessions (Repeat for Each Location)**

This is the main loop. You will execute this entire protocol, from calibration to negative examples, in *each* of your chosen locations.

#### **SESSION 1: The Office (Cool, Overhead Lighting)**

**Step 1.1: Calibrate All Office Camera Settings**
1.  Launch the RealSense Viewer. Disable all "auto" controls (Exposure, Gain, White Balance). Ensure the IR Projector is ON.
2.  **Calibrate "Sharp/Nominal" Profile (for `led_off`):**
    *   Hold a non-glowing ball. Juggle or move it at a realistic speed.
    *   Set `Exposure` to the lowest possible value that completely **freezes motion** and eliminates all blur. Note this value as **Office_Sharp_Exposure**.
    *   Now, adjust `Gain` until the scene is well-lit but not noisy. Note this as **Office_Nominal_Gain**.
    *   Briefly toggle Auto `White Balance`, note the Kelvin value, then manually set it. Note this as **Office_WB**.
3.  **Calibrate "LED/Glow" Profile (for `led_on`):**
    *   Turn on an LED ball in the dimly lit or dark office.
    *   The goal is to see the ball's color and shape, not a washed-out white blob.
    *   Set a **very fast `Exposure`** (e.g., 500-2000µs). This is the key. Adjust until the glow is not overexposed. Note this as **Office_LED_Exposure**.
    *   Keep `Gain` as low as possible. The background will be dark; this is correct. Note this as **Office_LED_Gain**.
4.  **Calibrate "Intentional Blur" Profile:**
    *   Set a slow, fixed `Exposure` (e.g., 16667µs for 1/60s). Note this as **Office_Blur_Exposure**.
    *   Adjust `Gain` to get a usable image brightness.

**Step 1.2: Capture `led_off` Data (Total: ~350 Images)**
1.  **Attire:** Put on **Outfit #1**.
2.  **Settings:** Use the **"Sharp/Nominal" Profile** (`Office_Sharp_Exposure`, `Office_Nominal_Gain`, `Office_WB`).
3.  **Capture Normal Poses (1-150):**
    *   Capture the `led_off` ball in a wide variety of positions: close to the camera, far away, in corners, on the desk. Vary the camera angle.
    *   Capture interactions: ball in hand, next to a keyboard, partially blocked by a mug.
4.  **Capture Challenging Poses (151-250):**
    *   **Occlusion:** Systematically capture images where the ball is 30-70% hidden by your hand, arm, desk leg, or monitor.
    *   **Truncation:** Capture images where the ball is partially cut off by the edges of the frame.
    *   **Reflections:** Capture images where the ball's reflection is clearly visible in a monitor screen or window.
5.  **Settings:** Switch to the **"Intentional Blur" Profile**.
6.  **Capture Motion Blur (251-350):**
    *   Toss, roll, and drop the ball. Capture clear blur streaks against a sharp background. Get blur in all directions (horizontal, vertical, diagonal).

**Step 1.3: Capture `led_on` Data (Total: ~350 Images)**
1.  **Attire:** Change your shirt. You are now in **Outfit #1.1**.
2.  **Settings:** Use the **"LED/Glow" Profile** (`Office_LED_Exposure`, `Office_LED_Gain`, `Office_WB`).
3.  **Capture Normal Poses (1-150):**
    *   Turn off the main office lights so the room is dim or dark.
    *   Capture the `led_on` ball in various positions: held, stationary on the floor/desk, near decoy lights (monitor LEDs, phone screens).
4.  **Capture Challenging Poses (151-250):**
    *   Capture occlusion where the glowing ball is partially hidden.
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
3.  **Perform Step 1.2: Capture `led_off` Data** (~350 images), following the attire change checkpoint.
4.  **Perform Step 1.3: Capture `led_on` Data** (~350 images), following the attire change checkpoint.
5.  **Perform Step 1.4: Capture `dropped_ball` Data** (~200 images), with a fresh outfit.
6.  **Perform Step 1.5: Capture Hard Negative Data** (~50 images).

After completing 5 full sessions, you will have a comprehensive dataset of approximately 4,750 images, systematically covering a vast range of conditions.

---

### **Phase 3: The Labeling Protocol - The Rules of Truth**

Your labels are the answer key. They must be perfect.

#### **The Labeling Litmus Test: Your Step-by-Step Decision Process**
For any ball in any frame, follow this exact logic:

1.  **Is the ball on the floor after an uncontrolled drop?**
    *   **YES:** Label it **`dropped_ball`**. Stop here. The process is complete.
    *   **NO:** Proceed to the next question.
2.  **Is the ball visibly glowing from its own light source?**
    *   **YES:** Label it as **`led_on`**.
    *   **NO:** Label it as **`led_off`**.

#### **Detailed Labeling Rules (DOs and DON'Ts):**

*   **DO Label Snugly:** For sharp objects, zoom in. The bounding box edges should be tangent to the object's pixels. Minimize background inside the box.
*   **DO Label the Ball, Not the Glow:** For `led_on` balls, the bounding box should cover the physical ball itself, not the fuzzy aura or glow around it. Find the core shape and label it.
*   **DO Follow the Motion Blur Rule:** The box must be the **actual size of the ball**, not the size of the blurred streak. Consistently place the box on the **leading edge** of the blur (the side furthest along the direction of motion).
*   **DO Infer for Occlusion:** If a ball is partially hidden by your hand, draw the bounding box around the **inferred full shape** of the ball, as if you could see through the obstruction.
*   **DO Crop for Truncation:** If a ball is cut off by the frame edge, draw the box to cover **only the visible portion**, stopping exactly at the image border.
*   **DO Label Merged Blobs as One:** If two `led_on` balls merge into a single, visually indistinguishable blob of light, draw **one single `led_on` bounding box** around the entire merged shape.
*   **DO Create Empty Labels for Negatives:** For all images from the "Hard Negative" captures, create a corresponding **completely empty** label file. This is how the model learns what to ignore.

*   **DON'T Label Scenery:** **Crucial Rule.** Never label a ball sitting on a shelf or table *before* juggling as `led_off`. Never label a ball sitting on the floor *before* juggling as `dropped_ball`. These are background objects. The `dropped_ball` class is an *event*, not a location.
*   **DON'T Label Reflections:** Never draw a bounding box around a reflection in a monitor, window, or shiny floor. It is part of the background.
*   **DON'T Guess Inside Merged Blobs:** Do not use your knowledge to draw two boxes inside a single visual blob. Label what you see.
*   **DON'T Label Unidentifiable Fragments:** If less than ~25% of a ball is visible (due to occlusion or truncation) and it's no longer clearly a ball, do not label it.
*   **DON'T Create New Classes:** Never invent a "Not a Ball" class for hard negatives. Their power comes from being unlabeled.

---

### **Phase 4: Curation and Final Dataset Assembly**

1.  **Review Your Old "Auto" Dataset (Optional):**
    *   Go through any old images captured with auto-settings.
    *   For each image, ask: "Is it perfectly sharp, in focus, and well-lit?"
    *   If YES, you may keep it and label it according to the protocol.
    *   If NO (any blur, noise, softness), **DELETE IT PERMANENTLY.** It is a liability.
2.  **Check for Balance:** Once all labeling is complete, do a rough count of your labeled instances. Your goal is a distribution that looks something like this:
    *   `led_off`: ~40-45%
    *   `led_on`: ~40-45%
    *   `dropped_ball`: ~10-20%
    *   If the `dropped_ball` count is too low, go back and shoot another "Intentional Drops" session. This is non-negotiable for a robust model.

You now have a professional-grade, meticulously curated dataset ready for training. This protocol is a significant upfront investment of time, but it will directly translate into a model with superior accuracy, reliability, and the contextual awareness your project demands.

----------------

2025-09-11 13:36:46 this one is old, but being kept for posterity
### **The Master Protocol: A Step-by-Step Guide to Dataset Creation**

**Objective:** To create a dataset of ~6,000+ images for a high-performance, real-time, indoor ball and LED ball detection system.

**Classes:** `led_off`, `led_on`

**Core Principles:**
*   **Resolution:** 640x480 for all captures.
*   **IR Projector:** ON for all captures.
*   **Labeling:** Follow the Definitive Labeling Rules (tight fit on sharp, ball-sized box on leading edge of blur, label only visible parts of occluded objects, label the core of the LED ball not its glow).
*   **Manual Control:** All captures will use manual camera settings. No auto mode.

---

### **SESSION 1: The Office (Cool, Overhead Lighting)**

**Preparation:**
*   **Your Attire:** Put on **Outfit #1** (e.g., light-colored solid t-shirt, jeans, sneakers).
*   **Location Prep:** Tidy the office to a "baseline" state.
*   **Camera Prep:** Clean the lens. Mount the camera on a tripod if possible for stability, but handheld is fine if you keep your shutter speeds fast.

**Step 1.1: Standard `led_off` - Sharp & Nominal (250 Images)**
1.  Launch the RealSense Viewer. Disable all auto controls.
2.  **Calibrate "Sharp" Settings:**
    *   Set `Exposure` low enough to eliminate motion blur when you roll the ball (target: < 8000µs). This is your **Office_Sharp_Exposure**.
    *   Adjust `Gain` for a well-lit image. This is your **Office_Nominal_Gain**.
    *   Toggle Auto `White Balance`, note the value, then manually set it. This is your **Office_WB**.
3.  **Capture Images (1-125):**
    *   Take shots of the ball in diverse positions: close, far, on the desk, on the floor, in corners. Vary your camera angle for each.
4.  **Human Diversity Checkpoint #1:**
    *   **Change your shirt.** You are now wearing **Outfit #1.1** (e.g., dark-colored solid t-shirt, same pants/shoes).
5.  **Capture Images (126-250):**
    *   Continue capturing diverse shots.
    *   **Introduce Interaction:** Capture shots with the ball in your hand, next to a keyboard, next to a mug.
    *   **Introduce Overexposure:** For the last 50 images in this block, significantly increase the `Gain` to create bright, slightly washed-out examples. Reset the gain to **Office_Nominal_Gain** when finished.

**Step 1.2: Standard `led_off` - Sharp, Dim & Edgy (250 Images)**
1.  **Change Your Attire:** Put on **Outfit #2** (e.g., striped shirt, different pants, different shoes).
2.  **Calibrate "Dim" Settings:**
    *   Use **Office_Sharp_Exposure** and **Office_WB**.
    *   Significantly lower the `Gain` so the scene is dark and noisy. This is **Office_Dim_Gain**.
3.  **Capture Dim Images (1-150):**
    *   Focus on challenging lighting. Place the ball in shadows: under the desk, behind the monitor, in a dark corner.
4.  **Capture Edge Cases (151-250):**
    *   Reset `Gain` to **Office_Nominal_Gain**.
    *   Focus on **Occlusion:** Ball partially hidden by a desk leg, your hand, a book.
    *   Focus on **Truncation:** Ball partially cut off by the edge of the 640x480 frame.

**Step 1.3: Standard `led_off` - Motion Blur (100 Images)**
1.  **Calibrate "Blur" Settings:**
    *   Set `Exposure` to a slow value that creates blur (e.g., 16667µs). This is **Office_Blur_Exposure**.
    *   Adjust `Gain` for a well-lit image.
    *   Use **Office_WB**.
2.  **Capture Blurry Images (1-100):**
    *   Capture tosses, rolls, and drops. Ensure blur occurs in multiple directions (horizontal, vertical, diagonal).

**Step 1.4: LED `led_off` - All Scenarios (600 Images)**
1.  **Change Your Attire:** Put on **Outfit #3** (e.g., patterned shirt, same pants/shoes as Outfit #2).
2.  **Capture Sharp, Lights ON (1-200):**
    *   Use **Office_Sharp_Exposure**, **Office_Nominal_Gain**, and **Office_WB**.
    *   Capture the LED ball in diverse positions around the well-lit room.
3.  **Background Diversity Checkpoint #1:**
    *   **Change the scene.** Move a chair, put a backpack on the floor, open a cabinet door.
4.  **Capture Sharp, Lights OFF (201-400):**
    *   Turn off all office lights. The room should be dark.
    *   Use **Office_Sharp_Exposure**. You may need to increase the `Gain` significantly to see the dark background.
    *   Capture the ball stationary in the dark, held in your hand, and placed near reflective surfaces. Remember to label the ball, not the glow.
5.  **Capture Motion Blur (401-500):**
    *   Use **Office_Blur_Exposure**.
    *   Capture 50 images with the lights ON.
    *   Capture 50 images with the lights OFF (these will be streaks of light).
6.  **Capture Edge Cases (501-600):**
    *   Turn lights back on. Use sharp settings.
    *   **Decoy Lights:** Capture the LED ball near your monitor's power light, your phone screen, etc.
    *   **Occlusion:** Capture the LED ball partially hidden but its glow is still visible.

**Step 1.5: Negative Examples (20 Images)**
1.  Capture 20 images of the office from various angles with **NO ball present**. 10 with lights on, 10 with lights off.

**Session 1 Complete. You should have ~1220 images.**

---

### **SESSION 2: The Living Room (Warm, Off-Center Lighting)**

**Preparation:**
*   **Your Attire:** Start with **Outfit #4** (e.g., graphic t-shirt, shorts, different shoes).
*   **Location Prep:** Ensure the scene has typical living room clutter (pillows, remote controls, etc.).

**Step 2.1: Calibrate All Living Room Settings**
1.  Repeat the calibration process from Session 1 to find the new "golden values" for this different lighting:
    *   **LivingRoom_Sharp_Exposure**
    *   **LivingRoom_Nominal_Gain** & **LivingRoom_Dim_Gain**
    *   **LivingRoom_WB** (this will be much "warmer"/lower K value)
    *   **LivingRoom_Blur_Exposure**

**Step 2.2: Execute the Full Capture Protocol**
1.  Follow the **exact same step-by-step image counts and diversity checkpoints** as in Session 1, but using the new `LivingRoom_` settings.
    *   **Step 1.1 equivalent:** `led_off` - Sharp & Nominal (250 images).
        *   **Remember to change your shirt halfway through.** Put on **Outfit #4.1** (e.g., a different solid color shirt).
    *   **Step 1.2 equivalent:** `led_off` - Sharp, Dim & Edgy (250 images).
        *   **Remember to change into a full new outfit.** Start this block with **Outfit #5** (e.g., new patterned shirt, new pants/shorts).
    *   **Step 1.3 equivalent:** `led_off` - Motion Blur (100 images).
    *   **Step 1.4 equivalent:** `led_on` - All Scenarios (600 images).
        *   **Remember to change your shirt.** Start this block with **Outfit #6**.
        *   **Remember to change the background halfway through** (toss a blanket over the sofa, move a lamp).
    *   **Step 1.5 equivalent:** Negative Examples (20 images).

**Session 2 Complete. You now have ~2440 images.**

---

### **SESSIONS 3, 4, & 5: Sun Room, Hallway, Basement**

**Preparation:**
*   For each new session, you start with a **new, unique outfit**.
*   For each new session, you must **re-calibrate all camera settings** as the lighting is completely different. The `_WB` (White Balance) will be the most dramatic change between locations.

**Execution:**
*   Repeat the **entire, rigorous protocol** from Session 1 for each of the remaining locations.
*   Dutifully change your shirt/outfit at the specified checkpoints.
*   Dutifully alter the background scenery during the LED ball capture block.
*   Do not skip any steps. The repetition is what builds a robust dataset.

---

### **Final Phase: Curation of Old "Auto" Dataset**

**Action:**
1.  Open the folder of your old images captured with auto settings.
2.  Go through them one by one.
3.  **ASK ONE QUESTION:** "Is this image perfectly sharp and in focus?"
    *   If **YES**, move the image into your new dataset's `images` folder. It is a valuable, free sample.
    *   If **NO** (any blur, softness, or focus issues), **DELETE IT PERMANENTLY.** Do not hesitate. Its potential to harm your model with bad labels is greater than its value.

This operational script, followed precisely, will leave no stone unturned. It forces diversity in lighting, human appearance, background, object type, motion, and edge cases. It is a significant undertaking, but it is the blueprint for success.


-------------------




2025-09-11 13:20:47
Of course. Let's consolidate everything into a comprehensive, step-by-step action plan. This guide will be your definitive playbook from setup to training.

### The Complete Plan: Building a Robust YOLOv11n Dataset for Object Tracking

This plan is designed to maximize your model's real-world performance by creating a dataset that is both high-quality and intentionally challenging.

---

