#pragma once

#include "juggler.pb.h"
#include "../src/modules/ModuleBase.hpp"
#include "../src/modules/UdpBallColorModule.hpp"
#include "../src/modules/UdpBallSettingsModule.hpp"
#include "SimpleBallTracker.hpp" // Include the simplified ball tracker
#include "json.hpp" // Include nlohmann/json
#include <memory>
#include <queue>
#include <mutex>
#include <string>
#include <deque>
#include <atomic>
#include <zmq.hpp>
#include <librealsense2/rs.hpp>

// Legacy types for recording compatibility
enum class TrackerStatus {
    TRACKED,
    LOST,
    REMOVED
};

struct TrackedObject {
    cv::Rect_<float> box;
    cv::Point3f world_pos;
    int id;
    int class_id;
    std::string class_name;
    TrackerStatus status;
    int logical_id;
    bool is_left;
};

struct TrackedHand {
    cv::Point3f wrist_pos_3d;
    float confidence;
    int id;
    std::vector<cv::Point3f> keypoints;
};

class Engine {
public:
    enum class OutputFormat {
        DEFAULT,
        SIMPLE,
        LEGACY
    };

    Engine(const std::string& camera_settings_path, const std::string& device_name = "CPU", const std::string& model_name = "yolo11n", const std::string& pose_model_name = "yolo11n-pose", OutputFormat format = OutputFormat::DEFAULT, bool use_dnn_tracker = true, bool verbose = false);
    ~Engine();

    void run();
    void stop();

private:
    void processCommands();
    void sendCommand(const juggler::v1::CommandRequest& command);
    void saveRecording();
    void startContinuousRecording();
    void stopContinuousRecording();
    void initializeCamera();
    void applyCameraSettings();
    void loadCameraSettingsFromJson(const std::string& json_path);
    void stopCamera();
    void startCamera();
    void startCameraWithSettings(const std::string& settings_file);
    void startCameraWithSettings(const std::string& settings_file, uint32_t width, uint32_t height, uint32_t fps);
    std::unique_ptr<ModuleBase> create_module(const juggler::v1::CommandRequest& command);

    std::string camera_settings_path_;
    std::string json_content_;
    OutputFormat output_format_;

    // Thread-safe queue for commands
    std::queue<juggler::v1::CommandRequest> command_queue_;
    std::mutex command_queue_mutex_;

    std::atomic<bool> running_;
    std::unique_ptr<ModuleBase> active_module_;
    std::unique_ptr<UdpBallColorModule> color_module_;
    std::unique_ptr<juggler::modules::UdpBallSettingsModule> settings_module_;
    std::shared_ptr<juggler::BallTracker> ball_tracker_;
    std::shared_ptr<SimpleBallTracker> simple_tracker_; // Simplified ball tracker
    bool use_dnn_tracker_; // Flag to switch between old/new tracker (kept for compatibility)
    bool verbose_;

    // ZMQ
    zmq::context_t zmq_context_;
    zmq::socket_t zmq_publisher_;
    zmq::socket_t zmq_commander_;

    // RealSense
    rs2::pipeline pipe_;
    rs2::config rs_config_;
    rs2::align align_to_color_;
    std::atomic<bool> camera_running_;
    std::atomic<bool> ir_projector_active_;
    CameraIntrinsics camera_intrinsics_; // Store camera intrinsics
    cv::Mat last_depth_frame_; // Cache for calibration
    cv::Mat last_color_frame_; // Cache for color calibration
    
    // Camera configuration parameters
    uint32_t camera_width_;
    uint32_t camera_height_;
    uint32_t camera_fps_;

    // Frame buffer for recording
    struct RecordingFrame {
        cv::Mat frame;
        std::vector<Detection> raw_detections;
        std::vector<TrackedObject> tracked_objects;
        std::vector<TrackedHand> tracked_hands;
        std::vector<SimpleBall> tracked_balls;  // Store SimpleBall data for color visualization
        juggler::v1::VisualizationStates viz_states;  // Store visualization states
    };
    std::deque<RecordingFrame> frame_buffer_;
    std::mutex frame_buffer_mutex_;
    uint32_t frame_counter_;
    
    // Continuous recording state
    std::atomic<bool> continuous_recording_;
    std::deque<RecordingFrame> continuous_frame_buffer_;
    std::mutex continuous_frame_buffer_mutex_;
    std::string continuous_recording_session_;

    // Recording with bounding boxes state
    std::atomic<bool> record_with_yolo_boxes_;
    std::atomic<bool> record_with_bytetrack_boxes_;
    juggler::v1::VisualizationStates visualization_states_;  // Store current visualization states
    std::vector<Detection> last_raw_detections_; // Keep for calibration
    std::vector<TrackedObject> last_tracked_objects_; // Keep for calibration
    
    // Helper function to render visualizations on a frame
    cv::Mat renderVisualizationsOnFrame(const cv::Mat& frame, const RecordingFrame& rec_frame);
};