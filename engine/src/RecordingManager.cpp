#include "RecordingManager.hpp"
#include "DebugLog.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <chrono>

namespace fs = std::filesystem;

extern void writeDebugLog(const std::string& message);

// Forward declaration - will be provided by Engine.cpp
extern cv::Mat renderVisualizationsOnFrame(const cv::Mat& frame, const RecordingFrame& rec_frame,
                                          const CameraIntrinsics& camera_intrinsics,
                                          const juggler::v1::VisualizationStates& viz_states,
                                          bool record_with_yolo_boxes,
                                          IBallTracker* tracker);

RecordingManager::RecordingManager()
    : continuous_recording_(false) {
}

RecordingManager::~RecordingManager() {
    // Ensure recording is stopped
    if (continuous_recording_) {
        continuous_recording_ = false;
    }
}

void RecordingManager::addFrame(const RecordingFrame& frame) {
    std::lock_guard<std::mutex> lock(frame_buffer_mutex_);
    frame_buffer_.push_back(frame);
    if (frame_buffer_.size() > 150) {
        frame_buffer_.pop_front();
    }
}

void RecordingManager::clearBuffer() {
    std::lock_guard<std::mutex> lock(frame_buffer_mutex_);
    frame_buffer_.clear();
}

size_t RecordingManager::getBufferSize() const {
    std::lock_guard<std::mutex> lock(frame_buffer_mutex_);
    return frame_buffer_.size();
}

void RecordingManager::addContinuousFrame(const RecordingFrame& frame) {
    if (continuous_recording_) {
        std::lock_guard<std::mutex> lock(continuous_frame_buffer_mutex_);
        continuous_frame_buffer_.push_back(frame);
    }
}

void RecordingManager::startContinuousRecording() {
    if (continuous_recording_) {
        return;
    }
    
    // Clear the continuous buffer and start recording
    {
        std::lock_guard<std::mutex> lock(continuous_frame_buffer_mutex_);
        continuous_frame_buffer_.clear();
    }
    
    // Create session name with timestamp
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::tm buf;
    localtime_r(&in_time_t, &buf);
    std::stringstream ss;
    ss << "continuous_" << std::put_time(&buf, "%Y-%m-%d_%H-%M-%S");
    continuous_recording_session_ = ss.str();
    
    continuous_recording_ = true;
    INFO_LOG("Continuous recording started: ", continuous_recording_session_);
}

void RecordingManager::stopContinuousRecording(const CameraIntrinsics& camera_intrinsics,
                                               const juggler::v1::VisualizationStates& viz_states,
                                               bool record_with_yolo_boxes) {
    writeDebugLog("RecordingManager::stopContinuousRecording() - Starting...");
    if (!continuous_recording_) {
        writeDebugLog("RecordingManager::stopContinuousRecording() - Not recording, returning");
        return;
    }
    
    writeDebugLog("RecordingManager::stopContinuousRecording() - Stopping recording flag");
    continuous_recording_ = false;
    
    // Copy the buffer while holding the lock, then release it immediately
    std::deque<RecordingFrame> frames_to_save;
    std::string session_name;
    {
        writeDebugLog("RecordingManager::stopContinuousRecording() - Acquiring mutex to copy buffer");
        std::lock_guard<std::mutex> lock(continuous_frame_buffer_mutex_);
        
        if (continuous_frame_buffer_.empty()) {
            writeDebugLog("RecordingManager::stopContinuousRecording() - Buffer empty, returning");
            return;
        }
        
        writeDebugLog("RecordingManager::stopContinuousRecording() - Copying " + 
                     std::to_string(continuous_frame_buffer_.size()) + " frames");
        frames_to_save = continuous_frame_buffer_;
        session_name = continuous_recording_session_;
        continuous_frame_buffer_.clear();
        writeDebugLog("RecordingManager::stopContinuousRecording() - Buffer copied and cleared, releasing mutex");
    }
    
    writeDebugLog("RecordingManager::stopContinuousRecording() - Processing " + 
                 std::to_string(frames_to_save.size()) + " frames");
    saveFramesToDisk(frames_to_save, session_name, camera_intrinsics, viz_states, record_with_yolo_boxes);
}

void RecordingManager::saveRecording(const CameraIntrinsics& camera_intrinsics,
                                     const juggler::v1::VisualizationStates& viz_states,
                                     bool record_with_yolo_boxes) {
    std::lock_guard<std::mutex> lock(frame_buffer_mutex_);
    
    if (frame_buffer_.empty()) {
        return;
    }

    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::tm buf;
    localtime_r(&in_time_t, &buf);
    std::stringstream ss;
    ss << "rs455_" << std::put_time(&buf, "%Y-%m-%d_%H-%M-%S");
    
    std::deque<RecordingFrame> frames_to_save = frame_buffer_;
    saveFramesToDisk(frames_to_save, ss.str(), camera_intrinsics, viz_states, record_with_yolo_boxes);
}

void RecordingManager::saveFramesToDisk(const std::deque<RecordingFrame>& frames,
                                       const std::string& session_name,
                                       const CameraIntrinsics& camera_intrinsics,
                                       const juggler::v1::VisualizationStates& viz_states,
                                       bool record_with_yolo_boxes) {
    fs::path data_dir = "engine/data/1_raw_recordings";
    fs::path recording_dir = data_dir / session_name;
    fs::path recording_dir_no_boxes = recording_dir / "no_boxes";
    fs::path recording_dir_depth = recording_dir / "depth";

    try {
        fs::create_directories(recording_dir_no_boxes);
        fs::create_directories(recording_dir_depth);
        
        // Start recording logger
        if (recording_logger_.start(recording_dir.string())) {
            INFO_LOG("Recording logger started: ", recording_dir.string(), "/recording.log");
        }
        
        int frame_num = 0;
        for (const auto& rec_frame : frames) {
            // Save RGB frame
            std::string filename = session_name + "_frame_" + std::to_string(frame_num) + ".jpg";
            fs::path filepath = recording_dir_no_boxes / filename;
            cv::imwrite(filepath.string(), rec_frame.frame);
            
            // Save depth frame as 16-bit PNG
            std::string depth_filename = session_name + "_frame_" + std::to_string(frame_num) + "_depth.png";
            fs::path depth_filepath = recording_dir_depth / depth_filename;
            cv::imwrite(depth_filepath.string(), rec_frame.depth_frame);
            
            frame_num++;
            
            // Log frame data to recording.log
            if (recording_logger_.isActive()) {
                recording_logger_.logEvents(rec_frame.ball_events, rec_frame.tracked_balls, 
                                           rec_frame.tracked_hands_simple);
                recording_logger_.logFrame(rec_frame.tracked_balls,
                                          rec_frame.tracked_hands_simple,
                                          camera_intrinsics,
                                          viz_states,
                                          rec_frame.raw_detections,
                                          rec_frame.frame);
            }
        }
        
        // Close recording logger
        recording_logger_.close();
        
        INFO_LOG("Saved ", frames.size(), " RGB frames to ", recording_dir_no_boxes.string());
        INFO_LOG("Saved ", frames.size(), " depth frames to ", recording_dir_depth.string());

        writeDebugLog("RecordingManager::saveFramesToDisk() - Checking for visualizations...");
        // Check if any visualizations are enabled
        bool has_visualizations = record_with_yolo_boxes ||
                                 viz_states.show_trajectory_predictions() ||
                                 viz_states.show_raw_detections() ||
                                 viz_states.show_filtered_detections() ||
                                 viz_states.show_hand_tracking() ||
                                 viz_states.show_ball_states() ||
                                 viz_states.show_skeleton() ||
                                 viz_states.show_color_search() ||
                                 viz_states.show_color_tracker() ||
                                 viz_states.show_tracked_boxes() ||
                                 viz_states.show_unmatched_detections() ||
                                 viz_states.show_tails() ||
                                 viz_states.show_trajectory() ||
                                 viz_states.show_hand_velocity_zone() ||
                                 viz_states.show_yolo_color_calibration();

        // TODO: Re-enable visualization rendering after full Engine.cpp integration
        // For now, skip visualization rendering to allow compilation
        if (has_visualizations) {
            writeDebugLog("RecordingManager::saveFramesToDisk() - Visualizations requested but temporarily disabled during refactoring");
            INFO_LOG("Note: Visualization rendering temporarily disabled during refactoring. Will be re-enabled after integration.");
        } else {
            writeDebugLog("RecordingManager::saveFramesToDisk() - No visualizations enabled");
        }

        writeDebugLog("RecordingManager::saveFramesToDisk() - Complete");
    } catch (const fs::filesystem_error& e) {
        writeDebugLog("RecordingManager::saveFramesToDisk() - FILESYSTEM EXCEPTION: " + std::string(e.what()));
    } catch (const std::exception& e) {
        writeDebugLog("RecordingManager::saveFramesToDisk() - EXCEPTION: " + std::string(e.what()));
    }
}