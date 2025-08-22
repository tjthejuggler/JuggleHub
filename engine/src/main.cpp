#include <librealsense2/rs.hpp>
#include <opencv2/opencv.hpp>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <thread>
#include <chrono>
#include <atomic>
#include <memory>
#include <csignal>

// Protocol Buffers
#include "juggler.pb.h"

// ZeroMQ for communication
#include <zmq.hpp>

#ifdef ENABLE_HAND_TRACKING
#include "hand_tracker.hpp"
#endif

// Engine components
#include "BallTracker.hpp"
#include "Engine.hpp"

using namespace juggler::v1;

// Global configuration
struct EngineConfig {
    bool show_timestamp = false;
    std::string mode = "tracking";
    int width = 0;
    int height = 0;
    int fps = 0;
    double downscale_factor = 0.5;
    bool high_fps_preferred = false;
    bool track_hands = false;
    std::string zmq_endpoint = "tcp://*:5555";
    std::string settings_file = "ball_settings.json";
};

// Camera configuration presets
struct CameraMode {
    int width, height, fps;
};

// Constants
const double MIN_CONTOUR_AREA = 100.0;
const float MAX_DEPTH = 3.0f;
const double MERGE_DISTANCE_THRESHOLD = 80.0;

class JuggleEngine {
private:
    EngineConfig config_;
    std::unique_ptr<juggler::BallTracker> ball_tracker_;
    zmq::context_t zmq_context_;
    zmq::socket_t zmq_socket_;
    std::atomic<bool> running_;
    
    // RealSense components
    rs2::pipeline pipe_;
    rs2::config rs_config_;
    rs2::align align_to_color_;
    rs2::spatial_filter spat_filter_;
    rs2::temporal_filter temp_filter_;
    
#ifdef ENABLE_HAND_TRACKING
    std::unique_ptr<HandTracker> hand_tracker_;
#endif

public:
    JuggleEngine(const EngineConfig& config) 
        : config_(config)
        , zmq_context_(1)
        , zmq_socket_(zmq_context_, ZMQ_PUB)
        , running_(false)
        , align_to_color_(RS2_STREAM_COLOR)
    {
        // Initialize ZeroMQ
        zmq_socket_.bind(config_.zmq_endpoint);
        
        // Initialize ball tracker
        ball_tracker_ = std::make_unique<juggler::BallTracker>(config_.settings_file);
        
#ifdef ENABLE_HAND_TRACKING
        if (config_.track_hands) {
            try {
                std::string model_path = "/home/twain/Projects/mediapipe/hand_landmarker.task";
                hand_tracker_ = std::make_unique<HandTracker>(model_path);
                std::cerr << "Hand tracking enabled." << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "Warning: Could not initialize hand tracker: " << e.what() << std::endl;
            }
        }
#endif
    }
    
    bool initializeCamera() {
        std::vector<CameraMode> modes_to_try;
        if (config_.high_fps_preferred) {
            modes_to_try = {{848, 480, 90}, {640, 480, 60}, {1280, 720, 30}};
        } else {
            modes_to_try = {{1280, 720, 90}, {1280, 720, 60}, {1280, 720, 30}, {848, 480, 90}, {640, 480, 60}};
        }

        bool started = false;
        for (const auto& mode : modes_to_try) {
            try {
                rs_config_.disable_all_streams();
                rs_config_.enable_stream(RS2_STREAM_COLOR, mode.width, mode.height, RS2_FORMAT_BGR8, mode.fps);
                rs_config_.enable_stream(RS2_STREAM_DEPTH, mode.width, mode.height, RS2_FORMAT_Z16, mode.fps);
                pipe_.start(rs_config_);
                
                config_.width = mode.width;
                config_.height = mode.height;
                config_.fps = mode.fps;
                
                std::cerr << "Camera started at " << mode.width << "x" << mode.height << " @ " << mode.fps << " FPS" << std::endl;
                started = true;
                break;
            } catch (const rs2::error& e) {
                std::cerr << "Warning: Could not start " << mode.width << "x" << mode.height << " @ " << mode.fps << " FPS" << std::endl;
            }
        }

        if (!started) {
            std::cerr << "Error: Failed to start RealSense camera with any available mode." << std::endl;
            return false;
        }

        return true;
    }
    
    void run() {
        if (!initializeCamera()) {
            return;
        }
        
        running_ = true;
        std::cerr << "JuggleEngine started. Publishing on " << config_.zmq_endpoint << std::endl;
        
        // Get camera intrinsics
        auto profile = pipe_.get_active_profile();
        auto intrinsics = profile.get_stream(RS2_STREAM_COLOR).as<rs2::video_stream_profile>().get_intrinsics();
        
        uint32_t frame_number = 0;
        
        while (running_) {
            rs2::frameset frames;
            if (!pipe_.try_wait_for_frames(&frames, 100)) {
                continue;
            }
            
            auto aligned_frames = align_to_color_.process(frames);
            auto color_frame = aligned_frames.get_color_frame();
            rs2::depth_frame depth_frame = aligned_frames.get_depth_frame();
            
            if (!color_frame || !depth_frame) continue;
            
            // Apply depth filters
            depth_frame = spat_filter_.process(depth_frame);
            depth_frame = temp_filter_.process(depth_frame);
            
            // Convert to OpenCV matrices
            cv::Mat color_image(cv::Size(config_.width, config_.height), CV_8UC3, 
                              (void*)color_frame.get_data(), cv::Mat::AUTO_STEP);
            
            // Process frame and create FrameData message
            FrameData frame_data = processFrame(color_image, depth_frame, intrinsics, frame_number++);
            
            // Serialize and send via ZeroMQ
            std::string serialized_data;
            frame_data.SerializeToString(&serialized_data);
            
            zmq::message_t message(serialized_data.size());
            memcpy(message.data(), serialized_data.c_str(), serialized_data.size());
            zmq_socket_.send(message, zmq::send_flags::dontwait);
            
            // Also output to stdout for backward compatibility
            outputToStdout(frame_data);
            
            // Release matrices
            color_image.release();
        }
    }
    
    void stop() {
        running_ = false;
        pipe_.stop();
    }

private:
    FrameData processFrame(const cv::Mat& color_image, const rs2::depth_frame& depth_frame, 
                          const rs2_intrinsics& intrinsics, uint32_t frame_number) {
        FrameData frame_data;
        
        // Set timestamp
        auto now = std::chrono::system_clock::now();
        auto timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
            now.time_since_epoch()).count();
        frame_data.set_timestamp_us(timestamp_us);
        
        // Set frame metadata
        frame_data.set_frame_width(config_.width);
        frame_data.set_frame_height(config_.height);
        frame_data.set_frame_number(frame_number);
        
        // Set camera intrinsics
        CameraIntrinsics* cam_intrinsics = frame_data.mutable_intrinsics();
        cam_intrinsics->set_fx(intrinsics.fx);
        cam_intrinsics->set_fy(intrinsics.fy);
        cam_intrinsics->set_ppx(intrinsics.ppx);
        cam_intrinsics->set_ppy(intrinsics.ppy);
        cam_intrinsics->set_depth_scale(0.001); // RealSense depth scale
        
        // Detect balls
        auto detected_balls = ball_tracker_->detectBalls(color_image, depth_frame, intrinsics, config_.downscale_factor);
        
        for (const auto& detection : detected_balls) {
            Ball* ball = frame_data.add_balls();
            ball->set_id(detection.id);
            ball->set_color_name(detection.color_name);
            
            // Set 3D position
            Vector3* pos_3d = ball->mutable_position_3d();
            pos_3d->set_x(detection.world_x);
            pos_3d->set_y(detection.world_y);
            pos_3d->set_z(detection.world_z);
            
            // Set 2D position
            Vector2* pos_2d = ball->mutable_position_2d();
            pos_2d->set_x(detection.center.x);
            pos_2d->set_y(detection.center.y);
            
            ball->set_radius_px(15.0); // Default radius
            ball->set_depth_m(detection.world_z);
            ball->set_confidence(detection.confidence);
            ball->set_is_held(detection.is_held);
            ball->set_timestamp_us(timestamp_us);
            
            // Set color information
            Color* color = ball->mutable_color_bgr();
            color->set_b(0);
            color->set_g(255);
            color->set_r(0);
        }
        
#ifdef ENABLE_HAND_TRACKING
        // Process hand tracking
        if (config_.track_hands && hand_tracker_) {
            auto hands = hand_tracker_->DetectHands(color_image, timestamp_us / 1000);
            
            for (size_t i = 0; i < hands.size(); ++i) {
                Hand* hand = frame_data.add_hands();
                hand->set_side(i == 0 ? "left" : "right"); // Simplified
                hand->set_is_visible(true);
                hand->set_confidence(0.8); // Default confidence
                
                // Use palm landmark as hand position (landmark 0)
                if (!hands[i].normalized_landmarks.empty()) {
                    const auto& palm = hands[i].normalized_landmarks[0];
                    Vector2* pos_2d = hand->mutable_position_2d();
                    pos_2d->set_x(palm.x * config_.width);
                    pos_2d->set_y(palm.y * config_.height);
                    
                    // Get 3D position if depth is available
                    int x = static_cast<int>(palm.x * config_.width);
                    int y = static_cast<int>(palm.y * config_.height);
                    if (x >= 0 && x < config_.width && y >= 0 && y < config_.height) {
                        float depth = getAveragedDepth(depth_frame, x, y, 5);
                        if (depth > 0 && depth < MAX_DEPTH) {
                            float pixel[2] = {static_cast<float>(x), static_cast<float>(y)};
                            float point[3];
                            rs2_deproject_pixel_to_point(point, &intrinsics, pixel, depth);
                            
                            Vector3* pos_3d = hand->mutable_position_3d();
                            pos_3d->set_x(point[0]);
                            pos_3d->set_y(point[1]);
                            pos_3d->set_z(point[2]);
                        }
                    }
                }
            }
        }
#endif
        
        // Set system status
        SystemStatus* status = frame_data.mutable_status();
        status->set_camera_connected(true);
        status->set_engine_running(true);
        status->set_fps(config_.fps);
        status->set_frame_count(frame_number);
        status->set_mode(config_.mode);
        status->set_timestamp_us(timestamp_us);
        
        return frame_data;
    }
    
    float getAveragedDepth(const rs2::depth_frame& depth_frame, int x, int y, int patch_size) {
        float total_depth = 0.0f;
        int valid_pixel_count = 0;
        
        int start_x = std::max(0, x - patch_size / 2);
        int end_x = std::min(depth_frame.get_width() - 1, x + patch_size / 2);
        int start_y = std::max(0, y - patch_size / 2);
        int end_y = std::min(depth_frame.get_height() - 1, y + patch_size / 2);

        for (int cy = start_y; cy <= end_y; ++cy) {
            for (int cx = start_x; cx <= end_x; ++cx) {
                float depth = depth_frame.get_distance(cx, cy);
                if (depth > 0.0f) {
                    total_depth += depth;
                    valid_pixel_count++;
                }
            }
        }

        return valid_pixel_count > 0 ? total_depth / valid_pixel_count : 0.0f;
    }
    
    void outputToStdout(const FrameData& frame_data) {
        if (frame_data.balls_size() == 0) return;
        
        std::string output_line;
        if (config_.show_timestamp) {
            output_line += std::to_string(frame_data.timestamp_us() / 1000) + "|";
        }
        
        for (int i = 0; i < frame_data.balls_size(); ++i) {
            const auto& ball = frame_data.balls(i);
            output_line += ball.color_name() + "," +
                          std::to_string(ball.position_3d().x()) + "," +
                          std::to_string(ball.position_3d().y()) + "," +
                          std::to_string(ball.position_3d().z());
            if (i < frame_data.balls_size() - 1) {
                output_line += ";";
            }
        }
        
        std::cout << output_line << std::endl;
        std::cout.flush();
    }
};

// Function to parse command-line arguments
EngineConfig parseArguments(int argc, char* argv[]) {
    EngineConfig config;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--timestamp" || arg == "-t") {
            config.show_timestamp = true;
        } else if (arg == "--high-fps" || arg == "-r") {
            config.high_fps_preferred = true;
        } else if (arg == "--width" && i + 1 < argc) {
            config.width = std::stoi(argv[++i]);
        } else if (arg == "--height" && i + 1 < argc) {
            config.height = std::stoi(argv[++i]);
        } else if (arg == "--fps" && i + 1 < argc) {
            config.fps = std::stoi(argv[++i]);
        } else if (arg == "--downscale" && i + 1 < argc) {
            config.downscale_factor = std::stod(argv[++i]);
        } else if (arg == "--track-hands") {
            config.track_hands = true;
        } else if (arg == "--zmq-endpoint" && i + 1 < argc) {
            config.zmq_endpoint = argv[++i];
        } else if (arg == "calibrate" || arg == "stream" || arg == "tracking") {
            config.mode = arg;
        } else {
            std::cerr << "Warning: Unknown argument '" << arg << "' ignored." << std::endl;
        }
    }
    return config;
}

int main(int argc, char* argv[]) {
    try {
        // Initialize Protocol Buffers
        GOOGLE_PROTOBUF_VERIFY_VERSION;
        
        // Parse command-line arguments
        EngineConfig config = parseArguments(argc, argv);
        
        // Create and run engine
        JuggleEngine engine(config);
        
        // Set up signal handling for graceful shutdown
        signal(SIGINT, [](int) {
            std::cerr << "\nShutting down JuggleEngine..." << std::endl;
            exit(0);
        });
        
        engine.run();
        
        // Clean up Protocol Buffers
        google::protobuf::ShutdownProtobufLibrary();
        
    } catch (const rs2::error& e) {
        std::cerr << "RealSense error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}