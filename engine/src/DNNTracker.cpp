#include "DNNTracker.hpp"
#include <iostream>
#include <numeric>
#include <algorithm>

DNNTracker::DNNTracker(const std::string& model_path, const std::string& device_name) {
    // 1. Load the OpenVINO model
    ov::Core core;
    try {
        compiled_model = core.compile_model(model_path, device_name);
    } catch (const ov::Exception& ex) {
        std::cerr << "Error compiling model: " << ex.what() << std::endl;
        throw;
    }

    // 2. Get input and output information
    // Assuming a single input port
    ov::Output<const ov::Node> input_port = compiled_model.input();
    ov::Shape input_shape = input_port.get_shape();
    model_input_size = cv::Size(input_shape[3], input_shape[2]); // Width, Height

    // Initialize ByteTracker
    // Parameters: frame_rate (e.g., 30 for typical cameras), track_buffer (e.g., 30 for 1 second)
    // Adjust these based on your expected frame rate and tracking requirements
    byte_tracker = std::make_unique<BYTETracker>(30, 30);
}

std::vector<STrackPtr> DNNTracker::update(const cv::Mat& frame) {
    // 1. Preprocess the input frame
    cv::Mat resized_frame;
    cv::resize(frame, resized_frame, model_input_size);

    // Convert to float and normalize if necessary (YOLOv8 expects 0-1 range)
    resized_frame.convertTo(resized_frame, CV_32F, 1.0 / 255.0);

    // Create OpenVINO tensor from cv::Mat
    ov::Tensor input_tensor(compiled_model.input().get_element_type(), compiled_model.input().get_shape(), resized_frame.data);

    // 2. Perform inference
    infer_request = compiled_model.create_infer_request();
    infer_request.set_input_tensor(input_tensor);
    infer_request.infer();

    // 3. Get output tensor
    const ov::Tensor& output_tensor = infer_request.get_output_tensor();

    // 4. Postprocess the output
    std::vector<Object> detected_objects = postprocess(output_tensor, frame.size());

    // Convert detected_objects to BBox type for ByteTrack
    std::vector<BBox> detections;
    for (const auto& obj : detected_objects) {
        detections.push_back(BBox{
            obj.rect.x, obj.rect.y, obj.rect.width, obj.rect.height, obj.prob, obj.label
        });
    }

    // 5. Update ByteTracker
    // frame_id and timestamp are often incremented or derived from an external source
    // For simplicity here, we use dummy values.
    // You might want to pass frame_id and timestamp from Engine.cpp
    return byte_tracker->update(detections, frame.cols, frame.rows, 1, 0.0);
}

std::vector<Object> DNNTracker::postprocess(const ov::Tensor& output_tensor, const cv::Size& original_size) {
    std::vector<Object> objects;
    const float* output_data = output_tensor.data<const float>();
    ov::Shape output_shape = output_tensor.get_shape();

    // YOLOv8 output format: [1, 84, 8400]
    // 84 -> 4 bounding box coords (cx, cy, w, h) + 80 class scores
    // 8400 -> number of detections
    
    // Transpose the output to make it easier to iterate (from [1, 84, 8400] to [1, 8400, 84])
    // OpenVINO output is often NCHW, but YOLOv8 usually outputs NC*num_boxes, where C is features
    // Let's assume the data is already flattened and correctly ordered for direct iteration over boxes.
    // If output_shape[1] is 84 and output_shape[2] is 8400, then we iterate 8400 times.
    size_t num_detections = output_shape[2]; // 8400
    size_t num_features = output_shape[1];   // 84 (bbox + scores)
    const float confidence_threshold = 0.25f; // YOLOv8 default confidence
    const float nms_threshold = 0.45f;        // NMS threshold

    std::vector<cv::Rect> bboxes;
    std::vector<float> confidences;
    std::vector<int> class_ids;

    for (size_t i = 0; i < num_detections; ++i) {
        // Data for current detection: (4 bbox + 80 classes)
        const float* detection_data = output_data + i * num_features;

        // Find the class with the highest score
        float max_score = 0;
        int class_id = -1;
        for (size_t j = 4; j < num_features; ++j) { // Scores start from index 4
            if (detection_data[j] > max_score) {
                max_score = detection_data[j];
                class_id = j - 4; // Adjust index to get class ID (0-79)
            }
        }

        // Apply confidence threshold
        if (max_score > confidence_threshold) {
            // Extract bounding box (cx, cy, w, h)
            float cx = detection_data[0];
            float cy = detection_data[1];
            float w = detection_data[2];
            float h = detection_data[3];

            // Convert (cx, cy, w, h) to (x, y, width, height)
            float x = cx - w / 2;
            float y = cy - h / 2;

            // Scale bounding box to original image size
            float x_scale = (float)original_size.width / model_input_size.width;
            float y_scale = (float)original_size.height / model_input_size.height;

            cv::Rect_<float> rect_scaled(x * x_scale, y * y_scale, w * x_scale, h * y_scale);
            
            bboxes.push_back(rect_scaled);
            confidences.push_back(max_score);
            class_ids.push_back(class_id);
        }
    }

    // Apply Non-Maximum Suppression (NMS)
    std::vector<int> indices;
    cv::dnn::NMSBoxes(bboxes, confidences, confidence_threshold, nms_threshold, indices);

    for (int idx : indices) {
        Object obj;
        obj.rect = bboxes[idx];
        obj.label = class_ids[idx];
        obj.prob = confidences[idx];
        objects.push_back(obj);
    }

    return objects;
}