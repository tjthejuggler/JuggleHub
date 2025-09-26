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

class DNNTracker {
public:
    DNNTracker(const std::string& model_path, const std::string& device_name);
    ~DNNTracker();

    std::pair<std::vector<TrackedObject>, std::vector<Detection>> update(const cv::Mat& color_frame, const cv::Mat& depth_frame, const CameraIntrinsics& intrinsics);

    void update_setting(const std::string& key, const std::string& value);
    void calibrate_object(int logical_id, const cv::Point2f& pixel_coords, const cv::Mat& depth_frame, const CameraIntrinsics& intrinsics);
    const std::vector<PersistentTracker>& get_ball_trackers() const { return logical_ball_trackers_; }
    static cv::Point2f project_3d_to_2d(const cv::Point3f& world_pos, const CameraIntrinsics& intrinsics);

private:
    void reinitialize_tracker();
    void initialize_logical_trackers();

    // --- Member Variables ---
    // Timing
    std::chrono::steady_clock::time_point last_update_time_;

    // OpenVINO
    ov::Core core;
    ov::CompiledModel compiled_model;
    ov::InferRequest infer_request;

    // Bytetrack
    std::unique_ptr<byte_track::BYTETracker> tracker;

    // --- Persistent Logical Trackers ---
    std::vector<PersistentTracker> logical_ball_trackers_;
    std::vector<PersistentTracker> logical_hand_trackers_;
    const int NUM_BALLS = 3; // Configurable number of balls
    const int NUM_HANDS = 2; // Configurable number of hands

    // --- State Caching ---
    std::vector<Detection> last_raw_detections_;

    // Model & Preprocessing Parameters
    int input_width_ = 640;
    int input_height_ = 640;
    float confidence_threshold_ = 0.25;
    float nms_threshold_ = 0.5;

    // ByteTrack Parameters
    int track_buffer_ = 150;
    float track_thresh_ = 0.25f;
    float high_thresh_ = 0.5f;
    float match_thresh_ = 0.7f;

    // --- NEW MODEL CONFIGURATION MEMBERS ---
    const int num_classes_ = 4;
    const std::vector<std::string> class_names_ = {"led_on", "led_off", "dropped_ball", "hand"};

    // --- Private Methods ---
    void manage_hand_tracks(const std::vector<Detection>& hand_detections);
    void manage_ball_occlusion();
    cv::Mat preprocess(const cv::Mat& frame, float& scale_x, float& scale_y);
    std::vector<byte_track::Object> postprocess(const cv::Mat& color_frame, const cv::Mat& depth_frame, const CameraIntrinsics& intrinsics, const ov::Tensor& output_tensor, float scale_x, float scale_y, std::vector<Detection>& raw_detections);
};