#pragma once

#include <librealsense2/rs.hpp>
#include <opencv2/opencv.hpp>
#include <string>
#include <atomic>

struct CameraIntrinsics {
    float fx, fy, ppx, ppy;
};

class CameraManager {
public:
    CameraManager();
    ~CameraManager();

    // Initialization and configuration
    void initialize(const std::string& camera_settings_path, uint32_t width, uint32_t height, uint32_t fps);
    void loadSettingsFromJson(const std::string& json_path);
    
    // Camera control
    void start();
    void stop();
    void startWithSettings(const std::string& settings_file);
    void startWithSettings(const std::string& settings_file, uint32_t width, uint32_t height, uint32_t fps);
    
    // Frame acquisition
    bool getFrames(cv::Mat& color_image, cv::Mat& depth_image);
    
    // Getters
    bool isRunning() const { return camera_running_; }
    bool isIRProjectorActive() const { return ir_projector_active_; }
    const CameraIntrinsics& getIntrinsics() const { return camera_intrinsics_; }
    const cv::Mat& getLastColorFrame() const { return last_color_frame_; }
    const cv::Mat& getLastDepthFrame() const { return last_depth_frame_; }
    uint32_t getWidth() const { return camera_width_; }
    uint32_t getHeight() const { return camera_height_; }
    uint32_t getFPS() const { return camera_fps_; }

private:
    void applySettings();

    // RealSense pipeline
    rs2::pipeline pipe_;
    rs2::config rs_config_;
    rs2::align align_to_color_;
    
    // State
    std::atomic<bool> camera_running_;
    std::atomic<bool> ir_projector_active_;
    
    // Configuration
    std::string camera_settings_path_;
    std::string json_content_;
    uint32_t camera_width_;
    uint32_t camera_height_;
    uint32_t camera_fps_;
    
    // Cached data
    CameraIntrinsics camera_intrinsics_;
    cv::Mat last_color_frame_;
    cv::Mat last_depth_frame_;
};