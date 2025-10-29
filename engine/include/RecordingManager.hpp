#pragma once

#include "IBallTracker.hpp"
#include "RecordingLogger.hpp"
#include "juggler.pb.h"
#include <opencv2/opencv.hpp>
#include <deque>
#include <mutex>
#include <atomic>
#include <string>
#include <vector>

// Forward declarations for legacy compatibility
enum class TrackerStatus;
struct TrackedObject;
struct TrackedHand;

struct RecordingFrame {
    cv::Mat frame;
    cv::Mat depth_frame;
    std::vector<Detection> raw_detections;
    std::vector<TrackedObject> tracked_objects;
    std::vector<TrackedHand> tracked_hands;
    std::vector<SimpleBall> tracked_balls;
    std::vector<SimpleHand> tracked_hands_simple;
    std::vector<BallEvent> ball_events;
    juggler::v1::VisualizationStates viz_states;
};

class RecordingManager {
public:
    RecordingManager();
    ~RecordingManager();

    // Frame buffer management
    void addFrame(const RecordingFrame& frame);
    void clearBuffer();
    size_t getBufferSize() const;
    
    // Recording control
    void startContinuousRecording();
    void stopContinuousRecording(const CameraIntrinsics& camera_intrinsics,
                                 const juggler::v1::VisualizationStates& viz_states,
                                 bool record_with_yolo_boxes);
    bool isContinuousRecording() const { return continuous_recording_; }
    
    // Save recording from buffer
    void saveRecording(const CameraIntrinsics& camera_intrinsics,
                      const juggler::v1::VisualizationStates& viz_states,
                      bool record_with_yolo_boxes);
    
    // Get frames for continuous recording
    void addContinuousFrame(const RecordingFrame& frame);

private:
    void saveFramesToDisk(const std::deque<RecordingFrame>& frames,
                         const std::string& session_name,
                         const CameraIntrinsics& camera_intrinsics,
                         const juggler::v1::VisualizationStates& viz_states,
                         bool record_with_yolo_boxes);

    // Frame buffers
    std::deque<RecordingFrame> frame_buffer_;
    mutable std::mutex frame_buffer_mutex_;
    
    std::deque<RecordingFrame> continuous_frame_buffer_;
    mutable std::mutex continuous_frame_buffer_mutex_;
    
    // State
    std::atomic<bool> continuous_recording_;
    std::string continuous_recording_session_;
    
    // Recording logger
    RecordingLogger recording_logger_;
};