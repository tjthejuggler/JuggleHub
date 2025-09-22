2025-09-22 18:33:39

Project Vision: Achieving Robust Juggling Analysis

Our goal is to create a premier system for real-time juggling analysis. The foundation of this system is the ability to have a consistent, accurate location for every ball and hand in every single frame. This persistent tracking is essential for all downstream tasks, from pattern recognition and AI coaching to interactive art.
The Challenge: The Limits of a Single-Model Approach

We are currently using a single, fine-tuned YOLO model to detect both juggling balls and the juggler's hands. This has revealed a fundamental challenge:

    Balls are "easy" targets: They are uniform, high-contrast objects that YOLO can be trained to detect with high accuracy on a relatively small dataset.

    Hands are "hard" targets: They are complex, deformable objects with immense variation in pose, lighting, and occlusion.

While our model is adequate for balls, it struggles to provide the reliable, consistent hand tracking we need. This weakness in hand detection is the root cause of our primary issue: the system fails to understand when a ball is caught. When a ball disappears into an undetected hand, its tracker is eventually lost, gets "stuck to the wall," and breaks the continuity of the analysis. Trying to perfect a single model for both tasks would require a prohibitively large dataset of hand images and would still likely result in a compromise.
The Solution: A Hybrid, "Best-in-Class" Architecture

Our plan is to pivot from a single "generalist" model to a "hybrid specialist" architecture, using the absolute best tool for each specific task and fusing their results.

    The Ball Expert (YOLO): We will retrain our YOLO model to do one thing perfectly: detect juggling balls. By removing all hand data, the model will become smaller, faster, and even more accurate at its specialized task.

    The Hand Expert (MediaPipe): We will integrate Google's MediaPipe Hand Landmarker, the industry-standard solution for hand tracking. It is a pre-trained, highly optimized model that provides incredibly robust and detailed information—not just a bounding box, but 21 3D landmarks for each hand.

In every frame, the engine will run both specialist models concurrently, combining their high-quality outputs. This gives us the best of both worlds: state-of-the-art hand tracking and hyper-specialized ball detection.

This hybrid approach solves our core problem, eliminates the need for further hand annotation, and provides a much richer, more accurate data stream, paving the way for the advanced analysis features central to the JuggleHub vision. For development and comparison, we will retain the ability to use the old "YOLO-only" method via a command-line flag.

### **Part 2: Detailed Implementation Plan**

This plan covers the C++ engine modifications, build system changes, and API updates.

#### **Step 1: Build System - Add MediaPipe as a Dependency**

MediaPipe must be integrated into our CMake build process. This is the most complex external step.

1.  **Download and Build MediaPipe:**
    *   Follow the official MediaPipe instructions to install it on Linux. This typically involves using `bazel` to build the required libraries. The key output we need is a static or shared library file (e.g., `libmediapipe_c.so`) and the corresponding C++ headers.
    *   We will specifically use the **MediaPipe C++ API**, not the Python or JS versions.

2.  **Update `engine/CMakeLists.txt`:**
    *   Add `find_package(MediaPipe REQUIRED)` to locate the installed library and headers. We will need to create a `FindMediaPipe.cmake` file to help CMake find the necessary components if one is not provided by the MediaPipe installation.
    *   Add the MediaPipe include directories to our target: `target_include_directories(juggle_engine PRIVATE ${MediaPipe_INCLUDE_DIRS})`.
    *   Link the `juggle_engine` executable against the MediaPipe library: `target_link_libraries(juggle_engine PRIVATE ${MediaPipe_LIBRARIES})`.

#### **Step 2: C++ - Create the `HandTracker` Class**

To keep the code clean, we will encapsulate all MediaPipe logic into its own class, similar to `DNNTracker`.

1.  **Create `engine/include/HandTracker.hpp`:**
    *   This class will manage the MediaPipe Hand Landmarker.
    *   It needs to be initialized with the model file (`.task` file) provided by MediaPipe.
    *   It will have one primary public method: `std::vector<HandResult> update(const cv::Mat& frame);`.
    *   The `HandResult` struct will contain the 21 3D landmarks and a simple bounding box derived from those landmarks for each detected hand.

2.  **Create `engine/src/HandTracker.cpp`:**
    *   Implement the constructor to initialize the MediaPipe graph and load the model.
    *   Implement the `update` method. This will involve:
        *   Converting the `cv::Mat` frame into MediaPipe's native `ImageFrame` format.
        *   Sending the frame into the MediaPipe graph.
        *   Receiving the output packets containing the landmark data.
        *   Parsing the landmark data into our `HandResult` structs.

#### **Step 3: C++ - Refactor the `Engine` Class to Manage Both Trackers**

The main `Engine` class will now orchestrate both the `DNNTracker` (for balls) and the new `HandTracker`.

1.  **Modify `engine/include/Engine.hpp`:**
    *   Add `#include "HandTracker.hpp"`.
    *   Add a `std::unique_ptr<HandTracker> hand_tracker_;`.
    *   Add a boolean flag `bool use_mediapipe_hands_ = true;` to control the new behavior.
    *   Modify the constructor to accept the new `--yolo_balls_and_hands` flag and set `use_mediapipe_hands_` accordingly.

2.  **Modify `engine/src/Engine.cpp`:**
    *   **In the constructor:**
        *   Based on the `use_mediapipe_hands_` flag, initialize either *just* the `dnn_tracker_` (old mode) or *both* the `dnn_tracker_` and the new `hand_tracker_` (new hybrid mode).
        *   When in hybrid mode, the `DNNTracker` should be initialized with the **new, ball-only YOLO model**.
    *   **In the `run()` method's main loop:**
        *   The core logic will branch based on `use_mediapipe_hands_`.
        *   **If `true` (Hybrid Mode):**
            1.  Call `auto ball_objects = dnn_tracker_->update(...)`. This now only returns balls.
            2.  Call `auto hand_results = hand_tracker_->update(...)`. This returns hands from MediaPipe.
            3.  Combine the results from both trackers. The hand data from MediaPipe will need to be converted into our `TrackedObject` format to be fed into the persistent tracking system. We can generate a simple bounding box by finding the min/max coordinates of the 21 landmarks.
        *   **If `false` (YOLO-Only Mode):**
            1.  The code will run as it did before, calling only `dnn_tracker_->update(...)`, which will return both balls and hands from the generalist YOLO model.
    *   The rest of the loop (protobuf serialization) will work as-is, because both branches produce a unified `std::vector<TrackedObject>`.

#### **Step 4: C++ - Update the `DNNTracker` for Ball-Only Operation**

The `DNNTracker`'s logic needs to be simplified when in hybrid mode.

1.  **Modify `DNNTracker.cpp`:**
    *   The `manage_hand_tracks` and much of the hand-related logic can be bypassed if the model is ball-only. The `class_names_` member should be updated to reflect the new model's output (e.g., `{"led_on", "led_off", "dropped_ball"}`).
    *   The `manage_ball_occlusion` logic will now receive hand data that originates from MediaPipe, but its internal logic (checking distances) remains the same and perfectly valid.

#### **Step 5: Command Line & Configuration**

1.  **Update `engine/src/main.cpp`:**
    *   Add a new command-line option `--yolo_balls_and_hands`.
    *   Pass the value of this flag to the `Engine` constructor.
2.  **Update `scripts/run_hub.sh`:**
    *   Add logic to pass the `--yolo_balls_and_hands` flag through to the C++ engine if it's provided to the script.
