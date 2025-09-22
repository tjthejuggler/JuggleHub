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
};

struct RawDetection {
    cv::Rect_<float> box;
    cv::Point3f world_pos; // The raw, measured 3D position
    float confidence;
    int class_id;
};

class DNNTracker {
public:
    DNNTracker(const std::string& model_path, const std::string& device_name);
    ~DNNTracker();

    std::pair<std::vector<TrackedObject>, std::vector<RawDetection>> update(const cv::Mat& color_frame, const cv::Mat& depth_frame, const CameraIntrinsics& intrinsics);

    void update_setting(const std::string& key, const std::string& value);
    const std::map<int, int>& get_held_ball_states() const { return held_ball_states_; };

private:
    void reinitialize_tracker();

    // --- Member Variables ---
    // Timing
    std::chrono::steady_clock::time_point last_update_time_;

    // OpenVINO
    ov::Core core;
    ov::CompiledModel compiled_model;
    ov::InferRequest infer_request;

    // Bytetrack & Kalman Filters
    std::unique_ptr<byte_track::BYTETracker> tracker;
    std::map<int, KalmanFilter3D> kalman_filters_; // Map track ID to its own filter
    std::map<int, int> track_class_ids_; // Map track ID to its class ID for model selection

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

    // --- Hand Tracking ---
    int left_hand_track_id_ = -1;
    int right_hand_track_id_ = -1;

    // --- Ball Occlusion ---
    std::map<int, int> held_ball_states_; // Map ball track ID -> hand track ID

    // --- Private Methods ---
    void manage_hand_tracks(std::vector<TrackedObject>& tracks, const std::vector<RawDetection>& raw_detections);
    void manage_ball_occlusion(std::vector<TrackedObject>& tracks);
    cv::Mat preprocess(const cv::Mat& frame, float& scale_x, float& scale_y);
    std::vector<byte_track::Object> postprocess(const cv::Mat& color_frame, const cv::Mat& depth_frame, const CameraIntrinsics& intrinsics, const ov::Tensor& output_tensor, float scale_x, float scale_y, std::vector<RawDetection>& raw_detections);
};