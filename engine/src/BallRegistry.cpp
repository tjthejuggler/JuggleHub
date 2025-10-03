#include "BallRegistry.hpp"
#include "DebugLog.hpp"
#include <fstream>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cmath>

namespace juggler {

// ===== ColorSample Serialization =====

json ColorSample::toJson() const {
    json j;
    j["mean_hsv"] = {mean_hsv[0], mean_hsv[1], mean_hsv[2]};
    j["std_hsv"] = {std_hsv[0], std_hsv[1], std_hsv[2]};
    j["min_hsv"] = {min_hsv[0], min_hsv[1], min_hsv[2]};
    j["max_hsv"] = {max_hsv[0], max_hsv[1], max_hsv[2]};
    j["weight"] = weight;
    j["lighting_condition"] = lighting_condition;
    j["sample_location"] = {sample_location.x, sample_location.y};
    j["timestamp"] = timestamp;
    return j;
}

ColorSample ColorSample::fromJson(const json& j) {
    ColorSample sample;
    sample.mean_hsv = cv::Scalar(j["mean_hsv"][0], j["mean_hsv"][1], j["mean_hsv"][2]);
    sample.std_hsv = cv::Scalar(j["std_hsv"][0], j["std_hsv"][1], j["std_hsv"][2]);
    sample.min_hsv = cv::Scalar(j["min_hsv"][0], j["min_hsv"][1], j["min_hsv"][2]);
    sample.max_hsv = cv::Scalar(j["max_hsv"][0], j["max_hsv"][1], j["max_hsv"][2]);
    sample.weight = j["weight"];
    sample.lighting_condition = j["lighting_condition"];
    sample.sample_location = cv::Point2f(j["sample_location"][0], j["sample_location"][1]);
    sample.timestamp = j["timestamp"];
    return sample;
}

// ===== ActiveBall Serialization =====

json ActiveBall::toJson() const {
    json j;
    j["id"] = id;
    j["display_name"] = display_name;
    j["aggregate_min_hsv"] = {aggregate_min_hsv[0], aggregate_min_hsv[1], aggregate_min_hsv[2]};
    j["aggregate_max_hsv"] = {aggregate_max_hsv[0], aggregate_max_hsv[1], aggregate_max_hsv[2]};
    j["aggregate_min_hsv2"] = {aggregate_min_hsv2[0], aggregate_min_hsv2[1], aggregate_min_hsv2[2]};
    j["aggregate_max_hsv2"] = {aggregate_max_hsv2[0], aggregate_max_hsv2[1], aggregate_max_hsv2[2]};
    j["is_active"] = is_active;
    j["logical_tracker_id"] = logical_tracker_id;
    j["min_confidence_threshold"] = min_confidence_threshold;
    j["expected_diameter_cm"] = expected_diameter_cm;
    j["frames_tracked"] = frames_tracked;
    j["frames_lost"] = frames_lost;
    j["created_timestamp"] = created_timestamp;
    j["last_seen_timestamp"] = last_seen_timestamp;
    
    j["color_samples"] = json::array();
    for (const auto& sample : color_samples) {
        j["color_samples"].push_back(sample.toJson());
    }
    
    return j;
}

ActiveBall ActiveBall::fromJson(const json& j) {
    ActiveBall ball;
    ball.id = j["id"];
    ball.display_name = j["display_name"];
    ball.aggregate_min_hsv = cv::Scalar(j["aggregate_min_hsv"][0], j["aggregate_min_hsv"][1], j["aggregate_min_hsv"][2]);
    ball.aggregate_max_hsv = cv::Scalar(j["aggregate_max_hsv"][0], j["aggregate_max_hsv"][1], j["aggregate_max_hsv"][2]);
    ball.aggregate_min_hsv2 = cv::Scalar(j["aggregate_min_hsv2"][0], j["aggregate_min_hsv2"][1], j["aggregate_min_hsv2"][2]);
    ball.aggregate_max_hsv2 = cv::Scalar(j["aggregate_max_hsv2"][0], j["aggregate_max_hsv2"][1], j["aggregate_max_hsv2"][2]);
    ball.is_active = j["is_active"];
    ball.logical_tracker_id = j["logical_tracker_id"];
    ball.min_confidence_threshold = j["min_confidence_threshold"];
    ball.expected_diameter_cm = j["expected_diameter_cm"];
    ball.frames_tracked = j["frames_tracked"];
    ball.frames_lost = j["frames_lost"];
    ball.created_timestamp = j["created_timestamp"];
    ball.last_seen_timestamp = j["last_seen_timestamp"];
    
    for (const auto& sample_json : j["color_samples"]) {
        ball.color_samples.push_back(ColorSample::fromJson(sample_json));
    }
    
    return ball;
}

// ===== BallRegistry Implementation =====

BallRegistry::BallRegistry()
    : max_active_balls_(5), next_ball_number_(1) {
    INFO_LOG("BallRegistry: Initialized with max_active_balls=", max_active_balls_);
}

std::string BallRegistry::createBall(const std::string& display_name) {
    std::string ball_id = generateBallId();
    
    ActiveBall ball;
    ball.id = ball_id;
    ball.display_name = display_name.empty() ? ("Ball " + std::to_string(next_ball_number_ - 1)) : display_name;
    ball.created_timestamp = getCurrentTimestamp();
    
    registered_balls_.push_back(ball);
    
    INFO_LOG("BallRegistry: Created ball '", ball.display_name, "' with ID '", ball_id, "'");
    return ball_id;
}

bool BallRegistry::deleteBall(const std::string& ball_id) {
    auto it = std::find_if(registered_balls_.begin(), registered_balls_.end(),
                          [&ball_id](const ActiveBall& b) { return b.id == ball_id; });
    
    if (it == registered_balls_.end()) {
        WARN_LOG("BallRegistry: Cannot delete ball '", ball_id, "' - not found");
        return false;
    }
    
    std::string display_name = it->display_name;
    registered_balls_.erase(it);
    
    INFO_LOG("BallRegistry: Deleted ball '", display_name, "' (ID: ", ball_id, ")");
    return true;
}

bool BallRegistry::activateBall(const std::string& ball_id) {
    ActiveBall* ball = getBall(ball_id);
    if (!ball) {
        WARN_LOG("BallRegistry: Cannot activate ball '", ball_id, "' - not found");
        return false;
    }
    
    if (ball->is_active) {
        DEBUG_LOG("BallRegistry: Ball '", ball->display_name, "' is already active");
        return true;
    }
    
    // Check if we've reached the maximum number of active balls
    int active_count = getActiveCount();
    if (active_count >= max_active_balls_) {
        WARN_LOG("BallRegistry: Cannot activate ball '", ball->display_name, 
                 "' - maximum active balls (", max_active_balls_, ") reached");
        return false;
    }
    
    // Check if ball has at least one color sample
    if (ball->color_samples.empty()) {
        WARN_LOG("BallRegistry: Cannot activate ball '", ball->display_name,
                 "' - no color samples. Please calibrate first.");
        return false;
    }
    
    // Find an available tracker ID
    std::vector<int> used_ids;
    for (const auto& b : registered_balls_) {
        if (b.is_active && b.logical_tracker_id >= 0) {
            used_ids.push_back(b.logical_tracker_id);
        }
    }
    std::sort(used_ids.begin(), used_ids.end());
    
    int tracker_id = 0;
    for (int id : used_ids) {
        if (id == tracker_id) {
            tracker_id++;
        } else {
            break;
        }
    }
    
    ball->is_active = true;
    ball->logical_tracker_id = tracker_id;
    
    INFO_LOG("BallRegistry: Activated ball '", ball->display_name, 
             "' with tracker ID ", tracker_id);
    return true;
}

bool BallRegistry::deactivateBall(const std::string& ball_id) {
    ActiveBall* ball = getBall(ball_id);
    if (!ball) {
        WARN_LOG("BallRegistry: Cannot deactivate ball '", ball_id, "' - not found");
        return false;
    }
    
    if (!ball->is_active) {
        DEBUG_LOG("BallRegistry: Ball '", ball->display_name, "' is already inactive");
        return true;
    }
    
    ball->is_active = false;
    ball->logical_tracker_id = -1;
    
    INFO_LOG("BallRegistry: Deactivated ball '", ball->display_name, "'");
    return true;
}

bool BallRegistry::addColorSample(const std::string& ball_id,
                                  const cv::Mat& hsv_frame,
                                  const cv::Point& click_point,
                                  const std::string& lighting_condition,
                                  int sample_radius) {
    ActiveBall* ball = getBall(ball_id);
    if (!ball) {
        WARN_LOG("BallRegistry: Cannot add color sample to ball '", ball_id, "' - not found");
        return false;
    }
    
    // Validate click point
    if (click_point.x < 0 || click_point.x >= hsv_frame.cols ||
        click_point.y < 0 || click_point.y >= hsv_frame.rows) {
        ERROR_LOG("BallRegistry: Invalid click point (", click_point.x, ",", click_point.y, ")");
        return false;
    }
    
    // Define sample region
    int x_min = std::max(0, click_point.x - sample_radius);
    int y_min = std::max(0, click_point.y - sample_radius);
    int x_max = std::min(hsv_frame.cols - 1, click_point.x + sample_radius);
    int y_max = std::min(hsv_frame.rows - 1, click_point.y + sample_radius);
    
    cv::Rect sample_rect(x_min, y_min, x_max - x_min + 1, y_max - y_min + 1);
    cv::Mat sample_region = hsv_frame(sample_rect);
    
    // Compute mean and standard deviation
    cv::Scalar mean, stddev;
    cv::meanStdDev(sample_region, mean, stddev);
    
    // Create color sample
    ColorSample sample;
    sample.mean_hsv = mean;
    sample.std_hsv = stddev;
    
    // Set HSV range with tolerance based on standard deviation
    // Use larger tolerance for hue (it's more variable), smaller for saturation/value
    float h_tolerance = std::max(8.0f, static_cast<float>(stddev[0]) * 2.0f);
    float s_tolerance = std::max(40.0f, static_cast<float>(stddev[1]) * 2.0f);
    float v_tolerance = std::max(40.0f, static_cast<float>(stddev[2]) * 2.0f);
    
    sample.min_hsv = cv::Scalar(
        std::max(0.0, mean[0] - h_tolerance),
        std::max(0.0, mean[1] - s_tolerance),
        std::max(0.0, mean[2] - v_tolerance)
    );
    sample.max_hsv = cv::Scalar(
        std::min(180.0, mean[0] + h_tolerance),
        std::min(255.0, mean[1] + s_tolerance),
        std::min(255.0, mean[2] + v_tolerance)
    );
    
    sample.weight = 1.0f;  // Default weight, can be adjusted later
    sample.lighting_condition = lighting_condition;
    sample.sample_location = cv::Point2f(click_point.x, click_point.y);
    sample.timestamp = getCurrentTimestamp();
    
    ball->color_samples.push_back(sample);
    
    // Recompute aggregate ranges
    recomputeAggregateRanges(ball_id);
    
    INFO_LOG("BallRegistry: Added color sample to ball '", ball->display_name, 
             "' (H:", static_cast<int>(mean[0]), " S:", static_cast<int>(mean[1]), 
             " V:", static_cast<int>(mean[2]), ") lighting:", lighting_condition,
             " - Total samples: ", ball->color_samples.size());
    
    return true;
}

bool BallRegistry::removeColorSample(const std::string& ball_id, int sample_index) {
    ActiveBall* ball = getBall(ball_id);
    if (!ball) {
        WARN_LOG("BallRegistry: Cannot remove color sample from ball '", ball_id, "' - not found");
        return false;
    }
    
    if (sample_index < 0 || sample_index >= static_cast<int>(ball->color_samples.size())) {
        ERROR_LOG("BallRegistry: Invalid sample index ", sample_index, 
                  " for ball '", ball->display_name, "'");
        return false;
    }
    
    ball->color_samples.erase(ball->color_samples.begin() + sample_index);
    
    // Recompute aggregate ranges if samples remain
    if (!ball->color_samples.empty()) {
        recomputeAggregateRanges(ball_id);
    }
    
    INFO_LOG("BallRegistry: Removed color sample ", sample_index, 
             " from ball '", ball->display_name, "' - Remaining samples: ", 
             ball->color_samples.size());
    
    return true;
}

void BallRegistry::recomputeAggregateRanges(const std::string& ball_id) {
    ActiveBall* ball = getBall(ball_id);
    if (!ball || ball->color_samples.empty()) {
        return;
    }
    
    computeAggregateRange(ball->color_samples,
                         ball->aggregate_min_hsv, ball->aggregate_max_hsv,
                         ball->aggregate_min_hsv2, ball->aggregate_max_hsv2);
    
    DEBUG_LOG("BallRegistry: Recomputed aggregate ranges for ball '", ball->display_name, "'");
}

std::vector<ActiveBall*> BallRegistry::getActiveBalls() {
    std::vector<ActiveBall*> active_balls;
    for (auto& ball : registered_balls_) {
        if (ball.is_active) {
            active_balls.push_back(&ball);
        }
    }
    return active_balls;
}

std::vector<const ActiveBall*> BallRegistry::getActiveBalls() const {
    std::vector<const ActiveBall*> active_balls;
    for (const auto& ball : registered_balls_) {
        if (ball.is_active) {
            active_balls.push_back(&ball);
        }
    }
    return active_balls;
}

std::vector<ActiveBall*> BallRegistry::getAllBalls() {
    std::vector<ActiveBall*> all_balls;
    for (auto& ball : registered_balls_) {
        all_balls.push_back(&ball);
    }
    return all_balls;
}

std::vector<const ActiveBall*> BallRegistry::getAllBalls() const {
    std::vector<const ActiveBall*> all_balls;
    for (const auto& ball : registered_balls_) {
        all_balls.push_back(&ball);
    }
    return all_balls;
}

ActiveBall* BallRegistry::getBall(const std::string& ball_id) {
    auto it = std::find_if(registered_balls_.begin(), registered_balls_.end(),
                          [&ball_id](const ActiveBall& b) { return b.id == ball_id; });
    return (it != registered_balls_.end()) ? &(*it) : nullptr;
}

const ActiveBall* BallRegistry::getBall(const std::string& ball_id) const {
    auto it = std::find_if(registered_balls_.begin(), registered_balls_.end(),
                          [&ball_id](const ActiveBall& b) { return b.id == ball_id; });
    return (it != registered_balls_.end()) ? &(*it) : nullptr;
}

ActiveBall* BallRegistry::getBallByTrackerId(int tracker_id) {
    auto it = std::find_if(registered_balls_.begin(), registered_balls_.end(),
                          [tracker_id](const ActiveBall& b) { 
                              return b.is_active && b.logical_tracker_id == tracker_id; 
                          });
    return (it != registered_balls_.end()) ? &(*it) : nullptr;
}

void BallRegistry::setMaxActiveBalls(int max) {
    if (max < 1 || max > 10) {
        WARN_LOG("BallRegistry: Invalid max_active_balls value ", max, " (must be 1-10)");
        return;
    }
    max_active_balls_ = max;
    INFO_LOG("BallRegistry: Set max_active_balls to ", max);
}

int BallRegistry::getActiveCount() const {
    int count = 0;
    for (const auto& ball : registered_balls_) {
        if (ball.is_active) count++;
    }
    return count;
}

bool BallRegistry::saveToFile(const std::string& filepath) {
    try {
        json j = toJson();
        std::ofstream file(filepath);
        if (!file.is_open()) {
            ERROR_LOG("BallRegistry: Failed to open file for writing: ", filepath);
            return false;
        }
        file << j.dump(4);  // Pretty print with 4-space indent
        INFO_LOG("BallRegistry: Saved ", registered_balls_.size(), " balls to ", filepath);
        return true;
    } catch (const std::exception& e) {
        ERROR_LOG("BallRegistry: Error saving to file: ", e.what());
        return false;
    }
}

bool BallRegistry::loadFromFile(const std::string& filepath) {
    try {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            WARN_LOG("BallRegistry: File not found: ", filepath, " - starting with empty registry");
            return false;
        }
        
        json j;
        file >> j;
        fromJson(j);
        
        INFO_LOG("BallRegistry: Loaded ", registered_balls_.size(), " balls from ", filepath);
        return true;
    } catch (const std::exception& e) {
        ERROR_LOG("BallRegistry: Error loading from file: ", e.what());
        return false;
    }
}

json BallRegistry::toJson() const {
    json j;
    j["max_active_balls"] = max_active_balls_;
    j["next_ball_number"] = next_ball_number_;
    j["balls"] = json::array();
    
    for (const auto& ball : registered_balls_) {
        j["balls"].push_back(ball.toJson());
    }
    
    return j;
}

void BallRegistry::fromJson(const json& j) {
    registered_balls_.clear();
    
    if (j.contains("max_active_balls")) {
        max_active_balls_ = j["max_active_balls"];
    }
    if (j.contains("next_ball_number")) {
        next_ball_number_ = j["next_ball_number"];
    }
    
    if (j.contains("balls")) {
        for (const auto& ball_json : j["balls"]) {
            registered_balls_.push_back(ActiveBall::fromJson(ball_json));
        }
    }
}

std::string BallRegistry::generateBallId() {
    std::ostringstream oss;
    oss << "ball_" << std::setfill('0') << std::setw(3) << next_ball_number_;
    next_ball_number_++;
    return oss.str();
}

void BallRegistry::computeAggregateRange(const std::vector<ColorSample>& samples,
                                        cv::Scalar& min_hsv, cv::Scalar& max_hsv,
                                        cv::Scalar& min_hsv2, cv::Scalar& max_hsv2) {
    if (samples.empty()) {
        min_hsv = cv::Scalar(0, 0, 0);
        max_hsv = cv::Scalar(180, 255, 255);
        min_hsv2 = cv::Scalar(-1, -1, -1);
        max_hsv2 = cv::Scalar(-1, -1, -1);
        return;
    }
    
    // Check if this is a wrap-around color (red/pink)
    bool is_wrap_around = isWrapAroundColor(samples);
    
    if (is_wrap_around) {
        // Handle wrap-around colors (red/pink that cross 0°/180° boundary)
        // Split into two ranges: [0, low_max] and [high_min, 180]
        float low_max = 0.0f;
        float high_min = 180.0f;
        float s_min = 255.0f, s_max = 0.0f;
        float v_min = 255.0f, v_max = 0.0f;
        
        for (const auto& sample : samples) {
            float h = sample.mean_hsv[0];
            
            if (h <= 15.0f) {
                // Low range (0-15)
                low_max = std::max(low_max, static_cast<float>(sample.max_hsv[0]));
            } else if (h >= 165.0f) {
                // High range (165-180)
                high_min = std::min(high_min, static_cast<float>(sample.min_hsv[0]));
            }
            
            // S and V are combined across all samples
            s_min = std::min(s_min, static_cast<float>(sample.min_hsv[1]));
            s_max = std::max(s_max, static_cast<float>(sample.max_hsv[1]));
            v_min = std::min(v_min, static_cast<float>(sample.min_hsv[2]));
            v_max = std::max(v_max, static_cast<float>(sample.max_hsv[2]));
        }
        
        // Range 1: high hue values (165-180)
        min_hsv = cv::Scalar(high_min, s_min, v_min);
        max_hsv = cv::Scalar(180, s_max, v_max);
        
        // Range 2: low hue values (0-15)
        min_hsv2 = cv::Scalar(0, s_min, v_min);
        max_hsv2 = cv::Scalar(low_max, s_max, v_max);
        
    } else {
        // Normal single range - take union of all sample ranges
        float h_min = 180.0f, h_max = 0.0f;
        float s_min = 255.0f, s_max = 0.0f;
        float v_min = 255.0f, v_max = 0.0f;
        
        for (const auto& sample : samples) {
            h_min = std::min(h_min, static_cast<float>(sample.min_hsv[0]));
            h_max = std::max(h_max, static_cast<float>(sample.max_hsv[0]));
            s_min = std::min(s_min, static_cast<float>(sample.min_hsv[1]));
            s_max = std::max(s_max, static_cast<float>(sample.max_hsv[1]));
            v_min = std::min(v_min, static_cast<float>(sample.min_hsv[2]));
            v_max = std::max(v_max, static_cast<float>(sample.max_hsv[2]));
        }
        
        min_hsv = cv::Scalar(h_min, s_min, v_min);
        max_hsv = cv::Scalar(h_max, s_max, v_max);
        min_hsv2 = cv::Scalar(-1, -1, -1);
        max_hsv2 = cv::Scalar(-1, -1, -1);
    }
}

bool BallRegistry::isWrapAroundColor(const std::vector<ColorSample>& samples) const {
    if (samples.empty()) return false;
    
    // Check if any samples are in the red/pink range (near 0° or 180°)
    bool has_low_hue = false;   // H < 15
    bool has_high_hue = false;  // H > 165
    
    for (const auto& sample : samples) {
        float h = sample.mean_hsv[0];
        if (h <= 15.0f) has_low_hue = true;
        if (h >= 165.0f) has_high_hue = true;
    }
    
    // It's a wrap-around color if we have samples on both sides of the boundary
    return has_low_hue && has_high_hue;
}

int64_t BallRegistry::getCurrentTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

} // namespace juggler