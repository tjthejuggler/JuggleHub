#include "DNNTracker.hpp"
#include <iostream>

DNNTracker::DNNTracker(const std::string& model_path, const std::string& device_name) {
    // 1. Initialize OpenVINO
    std::cout << "Loading OpenVINO model: " << model_path << std::endl;
    compiled_model = core.compile_model(model_path, device_name);
    infer_request = compiled_model.create_infer_request();
    std::cout << "Model loaded successfully." << std::endl;

    // 2. Initialize Bytetrack
    // These are the parameters from the Bytetrack library's constructor
    int frame_rate = 30;
    int track_buffer = 30;
    float track_thresh = 0.5f;
    float high_thresh = 0.6f;
    float match_thresh = 0.8f;
    tracker = std::make_unique<byte_track::BYTETracker>(frame_rate, track_buffer, track_thresh, high_thresh, match_thresh);
}

DNNTracker::~DNNTracker() {}

std::vector<TrackedObject> DNNTracker::update(const cv::Mat& frame) {
    // --- Main Inference Pipeline ---

    // 1. Preprocess the image for the neural network
    float scale_x, scale_y;
    cv::Mat preprocessed_image = preprocess(frame, scale_x, scale_y);
    
    // Create an OpenVINO tensor from the preprocessed image data
    ov::Tensor input_tensor(compiled_model.input().get_element_type(), compiled_model.input().get_shape(), preprocessed_image.data);
    infer_request.set_input_tensor(input_tensor);

    // 2. Run Inference
    infer_request.infer();
    const ov::Tensor& output_tensor = infer_request.get_output_tensor();

    // 3. Postprocess the raw model output to get detections
    std::vector<byte_track::Object> detections = postprocess(frame, output_tensor, scale_x, scale_y);

    // 4. Update Bytetrack with the new detections
    std::vector<std::shared_ptr<byte_track::STrack>> tracks = tracker->update(detections);

    // 5. Format the output for the main engine
    std::vector<TrackedObject> tracked_objects;
    for (const auto& track : tracks) {
        const auto& rect = track->getRect();
        tracked_objects.push_back({cv::Rect_<float>(rect.x(), rect.y(), rect.width(), rect.height()), (int)track->getTrackId(), 0});
    }
    return tracked_objects;
}

cv::Mat DNNTracker::preprocess(const cv::Mat& frame, float& scale_x, float& scale_y) {
    // --- Image Preprocessing for YOLOv8 ---
    // YOLOv8 expects a 640x640 BGR image, normalized to [0,1] in NCHW format.
    cv::Mat resized_frame;
    cv::resize(frame, resized_frame, cv::Size(input_width_, input_height_));
    
    scale_x = (float)frame.cols / input_width_;
    scale_y = (float)frame.rows / input_height_;

    // Convert to float and normalize
    cv::Mat float_frame;
    resized_frame.convertTo(float_frame, CV_32F, 1.0 / 255.0);

    // Convert from HWC (Height, Width, Channels) to CHW (Channels, Height, Width)
    cv::Mat chw_frame = cv::dnn::blobFromImage(float_frame);
    return chw_frame;
}

std::vector<byte_track::Object> DNNTracker::postprocess(const cv::Mat& frame, const ov::Tensor& output_tensor, float scale_x, float scale_y) {
    // --- Post-processing for YOLOv8 Output ---
    const float* output_data = output_tensor.data<float>();

    // The output shape for YOLOv8 is [1, 84, 8400] -> 84 = 4 box coords + 80 class scores
    cv::Mat output_buffer(output_tensor.get_shape()[1], output_tensor.get_shape()[2], CV_32F, (void*)output_data);
    cv::transpose(output_buffer, output_buffer); // Transpose to [8400, 84] for easier iteration

    std::vector<cv::Rect> boxes;
    std::vector<float> confidences;
    std::vector<int> class_ids;

    for (int i = 0; i < output_buffer.rows; ++i) {
        // Find the class with the highest score
        cv::Mat classes_scores = output_buffer.row(i).colRange(4, 84);
        cv::Point class_id_point;
        double max_class_score;
        cv::minMaxLoc(classes_scores, 0, &max_class_score, 0, &class_id_point);

        if (max_class_score > confidence_threshold_) {
            // Class ID 32 is "sports ball" in the standard COCO dataset
            if (class_id_point.x == 32) {
                float cx = output_buffer.at<float>(i, 0);
                float cy = output_buffer.at<float>(i, 1);
                float w = output_buffer.at<float>(i, 2);
                float h = output_buffer.at<float>(i, 3);

                // Scale boxes back to original frame size
                int left = static_cast<int>((cx - 0.5 * w) * scale_x);
                int top = static_cast<int>((cy - 0.5 * h) * scale_y);
                int width = static_cast<int>(w * scale_x);
                int height = static_cast<int>(h * scale_y);

                boxes.push_back(cv::Rect(left, top, width, height));
                confidences.push_back(max_class_score);
                class_ids.push_back(class_id_point.x);
            }
        }
    }

    // Apply Non-Maximum Suppression (NMS) to remove overlapping boxes
    std::vector<int> indices;
    cv::dnn::NMSBoxes(boxes, confidences, confidence_threshold_, nms_threshold_, indices);

    // Format detections for Bytetrack
    std::vector<byte_track::Object> objects;
    for (int idx : indices) {
        const auto& box = boxes[idx];
        byte_track::Rect<float> rect(box.x, box.y, box.width, box.height);
        objects.push_back({rect, class_ids[idx], confidences[idx]});
    }
    return objects;
}