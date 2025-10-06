#pragma once

#include <opencv2/opencv.hpp>
#include <deque>
#include <chrono>

/**
 * Simple color-based prediction system
 * Tracks recent color detection positions and predicts next location
 * Separate modes for "in air" (with gravity) and "held" (no gravity)
 */
class ColorBasedPredictor {
public:
    struct PredictionSettings {
        int history_frames = 5;           // Number of frames to use for prediction
        float prediction_radius_m = 0.15f; // Radius of prediction circle in meters (15cm default)
        float gravity = 9.81f;             // Gravity constant (m/s²)
        
        PredictionSettings() = default;
    };
    
    struct DetectionPoint {
        cv::Point3f position;
        std::chrono::steady_clock::time_point timestamp;
        
        DetectionPoint(const cv::Point3f& pos, std::chrono::steady_clock::time_point t)
            : position(pos), timestamp(t) {}
    };
    
    ColorBasedPredictor() = default;
    
    // Add a new color detection
    void addDetection(const cv::Point3f& position) {
        auto now = std::chrono::steady_clock::now();
        history_.push_back(DetectionPoint(position, now));
        
        // Keep only recent history
        while (history_.size() > static_cast<size_t>(settings_.history_frames)) {
            history_.pop_front();
        }
    }
    
    // Clear history (e.g., when ball is lost)
    void clear() {
        history_.clear();
    }
    
    // Get predicted position based on recent detections
    // dt: time delta to predict ahead (typically frame delta time)
    // is_in_air: whether to apply gravity to the prediction
    // Returns: predicted position, or (0,0,0) if insufficient data
    cv::Point3f getPredictedPosition(float dt, bool is_in_air) const {
        if (history_.size() < 2) {
            // Need at least 2 points to calculate velocity
            return cv::Point3f(0, 0, 0);
        }
        
        // Calculate average velocity from recent detections
        cv::Point3f velocity(0, 0, 0);
        int velocity_samples = 0;
        
        for (size_t i = 1; i < history_.size(); ++i) {
            const auto& prev = history_[i - 1];
            const auto& curr = history_[i];
            
            float dt = std::chrono::duration_cast<std::chrono::duration<float>>(
                curr.timestamp - prev.timestamp).count();
            
            if (dt > 0.001f) {  // Avoid division by very small numbers
                cv::Point3f vel = (curr.position - prev.position) / dt;
                velocity += vel;
                velocity_samples++;
            }
        }
        
        if (velocity_samples == 0) {
            return history_.back().position;  // Return last known position
        }
        
        velocity /= static_cast<float>(velocity_samples);
        
        // Get most recent position
        cv::Point3f current_pos = history_.back().position;
        
        // Predict forward in time using provided dt
        cv::Point3f predicted = current_pos + velocity * dt;
        
        // Apply gravity if in air
        if (is_in_air) {
            // y += 0.5 * g * t²
            predicted.y += 0.5f * settings_.gravity * dt * dt;
        }
        
        return predicted;
    }
    
    // Get prediction radius in meters
    float getPredictionRadius() const {
        return settings_.prediction_radius_m;
    }
    
    // Check if we have enough data for prediction
    bool hasEnoughData() const {
        return history_.size() >= 2;
    }
    
    // Get/set settings
    const PredictionSettings& getSettings() const { return settings_; }
    void setSettings(const PredictionSettings& settings) { settings_ = settings; }
    
    // Get history size
    size_t getHistorySize() const { return history_.size(); }

private:
    std::deque<DetectionPoint> history_;
    PredictionSettings settings_;
};