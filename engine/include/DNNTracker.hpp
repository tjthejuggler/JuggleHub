#ifndef DNN_TRACKER_HPP
#define DNN_TRACKER_HPP

#include <vector>
#include <opencv2/opencv.hpp>
#include <openvino/openvino.hpp>
#include "bytetrack/BYTETracker.h" // NEW AND CORRECT

// Structure to hold detected object information
struct Object {
    cv::Rect_<float> rect;
    int label;
    float prob;
};

class DNNTracker {
public:
    DNNTracker(const std::string& model_path, const std::string& device_name = "CPU");
    std::vector<STrackPtr> update(const cv::Mat& frame);

private:
    ov::Core core;
    ov::CompiledModel compiled_model;
    ov::InferRequest infer_request;
    cv::Size model_input_size;
    std::unique_ptr<BYTETracker> byte_tracker;

    std::vector<Object> postprocess(const ov::Tensor& output_tensor, const cv::Size& original_size);
};

#endif // DNN_TRACKER_HPP