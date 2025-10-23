#pragma once

#include <string>
#include <vector>
#include <opencv2/opencv.hpp>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

/**
 * @brief Manages playback of recorded juggling sessions
 * 
 * Loads and sequences through RGB and depth frames from a recording directory.
 * Supports forward/backward stepping and automatic playback with speed control.
 */
class PlaybackManager {
public:
    PlaybackManager();
    ~PlaybackManager() = default;
    
    /**
     * @brief Load a recording directory
     * @param recording_dir Path to recording directory (must contain no_boxes/ and depth/ subdirs)
     * @return true if successfully loaded, false otherwise
     */
    bool loadRecording(const std::string& recording_dir);
    
    /**
     * @brief Get the next frame in sequence
     * @param rgb_out Output RGB frame
     * @param depth_out Output depth frame
     * @return true if frame loaded successfully, false if end of recording
     */
    bool getNextFrame(cv::Mat& rgb_out, cv::Mat& depth_out);
    
    /**
     * @brief Get the previous frame in sequence
     * @param rgb_out Output RGB frame
     * @param depth_out Output depth frame
     * @return true if frame loaded successfully, false if at beginning
     */
    bool getPreviousFrame(cv::Mat& rgb_out, cv::Mat& depth_out);
    
    /**
     * @brief Get a specific frame by number
     * @param frame_num Frame number (0-indexed)
     * @param rgb_out Output RGB frame
     * @param depth_out Output depth frame
     * @return true if frame loaded successfully, false otherwise
     */
    bool getFrameAt(int frame_num, cv::Mat& rgb_out, cv::Mat& depth_out);
    
    /**
     * @brief Reset playback to the beginning
     */
    void reset();
    
    /**
     * @brief Unload current recording and clear state
     */
    void unload();
    
    // State queries
    int getCurrentFrameNumber() const { return current_frame_; }
    int getTotalFrames() const { return total_frames_; }
    bool isLoaded() const { return is_loaded_; }
    std::string getRecordingDirectory() const { return recording_dir_; }
    std::string getSessionName() const { return session_name_; }
    
    // Playback settings
    void setSpeed(float speed) { playback_speed_ = std::max(0.1f, std::min(2.0f, speed)); }
    float getSpeed() const { return playback_speed_; }
    void setPaused(bool paused) { is_paused_ = paused; }
    bool isPaused() const { return is_paused_; }
    
private:
    /**
     * @brief Scan recording directory and build frame lists
     * @return true if valid recording structure found
     */
    bool scanRecordingDirectory();
    
    /**
     * @brief Load a specific RGB/depth frame pair
     * @param frame_num Frame number to load
     * @param rgb_out Output RGB frame
     * @param depth_out Output depth frame
     * @return true if both frames loaded successfully
     */
    bool loadFramePair(int frame_num, cv::Mat& rgb_out, cv::Mat& depth_out);
    
    /**
     * @brief Extract session name from directory path
     * @param dir_path Directory path
     * @return Session name (directory basename)
     */
    std::string extractSessionName(const std::string& dir_path);
    
    std::string recording_dir_;              // Path to recording directory
    std::vector<std::string> rgb_frame_paths_;   // Sorted list of RGB frame paths
    std::vector<std::string> depth_frame_paths_; // Sorted list of depth frame paths
    int current_frame_;                      // Current frame index (0-based)
    int total_frames_;                       // Total number of frames
    bool is_loaded_;                         // Whether a recording is loaded
    bool is_paused_;                         // Whether playback is paused
    float playback_speed_;                   // Playback speed multiplier
    std::string session_name_;               // Session name (e.g., "continuous_2025-10-23_09-35-17")
};