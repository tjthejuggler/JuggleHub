#pragma once

#include "juggler.pb.h"
#include "../src/modules/ModuleBase.hpp"
#include "../src/modules/UdpBallColorModule.hpp"
#include "../src/modules/UdpBallSettingsModule.hpp"
#include "IBallTracker.hpp"
#include "SimpleBallTracker.hpp"
#include "Simple2DBallTracker.hpp"
#include "New3DTracker.hpp"
#include "ColorOnlyTracker.hpp"
#include "CameraManager.hpp"
#include "RecordingManager.hpp"
#include "PlaybackController.hpp"
#include "CommandProcessor.hpp"
#include "VisualizationRenderer.hpp"
#include <memory>
#include <string>
#include <atomic>
#include <zmq.hpp>

class Engine {
public:
    enum class OutputFormat {
        DEFAULT,
        SIMPLE,
        LEGACY
    };

    Engine(const std::string& camera_settings_path, const std::string& device_name = "CPU", const std::string& model_name = "yolo11n", const std::string& pose_model_name = "yolo11n-pose", OutputFormat format = OutputFormat::DEFAULT, bool use_dnn_tracker = true, bool verbose = false, bool simple_tracking = false);
    ~Engine();

    void run();
    void stop();
    
    // Tracker management
    void setTrackerType(const std::string& tracker_type);
    std::string getTrackerType() const { return current_tracker_type_; }

private:
    // New refactored components
    std::unique_ptr<CameraManager> camera_manager_;
    std::unique_ptr<RecordingManager> recording_manager_;
    std::unique_ptr<PlaybackController> playback_controller_;
    std::unique_ptr<CommandProcessor> command_processor_;
    std::unique_ptr<VisualizationRenderer> visualization_renderer_;

    OutputFormat output_format_;
    std::atomic<bool> running_;
    
    // Tracker system (polymorphic - can be any IBallTracker implementation)
    std::shared_ptr<IBallTracker> tracker_;
    std::string current_tracker_type_;  // "depth_based", "simple_2d", "new_3d", or "color_only"
    
    // Tracker instances
    std::shared_ptr<SimpleBallTracker> simple_tracker_;
    std::shared_ptr<Simple2DBallTracker> simple_2d_tracker_;
    std::shared_ptr<New3DTracker> new_3d_tracker_;
    std::shared_ptr<ColorOnlyTracker> color_only_tracker_;
    bool use_dnn_tracker_;
    bool verbose_;
    bool simple_tracking_;  // When true, use depth+color tracking instead of YOLO for balls

    // ZMQ
    zmq::context_t zmq_context_;
    zmq::socket_t zmq_publisher_;

    // Module system
    std::unique_ptr<UdpBallColorModule> color_module_;
    std::unique_ptr<juggler::modules::UdpBallSettingsModule> settings_module_;
    
    // Frame tracking
    uint32_t frame_counter_;
    std::atomic<bool> record_with_yolo_boxes_;
    std::atomic<bool> video_feed_enabled_;
    juggler::v1::VisualizationStates visualization_states_;
    std::vector<Detection> last_raw_detections_;
    std::vector<TrackedObject> last_tracked_objects_;
    
    // Helper function to render visualizations on a frame
    cv::Mat renderVisualizationsOnFrame(const cv::Mat& frame, const RecordingFrame& rec_frame);
};