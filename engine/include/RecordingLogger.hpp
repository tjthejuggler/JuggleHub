#pragma once

#include <fstream>
#include <string>
#include <vector>
#include <chrono>
#include <iomanip>
#include <sstream>
#include "SimpleBallTracker.hpp"

/**
 * RecordingLogger - Creates detailed log files during recording sessions
 * Captures frame-by-frame information about Kalman filter history,
 * color tracker positions, and prediction details for debugging
 */
class RecordingLogger {
public:
    RecordingLogger() : is_active_(false), frame_number_(0) {}
    
    ~RecordingLogger() {
        close();
    }
    
    // Start a new recording log session
    bool start(const std::string& recording_dir) {
        if (is_active_) {
            close();
        }
        
        std::string log_path = recording_dir + "/recording.log";
        log_file_.open(log_path, std::ios::out | std::ios::trunc);
        
        if (!log_file_.is_open()) {
            return false;
        }
        
        is_active_ = true;
        frame_number_ = 0;
        
        // Write header
        log_file_ << "========================================\n";
        log_file_ << "JUGGLEHUB RECORDING LOG\n";
        log_file_ << "========================================\n";
        log_file_ << "Timestamp: " << getCurrentTimestamp() << "\n";
        log_file_ << "Recording Directory: " << recording_dir << "\n";
        log_file_ << "========================================\n\n";
        
        return true;
    }
    
    // Log a single frame's tracking data
    void logFrame(const std::vector<SimpleBall>& balls, 
                  const std::vector<SimpleHand>& hands,
                  const CameraIntrinsics& intrinsics) {
        if (!is_active_) return;
        
        log_file_ << "================================================================================\n";
        log_file_ << "FRAME " << frame_number_ << "\n";
        log_file_ << "================================================================================\n";
        log_file_ << "Timestamp: " << getCurrentTimestamp() << "\n";
        log_file_ << "Number of tracked balls: " << balls.size() << "\n";
        log_file_ << "Number of detected hands: " << hands.size() << "\n\n";
        
        // Log hand positions first
        if (!hands.empty()) {
            log_file_ << "--- HAND POSITIONS ---\n";
            for (const auto& hand : hands) {
                log_file_ << "  Hand " << hand.id << " (" << (hand.id == 0 ? "LEFT" : "RIGHT") << "):\n";
                log_file_ << "    Wrist 3D: (" << std::fixed << std::setprecision(4)
                         << hand.wrist_pos_3d.x << ", " 
                         << hand.wrist_pos_3d.y << ", " 
                         << hand.wrist_pos_3d.z << ") m\n";
                log_file_ << "    Visible: " << (hand.is_visible ? "YES" : "NO") << "\n";
                log_file_ << "    Confidence: " << std::setprecision(3) << hand.confidence << "\n";
            }
            log_file_ << "\n";
        }
        
        // Log each ball's detailed information
        for (const auto& ball : balls) {
            log_file_ << "--- BALL " << ball.id << " (" << ball.color_name << ") ---\n";
            
            // Current state
            log_file_ << "  Current State:\n";
            log_file_ << "    Position 3D: (" << std::fixed << std::setprecision(4)
                     << ball.position.x << ", " 
                     << ball.position.y << ", " 
                     << ball.position.z << ") m\n";
            log_file_ << "    Position 2D (pixel): (" << std::setprecision(2)
                     << ball.pixel_pos.x << ", " 
                     << ball.pixel_pos.y << ")\n";
            log_file_ << "    BBox: [" << std::setprecision(1)
                     << ball.bbox.x << ", " << ball.bbox.y << ", "
                     << ball.bbox.width << ", " << ball.bbox.height << "]\n";
            log_file_ << "    State: " << (ball.is_held ? "HELD" : "IN_FLIGHT") << "\n";
            log_file_ << "    Held by hand: " << (ball.held_by_hand_id >= 0 ? 
                     std::to_string(ball.held_by_hand_id) : "NONE") << "\n";
            log_file_ << "    Distance to nearest wrist: " << std::setprecision(4)
                     << ball.distance_to_nearest_wrist << " m\n";
            log_file_ << "    Has YOLO detection: " << (ball.has_yolo_detection ? "YES" : "NO") << "\n";
            log_file_ << "    Frames without YOLO: " << ball.frames_without_yolo << "\n";
            log_file_ << "    YOLO confidence: " << std::setprecision(3) << ball.yolo_confidence << "\n";
            log_file_ << "    YOLO class: " << (ball.yolo_class_id == 0 ? "ball" : "ball_held") << "\n";
            log_file_ << "    Color match score: " << std::setprecision(3) << ball.color_match_score << "\n";
            log_file_ << "    Tracking reason: " << ball.tracking_reason << "\n\n";
            
            // Color-based predictor history (THE KEY INFORMATION YOU REQUESTED)
            log_file_ << "  COLOR PREDICTOR HISTORY:\n";
            const auto& history = ball.color_predictor.getHistory();
            log_file_ << "    History size: " << history.size() << " frames\n";
            log_file_ << "    Has enough data for prediction: " 
                     << (ball.color_predictor.hasEnoughData() ? "YES" : "NO") << "\n";
            
            if (!history.empty()) {
                log_file_ << "    History entries (oldest to newest):\n";
                for (size_t i = 0; i < history.size(); ++i) {
                    const auto& entry = history[i];
                    auto time_since_epoch = entry.timestamp.time_since_epoch();
                    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(time_since_epoch).count();
                    
                    log_file_ << "      [" << i << "] Position: ("
                             << std::fixed << std::setprecision(4)
                             << entry.position.x << ", "
                             << entry.position.y << ", "
                             << entry.position.z << ") m, Timestamp: " << ms << " ms\n";
                }
                
                // Calculate and log velocity from history
                if (history.size() >= 2) {
                    cv::Point3f velocity = ball.color_predictor.getVelocity();
                    log_file_ << "    Calculated velocity: ("
                             << std::fixed << std::setprecision(4)
                             << velocity.x << ", "
                             << velocity.y << ", "
                             << velocity.z << ") m/s\n";
                    
                    float speed = std::sqrt(velocity.x * velocity.x + 
                                          velocity.y * velocity.y + 
                                          velocity.z * velocity.z);
                    log_file_ << "    Speed magnitude: " << std::setprecision(4) << speed << " m/s\n";
                }
            } else {
                log_file_ << "    (No history available)\n";
            }
            log_file_ << "\n";
            
            // Prediction details
            if (ball.color_predictor.hasEnoughData()) {
                log_file_ << "  PREDICTION DETAILS:\n";
                float prediction_dt = 1.0f / 60.0f;  // Assume 60 FPS
                cv::Point3f pred_pos = ball.color_predictor.getPredictedPosition(
                    prediction_dt, !ball.is_held);
                
                log_file_ << "    Prediction time delta: " << std::setprecision(6) 
                         << prediction_dt << " s\n";
                log_file_ << "    Gravity applied: " << (ball.is_held ? "NO" : "YES") << "\n";
                log_file_ << "    Predicted position: ("
                         << std::fixed << std::setprecision(4)
                         << pred_pos.x << ", "
                         << pred_pos.y << ", "
                         << pred_pos.z << ") m\n";
                log_file_ << "    Prediction radius: " << std::setprecision(4)
                         << ball.color_predictor.getPredictionRadius() << " m\n";
                
                // Calculate prediction error if we have current position
                if (ball.has_yolo_detection && pred_pos.z > 0) {
                    float dx = ball.position.x - pred_pos.x;
                    float dy = ball.position.y - pred_pos.y;
                    float dz = ball.position.z - pred_pos.z;
                    float error = std::sqrt(dx*dx + dy*dy + dz*dz);
                    
                    log_file_ << "    Prediction error: " << std::setprecision(4) << error << " m\n";
                    log_file_ << "    Error components: dx=" << dx << ", dy=" << dy << ", dz=" << dz << " m\n";
                }
            } else {
                log_file_ << "  PREDICTION: Not enough history data\n";
            }
            log_file_ << "\n";
            
            // Legacy Kalman filter info (if used)
            log_file_ << "  LEGACY KALMAN FILTER:\n";
            log_file_ << "    (Note: Kalman filter is only used as fallback when YOLO fails)\n";
            log_file_ << "    Frames without YOLO: " << ball.frames_without_yolo << "\n";
            if (ball.frames_without_yolo > 0) {
                log_file_ << "    Kalman fallback is ACTIVE\n";
            } else {
                log_file_ << "    Kalman fallback is INACTIVE (using YOLO)\n";
            }
            log_file_ << "\n";
            
            // Detection evaluations (scoring details)
            if (!ball.detection_evaluations.empty()) {
                log_file_ << "  DETECTION EVALUATIONS:\n";
                log_file_ << "    Number of detections evaluated: " << ball.detection_evaluations.size() << "\n";
                for (const auto& eval : ball.detection_evaluations) {
                    log_file_ << "    Detection #" << eval.detection_index << ":\n";
                    log_file_ << "      Result: " << eval.result << "\n";
                    log_file_ << "      Passed filters: " << (eval.passed_filters ? "YES" : "NO") << "\n";
                    if (eval.passed_filters) {
                        log_file_ << "      Total score: " << std::setprecision(4) << eval.total_score << "\n";
                        log_file_ << "      Class score: " << eval.class_score << "\n";
                        log_file_ << "      Confidence score: " << eval.confidence_score << "\n";
                        log_file_ << "      Color score: " << eval.color_score << "\n";
                        log_file_ << "      Kalman score: " << eval.kalman_score << "\n";
                    }
                    if (eval.distance_to_prediction >= 0) {
                        log_file_ << "      Distance to prediction: " 
                                 << std::setprecision(4) << eval.distance_to_prediction << " m\n";
                    }
                }
            }
            log_file_ << "\n";
        }
        
        log_file_ << "\n";
        log_file_.flush();  // Ensure data is written immediately
        frame_number_++;
    }
    
    // Close the log file
    void close() {
        if (is_active_ && log_file_.is_open()) {
            log_file_ << "========================================\n";
            log_file_ << "END OF RECORDING LOG\n";
            log_file_ << "Total frames logged: " << frame_number_ << "\n";
            log_file_ << "========================================\n";
            log_file_.close();
        }
        is_active_ = false;
    }
    
    bool isActive() const { return is_active_; }
    
private:
    std::ofstream log_file_;
    bool is_active_;
    int frame_number_;
    
    std::string getCurrentTimestamp() {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::tm tm_buf;
        localtime_r(&time_t, &tm_buf);
        
        std::ostringstream oss;
        oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
        return oss.str();
    }
};