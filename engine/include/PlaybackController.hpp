#pragma once

#include "PlaybackManager.hpp"
#include <opencv2/opencv.hpp>
#include <memory>
#include <atomic>
#include <chrono>
#include <string>

class PlaybackController {
public:
    PlaybackController();
    ~PlaybackController();

    // Playback control
    bool startPlayback(const std::string& recording_dir);
    void stopPlayback();
    void stepForward();
    void stepBackward();
    void setSpeed(float speed);
    void pause();
    void resume();
    
    // State queries
    bool isActive() const { return playback_mode_; }
    bool isPaused() const;
    bool isLoaded() const;
    
    // Frame acquisition
    bool getNextFrame(cv::Mat& color_image, cv::Mat& depth_image, uint32_t camera_fps);
    bool getCurrentFrame(cv::Mat& color_image, cv::Mat& depth_image);
    
    // Playback info
    std::string getSessionName() const;
    std::string getRecordingDirectory() const;
    int getCurrentFrameNumber() const;
    int getTotalFrames() const;
    float getSpeed() const;

private:
    std::unique_ptr<PlaybackManager> playback_manager_;
    std::atomic<bool> playback_mode_;
    std::chrono::steady_clock::time_point last_frame_time_;
};