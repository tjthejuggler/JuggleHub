#include "Simple2DBallTracker.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>

// ============================================================================
// CONSTRUCTOR
// ============================================================================

Simple2DBallTracker::Simple2DBallTracker(const std::string& ball_model_path,
                                         const std::string& pose_model_path,
                                         const std::string& device_name)
    : next_ball_id_(0),
      recording_frame_number_(-1),
      input_width_(640),
      input_height_(640),
      ball_confidence_threshold_(0.25f),
      ball_held_confidence_threshold_(0.25f),
      nms_threshold_(0.45f),
      enable_ball_detection_(true) {
    
    std::cout << "[Simple2DBallTracker] Initializing 2D-only ball tracker..." << std::endl;
    
    // Load OpenVINO models
    ball_model_ = core_.compile_model(ball_model_path, device_name);
    ball_infer_ = ball_model_.create_infer_request();
    
    pose_model_ = core_.compile_model(pose_model_path, device_name);
    pose_infer_ = pose_model_.create_infer_request();
    
    std::cout << "[Simple2DBallTracker] Models loaded successfully" << std::endl;
    std::cout << "[Simple2DBallTracker] Device: " << device_name << std::endl;
    std::cout << "[Simple2DBallTracker] Input size: " << input_width_ << "x" << input_height_ << std::endl;
}

// ============================================================================
// MAIN UPDATE METHOD
// ============================================================================

std::pair<std::vector<SimpleBall>, std::vector<BallEvent>> Simple2DBallTracker::update(
    const cv::Mat& color_frame,
    const cv::Mat& depth_frame,
    const CameraIntrinsics& intrinsics) {
    
    // IGNORE depth_frame - this is 2D-only tracking
    (void)depth_frame;
    (void)intrinsics;
    
    std::cout << "[Simple2DBallTracker] Frame update started" << std::endl;
    
    // Run YOLO ball detection (2D only)
    std::vector<Detection> ball_detections = runBallDetection(color_frame);
    std::cout << "[Simple2DBallTracker] Ball detections: " << ball_detections.size() << std::endl;
    
    // Run YOLO pose estimation for hands (2D only)
    std::vector<SimpleHand> hands = runPoseEstimation(color_frame);
    std::cout << "[Simple2DBallTracker] Hands detected: " << hands.size() << std::endl;
    
    // Store for getters
    hands_ = hands;
    raw_detections_ = ball_detections;
    
    // Simple nearest-neighbor tracking to assign IDs
    std::vector<Simple2DBall> tracked_balls_2d;
    
    for (const auto& det : ball_detections) {
        Simple2DBall ball_2d;
        
        // Calculate 2D center from bounding box
        ball_2d.center = cv::Point2f(det.box.x + det.box.width / 2.0f,
                                     det.box.y + det.box.height / 2.0f);
        ball_2d.bbox = det.box;
        ball_2d.confidence = det.confidence;
        ball_2d.class_id = det.class_id;
        ball_2d.frames_since_seen = 0;
        
        // Find closest existing ball within threshold
        int assigned_id = findClosestBallId(det, MAX_TRACKING_DISTANCE);
        
        if (assigned_id >= 0) {
            // Existing ball found
            ball_2d.id = assigned_id;
        } else {
            // New ball - assign new ID
            ball_2d.id = next_ball_id_++;
        }
        
        tracked_balls_2d.push_back(ball_2d);
    }
    
    // Update tracked balls list
    balls_2d_ = tracked_balls_2d;
    
    // Convert Simple2DBall to SimpleBall format for compatibility with Engine
    std::vector<SimpleBall> balls_3d;
    for (const auto& ball_2d : tracked_balls_2d) {
        SimpleBall ball;
        ball.id = ball_2d.id;
        ball.pixel_pos = ball_2d.center;
        ball.bbox = ball_2d.bbox;
        ball.has_yolo_detection = true;
        ball.yolo_confidence = ball_2d.confidence;
        ball.yolo_class_id = ball_2d.class_id;
        
        // 2D-only: No 3D position
        ball.position = cv::Point3f(0, 0, 0);
        
        // 2D-only: No color matching
        ball.color_name = "unknown";
        ball.color_match_score = 0.0f;
        
        // 2D-only: No state tracking
        ball.is_held = false;
        ball.held_by_hand_id = -1;
        
        balls_3d.push_back(ball);
    }
    
    // 2D-only: No throw/catch events
    std::vector<BallEvent> events;
    
    std::cout << "[Simple2DBallTracker] Frame update complete: " << balls_3d.size() << " balls tracked" << std::endl;
    
    return {balls_3d, events};
}

// ============================================================================
// YOLO PREPROCESSING
// ============================================================================

cv::Mat Simple2DBallTracker::preprocess(const cv::Mat& frame, float& scale_x, float& scale_y) {
    // Resize to 640x640
    cv::Mat resized_frame;
    cv::resize(frame, resized_frame, cv::Size(input_width_, input_height_));
    
    // Calculate scale factors for converting back to original coordinates
    scale_x = static_cast<float>(frame.cols) / input_width_;
    scale_y = static_cast<float>(frame.rows) / input_height_;
    
    // Normalize to [0, 1] and convert to float
    cv::Mat float_frame;
    resized_frame.convertTo(float_frame, CV_32F, 1.0 / 255.0);
    
    // Convert to blob format (NCHW)
    return cv::dnn::blobFromImage(float_frame);
}

// ============================================================================
// BALL DETECTION (2D ONLY)
// ============================================================================

std::vector<Detection> Simple2DBallTracker::runBallDetection(const cv::Mat& color_frame) {
    // If ball detection is disabled, return empty vector
    if (!enable_ball_detection_) {
        return std::vector<Detection>();
    }
    
    // Preprocess
    float scale_x, scale_y;
    cv::Mat preprocessed = preprocess(color_frame, scale_x, scale_y);
    
    // Run inference
    ov::Tensor input_tensor(ball_model_.input().get_element_type(),
                           ball_model_.input().get_shape(),
                           preprocessed.data);
    ball_infer_.set_input_tensor(input_tensor);
    ball_infer_.infer();
    const ov::Tensor& output_tensor = ball_infer_.get_output_tensor();
    
    // Parse YOLO output
    const float* output_data = output_tensor.data<const float>();
    const int num_channels = 4 + NUM_CLASSES;  // 4 bbox coords + class scores
    
    // YOLO output format: [batch, channels, num_detections]
    // We need to transpose to [num_detections, channels]
    cv::Mat output_buffer(num_channels, output_tensor.get_shape()[2], CV_32F, (void*)output_data);
    cv::transpose(output_buffer, output_buffer);
    
    std::vector<cv::Rect> boxes;
    std::vector<float> confidences;
    std::vector<int> class_ids;
    std::vector<Detection> raw_detections;
    
    // Parse each detection (8400 detections × 6 values)
    for (int i = 0; i < output_buffer.rows; ++i) {
        // Extract class scores (columns 4 onwards)
        cv::Mat class_scores = output_buffer.row(i).colRange(4, num_channels);
        cv::Point class_id_point;
        double max_class_score;
        cv::minMaxLoc(class_scores, nullptr, &max_class_score, nullptr, &class_id_point);
        
        float confidence = static_cast<float>(max_class_score);
        int class_id = class_id_point.x;
        
        // Apply class-specific confidence thresholds
        // class_id=0 is 'ball', class_id=1 is 'ball_held'
        float threshold = (class_id == 0) ? ball_confidence_threshold_ : ball_held_confidence_threshold_;
        
        if (confidence > threshold) {
            // Extract bounding box (center_x, center_y, width, height)
            float cx = output_buffer.at<float>(i, 0);
            float cy = output_buffer.at<float>(i, 1);
            float w = output_buffer.at<float>(i, 2);
            float h = output_buffer.at<float>(i, 3);
            
            // Convert to (left, top, width, height) in original image coordinates
            int left = static_cast<int>((cx - 0.5f * w) * scale_x);
            int top = static_cast<int>((cy - 0.5f * h) * scale_y);
            int width = static_cast<int>(w * scale_x);
            int height = static_cast<int>(h * scale_y);
            
            boxes.push_back(cv::Rect(left, top, width, height));
            confidences.push_back(confidence);
            class_ids.push_back(class_id);
            
            // Create detection struct (2D only - no 3D position)
            Detection det;
            det.box = cv::Rect_<float>(left, top, width, height);
            det.world_pos = cv::Point3f(0, 0, 0);  // No 3D position in 2D mode
            det.confidence = confidence;
            det.class_id = class_id;
            det.index = raw_detections.size();
            raw_detections.push_back(det);
        }
    }
    
    // Apply NMS (Non-Maximum Suppression) to remove overlapping boxes
    std::vector<int> nms_indices;
    float min_threshold = std::min(ball_confidence_threshold_, ball_held_confidence_threshold_);
    cv::dnn::NMSBoxes(boxes, confidences, min_threshold, nms_threshold_, nms_indices);
    
    // Filter detections by NMS results
    std::vector<Detection> filtered_detections;
    for (int idx : nms_indices) {
        for (auto& det : raw_detections) {
            if (det.box.x == boxes[idx].x && det.box.y == boxes[idx].y) {
                det.index = filtered_detections.size();
                filtered_detections.push_back(det);
                break;
            }
        }
    }
    
    return filtered_detections;
}

// ============================================================================
// POSE ESTIMATION (2D ONLY)
// ============================================================================

std::vector<SimpleHand> Simple2DBallTracker::runPoseEstimation(const cv::Mat& color_frame) {
    // If pose detection is disabled, return empty vector
    if (!enable_pose_detection_) {
        return std::vector<SimpleHand>();
    }
    
    std::vector<SimpleHand> hands;
    
    // Preprocess
    float scale_x, scale_y;
    cv::Mat preprocessed = preprocess(color_frame, scale_x, scale_y);
    
    // Run inference
    ov::Tensor input_tensor(pose_model_.input().get_element_type(),
                           pose_model_.input().get_shape(),
                           preprocessed.data);
    pose_infer_.set_input_tensor(input_tensor);
    pose_infer_.infer();
    const ov::Tensor& output_tensor = pose_infer_.get_output_tensor();
    
    // Parse YOLO-Pose output
    const float* output_data = output_tensor.data<const float>();
    const auto& shape = output_tensor.get_shape();
    
    if (shape.size() < 3) {
        return hands;
    }
    
    const int num_channels = shape[1];
    const int num_detections = shape[2];
    
    cv::Mat output_buffer(num_channels, num_detections, CV_32F, (void*)output_data);
    cv::transpose(output_buffer, output_buffer);
    
    const float pose_confidence_threshold = 0.3f;
    const float keypoint_confidence_threshold = 0.5f;
    
    // Parse each person detection
    for (int i = 0; i < output_buffer.rows; ++i) {
        float person_confidence = output_buffer.at<float>(i, 4);
        
        if (person_confidence < pose_confidence_threshold) continue;
        
        // Extract keypoints (17 keypoints in COCO format)
        // Each keypoint has 3 values: x, y, confidence
        std::vector<cv::Point3f> keypoints_3d(17, cv::Point3f(0, 0, 0));
        std::vector<float> keypoint_confidences(17, 0.0f);
        
        for (int kp_idx = 0; kp_idx < 17; ++kp_idx) {
            int base_idx = 5 + kp_idx * 3;
            
            float kp_x = output_buffer.at<float>(i, base_idx + 0) * scale_x;
            float kp_y = output_buffer.at<float>(i, base_idx + 1) * scale_y;
            float kp_conf = output_buffer.at<float>(i, base_idx + 2);
            
            keypoint_confidences[kp_idx] = kp_conf;
            
            if (kp_conf > keypoint_confidence_threshold) {
                // Store as 3D point with z=0 (2D only)
                keypoints_3d[kp_idx] = cv::Point3f(kp_x, kp_y, 0);
            }
        }
        
        // Create hands from wrist keypoints
        // COCO format: 9=left wrist, 10=right wrist
        
        // Left hand (id=0)
        if (keypoint_confidences[9] > keypoint_confidence_threshold &&
            keypoints_3d[9].x > 0) {
            
            SimpleHand left_hand;
            left_hand.id = 0;
            left_hand.is_visible = true;
            left_hand.confidence = keypoint_confidences[9];
            left_hand.wrist_pos_3d = keypoints_3d[9];  // z=0 for 2D mode
            left_hand.keypoints = keypoints_3d;
            
            hands.push_back(left_hand);
        }
        
        // Right hand (id=1)
        if (keypoint_confidences[10] > keypoint_confidence_threshold &&
            keypoints_3d[10].x > 0) {
            
            SimpleHand right_hand;
            right_hand.id = 1;
            right_hand.is_visible = true;
            right_hand.confidence = keypoint_confidences[10];
            right_hand.wrist_pos_3d = keypoints_3d[10];  // z=0 for 2D mode
            right_hand.keypoints = keypoints_3d;
            
            hands.push_back(right_hand);
        }
        
        // Only process first person
        break;
    }
    
    return hands;
}

// ============================================================================
// SIMPLE TRACKING METHOD
// ============================================================================

int Simple2DBallTracker::findClosestBallId(const Detection& detection, float max_distance) {
    float det_center_x = detection.box.x + detection.box.width / 2.0f;
    float det_center_y = detection.box.y + detection.box.height / 2.0f;
    
    float min_distance = max_distance;
    int closest_id = -1;
    
    // Search through currently tracked balls
    for (const auto& ball : balls_2d_) {
        // Calculate Euclidean distance in 2D pixel space
        float dx = det_center_x - ball.center.x;
        float dy = det_center_y - ball.center.y;
        float distance = std::sqrt(dx * dx + dy * dy);
        
        if (distance < min_distance) {
            min_distance = distance;
            closest_id = ball.id;
        }
    }
    
    return closest_id;
}

// ============================================================================
// SETTINGS UPDATE METHOD
// ============================================================================

bool Simple2DBallTracker::updateSetting(const std::string& key, const std::string& value) {
    if (key == "enable_ball_detection") {
        enable_ball_detection_ = (value == "true" || value == "1");
        std::cout << "[Simple2DBallTracker] Ball detection "
                  << (enable_ball_detection_ ? "enabled" : "disabled") << std::endl;
        return true;
    }
    else if (key == "enable_pose_detection") {
        enable_pose_detection_ = (value == "true" || value == "1");
        std::cout << "[Simple2DBallTracker] Pose detection "
                  << (enable_pose_detection_ ? "enabled" : "disabled") << std::endl;
        return true;
    }
    
    // Add other settings here as needed
    
    return false;
}