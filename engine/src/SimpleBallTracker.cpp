#include "SimpleBallTracker.hpp"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <set>

// Helper function for depth filtering
static float get_filtered_depth(const cv::Mat& depth_frame, const cv::Point2f& pixel) {
    const int SAMPLE_SIZE = 5;
    const int half_size = SAMPLE_SIZE / 2;
    
    std::vector<float> depth_samples;
    depth_samples.reserve(SAMPLE_SIZE * SAMPLE_SIZE);
    
    int center_x = static_cast<int>(pixel.x);
    int center_y = static_cast<int>(pixel.y);
    
    for (int dy = -half_size; dy <= half_size; ++dy) {
        for (int dx = -half_size; dx <= half_size; ++dx) {
            int x = center_x + dx;
            int y = center_y + dy;
            
            if (x >= 0 && x < depth_frame.cols && y >= 0 && y < depth_frame.rows) {
                uint16_t depth_mm = depth_frame.at<uint16_t>(y, x);
                float depth_m = depth_mm / 1000.0f;
                
                if (depth_m > 0.1f && depth_m < 3.0f) {
                    depth_samples.push_back(depth_m);
                }
            }
        }
    }
    
    if (depth_samples.empty()) return 0.0f;
    
    std::sort(depth_samples.begin(), depth_samples.end());
    return depth_samples[depth_samples.size() / 2];
}

SimpleBallTracker::SimpleBallTracker(const std::string& ball_model_path,
                                    const std::string& pose_model_path,
                                    const std::string& device_name,
                                    const std::string& settings_file)
    : settings_file_(settings_file) {
    
    std::cout << "[SimpleBallTracker] Initializing..." << std::endl;
    
    // Load OpenVINO models
    std::cout << "[SimpleBallTracker] Loading ball model: " << ball_model_path << std::endl;
    ball_model_ = core_.compile_model(ball_model_path, device_name);
    ball_infer_ = ball_model_.create_infer_request();
    
    std::cout << "[SimpleBallTracker] Loading pose model: " << pose_model_path << std::endl;
    pose_model_ = core_.compile_model(pose_model_path, device_name);
    pose_infer_ = pose_model_.create_infer_request();
    
    last_update_time_ = std::chrono::steady_clock::now();
    
    // Load color profiles from settings file
    if (!loadSettings()) {
        std::cerr << "[SimpleBallTracker] Failed to load settings, using defaults" << std::endl;
        // Set up default color profiles
        color_profiles_.push_back(ColorProfile("green", cv::Scalar(45, 100, 100), cv::Scalar(75, 255, 255)));
        color_profiles_.push_back(ColorProfile("pink", cv::Scalar(140, 100, 100), cv::Scalar(175, 255, 255)));
        color_profiles_.push_back(ColorProfile("orange", cv::Scalar(5, 100, 100), cv::Scalar(20, 255, 255)));
    }
    
    // Initialize balls based on enabled color profiles
    int ball_id = 0;
    for (const auto& profile : color_profiles_) {
        if (profile.enabled && ball_id < 3) {  // Max 3 balls
            SimpleBall ball;
            ball.id = ball_id++;
            ball.color_name = profile.name;
            balls_.push_back(ball);
        }
    }
    
    std::cout << "[SimpleBallTracker] Initialized with " << balls_.size() << " balls" << std::endl;
}

bool SimpleBallTracker::loadSettings() {
    try {
        std::ifstream file(settings_file_);
        if (!file.is_open()) {
            return false;
        }
        
        json j;
        file >> j;
        
        color_profiles_.clear();
        
        // ball_settings.json format: top-level keys are color names
        for (auto& [color_name, color_data] : j.items()) {
            bool enabled = color_data.value("enabled", true);
            
            cv::Scalar min_hsv(
                color_data["min_hsv"][0],
                color_data["min_hsv"][1],
                color_data["min_hsv"][2]
            );
            
            cv::Scalar max_hsv(
                color_data["max_hsv"][0],
                color_data["max_hsv"][1],
                color_data["max_hsv"][2]
            );
            
            cv::Scalar min_hsv2(-1, -1, -1);
            cv::Scalar max_hsv2(-1, -1, -1);
            
            if (color_data.contains("min_hsv2")) {
                min_hsv2 = cv::Scalar(
                    color_data["min_hsv2"][0],
                    color_data["min_hsv2"][1],
                    color_data["min_hsv2"][2]
                );
            }
            
            if (color_data.contains("max_hsv2")) {
                max_hsv2 = cv::Scalar(
                    color_data["max_hsv2"][0],
                    color_data["max_hsv2"][1],
                    color_data["max_hsv2"][2]
                );
            }
            
            color_profiles_.push_back(ColorProfile(color_name, min_hsv, max_hsv, min_hsv2, max_hsv2, enabled));
            
            std::cout << "[SimpleBallTracker] Loaded color '" << color_name << "' enabled=" << enabled << std::endl;
        }
        
        std::cout << "[SimpleBallTracker] Loaded " << color_profiles_.size() << " color profiles" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "[SimpleBallTracker] Error loading settings: " << e.what() << std::endl;
        return false;
    }
}

void SimpleBallTracker::saveSettings() {
    try {
        json j;
        
        // Save in ball_settings.json format: top-level keys are color names
        for (const auto& profile : color_profiles_) {
            json color_data;
            color_data["enabled"] = profile.enabled;
            color_data["min_hsv"] = {profile.min_hsv[0], profile.min_hsv[1], profile.min_hsv[2]};
            color_data["max_hsv"] = {profile.max_hsv[0], profile.max_hsv[1], profile.max_hsv[2]};
            
            if (profile.min_hsv2[0] >= 0) {
                color_data["min_hsv2"] = {profile.min_hsv2[0], profile.min_hsv2[1], profile.min_hsv2[2]};
                color_data["max_hsv2"] = {profile.max_hsv2[0], profile.max_hsv2[1], profile.max_hsv2[2]};
            }
            
            j[profile.name] = color_data;
        }
        
        std::ofstream file(settings_file_);
        file << j.dump(4);  // Pretty print with 4-space indent
        
        std::cout << "[SimpleBallTracker] Settings saved to " << settings_file_ << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "[SimpleBallTracker] Error saving settings: " << e.what() << std::endl;
    }
}

bool SimpleBallTracker::updateSetting(const std::string& key, const std::string& value) {
    // Handle color profile enable/disable
    if (key.find("track_") == 0) {
        std::string color_name = key.substr(6);  // Remove "track_" prefix
        bool enable = (value == "true" || value == "1");
        
        for (auto& profile : color_profiles_) {
            if (profile.name == color_name) {
                profile.enabled = enable;
                saveSettings();
                return true;
            }
        }
    }
    
    return false;
}

float SimpleBallTracker::matchColor(const Detection& det, const ColorProfile& profile, 
                                   const cv::Mat& hsv_frame) {
    // Get detection center
    cv::Point2f center(det.box.x + det.box.width / 2.0f,
                      det.box.y + det.box.height / 2.0f);
    
    // Check bounds
    if (center.x < 0 || center.x >= hsv_frame.cols ||
        center.y < 0 || center.y >= hsv_frame.rows) {
        return 0.0f;
    }
    
    // Sample 7x7 region around center
    const int sample_radius = 7;
    int match_count = 0;
    int total_count = 0;
    
    for (int dy = -sample_radius; dy <= sample_radius; dy++) {
        for (int dx = -sample_radius; dx <= sample_radius; dx++) {
            int x = static_cast<int>(center.x) + dx;
            int y = static_cast<int>(center.y) + dy;
            
            if (x >= 0 && x < hsv_frame.cols && y >= 0 && y < hsv_frame.rows) {
                cv::Vec3b hsv = hsv_frame.at<cv::Vec3b>(y, x);
                
                // Check primary range
                bool matches = (hsv[0] >= profile.min_hsv[0] && hsv[0] <= profile.max_hsv[0] &&
                               hsv[1] >= profile.min_hsv[1] && hsv[1] <= profile.max_hsv[1] &&
                               hsv[2] >= profile.min_hsv[2] && hsv[2] <= profile.max_hsv[2]);
                
                // Check secondary range for wrap-around colors (like red)
                if (!matches && profile.min_hsv2[0] >= 0) {
                    matches = (hsv[0] >= profile.min_hsv2[0] && hsv[0] <= profile.max_hsv2[0] &&
                              hsv[1] >= profile.min_hsv2[1] && hsv[1] <= profile.max_hsv2[1] &&
                              hsv[2] >= profile.min_hsv2[2] && hsv[2] <= profile.max_hsv2[2]);
                }
                
                if (matches) match_count++;
                total_count++;
            }
        }
    }
    
    return total_count > 0 ? static_cast<float>(match_count) / total_count : 0.0f;
}

const Detection* SimpleBallTracker::findBestColorMatch(
    const std::vector<Detection>& detections,
    const ColorProfile& profile,
    const cv::Mat& hsv_frame,
    const std::set<int>& used_indices) {
    
    const Detection* best_det = nullptr;
    float best_score = MIN_COLOR_MATCH_SCORE;
    
    for (const auto& det : detections) {
        // Skip if already used
        if (used_indices.find(det.index) != used_indices.end()) {
            continue;
        }
        
        // Skip if invalid depth
        if (det.world_pos.z < MIN_DEPTH || det.world_pos.z > MAX_DEPTH) {
            continue;
        }
        
        float score = matchColor(det, profile, hsv_frame);
        
        if (score > best_score) {
            best_score = score;
            best_det = &det;
        }
    }
    
    return best_det;
}

cv::Point2f SimpleBallTracker::searchForColorBlob(const cv::Mat& hsv_frame,
                                                 const ColorProfile& profile,
                                                 const cv::Point2f& search_center,
                                                 int radius) {
    // Create mask for color
    cv::Mat mask1, mask2, mask;
    cv::inRange(hsv_frame, profile.min_hsv, profile.max_hsv, mask1);
    
    if (profile.min_hsv2[0] >= 0) {
        cv::inRange(hsv_frame, profile.min_hsv2, profile.max_hsv2, mask2);
        cv::bitwise_or(mask1, mask2, mask);
    } else {
        mask = mask1;
    }
    
    // Find contours
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    
    // Find largest contour within search radius
    double max_area = 0;
    cv::Point2f best_center(-1, -1);
    
    for (const auto& contour : contours) {
        double area = cv::contourArea(contour);
        if (area < 50.0) continue;  // Minimum blob size
        
        cv::Moments m = cv::moments(contour);
        if (m.m00 == 0) continue;
        
        cv::Point2f center(m.m10 / m.m00, m.m01 / m.m00);
        
        // Check if within search radius
        float dist = cv::norm(center - search_center);
        if (dist > radius) continue;
        
        if (area > max_area) {
            max_area = area;
            best_center = center;
        }
    }
    
    return best_center;
}

float SimpleBallTracker::getDepthAtPoint(const cv::Mat& depth_frame, const cv::Point2f& point) {
    if (point.x < 0 || point.x >= depth_frame.cols ||
        point.y < 0 || point.y >= depth_frame.rows) {
        return 0.0f;
    }
    
    // Sample 5x5 region and take median
    const int sample_size = 5;
    const int half_size = sample_size / 2;
    std::vector<float> samples;
    
    int cx = static_cast<int>(point.x);
    int cy = static_cast<int>(point.y);
    
    for (int dy = -half_size; dy <= half_size; dy++) {
        for (int dx = -half_size; dx <= half_size; dx++) {
            int x = cx + dx;
            int y = cy + dy;
            
            if (x >= 0 && x < depth_frame.cols && y >= 0 && y < depth_frame.rows) {
                uint16_t depth_mm = depth_frame.at<uint16_t>(y, x);
                float depth_m = depth_mm / 1000.0f;
                
                if (depth_m > MIN_DEPTH && depth_m < MAX_DEPTH) {
                    samples.push_back(depth_m);
                }
            }
        }
    }
    
    if (samples.empty()) return 0.0f;
    
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

cv::Point3f SimpleBallTracker::deprojectToWorld(const cv::Point2f& pixel, float depth,
                                               const CameraIntrinsics& intrinsics) {
    if (depth <= 0) {
        return cv::Point3f(0, 0, 0);
    }
    
    float x = (pixel.x - intrinsics.ppx) * depth / intrinsics.fx;
    float y = (pixel.y - intrinsics.ppy) * depth / intrinsics.fy;
    return cv::Point3f(x, y, depth);
}

uint64_t SimpleBallTracker::getCurrentTimestamp() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

bool SimpleBallTracker::isBallHeld(SimpleBall& ball, const std::vector<SimpleHand>& hands) {
    // Use weighted scoring system based on tracking settings
    float held_score = 0.0f;
    float in_air_score = 0.0f;
    
    // 1. ML Classification Evidence
    if (ball.yolo_class_id == 1) {  // ball_held class
        held_score += tracking_settings_.ml_ball_held_weight;
    } else if (ball.yolo_class_id == 0) {  // ball (in-air) class
        in_air_score += tracking_settings_.ml_ball_weight;
    }
    
    // 2. Wrist Proximity Evidence
    float min_dist = std::numeric_limits<float>::max();
    int closest_hand = -1;
    
    for (const auto& hand : hands) {
        if (!hand.is_visible) continue;
        float dist = cv::norm(ball.position - hand.wrist_pos_3d);
        if (dist < min_dist) {
            min_dist = dist;
            closest_hand = hand.id;
        }
    }
    
    if (min_dist < tracking_settings_.wrist_proximity_threshold) {
        held_score += tracking_settings_.wrist_proximity_weight;
        ball.held_by_hand_id = closest_hand;
    } else {
        ball.held_by_hand_id = -1;
    }
    
    // Decision: held if held_score is higher
    return held_score > in_air_score;
}

std::vector<BallEvent> SimpleBallTracker::detectStatesAndEvents(
    std::vector<SimpleBall>& balls,
    const std::vector<SimpleHand>& hands) {
    
    std::vector<BallEvent> events;
    
    for (auto& ball : balls) {
        bool was_held = ball.previous_is_held;
        bool now_held = isBallHeld(ball, hands);
        
        // Debounce state changes (require MIN_FRAMES_FOR_STATE_CHANGE consecutive frames)
        if (now_held != ball.is_held) {
            ball.state_change_counter++;
            if (ball.state_change_counter >= MIN_FRAMES_FOR_STATE_CHANGE) {
                ball.is_held = now_held;
                ball.state_change_counter = 0;
                
                // Generate event
                if (was_held && !now_held) {
                    events.push_back({
                        BallEvent::THROW,
                        ball.id,
                        ball.held_by_hand_id,
                        getCurrentTimestamp()
                    });
                    std::cout << "[SimpleBallTracker] THROW detected: Ball " << ball.id 
                             << " from hand " << ball.held_by_hand_id << std::endl;
                }
                else if (!was_held && now_held) {
                    events.push_back({
                        BallEvent::CATCH,
                        ball.id,
                        ball.held_by_hand_id,
                        getCurrentTimestamp()
                    });
                    std::cout << "[SimpleBallTracker] CATCH detected: Ball " << ball.id 
                             << " by hand " << ball.held_by_hand_id << std::endl;
                }
            }
        }
        else {
            ball.state_change_counter = 0;
        }
        
        // Update previous state
        ball.previous_is_held = ball.is_held;
    }
    
    return events;
}

std::pair<std::vector<SimpleBall>, std::vector<BallEvent>> SimpleBallTracker::update(
    const cv::Mat& color_frame,
    const cv::Mat& depth_frame,
    const CameraIntrinsics& intrinsics) {
    
    // Calculate dt
    auto current_time = std::chrono::steady_clock::now();
    float dt = std::chrono::duration_cast<std::chrono::duration<float>>(
        current_time - last_update_time_).count();
    last_update_time_ = current_time;
    
    // Run YOLO detection
    std::vector<Detection> yolo_detections = runBallDetection(color_frame, depth_frame, intrinsics);
    
    // Run pose estimation
    std::vector<SimpleHand> hands = runPoseEstimation(color_frame, depth_frame, intrinsics);
    hands_ = hands;  // Store for getters
    
    // Convert to HSV once
    cv::Mat hsv_frame;
    cv::cvtColor(color_frame, hsv_frame, cv::COLOR_BGR2HSV);
    
    // Track which detections are used
    std::set<int> used_detections;
    
    // For each ball, try to match with YOLO detection
    for (auto& ball : balls_) {
        // Find matching color profile
        const ColorProfile* profile = nullptr;
        for (const auto& p : color_profiles_) {
            if (p.name == ball.color_name && p.enabled) {
                profile = &p;
                break;
            }
        }
        
        if (!profile) continue;
        
        // Find best matching detection
        const Detection* best_det = findBestColorMatch(yolo_detections, *profile, 
                                                       hsv_frame, used_detections);
        
        if (best_det) {
            // Update from YOLO detection
            ball.position = best_det->world_pos;
            ball.pixel_pos = cv::Point2f(best_det->box.x + best_det->box.width / 2.0f,
                                         best_det->box.y + best_det->box.height / 2.0f);
            ball.bbox = best_det->box;
            ball.has_yolo_detection = true;
            ball.frames_without_yolo = 0;
            ball.yolo_confidence = best_det->confidence;
            ball.yolo_class_id = best_det->class_id;
            ball.color_match_score = matchColor(*best_det, *profile, hsv_frame);
            
            // Update Kalman filter
            ball.kalman.update(KalmanFilter3D::MeasurementVector(
                ball.position.x, ball.position.y, ball.position.z));
            
            used_detections.insert(best_det->index);
        }
        else {
            // No YOLO detection - use fallback
            ball.has_yolo_detection = false;
            ball.frames_without_yolo++;
            
            if (ball.frames_without_yolo < 5) {
                // Use Kalman prediction for short gaps
                ball.kalman.predict(dt);
                auto state = ball.kalman.get_state();
                ball.position = cv::Point3f(state(0), state(1), state(2));
            }
            else if (ball.frames_without_yolo < MAX_FRAMES_WITHOUT_YOLO) {
                // Check if near a hand - if so, ball is held even without YOLO detection
                // Use the undetected_near_hand_threshold for this check
                bool near_hand = false;
                for (const auto& hand : hands) {
                    if (!hand.is_visible) continue;
                    float dist = cv::norm(ball.position - hand.wrist_pos_3d);
                    if (dist < tracking_settings_.undetected_near_hand_threshold) {
                        ball.position = hand.wrist_pos_3d;
                        ball.held_by_hand_id = hand.id;
                        // Treat as ball_held class when near hand without YOLO detection
                        ball.yolo_class_id = 1;  // ball_held
                        near_hand = true;
                        break;
                    }
                }
                
                if (!near_hand) {
                    // Search for color blob
                    cv::Point2f blob_pos = searchForColorBlob(hsv_frame, *profile,
                                                              ball.pixel_pos,
                                                              COLOR_SEARCH_RADIUS);
                    if (blob_pos.x >= 0) {
                        float depth = getDepthAtPoint(depth_frame, blob_pos);
                        if (depth > MIN_DEPTH && depth < MAX_DEPTH) {
                            cv::Point3f new_position = deprojectToWorld(blob_pos, depth, intrinsics);
                            
                            // Check if color blob is near a hand
                            // Use undetected_near_hand_threshold since this is a color-tracked (undetected by YOLO) ball
                            bool blob_near_hand = false;
                            for (const auto& hand : hands) {
                                if (!hand.is_visible) continue;
                                float dist = cv::norm(new_position - hand.wrist_pos_3d);
                                if (dist < tracking_settings_.undetected_near_hand_threshold) {
                                    // Color blob detected near hand - ball is held
                                    ball.position = new_position;
                                    ball.pixel_pos = blob_pos;
                                    ball.held_by_hand_id = hand.id;
                                    ball.yolo_class_id = 1;  // ball_held
                                    blob_near_hand = true;
                                    
                                    // Update Kalman with color blob measurement
                                    ball.kalman.update(KalmanFilter3D::MeasurementVector(
                                        ball.position.x, ball.position.y, ball.position.z));
                                    break;
                                }
                            }
                            
                            if (!blob_near_hand) {
                                // Color blob found but not near hand - ball is in flight
                                ball.position = new_position;
                                ball.pixel_pos = blob_pos;
                                ball.yolo_class_id = 0;  // ball (in flight)
                                
                                // Update Kalman with color blob measurement
                                ball.kalman.update(KalmanFilter3D::MeasurementVector(
                                    ball.position.x, ball.position.y, ball.position.z));
                            }
                        }
                    }
                }
            }
        }
    }
    
    // Detect ball states and events
    std::vector<BallEvent> events = detectStatesAndEvents(balls_, hands);
    
    return {balls_, events};
}

bool SimpleBallTracker::calibrateColor(const std::string& color_name,
                                      const cv::Point& click_point,
                                      std::string& error_message) {
    // Use last raw detections and last color frame
    if (last_color_frame_.empty()) {
        error_message = "No color frame available for calibration";
        return false;
    }
    
    // Find detection containing click point
    const Detection* clicked_det = nullptr;
    for (const auto& det : last_raw_detections_) {
        if (click_point.x >= det.box.x && 
            click_point.x <= (det.box.x + det.box.width) &&
            click_point.y >= det.box.y && 
            click_point.y <= (det.box.y + det.box.height)) {
            clicked_det = &det;
            break;
        }
    }
    
    if (!clicked_det) {
        error_message = "No detection found at click location (" +
                       std::to_string(click_point.x) + "," + std::to_string(click_point.y) + ")";
        return false;
    }
    
    // Convert to HSV and sample detection box
    cv::Mat hsv_frame;
    cv::cvtColor(last_color_frame_, hsv_frame, cv::COLOR_BGR2HSV);
    
    cv::Rect roi(
        std::max(0, static_cast<int>(clicked_det->box.x)),
        std::max(0, static_cast<int>(clicked_det->box.y)),
        std::min(static_cast<int>(clicked_det->box.width),
                 hsv_frame.cols - static_cast<int>(clicked_det->box.x)),
        std::min(static_cast<int>(clicked_det->box.height),
                 hsv_frame.rows - static_cast<int>(clicked_det->box.y))
    );
    
    if (roi.width <= 0 || roi.height <= 0) {
        error_message = "Invalid detection box dimensions";
        return false;
    }
    
    cv::Mat roi_hsv = hsv_frame(roi);
    
    // Calculate mean and stddev
    cv::Scalar mean, stddev;
    cv::meanStdDev(roi_hsv, mean, stddev);
    
    // Set range as mean ± 2*stddev (with minimum tolerances)
    float hue_tol = std::max(15.0f, static_cast<float>(stddev[0]) * 2.0f);
    float sat_tol = std::max(50.0f, static_cast<float>(stddev[1]) * 2.0f);
    float val_tol = std::max(50.0f, static_cast<float>(stddev[2]) * 2.0f);
    
    cv::Scalar min_hsv(
        std::max(0.0, mean[0] - hue_tol),
        std::max(0.0, mean[1] - sat_tol),
        std::max(0.0, mean[2] - val_tol)
    );
    
    cv::Scalar max_hsv(
        std::min(180.0, mean[0] + hue_tol),
        std::min(255.0, mean[1] + sat_tol),
        std::min(255.0, mean[2] + val_tol)
    );
    
    // Find and update color profile
    for (auto& profile : color_profiles_) {
        if (profile.name == color_name) {
            profile.min_hsv = min_hsv;
            profile.max_hsv = max_hsv;
            
            saveSettings();
            
            std::cout << "[SimpleBallTracker] Calibrated color '" << color_name << "'" << std::endl;
            std::cout << "  H: [" << min_hsv[0] << ", " << max_hsv[0] << "]" << std::endl;
            std::cout << "  S: [" << min_hsv[1] << ", " << max_hsv[1] << "]" << std::endl;
            std::cout << "  V: [" << min_hsv[2] << ", " << max_hsv[2] << "]" << std::endl;
            
            return true;
        }
    }
    
    error_message = "Color profile not found: " + color_name;
    return false;
}

// Add YOLO detection and pose estimation methods

cv::Mat SimpleBallTracker::preprocess(const cv::Mat& frame, float& scale_x, float& scale_y) {
    cv::Mat resized_frame;
    cv::resize(frame, resized_frame, cv::Size(input_width_, input_height_));
    scale_x = (float)frame.cols / input_width_;
    scale_y = (float)frame.rows / input_height_;
    cv::Mat float_frame;
    resized_frame.convertTo(float_frame, CV_32F, 1.0 / 255.0);
    return cv::dnn::blobFromImage(float_frame);
}

std::vector<Detection> SimpleBallTracker::runBallDetection(
    const cv::Mat& color_frame,
    const cv::Mat& depth_frame,
    const CameraIntrinsics& intrinsics) {
    
    // Store color frame for calibration
    last_color_frame_ = color_frame.clone();
    
    // Preprocess
    float scale_x, scale_y;
    cv::Mat preprocessed = preprocess(color_frame, scale_x, scale_y);
    
    // Run inference
    ov::Tensor input_tensor(ball_model_.input().get_element_type(),
                           ball_model_.input().get_shape(),
                           preprocessed.data);
    ball_infer_.set_input_tensor(input_tensor);
    ball_infer_.infer();
    const ov::Tensor& output_tensor = ball_infer_.get_output_tensor();
    
    // Postprocess
    const float* output_data = output_tensor.data<const float>();
    const int num_channels = 4 + num_classes_;
    
    cv::Mat output_buffer(num_channels, output_tensor.get_shape()[2], CV_32F, (void*)output_data);
    cv::transpose(output_buffer, output_buffer);
    
    std::vector<cv::Rect> boxes;
    std::vector<float> confidences;
    std::vector<int> class_ids;
    std::vector<Detection> raw_detections;
    
    for (int i = 0; i < output_buffer.rows; ++i) {
        cv::Mat class_scores = output_buffer.row(i).colRange(4, num_channels);
        cv::Point class_id_point;
        double max_class_score;
        cv::minMaxLoc(class_scores, nullptr, &max_class_score, nullptr, &class_id_point);
        
        float confidence = static_cast<float>(max_class_score);
        
        if (confidence > confidence_threshold_) {
            float cx = output_buffer.at<float>(i, 0);
            float cy = output_buffer.at<float>(i, 1);
            float w = output_buffer.at<float>(i, 2);
            float h = output_buffer.at<float>(i, 3);
            
            int left = static_cast<int>((cx - 0.5 * w) * scale_x);
            int top = static_cast<int>((cy - 0.5 * h) * scale_y);
            int width = static_cast<int>(w * scale_x);
            int height = static_cast<int>(h * scale_y);
            
            boxes.push_back(cv::Rect(left, top, width, height));
            confidences.push_back(confidence);
            class_ids.push_back(class_id_point.x);
            
            // Calculate 3D position
            cv::Point2f center_pixel(left + width / 2.0f, top + height / 2.0f);
            cv::Point3f world_pos(0, 0, 0);
            
            if (center_pixel.x >= 0 && center_pixel.x < depth_frame.cols &&
                center_pixel.y >= 0 && center_pixel.y < depth_frame.rows) {
                float depth_value_m = get_filtered_depth(depth_frame, center_pixel);
                world_pos = deprojectToWorld(center_pixel, depth_value_m, intrinsics);
            }
            
            Detection det;
            det.box = cv::Rect_<float>(left, top, width, height);
            det.world_pos = world_pos;
            det.confidence = confidence;
            det.class_id = class_id_point.x;
            det.index = raw_detections.size();
            raw_detections.push_back(det);
        }
    }
    
    // Apply NMS
    std::vector<int> nms_indices;
    cv::dnn::NMSBoxes(boxes, confidences, confidence_threshold_, nms_threshold_, nms_indices);
    
    // Filter detections by NMS results
    std::vector<Detection> filtered_detections;
    for (int idx : nms_indices) {
        for (auto& det : raw_detections) {
            if (det.box.x == boxes[idx].x && det.box.y == boxes[idx].y) {
                det.index = filtered_detections.size();
                filtered_detections.push_back(det);
                break;
            }
        }
    }
    
    last_raw_detections_ = filtered_detections;
    return filtered_detections;
}

std::vector<SimpleHand> SimpleBallTracker::runPoseEstimation(
    const cv::Mat& color_frame,
    const cv::Mat& depth_frame,
    const CameraIntrinsics& intrinsics) {
    
    std::vector<SimpleHand> hands;
    
    // Preprocess
    float scale_x, scale_y;
    cv::Mat preprocessed = preprocess(color_frame, scale_x, scale_y);
    
    // Run inference
    ov::Tensor input_tensor(pose_model_.input().get_element_type(),
                           pose_model_.input().get_shape(),
                           preprocessed.data);
    pose_infer_.set_input_tensor(input_tensor);
    pose_infer_.infer();
    const ov::Tensor& output_tensor = pose_infer_.get_output_tensor();
    
    // Parse YOLO-Pose output
    const float* output_data = output_tensor.data<const float>();
    const auto& shape = output_tensor.get_shape();
    
    if (shape.size() < 3) {
        return hands;
    }
    
    const int num_channels = shape[1];
    const int num_detections = shape[2];
    
    cv::Mat output_buffer(num_channels, num_detections, CV_32F, (void*)output_data);
    cv::transpose(output_buffer, output_buffer);
    
    const float pose_confidence_threshold = 0.3f;
    const float keypoint_confidence_threshold = 0.5f;
    
    for (int i = 0; i < output_buffer.rows; ++i) {
        float cx = output_buffer.at<float>(i, 0);
        float cy = output_buffer.at<float>(i, 1);
        float w = output_buffer.at<float>(i, 2);
        float h = output_buffer.at<float>(i, 3);
        float person_confidence = output_buffer.at<float>(i, 4);
        
        if (person_confidence < pose_confidence_threshold) continue;
        
        // Extract keypoints (17 keypoints in COCO format)
        std::vector<cv::Point3f> keypoints_3d(17, cv::Point3f(0, 0, 0));
        std::vector<float> keypoint_confidences(17, 0.0f);
        
        for (int kp_idx = 0; kp_idx < 17; ++kp_idx) {
            int base_idx = 5 + kp_idx * 3;
            
            float kp_x = output_buffer.at<float>(i, base_idx + 0) * scale_x;
            float kp_y = output_buffer.at<float>(i, base_idx + 1) * scale_y;
            float kp_conf = output_buffer.at<float>(i, base_idx + 2);
            
            keypoint_confidences[kp_idx] = kp_conf;
            
            if (kp_conf > keypoint_confidence_threshold) {
                cv::Point2f pixel(kp_x, kp_y);
                
                if (pixel.x >= 0 && pixel.x < depth_frame.cols &&
                    pixel.y >= 0 && pixel.y < depth_frame.rows) {
                    
                    float depth_value_m = get_filtered_depth(depth_frame, pixel);
                    
                    if (depth_value_m > MIN_DEPTH && depth_value_m < MAX_DEPTH) {
                        keypoints_3d[kp_idx] = deprojectToWorld(pixel, depth_value_m, intrinsics);
                    }
                }
            }
        }
        
        // Create hands from wrist keypoints (9=left wrist, 10=right wrist)
        if (keypoint_confidences[9] > keypoint_confidence_threshold &&
            keypoints_3d[9].z > MIN_DEPTH && keypoints_3d[9].z < MAX_DEPTH) {
            
            SimpleHand left_hand;
            left_hand.wrist_pos_3d = keypoints_3d[9];
            left_hand.confidence = keypoint_confidences[9];
            left_hand.id = 0;
            left_hand.is_visible = true;
            left_hand.keypoints = keypoints_3d;
            hands.push_back(left_hand);
        }
        
        if (keypoint_confidences[10] > keypoint_confidence_threshold &&
            keypoints_3d[10].z > MIN_DEPTH && keypoints_3d[10].z < MAX_DEPTH) {
            
            SimpleHand right_hand;
            right_hand.wrist_pos_3d = keypoints_3d[10];
            right_hand.confidence = keypoint_confidences[10];
            right_hand.id = 1;
            right_hand.is_visible = true;
            right_hand.keypoints = keypoints_3d;
            hands.push_back(right_hand);
        }
        
        // Only process first person
        break;
    }
    
    return hands;
}

cv::Point2f SimpleBallTracker::project_3d_to_2d(const cv::Point3f& world_pos,
                                               const CameraIntrinsics& intrinsics) {
    if (world_pos.z > 0) {
        float x_2d = (world_pos.x * intrinsics.fx) / world_pos.z + intrinsics.ppx;
        float y_2d = (world_pos.y * intrinsics.fy) / world_pos.z + intrinsics.ppy;
        return cv::Point2f(x_2d, y_2d);
    }
    return cv::Point2f(-1, -1);
}
