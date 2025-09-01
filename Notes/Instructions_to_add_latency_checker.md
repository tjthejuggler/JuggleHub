You are right to ask for a single, consolidated plan. Combining everything ensures no steps are missed. Here is the complete, self-contained plan to implement the conditional inference latency measurement feature. You can provide this entire block to the next instance.

---

### **Objective: Implement Conditional Inference Latency Measurement**

The goal is to measure the exact model inference time in the C++ engine and display it in the Python UI. This measurement should only occur when the engine is run with the `--verbose` flag to avoid any potential performance impact during normal operation.

**Here is the complete, step-by-step implementation plan:**

---

#### **Step 1: Add Latency Field to Protocol Buffer**

First, update the data contract to include a field for latency.

1.  **Edit the file:** [`api/v1/juggler.proto`](api/v1/juggler.proto)
2.  **Apply this change:** Add a `float` field named `inference_latency_ms` to the `FrameData` message.

```diff
--- a/api/v1/juggler.proto
+++ b/api/v1/juggler.proto
@@ -40,6 +40,7 @@
     repeated Ball balls = 3;
     bytes color_image_b64 = 4;
     repeated RawDetection raw_detections = 5;
+    float inference_latency_ms = 6; // Latency of the model inference in milliseconds
 }
 
 message Ball {

```

---

#### **Step 2: Regenerate Python Protocol Buffer Code**

After modifying the `.proto` file, the corresponding Python code must be regenerated.

1.  **Execute the following command in the terminal:**
    ```bash
    ./scripts/generate_protos.sh
    ```

---

#### **Step 3: Update the `DNNTracker` C++ Header**

Modify the `DNNTracker` class to handle the `verbose` flag and to return the latency value.

1.  **Edit the file:** [`engine/include/DNNTracker.hpp`](engine/include/DNNTracker.hpp)
2.  **Apply this change:** Add a `verbose_` member variable, update the constructor to accept the verbose flag, and change the `update` method's return type from a `std::pair` to a `std::tuple` to include the latency.

```diff
--- a/engine/include/DNNTracker.hpp
+++ b/engine/include/DNNTracker.hpp
@@ -5,6 +5,7 @@
 #include <opencv2/core.hpp>
 #include <openvino/openvino.hpp>
 #include <string>
+#include <tuple>
 #include "bytetrack/BYTETracker.h"
 
 // Forward declarations
@@ -29,7 +30,7 @@
 class DNNTracker {
 public:
-    DNNTracker(const std::string& model_path, const std::string& device_name);
+    DNNTracker(const std::string& model_path, const std::string& device_name, bool verbose = false);
     ~DNNTracker();
-    std::pair<std::vector<TrackedObject>, std::vector<RawDetection>> update(const cv::Mat& frame);
+    std::tuple<std::vector<TrackedObject>, std::vector<RawDetection>, float> update(const cv::Mat& frame);
     void update_setting(const std::string& key, const std::string& value);
 
 private:
@@ -45,6 +46,7 @@
     float confidence_threshold_ = 0.25f; // Default confidence threshold
     float nms_threshold_ = 0.7f;      // Default NMS threshold
     std::unique_ptr<byte_track::BYTETracker> tracker;
+    bool verbose_;
 };
 

```

---

#### **Step 4: Implement the C++ Timing Logic**

Now, add the code to perform the measurement in the `.cpp` file.

1.  **Edit the file:** [`engine/src/DNNTracker.cpp`](engine/src/DNNTracker.cpp)
2.  **Apply these two changes:**
    a. Update the constructor to accept and store the `verbose` flag.
    b. Implement the conditional timing logic within the `update` method and change its return statement.

**Change 1: Constructor**
```diff
--- a/engine/src/DNNTracker.cpp
+++ b/engine/src/DNNTracker.cpp
@@ -1,8 +1,9 @@
 #include "DNNTracker.hpp"
 #include <iostream>
+#include <chrono>
 
-DNNTracker::DNNTracker(const std::string& model_path, const std::string& device_name) {
+DNNTracker::DNNTracker(const std::string& model_path, const std::string& device_name, bool verbose)
+    : verbose_(verbose) {
     // 1. Initialize OpenVINO
     std::cout << "Loading OpenVINO model: " << model_path << std::endl;
     std::cout << "Compiling model for device: " << device_name << std::endl;

```

**Change 2: `update` Method**
```diff
--- a/engine/src/DNNTracker.cpp
+++ b/engine/src/DNNTracker.cpp
@@ -21,21 +21,29 @@
 
 DNNTracker::~DNNTracker() {}
 
-std::pair<std::vector<TrackedObject>, std::vector<RawDetection>> DNNTracker::update(const cv::Mat& frame) {
+std::tuple<std::vector<TrackedObject>, std::vector<RawDetection>, float> DNNTracker::update(const cv::Mat& frame) {
     // --- Main Inference Pipeline ---
 
     // 1. Preprocess the image for the neural network
     float scale_x, scale_y;
     cv::Mat preprocessed_image = preprocess(frame, scale_x, scale_y);
-    
+
     // Create an OpenVINO tensor from the preprocessed image data
     ov::Tensor input_tensor(compiled_model.input().get_element_type(), compiled_model.input().get_shape(), preprocessed_image.data);
     infer_request.set_input_tensor(input_tensor);
 
+    float inference_latency = 0.0f;
+    if (verbose_) {
+        auto start_time = std::chrono::high_resolution_clock::now();
+        infer_request.infer();
+        auto end_time = std::chrono::high_resolution_clock::now();
+        std::chrono::duration<float, std::milli> latency = end_time - start_time;
+        inference_latency = latency.count();
+    } else {
+        infer_request.infer();
+    }
+
     // 2. Run Inference
-    infer_request.infer();
     const ov::Tensor& output_tensor = infer_request.get_output_tensor();
 
     // 3. Postprocess the raw model output to get detections
@@ -51,7 +59,7 @@
         const auto& rect = track->getRect();
         tracked_objects.push_back({cv::Rect_<float>(rect.x(), rect.y(), rect.width(), rect.height()), (int)track->getTrackId(), 0});
     }
-    return {tracked_objects, raw_detections};
+    return {tracked_objects, raw_detections, inference_latency};
 }
 
 void DNNTracker::update_setting(const std::string& key, const std::string& value) {

```

---

#### **Step 5: Connect the Engine to the DNNTracker**

Pass the `verbose` flag to the tracker and handle the new latency value.

1.  **Edit the file:** [`engine/src/Engine.cpp`](engine/src/Engine.cpp)
2.  **Apply these two changes:**
    a. Pass the `verbose_` flag when creating the `DNNTracker`.
    b. Update the `run` method to receive the latency and set it on the `frame_data` message.

```diff
--- a/engine/src/Engine.cpp
+++ b/engine/src/Engine.cpp
@@ -35,7 +35,7 @@
     if (verbose_) std::cout << "Initializing DNNTracker..." << std::endl;
     try {
         // This assumes your models are in JuggleHub/engine/models/
-        dnn_tracker_ = std::make_shared<DNNTracker>("engine/models/yolov8n.xml", device_name);
+        dnn_tracker_ = std::make_shared<DNNTracker>("engine/models/yolov8n.xml", device_name, verbose_);
     } catch (const std::exception& e) {
         std::cerr << "FATAL ERROR: Failed to initialize DNNTracker: " << e.what() << std::endl;
         // Exit or handle the critical failure appropriately
@@ -121,8 +121,9 @@
         if (use_dnn_tracker_) {
             if (!dnn_tracker_) return; // Safety check
 
-            auto [tracker_results, raw_detections] = dnn_tracker_->update(color_image);
+            auto [tracker_results, raw_detections, latency] = dnn_tracker_->update(color_image);
             tracked_objects = tracker_results;
+            frame_data.set_inference_latency_ms(latency);
 
             // Populate raw detections in protobuf
             for (const auto& det : raw_detections) {

```

---

#### **Step 6: Display the Latency in the UI**

Finally, update the Python UI to show the new metric.

1.  **Edit the file:** [`hub/components/ui.py`](hub/components/ui.py)
2.  **Apply these two changes:**
    a. In the `UI.__init__` method, create a new `QLabel` widget for the latency.
    b. In the `UI.update_data` method, update the label's text with the new value from `frame_data`.

**Change 1: `__init__` Method**
```python
# In hub/components/ui.py, inside UI.__init__
# Add this with the other label definitions

self.latency_label = QLabel("Inference Latency: N/A")
self.info_layout.addWidget(self.latency_label)
```

**Change 2: `update_data` Method**
```python
# In hub/components/ui.py, inside UI.update_data
# Add this code to the end of the method

if frame_data.inference_latency_ms > 0:
    self.latency_label.setText(f"Inference Latency: {frame_data.inference_latency_ms:.2f} ms")
else:
    self.latency_label.setText("Inference Latency: N/A")
```

---

#### **Final Verification**

After all changes are applied, the final step is to build and run the application.

1.  **Build the C++ engine:** `./scripts/build_engine.sh`
2.  **Run the application:** `./scripts/run_hub.sh --use-venv`

The UI should now display the "Inference Latency" metric when running, and you can switch between `"CPU"`, `"GPU"`, and `"NPU"` in the [`scripts/run_hub.sh`](scripts/run_hub.sh) file to compare their performance directly.