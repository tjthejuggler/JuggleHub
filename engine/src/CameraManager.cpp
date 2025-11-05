#include "CameraManager.hpp"
#include "DebugLog.hpp"
#include <librealsense2/rs_advanced_mode.hpp>
#include <fstream>
#include <sstream>
#include <thread>
#include <chrono>

extern void writeDebugLog(const std::string& message);

CameraManager::CameraManager()
    : align_to_color_(RS2_STREAM_COLOR),
      camera_running_(false),
      ir_projector_active_(false),
      camera_width_(640),
      camera_height_(480),
      camera_fps_(60) {
}

CameraManager::~CameraManager() {
    stop();
}

void CameraManager::initialize(const std::string& camera_settings_path, uint32_t width, uint32_t height, uint32_t fps) {
    camera_settings_path_ = camera_settings_path;
    camera_width_ = width;
    camera_height_ = height;
    camera_fps_ = fps;

    // Load camera settings from JSON file first
    if (!camera_settings_path_.empty()) {
        loadSettingsFromJson(camera_settings_path_);
    }

    // Configure camera streams but do not start them
    rs_config_.enable_stream(RS2_STREAM_COLOR, camera_width_, camera_height_, RS2_FORMAT_BGR8, camera_fps_);
    rs_config_.enable_stream(RS2_STREAM_DEPTH, camera_width_, camera_height_, RS2_FORMAT_Z16, camera_fps_);
    
    INFO_LOG("Enabling hardware-accelerated frame alignment for better performance");
}

void CameraManager::loadSettingsFromJson(const std::string& json_path) {
    try {
        std::ifstream file(json_path);
        if (!file.is_open()) {
            throw std::runtime_error("Could not open camera settings file: " + json_path);
        }
        
        // Read the entire JSON file content into a string
        std::stringstream buffer;
        buffer << file.rdbuf();
        json_content_ = buffer.str();
        
        INFO_LOG("Loaded camera settings from: ", json_path);
    } catch (const std::exception& e) {
        throw;
    }
}

void CameraManager::applySettings() {
    if (json_content_.empty()) {
        return;
    }

    try {
        if (!camera_running_) {
            return;
        }

        auto profile = pipe_.get_active_profile();
        rs2::device dev = profile.get_device();

        if (dev.is<rs2::serializable_device>()) {
            rs2::serializable_device serializable_dev = dev.as<rs2::serializable_device>();
            serializable_dev.load_json(json_content_);
        }
    } catch (const rs2::error& e) {
        ERROR_LOG("RealSense error applying settings: ", e.what());
    } catch (const std::exception& e) {
        ERROR_LOG("Error applying settings: ", e.what());
    }
}

void CameraManager::stop() {
    if (!camera_running_) {
        return;
    }

    try {
        pipe_.stop();
        INFO_LOG("[LOG] Camera stopped successfully.");
        camera_running_ = false;
        ir_projector_active_ = false;
    } catch (const rs2::error& e) {
        ERROR_LOG("Error stopping camera: ", e.what());
    }
}

void CameraManager::start() {
    writeDebugLog("CameraManager::start() - Starting...");
    
    if (camera_running_) {
        writeDebugLog("CameraManager::start() - Camera already running, returning");
        return;
    }

    DEBUG_LOG("[LOG] Attempting to start camera pipeline...");
    writeDebugLog("CameraManager::start() - Attempting to start RealSense pipeline...");

    try {
        writeDebugLog("CameraManager::start() - Calling pipe_.start()...");
        rs2::pipeline_profile profile = pipe_.start(rs_config_);
        camera_running_ = true;
        INFO_LOG("[LOG] Camera pipeline started successfully.");
        writeDebugLog("CameraManager::start() - Pipeline started successfully");

        // Store Camera Intrinsics
        writeDebugLog("CameraManager::start() - Retrieving camera intrinsics...");
        auto stream = profile.get_stream(RS2_STREAM_DEPTH).as<rs2::video_stream_profile>();
        auto intrinsics = stream.get_intrinsics();
        camera_intrinsics_.fx = intrinsics.fx;
        camera_intrinsics_.fy = intrinsics.fy;
        camera_intrinsics_.ppx = intrinsics.ppx;
        camera_intrinsics_.ppy = intrinsics.ppy;
        writeDebugLog("CameraManager::start() - Intrinsics: fx=" + std::to_string(intrinsics.fx) +
                      " fy=" + std::to_string(intrinsics.fy) +
                      " ppx=" + std::to_string(intrinsics.ppx) +
                      " ppy=" + std::to_string(intrinsics.ppy));

        // Wait for device to stabilize
        writeDebugLog("CameraManager::start() - Waiting 500ms for device to stabilize...");
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        // Apply advanced settings from JSON
        writeDebugLog("CameraManager::start() - Applying camera settings...");
        applySettings();
        writeDebugLog("CameraManager::start() - Camera settings applied");

        // Enable IR projector
        writeDebugLog("CameraManager::start() - Enabling IR projector...");
        try {
            auto sensor = profile.get_device().first<rs2::depth_sensor>();
            if (sensor.supports(RS2_OPTION_EMITTER_ENABLED)) {
                sensor.set_option(RS2_OPTION_EMITTER_ENABLED, 1.f);
                ir_projector_active_ = true;
                writeDebugLog("CameraManager::start() - IR projector enabled");
            } else {
                writeDebugLog("CameraManager::start() - IR projector not supported");
            }
        } catch (const rs2::error& e) {
            ir_projector_active_ = false;
            writeDebugLog("CameraManager::start() - Failed to enable IR projector: " + std::string(e.what()));
        }

        writeDebugLog("CameraManager::start() - Complete");
    } catch (const rs2::error& e) {
        camera_running_ = false;
        writeDebugLog("CameraManager::start() - EXCEPTION: " + std::string(e.what()));
        throw;
    }
}

void CameraManager::startWithSettings(const std::string& settings_file) {
    startWithSettings(settings_file, camera_width_, camera_height_, camera_fps_);
}

void CameraManager::startWithSettings(const std::string& settings_file, uint32_t width, uint32_t height, uint32_t fps) {
    INFO_LOG("Reconfiguring camera with settings: ", settings_file,
             " at ", width, "x", height, " @ ", fps, " FPS");

    // Stop the pipeline completely
    if (camera_running_) {
        stop();
    }

    // Create a new configuration object
    rs2::config new_config;

    // Update member variables
    camera_width_ = width;
    camera_height_ = height;
    camera_fps_ = fps;
    camera_settings_path_ = settings_file;

    // Enable streams on new configuration
    new_config.enable_stream(RS2_STREAM_COLOR, camera_width_, camera_height_, RS2_FORMAT_BGR8, camera_fps_);
    new_config.enable_stream(RS2_STREAM_DEPTH, camera_width_, camera_height_, RS2_FORMAT_Z16, camera_fps_);

    // Replace old configuration
    rs_config_ = new_config;

    // Load and apply JSON settings
    if (!camera_settings_path_.empty()) {
        loadSettingsFromJson(camera_settings_path_);
    }

    // Start camera with new configuration
    start();
}

bool CameraManager::getFrames(cv::Mat& color_image, cv::Mat& depth_image) {
    if (!camera_running_) {
        return false;
    }

    try {
        rs2::frameset frames = pipe_.wait_for_frames(5000);
        
        // Align depth to color
        rs2::frameset aligned_frames = align_to_color_.process(frames);
        
        rs2::video_frame color_frame = aligned_frames.get_color_frame();
        rs2::depth_frame depth_frame = aligned_frames.get_depth_frame();
        
        if (!color_frame || !depth_frame) {
            return false;
        }
        
        // Convert to OpenCV Mat
        color_image = cv::Mat(cv::Size(color_frame.get_width(), color_frame.get_height()),
                             CV_8UC3, (void*)color_frame.get_data(), cv::Mat::AUTO_STEP).clone();
        
        depth_image = cv::Mat(cv::Size(depth_frame.get_width(), depth_frame.get_height()),
                             CV_16UC1, (void*)depth_frame.get_data(), cv::Mat::AUTO_STEP).clone();
        
        // Cache frames
        last_color_frame_ = color_image.clone();
        last_depth_frame_ = depth_image.clone();
        
        return true;
        
    } catch (const rs2::error& e) {
        ERROR_LOG("RealSense error: ", e.what());
        return false;
    }
}

void CameraManager::setExposure(int exposure_microseconds) {
    if (!camera_running_) {
        ERROR_LOG("Cannot set exposure: camera is not running");
        return;
    }

    try {
        auto profile = pipe_.get_active_profile();
        rs2::device dev = profile.get_device();
        
        // Get the color sensor
        auto sensors = dev.query_sensors();
        for (auto& sensor : sensors) {
            if (sensor.is<rs2::color_sensor>()) {
                // Disable auto exposure first
                if (sensor.supports(RS2_OPTION_ENABLE_AUTO_EXPOSURE)) {
                    sensor.set_option(RS2_OPTION_ENABLE_AUTO_EXPOSURE, 0.0f);
                    INFO_LOG("Auto exposure disabled");
                }
                
                // Set manual exposure
                if (sensor.supports(RS2_OPTION_EXPOSURE)) {
                    // Clamp exposure value to valid range
                    auto range = sensor.get_option_range(RS2_OPTION_EXPOSURE);
                    float clamped_exposure = std::max(range.min, std::min(range.max, static_cast<float>(exposure_microseconds)));
                    
                    sensor.set_option(RS2_OPTION_EXPOSURE, clamped_exposure);
                    INFO_LOG("Camera exposure set to ", clamped_exposure, " microseconds");
                } else {
                    ERROR_LOG("Camera does not support manual exposure control");
                }
                break;
            }
        }
    } catch (const rs2::error& e) {
        ERROR_LOG("RealSense error setting exposure: ", e.what());
    } catch (const std::exception& e) {
        ERROR_LOG("Error setting exposure: ", e.what());
    }
}

void CameraManager::setAutoExposure(bool enabled) {
    if (!camera_running_) {
        ERROR_LOG("Cannot set auto exposure: camera is not running");
        return;
    }

    try {
        auto profile = pipe_.get_active_profile();
        rs2::device dev = profile.get_device();
        
        // Get the color sensor
        auto sensors = dev.query_sensors();
        for (auto& sensor : sensors) {
            if (sensor.is<rs2::color_sensor>()) {
                // Set auto exposure
                if (sensor.supports(RS2_OPTION_ENABLE_AUTO_EXPOSURE)) {
                    sensor.set_option(RS2_OPTION_ENABLE_AUTO_EXPOSURE, enabled ? 1.0f : 0.0f);
                    INFO_LOG("Auto exposure ", enabled ? "enabled" : "disabled");
                } else {
                    ERROR_LOG("Camera does not support auto exposure control");
                }
                break;
            }
        }
    } catch (const rs2::error& e) {
        ERROR_LOG("RealSense error setting auto exposure: ", e.what());
    } catch (const std::exception& e) {
        ERROR_LOG("Error setting auto exposure: ", e.what());
    }
}