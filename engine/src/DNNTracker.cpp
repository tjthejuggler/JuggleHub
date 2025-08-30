#include "DNNTracker.hpp"
#include <iostream>

DNNTracker::DNNTracker(const std::string& model_path, const std::string& device_name) {
    // 1. Initialize OpenVINO
    std::cout << "Loading OpenVINO model: " << model_path << std::endl;
    // Force CPU usage to avoid GPU initialization errors on systems without a compatible GPU
    compiled_model = core.compile_model(model_path, "CPU");
    infer_request = compiled_model.create_infer_request();
    std::cout << "Model loaded successfully." << std::endl;

    // 2. Initialize Bytetrack
    // These are the parameters from the Bytetrack library's constructor
    int frame_rate = 30;
    int track_buffer = 150; // Increased from 30 to 150 (5 seconds at 30fps) for more persistent tracking
    float track_thresh = 0.25f; // Match the confidence_threshold_ to allow new tracks to be created
    float high_thresh = 0.6f;
    float match_thresh = 0.8f;
    tracker = std::make_unique<byte_track::BYTETracker>(frame_rate, track_buffer, track_thresh, high_thresh, match_thresh);
}

DNNTracker::~DNNTracker() {}

std::pair<std::vector<TrackedObject>, std::vector<RawDetection>> DNNTracker::update(const cv::Mat& frame) {
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
    std::vector<RawDetection> raw_detections;
    std::vector<byte_track::Object> detections = postprocess(frame, output_tensor, scale_x, scale_y, raw_detections);

    // 4. Update Bytetrack with the new detections
    std::vector<std::shared_ptr<byte_track::STrack>> tracks = tracker->update(detections);

    // 5. Format the output for the main engine
    std::vector<TrackedObject> tracked_objects;
    for (const auto& track : tracks) {
        const auto& rect = track->getRect();
        tracked_objects.push_back({cv::Rect_<float>(rect.x(), rect.y(), rect.width(), rect.height()), (int)track->getTrackId(), 0});
    }
    return {tracked_objects, raw_detections};
}

void DNNTracker::update_setting(const std::string& key, const std::string& value) {
    try {
        if (key == "confidence_threshold") {
            confidence_threshold_ = std::stof(value);
            std::cout << "Updated confidence_threshold to " << confidence_threshold_ << std::endl;
        } else if (key == "nms_threshold") {
            nms_threshold_ = std::stof(value);
            std::cout << "Updated nms_threshold to " << nms_threshold_ << std::endl;
        } else {
            std::cerr << "Warning: Unknown DNNTracker setting key '" << key << "'" << std::endl;
        }
    } catch (const std::invalid_argument& e) {
        std::cerr << "Error: Invalid value for " << key << ": " << value << std::endl;
    } catch (const std::out_of_range& e) {
        std::cerr << "Error: Value out of range for " << key << ": " << value << std::endl;
    }
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

std::vector<byte_track::Object> DNNTracker::postprocess(const cv::Mat& frame, const ov::Tensor& output_tensor, float scale_x, float scale_y, std::vector<RawDetection>& raw_detections) {
    // --- Post-processing for YOLOv8 Output ---
    const float* output_data = output_tensor.data<float>();
    raw_detections.clear();

    // The output shape for YOLOv8 is [1, 84, 8400] -> 84 = 4 box coords + 80 class scores
    cv::Mat output_buffer(output_tensor.get_shape()[1], output_tensor.get_shape()[2], CV_32F, (void*)output_data);
    cv::transpose(output_buffer, output_buffer); // Transpose to [8400, 84] for easier iteration

    std::vector<cv::Rect> boxes;
    std::vector<float> confidences;
    std::vector<int> class_ids;

    float raw_detection_threshold = 0.1f; // A very low threshold to see all potential balls

    for (int i = 0; i < output_buffer.rows; ++i) {
        // Find the class with the highest score
        cv::Mat classes_scores = output_buffer.row(i).colRange(4, 84);
        cv::Point class_id_point;
        double max_class_score;
        cv::minMaxLoc(classes_scores, 0, &max_class_score, 0, &class_id_point);

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
            
            // Populate raw detections for visualization
            if (max_class_score > raw_detection_threshold) {
                raw_detections.push_back({cv::Rect_<float>(left, top, width, height), (float)max_class_score});
            }

            // Add all potential balls to the list for NMS and tracking.
            // The confidence_threshold will be applied inside the NMSBoxes function.
            boxes.push_back(cv::Rect(left, top, width, height));
            confidences.push_back(max_class_score);
            class_ids.push_back(class_id_point.x);
        }
    }

    // Bytetrack is designed to handle raw detections, so we will not apply NMS here.
    // We will format all detections that passed the initial confidence check.
    std::vector<byte_track::Object> objects;
    for (size_t i = 0; i < boxes.size(); ++i) {
        byte_track::Rect<float> rect(boxes[i].x, boxes[i].y, boxes[i].width, boxes[i].height);
        objects.push_back({rect, class_ids[i], confidences[i]});
    }
    return objects;
}