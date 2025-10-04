#pragma once

#include <vector>
#include <string>
#include <memory> 
#include <map>
#include <chrono>

// OpenCV and OpenVINO headers
#include <opencv2/opencv.hpp>
#include <openvino/openvino.hpp>

// Local headers
#include "ByteTrack/BYTETracker.h"
#include "KalmanFilter3D.hpp"
#include "PersistentTracker.hpp" // New persistent tracker data structure
#include "ColorTracker.hpp" // Color-based ball tracking
#include "ThrowCatchDetector.hpp" // Throw and catch event detection
#include "AdaptiveColorManager.hpp" // Adaptive color range adjustment

// Simple struct to hold camera intrinsics needed for deprojection
struct CameraIntrinsics {
    float fx, fy; // focal lengths
    float ppx, ppy; // principal points
};

// A clean data structure to pass tracking results back to the main engine
struct TrackedObject {
    cv::Rect_<float> box;
    cv::Point3f world_pos; // This will now be the Kalman-filtered position
    int id;
    int class_id;
    std::string class_name;
    TrackerStatus status; // The current state of the tracker (e.g., TRACKED, PREDICTED)
    int logical_id;       // The persistent ID of the object
    bool is_left;         // For hands, true if it's the left hand
};

struct Detection {
    cv::Rect_<float> box;
    cv::Point3f world_pos; // The raw, measured 3D position
    float confidence;
    int class_id;
};

struct TrackedHand {
    cv::Point3f wrist_pos_3d;
    float confidence;
    int id; // 0 for left, 1 for right
    std::vector<cv::Point3f> keypoints;
};

class DNNTracker {
public:
    DNNTracker(const std::string& ball_model_path, const std::string& pose_model_path, const std::string& device_name);
    ~DNNTracker();

    std::pair<std::vector<TrackedObject>, std::vector<TrackedHand>> update(const cv::Mat& color_frame, const cv::Mat& depth_frame, const CameraIntrinsics& intrinsics);

    void update_setting(const std::string& key, const std::string& value);
    void calibrate_object(int logical_id, const cv::Point2f& pixel_coords, const cv::Mat& depth_frame, const CameraIntrinsics& intrinsics);
    bool calibrate_color(const std::string& color_name, const cv::Point& click_point, std::string& error_message);
    const std::vector<PersistentTracker>& get_ball_trackers() const { return logical_ball_trackers_; }
    const std::vector<Detection>& get_last_raw_detections() const { return last_raw_detections_; }
    const std::vector<Detection>& get_unmatched_detections() const { return unmatched_detections_; }
    const std::vector<juggler::ColorTrackedBall>& get_color_tracked_balls() const { return color_tracked_balls_; }
    static cv::Point2f project_3d_to_2d(const cv::Point3f& world_pos, const CameraIntrinsics& intrinsics);
    
    // Getters for logical trackers (needed for Engine.cpp)
    const std::vector<PersistentTracker>& get_logical_ball_trackers() const { return logical_ball_trackers_; }
    const std::vector<PersistentTracker>& get_logical_hand_trackers() const { return logical_hand_trackers_; }
    juggler::ColorTracker* get_color_tracker() { return color_tracker_.get(); }
    
    // Getters for visualization data
    const std::vector<cv::Point3f>& get_predicted_positions() const { return predicted_positions_; }
    const std::vector<std::string>& get_predicted_labels() const { return predicted_tracker_labels_; }
    const std::vector<Detection>& get_filtered_detections() const { return filtered_detections_; }
    const std::vector<std::string>& get_filter_reasons() const { return filter_reasons_; }
    const std::vector<std::pair<int, int>>& get_tracker_associations() const { return tracker_associations_; }
    const std::vector<float>& get_association_distances() const { return association_distances_; }
    const std::vector<int>& get_newly_initialized_trackers() const { return newly_initialized_tracker_ids_; }
    const std::vector<cv::Point3f>& get_new_tracker_positions() const { return new_tracker_positions_; }
    

private:
    void reinitialize_tracker();
    void initialize_logical_trackers();

    // --- Member Variables ---
    // Timing
    std::chrono::steady_clock::time_point last_update_time_;

    // OpenVINO
    ov::Core core;
    ov::CompiledModel ball_compiled_model;
    ov::InferRequest ball_infer_request;
    ov::CompiledModel pose_compiled_model;
    ov::InferRequest pose_infer_request;

    // Bytetrack
    std::unique_ptr<byte_track::BYTETracker> tracker;

    // --- Color Tracker ---
    std::unique_ptr<juggler::ColorTracker> color_tracker_;
    std::vector<juggler::ColorTrackedBall> color_tracked_balls_;
    
    // --- Adaptive Color Manager ---
    std::unique_ptr<juggler::AdaptiveColorManager> adaptive_color_manager_;
    
    // --- Throw/Catch Detector ---
    std::unique_ptr<juggler::ThrowCatchDetector> throw_catch_detector_;
    std::vector<juggler::ThrowCatchDetector::DetectedEvent> detected_events_;

    // --- Persistent Logical Trackers ---
    std::vector<PersistentTracker> logical_ball_trackers_;
    std::vector<PersistentTracker> logical_hand_trackers_;
    const int NUM_BALLS = 3; // Configurable number of balls
    const int NUM_HANDS = 2; // Configurable number of hands

    // --- State Caching ---
    std::vector<Detection> last_raw_detections_;
    std::vector<Detection> unmatched_detections_;
    cv::Mat last_color_frame_; // For color calibration
    
    // --- Visualization Data ---
    std::vector<cv::Point3f> predicted_positions_;
    std::vector<std::string> predicted_tracker_labels_;
    std::vector<Detection> filtered_detections_;
    std::vector<std::string> filter_reasons_;
    std::vector<std::pair<int, int>> tracker_associations_; // (tracker_idx, detection_idx)
    std::vector<float> association_distances_;
    std::vector<int> newly_initialized_tracker_ids_;
    std::vector<cv::Point3f> new_tracker_positions_;

    // Model & Preprocessing Parameters
    int input_width_ = 640;
    int input_height_ = 640;
    float confidence_threshold_ = 0.25;
    float nms_threshold_ = 0.5;

    // ByteTrack Parameters
    int track_buffer_ = 150;      // How many frames to keep lost tracks (5 seconds at 30fps)
    float track_thresh_ = 0.25f;  // Minimum confidence to START a new track
    float high_thresh_ = 0.35f;   // CHANGED from 0.5 - treat more balls as high confidence
    float match_thresh_ = 0.5f;   // CHANGED from 0.7 - allow larger motion between frames

    bool pose_model_enabled_ = true;

    // --- NEW MODEL CONFIGURATION MEMBERS ---
    // Updated to support 2-class ball model: ball (in flight) and ball_held (in hand)
    const int num_classes_ = 2;
    const std::vector<std::string> class_names_ = {"ball", "ball_held"};

    // --- Private Methods ---
    void manage_hand_tracks(const std::vector<Detection>& hand_detections);
    void manage_ball_occlusion();
    cv::Mat preprocess(const cv::Mat& frame, float& scale_x, float& scale_y);
    std::vector<byte_track::Object> postprocess_ball_detection(const cv::Mat& color_frame, const cv::Mat& depth_frame, const CameraIntrinsics& intrinsics, const ov::Tensor& output_tensor, float scale_x, float scale_y, std::vector<Detection>& raw_detections);
    std::vector<TrackedHand> run_pose_estimation(const cv::Mat& color_frame, const cv::Mat& depth_frame, const CameraIntrinsics& intrinsics);
};