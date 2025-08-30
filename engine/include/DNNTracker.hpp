#pragma once

#include <vector>
#include <string>
#include <memory> // Required for std::unique_ptr

// OpenCV and OpenVINO headers
#include <opencv2/opencv.hpp>
#include <openvino/openvino.hpp>

// Bytetrack header from the new library
#include "ByteTrack/BYTETracker.h"

// A clean data structure to pass tracking results back to the main engine
struct TrackedObject {
    cv::Rect_<float> box;
    int id;
    int class_id;
};

class DNNTracker {
public:
    DNNTracker(const std::string& model_path, const std::string& device_name);
    ~DNNTracker();

    std::vector<TrackedObject> update(const cv::Mat& frame);

    void update_setting(const std::string& key, const std::string& value);

private:
    // --- Member Variables ---
    // OpenVINO
    ov::Core core;
    ov::CompiledModel compiled_model;
    ov::InferRequest infer_request;

    // Bytetrack
    std::unique_ptr<byte_track::BYTETracker> tracker;

    // Model & Preprocessing Parameters
    int input_width_ = 640;
    int input_height_ = 640;
    float confidence_threshold_ = 0.25; // Lowered threshold for more stable tracking
    float nms_threshold_ = 0.5;

    // --- Private Methods ---
    cv::Mat preprocess(const cv::Mat& frame, float& scale_x, float& scale_y);
    std::vector<byte_track::Object> postprocess(const cv::Mat& frame, const ov::Tensor& output_tensor, float scale_x, float scale_y);
};