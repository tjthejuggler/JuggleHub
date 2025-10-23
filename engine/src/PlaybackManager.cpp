#include "../include/PlaybackManager.hpp"
#include "../include/DebugLog.hpp"
#include <algorithm>
#include <regex>

PlaybackManager::PlaybackManager()
    : current_frame_(0),
      total_frames_(0),
      is_loaded_(false),
      is_paused_(false),
      playback_speed_(1.0f) {
}

bool PlaybackManager::loadRecording(const std::string& recording_dir) {
    // Unload any existing recording
    unload();
    
    recording_dir_ = recording_dir;
    
    // Verify directory exists
    if (!fs::exists(recording_dir_) || !fs::is_directory(recording_dir_)) {
        ERROR_LOG("Recording directory does not exist: ", recording_dir_);
        return false;
    }
    
    // Extract session name
    session_name_ = extractSessionName(recording_dir_);
    
    // Scan directory structure
    if (!scanRecordingDirectory()) {
        ERROR_LOG("Failed to scan recording directory: ", recording_dir_);
        return false;
    }
    
    // Verify we have frames
    if (total_frames_ == 0) {
        ERROR_LOG("No frames found in recording directory: ", recording_dir_);
        return false;
    }
    
    is_loaded_ = true;
    current_frame_ = 0;
    
    INFO_LOG("✅ Loaded recording: ", session_name_, " (", total_frames_, " frames)");
    return true;
}

bool PlaybackManager::scanRecordingDirectory() {
    fs::path no_boxes_dir = fs::path(recording_dir_) / "no_boxes";
    fs::path depth_dir = fs::path(recording_dir_) / "depth";
    
    // Verify subdirectories exist
    if (!fs::exists(no_boxes_dir) || !fs::is_directory(no_boxes_dir)) {
        ERROR_LOG("Missing no_boxes directory: ", no_boxes_dir.string());
        return false;
    }
    
    if (!fs::exists(depth_dir) || !fs::is_directory(depth_dir)) {
        ERROR_LOG("Missing depth directory: ", depth_dir.string());
        return false;
    }
    
    // Scan RGB frames
    rgb_frame_paths_.clear();
    for (const auto& entry : fs::directory_iterator(no_boxes_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".jpg") {
            rgb_frame_paths_.push_back(entry.path().string());
        }
    }
    
    // Scan depth frames
    depth_frame_paths_.clear();
    for (const auto& entry : fs::directory_iterator(depth_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".png") {
            depth_frame_paths_.push_back(entry.path().string());
        }
    }
    
    // Sort frames by frame number (extract from filename)
    auto extractFrameNumber = [](const std::string& path) -> int {
        std::regex frame_regex("frame_(\\d+)");
        std::smatch match;
        if (std::regex_search(path, match, frame_regex)) {
            return std::stoi(match[1].str());
        }
        return -1;
    };
    
    std::sort(rgb_frame_paths_.begin(), rgb_frame_paths_.end(),
              [&](const std::string& a, const std::string& b) {
                  return extractFrameNumber(a) < extractFrameNumber(b);
              });
    
    std::sort(depth_frame_paths_.begin(), depth_frame_paths_.end(),
              [&](const std::string& a, const std::string& b) {
                  return extractFrameNumber(a) < extractFrameNumber(b);
              });
    
    // Verify we have matching RGB and depth frames
    if (rgb_frame_paths_.size() != depth_frame_paths_.size()) {
        WARN_LOG("Mismatch in RGB/depth frame counts: RGB=", rgb_frame_paths_.size(),
                 " Depth=", depth_frame_paths_.size());
        // Use the smaller count
        total_frames_ = std::min(rgb_frame_paths_.size(), depth_frame_paths_.size());
    } else {
        total_frames_ = rgb_frame_paths_.size();
    }
    
    return total_frames_ > 0;
}

bool PlaybackManager::loadFramePair(int frame_num, cv::Mat& rgb_out, cv::Mat& depth_out) {
    if (!is_loaded_ || frame_num < 0 || frame_num >= total_frames_) {
        return false;
    }
    
    // Load RGB frame
    rgb_out = cv::imread(rgb_frame_paths_[frame_num], cv::IMREAD_COLOR);
    if (rgb_out.empty()) {
        ERROR_LOG("Failed to load RGB frame: ", rgb_frame_paths_[frame_num]);
        return false;
    }
    
    // Load depth frame
    depth_out = cv::imread(depth_frame_paths_[frame_num], cv::IMREAD_UNCHANGED);
    if (depth_out.empty()) {
        ERROR_LOG("Failed to load depth frame: ", depth_frame_paths_[frame_num]);
        return false;
    }
    
    return true;
}

bool PlaybackManager::getNextFrame(cv::Mat& rgb_out, cv::Mat& depth_out) {
    if (!is_loaded_) {
        return false;
    }
    
    // Check if we're at the end
    if (current_frame_ >= total_frames_) {
        return false;  // End of recording
    }
    
    // Load current frame
    if (!loadFramePair(current_frame_, rgb_out, depth_out)) {
        return false;
    }
    
    // Advance to next frame
    current_frame_++;
    
    return true;
}

bool PlaybackManager::getPreviousFrame(cv::Mat& rgb_out, cv::Mat& depth_out) {
    if (!is_loaded_) {
        return false;
    }
    
    // Check if we're at the beginning
    if (current_frame_ <= 0) {
        return false;
    }
    
    // Move back one frame
    current_frame_--;
    
    // Load frame
    return loadFramePair(current_frame_, rgb_out, depth_out);
}

bool PlaybackManager::getFrameAt(int frame_num, cv::Mat& rgb_out, cv::Mat& depth_out) {
    if (!is_loaded_ || frame_num < 0 || frame_num >= total_frames_) {
        return false;
    }
    
    current_frame_ = frame_num;
    return loadFramePair(current_frame_, rgb_out, depth_out);
}

void PlaybackManager::reset() {
    current_frame_ = 0;
    is_paused_ = false;
}

void PlaybackManager::unload() {
    recording_dir_.clear();
    rgb_frame_paths_.clear();
    depth_frame_paths_.clear();
    session_name_.clear();
    current_frame_ = 0;
    total_frames_ = 0;
    is_loaded_ = false;
    is_paused_ = false;
    playback_speed_ = 1.0f;
}

std::string PlaybackManager::extractSessionName(const std::string& dir_path) {
    fs::path path(dir_path);
    return path.filename().string();
}