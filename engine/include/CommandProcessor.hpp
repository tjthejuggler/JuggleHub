#pragma once

#include "juggler.pb.h"
#include "IBallTracker.hpp"
#include "CameraManager.hpp"
#include "RecordingManager.hpp"
#include "PlaybackController.hpp"
#include "../src/modules/ModuleBase.hpp"
#include <zmq.hpp>
#include <memory>
#include <queue>
#include <mutex>
#include <atomic>
#include <functional>
#include <string>

// Forward declarations
class UdpBallColorModule;
namespace juggler { namespace modules { class UdpBallSettingsModule; } }

class CommandProcessor {
public:
    CommandProcessor(zmq::context_t& zmq_context);
    ~CommandProcessor();

    // Command processing
    void start();
    void stop();
    void processCommands();
    
    // Internal command queue
    void sendCommand(const juggler::v1::CommandRequest& command);
    
    // Set component references (dependency injection)
    void setCameraManager(CameraManager* camera_mgr) { camera_manager_ = camera_mgr; }
    void setRecordingManager(RecordingManager* rec_mgr) { recording_manager_ = rec_mgr; }
    void setPlaybackController(PlaybackController* playback_ctrl) { playback_controller_ = playback_ctrl; }
    void setTracker(std::shared_ptr<IBallTracker> tracker) { tracker_ = tracker; }
    void setSimpleTracker(std::shared_ptr<IBallTracker> tracker) { simple_tracker_ = tracker; }
    void setSimple2DTracker(std::shared_ptr<IBallTracker> tracker) { simple_2d_tracker_ = tracker; }
    void setNew3DTracker(std::shared_ptr<IBallTracker> tracker) { new_3d_tracker_ = tracker; }
    void setColorModule(UdpBallColorModule* color_module) { color_module_ = color_module; }
    void setSettingsModule(juggler::modules::UdpBallSettingsModule* settings_module) { settings_module_ = settings_module; }
    void setVisualizationStates(juggler::v1::VisualizationStates* viz_states) { visualization_states_ = viz_states; }
    void setRecordWithYoloBoxes(std::atomic<bool>* flag) { record_with_yolo_boxes_ = flag; }
    void setVideoFeedEnabled(std::atomic<bool>* flag) { video_feed_enabled_ = flag; }
    void setCurrentTrackerType(std::string* tracker_type) { current_tracker_type_ = tracker_type; }
    
    // Callbacks for operations that need Engine context
    using TrackerSwitchCallback = std::function<void(const std::string&)>;
    void setTrackerSwitchCallback(TrackerSwitchCallback callback) { tracker_switch_callback_ = callback; }

private:
    std::unique_ptr<ModuleBase> createModule(const juggler::v1::CommandRequest& command);
    void handleExternalCommand(const juggler::v1::CommandRequest& command, juggler::v1::CommandResponse& response);
    void handleInternalCommand(const juggler::v1::CommandRequest& command);

    // ZMQ sockets
    zmq::socket_t zmq_commander_;
    
    // Component references (not owned)
    CameraManager* camera_manager_;
    RecordingManager* recording_manager_;
    PlaybackController* playback_controller_;
    std::shared_ptr<IBallTracker> tracker_;
    std::shared_ptr<IBallTracker> simple_tracker_;
    std::shared_ptr<IBallTracker> simple_2d_tracker_;
    std::shared_ptr<IBallTracker> new_3d_tracker_;
    UdpBallColorModule* color_module_;
    juggler::modules::UdpBallSettingsModule* settings_module_;
    juggler::v1::VisualizationStates* visualization_states_;
    std::atomic<bool>* record_with_yolo_boxes_;
    std::atomic<bool>* video_feed_enabled_;
    std::string* current_tracker_type_;
    
    // Module management
    std::unique_ptr<ModuleBase> active_module_;
    
    // Internal command queue
    std::queue<juggler::v1::CommandRequest> command_queue_;
    std::mutex command_queue_mutex_;
    
    // State
    std::atomic<bool> running_;
    
    // Callbacks
    TrackerSwitchCallback tracker_switch_callback_;
};