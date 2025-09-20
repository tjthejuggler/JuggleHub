#include "DNNTracker.hpp"
#include <iostream>
#include <algorithm> // Required for std::max and std::min

// --- CORRECTED HELPER FUNCTION ---
// The function now calls the member functions x(), y(), width(), height()
// with parentheses, which is the correct syntax.
static float calculate_iou(const byte_track::Rect<float>& box1, const byte_track::Rect<float>& box2) {
    // Find the coordinates of the intersection rectangle
    float xA = std::max(box1.x(), box2.x());
    float yA = std::max(box1.y(), box2.y());
    float xB = std::min(box1.x() + box1.width(), box2.x() + box2.width());
    float yB = std::min(box1.y() + box1.height(), box2.y() + box2.height());

    // Compute the area of intersection
    float intersection_area = std::max(0.0f, xB - xA) * std::max(0.0f, yB - yA);

    // Compute the area of both bounding boxes
    float box1_area = box1.width() * box1.height();
    float box2_area = box2.width() * box2.height();

    // Compute the area of the union
    float union_area = box1_area + box2_area - intersection_area;

    // Compute the IoU
    if (union_area > 0) {
        return intersection_area / union_area;
    } else {
        return 0.0f;
    }
}


DNNTracker::DNNTracker(const std::string& model_path, const std::string& device_name) {
    // 1. Initialize OpenVINO
    std::cout << "Loading OpenVINO model: " << model_path << std::endl;
    std::cout << "Compiling model for device: " << device_name << std::endl;
    compiled_model = core.compile_model(model_path, device_name);
    infer_request = compiled_model.create_infer_request();
    std::cout << "Model loaded successfully." << std::endl;

    // 2. Initialize Bytetrack
    int frame_rate = 30;
    int track_buffer = 150; 
    float track_thresh = 0.25f;
    float high_thresh = 0.6f;
    float match_thresh = 0.8f;
    tracker = std::make_unique<byte_track::BYTETracker>(frame_rate, track_buffer, track_thresh, high_thresh, match_thresh);
}

DNNTracker::~DNNTracker() {}

std::pair<std::vector<TrackedObject>, std::vector<RawDetection>> DNNTracker::update(const cv::Mat& frame) {
    // --- Main Inference Pipeline ---

    // 1. Preprocess
    float scale_x, scale_y;
    cv::Mat preprocessed_image = preprocess(frame, scale_x, scale_y);
    
    ov::Tensor input_tensor(compiled_model.input().get_element_type(), compiled_model.input().get_shape(), preprocessed_image.data);
    infer_request.set_input_tensor(input_tensor);

    // 2. Run Inference
    infer_request.infer();
    const ov::Tensor& output_tensor = infer_request.get_output_tensor();

    // 3. Postprocess to get raw detections (these have class IDs)
    std::vector<RawDetection> raw_detections;
    std::vector<byte_track::Object> detections = postprocess(frame, output_tensor, scale_x, scale_y, raw_detections);

    // 4. Update Bytetrack to get tracks (these have stable track IDs but no class IDs)
    std::vector<std::shared_ptr<byte_track::STrack>> tracks = tracker->update(detections);

    // 5. Re-associate tracks with their original detections to get the class ID
    std::vector<TrackedObject> tracked_objects;
    for (const auto& track : tracks) {
        const auto& track_rect = track->getRect();
        float best_iou = 0.0f;
        int associated_class_id = -1;

        for (const auto& det : detections) {
            float iou = calculate_iou(track_rect, det.rect);
            if (iou > best_iou) {
                best_iou = iou;
                associated_class_id = det.label; // 'label' holds the class_id
            }
        }
        
        if (best_iou > 0.8f) {
            tracked_objects.push_back({
                // Also corrected here to use function calls
                cv::Rect_<float>(track_rect.x(), track_rect.y(), track_rect.width(), track_rect.height()),
                (int)track->getTrackId(),
                associated_class_id
            });
        }
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
    cv::Mat resized_frame;
    cv::resize(frame, resized_frame, cv::Size(input_width_, input_height_));
    
    scale_x = (float)frame.cols / input_width_;
    scale_y = (float)frame.rows / input_height_;

    cv::Mat float_frame;
    resized_frame.convertTo(float_frame, CV_32F, 1.0 / 255.0);

    return cv::dnn::blobFromImage(float_frame);
}

std::vector<byte_track::Object> DNNTracker::postprocess(const cv::Mat& frame, const ov::Tensor& output_tensor, float scale_x, float scale_y, std::vector<RawDetection>& raw_detections) {
    raw_detections.clear();

    const float* output_data = output_tensor.data<const float>();

    const int num_channels = 4 + num_classes_;
    
    cv::Mat output_buffer(num_channels, output_tensor.get_shape()[2], CV_32F, (void*)output_data);
    cv::transpose(output_buffer, output_buffer);

    std::vector<cv::Rect> boxes;
    std::vector<float> confidences;
    std::vector<int> class_ids;
    
    float raw_detection_threshold = 0.1f;

    for (int i = 0; i < output_buffer.rows; ++i) {
        cv::Mat class_scores = output_buffer.row(i).colRange(4, num_channels);
        cv::Point class_id_point;
        double max_class_score;
        cv::minMaxLoc(class_scores, nullptr, &max_class_score, nullptr, &class_id_point);

        float confidence = static_cast<float>(max_class_score);
        
        if (confidence > confidence_threshold_) {
            float cx = output_buffer.at<float>(i, 0);
            float cy = output_buffer.at<float>(i, 1);
            float w = output_buffer.at<float>(i, 2);
            float h = output_buffer.at<float>(i, 3);
            int left = static_cast<int>((cx - 0.5 * w) * scale_x);
            int top = static_cast<int>((cy - 0.5 * h) * scale_y);
            int width = static_cast<int>(w * scale_x);
            int height = static_cast<int>(h * scale_y);

            boxes.push_back(cv::Rect(left, top, width, height));
            confidences.push_back(confidence);
            class_ids.push_back(class_id_point.x);
        }

        if (confidence > raw_detection_threshold) {
            float cx = output_buffer.at<float>(i, 0);
            float cy = output_buffer.at<float>(i, 1);
            float w = output_buffer.at<float>(i, 2);
            float h = output_buffer.at<float>(i, 3);
            int left = static_cast<int>((cx - 0.5 * w) * scale_x);
            int top = static_cast<int>((cy - 0.5 * h) * scale_y);
            int width = static_cast<int>(w * scale_x);
            int height = static_cast<int>(h * scale_y);
            raw_detections.push_back({cv::Rect_<float>(left, top, width, height), confidence});
        }
    }

    std::vector<int> nms_indices;
    cv::dnn::NMSBoxes(boxes, confidences, confidence_threshold_, nms_threshold_, nms_indices);

    std::vector<byte_track::Object> objects;
    for (int index : nms_indices) {
        objects.push_back({
            byte_track::Rect<float>(boxes[index].x, boxes[index].y, boxes[index].width, boxes[index].height),
            class_ids[index],
            confidences[index]
        });
    }
    
    return objects;
}