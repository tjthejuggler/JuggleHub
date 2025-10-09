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
    
    // Structure to store event information
    struct EventRecord {
        int frame_number;
        std::string event_type;  // "THROW" or "CATCH"
        int ball_id;
        std::string ball_color;
        int hand_id;
        std::string hand_side;  // "LEFT" or "RIGHT"
    };
    
    // Start a new recording log session
    bool start(const std::string& recording_dir) {
        if (is_active_) {
            close();
        }
        
        recording_dir_ = recording_dir;
        std::string log_path = recording_dir + "/recording.log";
        log_file_.open(log_path, std::ios::out | std::ios::trunc);
        
        if (!log_file_.is_open()) {
            return false;
        }
        
        is_active_ = true;
        frame_number_ = 0;
        event_records_.clear();
        
        // Write header (events summary will be written at close())
        log_file_ << "========================================\n";
        log_file_ << "JUGGLEHUB RECORDING LOG\n";
        log_file_ << "========================================\n";
        log_file_ << "Timestamp: " << getCurrentTimestamp() << "\n";
        log_file_ << "Recording Directory: " << recording_dir << "\n";
        log_file_ << "========================================\n\n";
        
        // Placeholder for events summary (will be filled in at close())
        log_file_ << "[EVENTS SUMMARY WILL BE INSERTED HERE]\n\n";
        
        return true;
    }
    
    // Log throw/catch events for this frame
    void logEvents(const std::vector<BallEvent>& events,
                   const std::vector<SimpleBall>& balls) {
        if (!is_active_) return;
        
        for (const auto& event : events) {
            EventRecord record;
            record.frame_number = frame_number_;
            record.event_type = (event.type == BallEvent::THROW) ? "THROW" : "CATCH";
            record.ball_id = event.ball_id;
            record.hand_id = event.hand_id;
            record.hand_side = (event.hand_id == 0) ? "LEFT" : "RIGHT";
            
            // Find ball color
            record.ball_color = "unknown";
            for (const auto& ball : balls) {
                if (ball.id == event.ball_id) {
                    record.ball_color = ball.color_name;
                    break;
                }
            }
            
            event_records_.push_back(record);
        }
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
        }
        
        log_file_ << "\n";
        log_file_.flush();  // Ensure data is written immediately
        frame_number_++;
    }
    
    // Close the log file and write events summary at the top
    void close() {
        if (is_active_ && log_file_.is_open()) {
            log_file_ << "========================================\n";
            log_file_ << "END OF RECORDING LOG\n";
            log_file_ << "Total frames logged: " << frame_number_ << "\n";
            log_file_ << "========================================\n";
            log_file_.close();
            
            // Now rewrite the file with events summary at the top
            if (!event_records_.empty()) {
                writeEventsToTop();
            }
        }
        is_active_ = false;
    }
    
    bool isActive() const { return is_active_; }
    
private:
    std::ofstream log_file_;
    bool is_active_;
    int frame_number_;
    std::string recording_dir_;
    std::vector<EventRecord> event_records_;
    
    std::string getCurrentTimestamp() {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::tm tm_buf;
        localtime_r(&time_t, &tm_buf);
        
        std::ostringstream oss;
        oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
        return oss.str();
    }
    
    void writeEventsToTop() {
        // Read the entire log file
        std::string log_path = recording_dir_ + "/recording.log";
        std::ifstream read_file(log_path);
        if (!read_file.is_open()) return;
        
        std::stringstream buffer;
        buffer << read_file.rdbuf();
        std::string content = buffer.str();
        read_file.close();
        
        // Find the placeholder and replace it with events summary
        std::string placeholder = "[EVENTS SUMMARY WILL BE INSERTED HERE]";
        size_t pos = content.find(placeholder);
        
        if (pos != std::string::npos) {
            std::ostringstream events_summary;
            events_summary << "========================================\n";
            events_summary << "THROW/CATCH EVENTS SUMMARY\n";
            events_summary << "========================================\n";
            events_summary << "Total events: " << event_records_.size() << "\n\n";
            
            if (!event_records_.empty()) {
                events_summary << "Frame | Event Type | Ball (Color) | Hand\n";
                events_summary << "------|------------|--------------|------\n";
                
                for (const auto& record : event_records_) {
                    events_summary << std::setw(5) << record.frame_number << " | "
                                  << std::setw(10) << std::left << record.event_type << " | "
                                  << "Ball " << record.ball_id << " (" << std::setw(6) << record.ball_color << ") | "
                                  << record.hand_side << "\n";
                }
            } else {
                events_summary << "No throw/catch events recorded.\n";
            }
            
            events_summary << "========================================\n";
            
            // Replace placeholder with events summary
            content.replace(pos, placeholder.length(), events_summary.str());
            
            // Write back to file
            std::ofstream write_file(log_path, std::ios::out | std::ios::trunc);
            if (write_file.is_open()) {
                write_file << content;
                write_file.close();
            }
        }
    }
};