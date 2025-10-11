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
 * Captures frame-by-frame information about trajectory-based tracking,
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
        float distance;  // Distance between ball and hand when event occurred
        float threshold;  // Threshold that was compared against
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
                   const std::vector<SimpleBall>& balls,
                   const std::vector<SimpleHand>& hands) {
        if (!is_active_) return;
        
        for (const auto& event : events) {
            EventRecord record;
            record.frame_number = frame_number_;
            record.event_type = (event.type == BallEvent::THROW) ? "THROW" : "CATCH";
            record.ball_id = event.ball_id;
            record.hand_id = event.hand_id;
            record.hand_side = (event.hand_id == 0) ? "LEFT" : "RIGHT";
            
            // Find ball color and calculate distance
            record.ball_color = "unknown";
            record.distance = 0.0f;
            record.threshold = 0.0f;
            
            for (const auto& ball : balls) {
                if (ball.id == event.ball_id) {
                    record.ball_color = ball.color_name;
                    
                    // Calculate distance between ball and hand
                    for (const auto& hand : hands) {
                        if (hand.id == event.hand_id) {
                            float dx = ball.position.x - hand.wrist_pos_3d.x;
                            float dy = ball.position.y - hand.wrist_pos_3d.y;
                            float dz = ball.position.z - hand.wrist_pos_3d.z;
                            record.distance = std::sqrt(dx*dx + dy*dy + dz*dz);
                            break;
                        }
                    }
                    break;
                }
            }
            
            // Set threshold based on event type
            // These values should match the thresholds in SimpleBallTracker
            if (event.type == BallEvent::CATCH) {
                record.threshold = 0.15f;  // catch_distance_threshold
            } else {  // THROW
                record.threshold = 0.20f;  // throw_distance_threshold
            }
            
            event_records_.push_back(record);
        }
    }
    
    // Log a single frame's tracking data
    void logFrame(const std::vector<SimpleBall>& balls,
                  const std::vector<SimpleHand>& hands,
                  const CameraIntrinsics& intrinsics [[maybe_unused]]) {
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
            log_file_ << "    Has YOLO detection: " << (ball.has_yolo_detection ? "YES" : "NO") << "\n";
            log_file_ << "    YOLO confidence: " << std::setprecision(3) << ball.yolo_confidence << "\n";
            log_file_ << "    YOLO class: " << (ball.yolo_class_id == 0 ? "ball" : "ball_held") << "\n";
            log_file_ << "    Color match score: " << std::setprecision(3) << ball.color_match_score << "\n";
            log_file_ << "    Tracking reason: " << ball.tracking_reason << "\n\n";
            
            // Trajectory-based tracking info
            log_file_ << "  TRAJECTORY TRACKING:\n";
            const auto& trajectory = ball.trajectory;
            log_file_ << "    Verified points: " << trajectory.verified_point_count << "\n";
            log_file_ << "    Trajectory confidence: " << std::setprecision(3)
                     << trajectory.trajectory_confidence << "\n";
            log_file_ << "    Search radius: " << std::setprecision(4)
                     << trajectory.search_radius_m << " m\n";
            log_file_ << "    Has enough data for prediction: "
                     << (trajectory.verified_point_count >= 3 ? "YES" : "NO") << "\n";
            
            if (!trajectory.points.empty()) {
                log_file_ << "    Trajectory points (oldest to newest):\n";
                for (size_t i = 0; i < trajectory.points.size(); ++i) {
                    const auto& point = trajectory.points[i];
                    log_file_ << "      [" << i << "] Position: ("
                             << std::fixed << std::setprecision(4)
                             << point.position.x << ", "
                             << point.position.y << ", "
                             << point.position.z << ") m, Verified: "
                             << (point.verified ? "YES" : "NO") << ", Confidence: "
                             << std::setprecision(3) << point.confidence << "\n";
                }
                
                // Log velocity from trajectory
                if (trajectory.verified_point_count >= 2) {
                    cv::Point3f velocity = trajectory.initial_velocity;
                    log_file_ << "    Initial velocity: ("
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
                log_file_ << "    (No trajectory points available)\n";
            }
            log_file_ << "\n";
            
            // Prediction details
            if (trajectory.verified_point_count >= 3 && !trajectory.predicted_path.empty()) {
                log_file_ << "  PREDICTION DETAILS:\n";
                log_file_ << "    Predicted path points: " << trajectory.predicted_path.size() << "\n";
                log_file_ << "    Gravity: " << std::setprecision(4) << trajectory.gravity << " m/s²\n";
                log_file_ << "    Prediction valid: " << (trajectory.prediction_valid ? "YES" : "NO") << "\n";
                
                // Show first predicted position
                if (!trajectory.predicted_path.empty()) {
                    const auto& pred_pos = trajectory.predicted_path[0];
                    log_file_ << "    Next predicted position: ("
                             << std::fixed << std::setprecision(4)
                             << pred_pos.x << ", "
                             << pred_pos.y << ", "
                             << pred_pos.z << ") m\n";
                    log_file_ << "    Search radius: " << std::setprecision(4)
                             << trajectory.search_radius_m << " m\n";
                    
                    // Calculate prediction error if we have current position
                    if (ball.has_yolo_detection && pred_pos.z > 0) {
                        float dx = ball.position.x - pred_pos.x;
                        float dy = ball.position.y - pred_pos.y;
                        float dz = ball.position.z - pred_pos.z;
                        float error = std::sqrt(dx*dx + dy*dy + dz*dz);
                        
                        log_file_ << "    Prediction error: " << std::setprecision(4) << error << " m\n";
                        log_file_ << "    Error components: dx=" << dx << ", dy=" << dy << ", dz=" << dz << " m\n";
                    }
                }
            } else {
                log_file_ << "  PREDICTION: Not enough trajectory data (need 3+ verified points)\n";
            }
            log_file_ << "\n";
        }
        
        log_file_ << "\n";
        log_file_.flush();  // Ensure data is written immediately
        frame_number_++;
    }
    
    // Log detailed velocity estimation and prediction calculations
    void logVelocityEstimation(int ball_id, const std::string& ball_color,
                               const std::vector<TrajectoryPoint>& points,
                               const cv::Point3f& estimated_velocity,
                               const cv::Point3f& current_position,
                               float gravity) {
        if (!is_active_) return;
        
        log_file_ << "  *** VELOCITY ESTIMATION DEBUG (Ball " << ball_id << " - " << ball_color << ") ***\n";
        log_file_ << "    Number of trajectory points: " << points.size() << "\n";
        log_file_ << "    Gravity: " << std::fixed << std::setprecision(4) << gravity << " m/s²\n\n";
        
        if (points.size() < 2) {
            log_file_ << "    ERROR: Not enough points for velocity estimation (need at least 2)\n\n";
            return;
        }
        
        // Show the points being used
        log_file_ << "    Trajectory points (for velocity calculation):\n";
        for (size_t i = 0; i < points.size(); ++i) {
            const auto& p = points[i];
            log_file_ << "      Point[" << i << "]: pos=("
                     << std::fixed << std::setprecision(4)
                     << p.position.x << ", " << p.position.y << ", " << p.position.z << ") m"
                     << " | timestamp=" << p.timestamp << " µs"
                     << " | verified=" << (p.verified ? "YES" : "NO") << "\n";
        }
        log_file_ << "\n";
        
        // Show two-point calculation if only 2 points
        if (points.size() == 2) {
            const auto& p1 = points[0];
            const auto& p2 = points[1];
            
            double dt = (p2.timestamp - p1.timestamp) / 1000000.0;
            cv::Point3f dp = p2.position - p1.position;
            
            log_file_ << "    TWO-POINT METHOD:\n";
            log_file_ << "      p1 = (" << p1.position.x << ", " << p1.position.y << ", " << p1.position.z << ") at t=" << p1.timestamp << " µs\n";
            log_file_ << "      p2 = (" << p2.position.x << ", " << p2.position.y << ", " << p2.position.z << ") at t=" << p2.timestamp << " µs\n";
            log_file_ << "      Δt = " << std::setprecision(6) << dt << " seconds\n";
            log_file_ << "      Δp = (" << std::setprecision(4) << dp.x << ", " << dp.y << ", " << dp.z << ") m\n";
            log_file_ << "      v = Δp / Δt = (" << (dp.x/dt) << ", " << (dp.y/dt) << ", " << (dp.z/dt) << ") m/s\n\n";
        } else {
            log_file_ << "    LEAST-SQUARES METHOD (using last " << std::min(10, (int)points.size()) << " points):\n";
            log_file_ << "      Setting t=0 at LAST point (current time)\n";
            log_file_ << "      All previous points have negative time values\n\n";
        }
        
        log_file_ << "    ESTIMATED VELOCITY (at current position):\n";
        log_file_ << "      v_current = (" << std::fixed << std::setprecision(4)
                 << estimated_velocity.x << ", "
                 << estimated_velocity.y << ", "
                 << estimated_velocity.z << ") m/s\n";
        
        float speed = std::sqrt(estimated_velocity.x * estimated_velocity.x +
                               estimated_velocity.y * estimated_velocity.y +
                               estimated_velocity.z * estimated_velocity.z);
        log_file_ << "      Speed magnitude: " << std::setprecision(4) << speed << " m/s\n";
        log_file_ << "      Horizontal speed: " << std::sqrt(estimated_velocity.x * estimated_velocity.x +
                                                             estimated_velocity.y * estimated_velocity.y) << " m/s\n";
        log_file_ << "      Vertical speed: " << estimated_velocity.z << " m/s "
                 << (estimated_velocity.z > 0 ? "(upward)" : "(downward)") << "\n\n";
        
        log_file_ << "    CURRENT POSITION (prediction starting point):\n";
        log_file_ << "      p_current = (" << std::fixed << std::setprecision(4)
                 << current_position.x << ", "
                 << current_position.y << ", "
                 << current_position.z << ") m\n\n";
        
        // Show prediction equation
        log_file_ << "    PREDICTION EQUATION:\n";
        log_file_ << "      For time t seconds into the future:\n";
        log_file_ << "      x(t) = " << current_position.x << " + " << estimated_velocity.x << " * t\n";
        log_file_ << "      y(t) = " << current_position.y << " + " << estimated_velocity.y << " * t\n";
        log_file_ << "      z(t) = " << current_position.z << " + " << estimated_velocity.z << " * t - 0.5 * " << gravity << " * t²\n\n";
        
        // Show first few predicted points
        log_file_ << "    SAMPLE PREDICTIONS (first 5 time steps at 0.033s intervals):\n";
        for (int i = 0; i < 5; ++i) {
            float t = i * 0.033f;
            float x = current_position.x + estimated_velocity.x * t;
            float y = current_position.y + estimated_velocity.y * t;
            float z = current_position.z + estimated_velocity.z * t - 0.5f * gravity * t * t;
            
            log_file_ << "      t=" << std::fixed << std::setprecision(3) << t << "s: ("
                     << std::setprecision(4) << x << ", " << y << ", " << z << ") m\n";
        }
        log_file_ << "\n";
        log_file_.flush();
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
                events_summary << "Frame | Event Type | Ball (Color) | Hand | Distance < Threshold\n";
                events_summary << "------|------------|--------------|------|---------------------\n";
                
                for (const auto& record : event_records_) {
                    events_summary << std::setw(5) << record.frame_number << " | "
                                  << std::setw(10) << std::left << record.event_type << " | "
                                  << "Ball " << record.ball_id << " (" << std::setw(6) << record.ball_color << ") | "
                                  << std::setw(4) << record.hand_side << " | "
                                  << std::fixed << std::setprecision(3) << record.distance << "m < "
                                  << std::setprecision(3) << record.threshold << "m\n";
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