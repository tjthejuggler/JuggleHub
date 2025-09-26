Here is a detailed, step-by-step guide for integrating YOLO-Pose into JuggleHub, designed to directly replace your MediaPipe plan while perfectly aligning with your project's vision and technical foundation.

---

### **Part 1: The Vision: A Unified, High-Performance Architecture**

Our goal remains to create a premier system for real-time juggling analysis, founded on consistent, accurate tracking of every ball and hand.

#### **The Challenge: The Complexity of a Hybrid Framework**

The MediaPipe plan proposed a hybrid system: a fine-tuned YOLO for balls and the MediaPipe framework for hands. While this uses "best-in-class" components, it forces us to maintain two completely separate AI ecosystems within our C++ engine:
*   **YOLO + OpenVINO:** Our fast, optimized, and familiar pipeline.
*   **MediaPipe:** A new, complex dependency with its own build system (Bazel), runtime, and data formats, adding significant maintenance overhead.

#### **The Solution: A Unified YOLO-Pose Architecture**

We will achieve a more elegant and higher-performance solution by fully committing to the OpenVINO ecosystem you have already established. Instead of adding a foreign framework, we will simply run a second, specialized OpenVINO model for pose estimation.

*   **The Ball Expert (YOLOv11):** Your existing `DNNTracker` remains, focused solely on detecting balls with maximum accuracy using your fine-tuned `yolo11n` model.
*   **The Pose Expert (YOLOv11-Pose):** We will introduce a new `PoseTracker` class. This class will be a near-clone of your `DNNTracker` but will be responsible for running a pre-trained `yolo11n-pose` model. It will provide robust, identity-aware hand locations by extracting the left and right wrist keypoints.

This approach gives us **three major advantages** over the MediaPipe plan:
1.  **Zero New Dependencies:** We are adding a new model, not a new framework. The entire system continues to rely only on OpenVINO, OpenCV, and CMake. The build process remains simple and fast.
2.  **Maximum Performance:** We can run both models through the same highly optimized OpenVINO runtime, taking full advantage of hardware acceleration (iGPU, NPU) for both tasks simultaneously without context switching between different inference engines.
3.  **Architectural Purity:** The code for both trackers will be nearly identical, differing only in the post-processing logic. This makes the system easier to understand, maintain, and extend.

This unified architecture delivers state-of-the-art hand and ball tracking while keeping the C++ engine lean, fast, and consistent.

### **Part 2: Detailed Implementation Plan**

This plan covers model preparation, API updates, C++ engine modifications, and build system changes.

#### **Step 1: Model Preparation**

First, we need to get the `yolo11n-pose` model and convert it into the OpenVINO Intermediate Representation (.xml/.bin) format that your engine requires.

1.  **Install/Update Ultralytics:**
    ```bash
    pip install ultralytics --upgrade
    ```

2.  **Export the Model:** Run this command in your terminal. It will download the pre-trained PyTorch model (`.pt`) and export it to OpenVINO format.
    ```bash
    yolo export model=yolov11n-pose.pt format=openvino imgsz=640
    ```
    *Note: `imgsz=640` sets the default input resolution. You can experiment with smaller sizes like 320 for even better performance.*

3.  **Place the Model Files:** The export command will create `yolov11n-pose_openvino_model/` containing `yolov11n-pose.xml` and `yolov11n-pose.bin`. Copy these two files into your `engine/models/` directory alongside your existing ball tracker model. Your `engine/models` directory should now look like this:
    ```
    engine/models/
    ├── yolo11n.bin
    ├── yolo11n.xml
    ├── yolov11n-pose.bin  <-- NEW
    └── yolov11n-pose.xml  <-- NEW
    ```

#### **Step 2: API Update (`api/v1/juggler.proto`)**

We need to create rich, dedicated message types for hands and their keypoints. This is far better than using a generic object structure.

1.  **Define `KeyPoint` and `Hand` Messages:** Add these new message definitions to `api/v1/juggler.proto`.
    ```protobuf
    message KeyPoint {
  // This enum represents the 17 standard keypoints from the COCO dataset.
  // YOLO-Pose models are trained on this format, and their output tensor
  // will contain keypoints in this exact order (index 0 to 16).
  enum KeyPointType {
    NOSE = 0;
    LEFT_EYE = 1;
    RIGHT_EYE = 2;
    LEFT_EAR = 3;
    RIGHT_EAR = 4;
    LEFT_SHOULDER = 5;
    RIGHT_SHOULDER = 6;
    LEFT_ELBOW = 7;
    RIGHT_ELBOW = 8;
    LEFT_WRIST = 9;
    RIGHT_WRIST = 10;
    LEFT_HIP = 11;
    RIGHT_HIP = 12;
    LEFT_KNEE = 13;
    RIGHT_KNEE = 14;
    LEFT_ANKLE = 15;
    RIGHT_ANKLE = 16;
  }
  
  KeyPointType type = 1;
  float x = 2;          // Image coordinate x
  float y = 3;          // Image coordinate y
  float confidence = 4; // Confidence score for this specific keypoint
}

    message Hand {
      enum HandType {
        HAND_UNKNOWN = 0;
        LEFT = 1;
        RIGHT = 2;
      }
      HandType type = 1;
      int32 id = 2; // Persistent ID (e.g., 0 for left, 1 for right)
      
      // The single most important keypoint for this hand's location
      KeyPoint wrist = 3; 

      // Optional: Bounding box derived from the person detection
      float box_x = 4;
      float box_y = 5;
      float box_width = 6;
      float box_height = 7;
    }
    ```

2.  **Update `FrameData` Message:** Modify the main `FrameData` message to use the new specific types.
    ```protobuf
    message FrameData {
      uint64 timestamp_us = 1;
      // ... other fields like camera_intrinsics ...
      
      repeated Ball balls = 10; // Assuming you have or will create a Ball message
      repeated Hand hands = 11; // Use our new Hand message
    }
    ```
    *(Remember to run your `./scripts/generate_protos.sh` or equivalent build step after changing the `.proto` file to regenerate the C++ and Python code.)*

#### **Step 3: C++ - Create the `PoseTracker` Class**

We will encapsulate the YOLO-Pose logic in its own class. This class will be a near-perfect copy of `DNNTracker`, adapted for pose output.

1.  **Create `engine/include/PoseTracker.hpp`:**
    *   Copy `engine/include/DNNTracker.hpp` to `engine/include/PoseTracker.hpp`.
    *   Rename the class from `DNNTracker` to `PoseTracker`.
    *   The public `update` method should now return `std::vector<juggle_hub::api::v1::Hand>`, using the new protobuf message type.

2.  **Create `engine/src/PoseTracker.cpp`:**
    *   Copy `engine/src/DNNTracker.cpp` to `engine/src/PoseTracker.cpp`.
    *   Rename the class implementation to `PoseTracker`.
    *   **Modify the `update` and post-processing methods:** This is the only significant change. The core inference logic is identical. You need to parse the output tensor from YOLO-Pose.
        *   The output shape will be different. It will be something like `[1, 56, N]` where `56` is `(box_coords + confidence + num_keypoints * 3)`.
        *   Iterate through the detections.
        *   For each detection (a person):
            *   Extract the bounding box coordinates.
            *   Extract the keypoint data. Keypoints come in triples: `(x, y, confidence)`.
            *   Find the keypoints for the **Left Wrist (index 9)** and **Right Wrist (index 10)**.
            *   Create two `juggle_hub::api::v1::Hand` objects, one for `LEFT` and one for `RIGHT`.
            *   Populate each `Hand` message with its type, a persistent ID (0 for left, 1 for right), and the `KeyPoint` message for its wrist.
        *   Return the vector of populated `Hand` messages.

#### **Step 4: C++ - Refactor the `Engine` Class to Manage Both Trackers**

The main `Engine` class will orchestrate both trackers.

1.  **Modify `engine/include/Engine.hpp`:**
    *   Add `#include "PoseTracker.hpp"`.
    *   Add a new member: `std::unique_ptr<PoseTracker> pose_tracker_;`.
    *   Add a boolean flag from the command line, e.g., `track_hands_`.

2.  **Modify `engine/src/Engine.cpp`:**
    *   **In the constructor:**
        *   Initialize the `dnn_tracker_` as before, loading the ball model (e.g., `yolo11n`).
        *   If `track_hands_` is true, initialize the `pose_tracker_` and tell it to load the pose model (`yolo11n-pose`).
    *   **In the `run()` method's main loop:**
        1.  Call `auto ball_objects = dnn_tracker_->update(...)`. This returns your tracked balls.
        2.  If `track_hands_` is true, call `auto hand_objects = pose_tracker_->update(...)`. This returns the `Hand` protobuf messages from YOLO-Pose.
        3.  **Populate `FrameData`:**
            *   Copy the `ball_objects` into the `FrameData`'s `balls` field.
            *   If hands were tracked, copy the `hand_objects` into the `FrameData`'s `hands` field.
        4.  Serialize and send the `FrameData` protobuf as before.

    *   **Crucial Simplification:** All previous complex logic for `manage_hand_tracks`, inferring left/right, and handling identity swaps can be **completely deleted**. YOLO-Pose's skeletal tracking provides the correct left/right identity on every frame, which is a massive simplification and improvement. The `manage_ball_occlusion` logic can now use the highly reliable hand positions from the `pose_tracker_`.

#### **Step 5: Command Line & Configuration**

1.  **Update `engine/src/main.cpp`:**
    *   Your existing `--track-hands` flag is perfect. Ensure it's passed to the `Engine` constructor.

2.  **Update `scripts/run_hub.sh`:**
    *   Modify the script to pass the `--track-hands` flag to the C++ engine when desired.
    *   Since you have a `--model` flag, you can make the engine smarter. You could have a single `--use-dnn-tracker` which loads the ball model, and `--track-hands` which *additionally* loads the pose model. This fits your existing structure well.

This integration plan leverages the full power of your existing architecture, minimizes new code, eliminates external dependencies, and provides a state-of-the-art, robust foundation for your pattern recognition pipeline.