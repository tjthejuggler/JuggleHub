#include "PlaybackController.hpp"
#include "DebugLog.hpp"
#include <thread>

extern void writeDebugLog(const std::string& message);

PlaybackController::PlaybackController()
    : playback_manager_(std::make_unique<PlaybackManager>()),
      playback_mode_(false) {
}

PlaybackController::~PlaybackController() {
    stopPlayback();
}

bool PlaybackController::startPlayback(const std::string& recording_dir) {
    writeDebugLog("PlaybackController::startPlayback() - Starting playback from: " + recording_dir);
    
    // Load recording
    if (!playback_manager_->loadRecording(recording_dir)) {
        ERROR_LOG("Failed to load recording: ", recording_dir);
        return false;
    }
    
    // Enter playback mode
    playback_mode_ = true;
    last_frame_time_ = std::chrono::steady_clock::now();
    
    INFO_LOG("✅ Playback started: ", playback_manager_->getSessionName(),
             " (", playback_manager_->getTotalFrames(), " frames)");
    return true;
}

void PlaybackController::stopPlayback() {
    writeDebugLog("PlaybackController::stopPlayback() - Stopping playback");
    
    if (!playback_mode_) {
        writeDebugLog("PlaybackController::stopPlayback() - Not in playback mode");
        return;
    }
    
    // Exit playback mode
    playback_mode_ = false;
    playback_manager_->unload();
    
    INFO_LOG("✅ Playback stopped");
}

void PlaybackController::stepForward() {
    if (!playback_mode_ || !playback_manager_->isLoaded()) {
        WARN_LOG("stepForward() - Not in playback mode or no recording loaded");
        return;
    }
    
    // Pause automatic playback
    playback_manager_->setPaused(true);
    
    // Get next frame
    cv::Mat rgb_frame, depth_frame;
    if (playback_manager_->getNextFrame(rgb_frame, depth_frame)) {
        writeDebugLog("PlaybackController::stepForward() - Stepped to frame " +
                     std::to_string(playback_manager_->getCurrentFrameNumber()));
    } else {
        WARN_LOG("stepForward() - Already at end");
    }
}

void PlaybackController::stepBackward() {
    if (!playback_mode_ || !playback_manager_->isLoaded()) {
        WARN_LOG("stepBackward() - Not in playback mode or no recording loaded");
        return;
    }
    
    // Pause automatic playback
    playback_manager_->setPaused(true);
    
    // Get previous frame
    cv::Mat rgb_frame, depth_frame;
    if (playback_manager_->getPreviousFrame(rgb_frame, depth_frame)) {
        writeDebugLog("PlaybackController::stepBackward() - Stepped to frame " +
                     std::to_string(playback_manager_->getCurrentFrameNumber()));
    } else {
        WARN_LOG("stepBackward() - Already at beginning");
    }
}

void PlaybackController::setSpeed(float speed) {
    if (!playback_manager_) {
        return;
    }
    
    playback_manager_->setSpeed(speed);
    INFO_LOG("Playback speed set to ", speed, "x");
}

void PlaybackController::pause() {
    if (!playback_mode_ || !playback_manager_->isLoaded()) {
        return;
    }
    
    playback_manager_->setPaused(true);
    INFO_LOG("Playback paused at frame ", playback_manager_->getCurrentFrameNumber());
}

void PlaybackController::resume() {
    if (!playback_mode_ || !playback_manager_->isLoaded()) {
        return;
    }
    
    playback_manager_->setPaused(false);
    last_frame_time_ = std::chrono::steady_clock::now();
    INFO_LOG("Playback resumed from frame ", playback_manager_->getCurrentFrameNumber());
}

bool PlaybackController::isPaused() const {
    return playback_manager_ && playback_manager_->isPaused();
}

bool PlaybackController::isLoaded() const {
    return playback_manager_ && playback_manager_->isLoaded();
}

bool PlaybackController::getNextFrame(cv::Mat& color_image, cv::Mat& depth_image, uint32_t camera_fps) {
    if (!playback_mode_ || !playback_manager_->isLoaded() || playback_manager_->isPaused()) {
        return false;
    }
    
    // Calculate frame timing based on playback speed
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - last_frame_time_).count();
    
    // Target frame time based on camera FPS and playback speed
    float target_frame_time = (1000.0f / camera_fps) / playback_manager_->getSpeed();
    
    if (elapsed >= target_frame_time) {
        // Time for next frame
        if (playback_manager_->getNextFrame(color_image, depth_image)) {
            last_frame_time_ = now;
            
            writeDebugLog("Playback frame " +
                        std::to_string(playback_manager_->getCurrentFrameNumber() - 1) +
                        " / " + std::to_string(playback_manager_->getTotalFrames()));
            return true;
        } else {
            // End of recording - loop back to beginning
            INFO_LOG("End of playback reached, looping to beginning");
            playback_manager_->reset();
            return false;
        }
    }
    
    return false;
}

bool PlaybackController::getCurrentFrame(cv::Mat& color_image, cv::Mat& depth_image) {
    if (!playback_mode_ || !playback_manager_->isLoaded()) {
        return false;
    }
    
    return playback_manager_->getFrameAt(playback_manager_->getCurrentFrameNumber(), 
                                        color_image, depth_image);
}

std::string PlaybackController::getSessionName() const {
    return playback_manager_ ? playback_manager_->getSessionName() : "";
}

std::string PlaybackController::getRecordingDirectory() const {
    return playback_manager_ ? playback_manager_->getRecordingDirectory() : "";
}

int PlaybackController::getCurrentFrameNumber() const {
    return playback_manager_ ? playback_manager_->getCurrentFrameNumber() : 0;
}

int PlaybackController::getTotalFrames() const {
    return playback_manager_ ? playback_manager_->getTotalFrames() : 0;
}

float PlaybackController::getSpeed() const {
    return playback_manager_ ? playback_manager_->getSpeed() : 1.0f;
}