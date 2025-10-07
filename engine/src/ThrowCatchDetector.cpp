#include "ThrowCatchDetector.hpp"
#include "DNNTracker.hpp" // For Detection struct
#include "DebugLogControl.hpp" // For g_enable_debug_log
#include <cmath>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <chrono>

// Debug logging helper - write to engine_debug.log
#define OPEN_DEBUG_LOG(var_name) \
    std::ofstream var_name; \
    if (g_enable_debug_log) var_name.open("engine_debug.log", std::ios::app)

namespace juggler {

ThrowCatchDetector::ThrowCatchDetector() : config_() {}

ThrowCatchDetector::ThrowCatchDetector(const Config& config) : config_(config) {}

std::vector<ThrowCatchDetector::DetectedEvent> ThrowCatchDetector::detectEvents(
    std::vector<PersistentTracker>& balls,
    std::vector<PersistentTracker>& hands,
    const std::vector<::Detection>& raw_detections,
    float dt
) {
    std::vector<DetectedEvent> events;
    
    // Get current timestamp
    auto now = std::chrono::system_clock::now();
    uint64_t timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()).count();
    
    // Update velocity history for all balls
    for (auto& ball : balls) {
        if (ball.status == TrackerStatus::TRACKED || ball.status == TrackerStatus::PREDICTED) {
            updateVelocityHistory(ball);
        }
    }
    
    // Check each ball against each hand for potential events
    for (auto& ball : balls) {
        if (ball.status == TrackerStatus::LOST) continue;
        
        // Find matching detection for this ball (to get ML classification)
        const ::Detection* ball_detection = findMatchingDetection(ball, raw_detections);
        
        // Update ML confidence if we have a detection
        if (ball_detection) {
            // class_id 0 = ball (in flight), class_id 1 = ball_held
            ball.ml_held_confidence = (ball_detection->class_id == 1) ?
                ball_detection->confidence : 0.0f;
        } else {
            // No YOLO detection - check if ball is near a hand (color tracking fallback)
            // If ball is in a held state and near a hand, maintain high held confidence
            if ((ball.ball_state == BallState::HELD_LEFT || ball.ball_state == BallState::HELD_RIGHT)) {
                // Check if still near the holding hand
                for (const auto& hand : hands) {
                    if (hand.status != TrackerStatus::TRACKED) continue;
                    int hand_id = hand.is_left_hand ? 0 : 1;
                    
                    // Check if this is the hand that was holding the ball
                    if ((ball.ball_state == BallState::HELD_LEFT && hand_id == 0) ||
                        (ball.ball_state == BallState::HELD_RIGHT && hand_id == 1)) {
                        
                        float distance = calculateDistance(ball.position, hand.position);
                        if (distance < config_.catch_distance * 1.5f) {  // Use 1.5x catch distance for held state
                            // Ball is still near the hand - maintain held confidence
                            ball.ml_held_confidence = 0.9f;  // High confidence for color-tracked held ball
                        } else {
                            // Ball moved away from hand
                            ball.ml_held_confidence = 0.0f;
                        }
                        break;
                    }
                }
            }
        }
        
        // Increment frames in current state
        ball.frames_in_current_state++;
        
        // Check against each hand
        for (auto& hand : hands) {
            if (hand.status != TrackerStatus::TRACKED) continue;
            
            // Determine which hand this is (left=0, right=1)
            int hand_id = hand.is_left_hand ? 0 : 1;
            
            // DEBUG: Print hand info
            DEBUG_LOG_WRITE({
                OPEN_DEBUG_LOG(debug_log);
                debug_log << "[THROW_CATCH_DEBUG] Checking hand: logical_id=" << hand.logical_id
                          << ", is_left_hand=" << hand.is_left_hand
                          << ", hand_id=" << hand_id
                          << " (" << (hand_id == 0 ? "LEFT" : "RIGHT") << ")" << std::endl;
            });
            
            // --- CATCH DETECTION ---
            if (ball.ball_state == BallState::IN_FLIGHT || 
                ball.ball_state == BallState::TRANSITIONING) {
                
                EventEvidence catch_evidence = evaluateCatchEvidence(
                    ball, hand, ball_detection, dt);
                
                if (catch_evidence.total_score >= config_.catch_threshold) {
                    // Transition to TRANSITIONING state first
                    if (ball.ball_state == BallState::IN_FLIGHT) {
                        ball.ball_state = BallState::TRANSITIONING;
                        ball.frames_in_current_state = 0;
                    }
                    // If we've been transitioning long enough, confirm the catch
                    else if (meetsTemporalRequirement(ball, config_.min_frames_for_event)) {
                        BallState new_state = (hand_id == 0) ? BallState::HELD_LEFT : BallState::HELD_RIGHT;
                        
                        DEBUG_LOG_WRITE({
                            OPEN_DEBUG_LOG(debug_log);
                            debug_log << "\n[CATCH] Ball " << ball.logical_id
                                      << " caught by hand_id=" << hand_id << " (" << (hand_id == 0 ? "LEFT" : "RIGHT") << ")"
                                      << " | hand.logical_id=" << hand.logical_id
                                      << " | hand.is_left_hand=" << hand.is_left_hand
                                      << " | Setting holding_hand_id=" << hand.logical_id
                                      << " | ball_state=" << (new_state == BallState::HELD_LEFT ? "HELD_LEFT" : "HELD_RIGHT")
                                      << std::endl;
                        });
                        
                        ball.ball_state = new_state;
                        ball.frames_in_current_state = 0;
                        ball.holding_hand_id = hand.logical_id;  // Use holding_hand_id instead of parent_id
                        ball.is_in_freefall = false;
                        
                        // Create catch event
                        ball.update_from_kf();
                        cv::Point3f pos(ball.position.x(), ball.position.y(), ball.position.z());
                        DetectedEvent event(DetectedEvent::CATCH, ball.logical_id,
                                          hand_id, timestamp_us, pos);
                        event.evidence = catch_evidence;
                        events.push_back(event);
                        
                        // Removed duplicate log
                    }
                }
                // Reset transitioning if we've been in it too long without confirming
                else if (ball.ball_state == BallState::TRANSITIONING && 
                         ball.frames_in_current_state > config_.max_transition_frames) {
                    ball.ball_state = BallState::IN_FLIGHT;
                    ball.frames_in_current_state = 0;
                }
            }
            
            // --- THROW DETECTION ---
            // Only check for throws from the hand that's currently holding the ball
            bool should_check_throw = (ball.ball_state == BallState::HELD_LEFT && hand_id == 0) ||
                                      (ball.ball_state == BallState::HELD_RIGHT && hand_id == 1);
            
            // Removed verbose per-frame log
            
            if (should_check_throw) {
                EventEvidence throw_evidence = evaluateThrowEvidence(ball, hand, dt);
                
                if (throw_evidence.total_score >= config_.throw_threshold) {
                    // Transition to TRANSITIONING state first
                    if (ball.ball_state != BallState::TRANSITIONING) {
                        // parent_id was already set during catch - don't overwrite it!
                        DEBUG_LOG_WRITE({
                            OPEN_DEBUG_LOG(debug_log);
                            debug_log << "[THROW_CATCH_DEBUG] THROW START: Ball " << ball.logical_id
                                      << " starting throw from hand_id=" << hand_id
                                      << " (hand.logical_id=" << hand.logical_id
                                      << ", is_left_hand=" << hand.is_left_hand << ")"
                                      << " -> Keeping parent_id=" << ball.parent_id << " (set during catch)" << std::endl;
                        });
                        
                        // DON'T overwrite parent_id here - it was set correctly during catch!
                        // ball.parent_id = hand.logical_id;  // BUG: This was overwriting the catch parent_id
                        ball.ball_state = BallState::TRANSITIONING;
                        ball.frames_in_current_state = 0;
                    }
                    // If we've been transitioning long enough, confirm the throw
                    else if (meetsTemporalRequirement(ball, config_.min_frames_for_event)) {
                        ball.ball_state = BallState::IN_FLIGHT;
                        ball.frames_in_current_state = 0;
                        ball.is_in_freefall = true;
                        
                        // Create throw event using the hand stored in parent_id
                        ball.update_from_kf();
                        cv::Point3f pos(ball.position.x(), ball.position.y(), ball.position.z());
                        
                        // Determine which hand threw based on holding_hand_id (set during catch)
                        int throwing_hand_id = hand_id;  // Default to current hand
                        DEBUG_LOG_WRITE({
                            OPEN_DEBUG_LOG(debug_log);
                            debug_log << "\n[THROW] Ball " << ball.logical_id
                                      << " | holding_hand_id=" << ball.holding_hand_id
                                      << " | Looking up hand..." << std::endl;
                            
                            for (const auto& h : hands) {
                                debug_log << "  Checking hand: logical_id=" << h.logical_id
                                          << " | is_left_hand=" << h.is_left_hand << std::endl;
                                if (h.logical_id == ball.holding_hand_id) {
                                    throwing_hand_id = h.is_left_hand ? 0 : 1;
                                    debug_log << "  ✓ MATCH! hand.logical_id=" << h.logical_id
                                              << " | hand.is_left_hand=" << h.is_left_hand
                                              << " | throwing_hand_id=" << throwing_hand_id
                                              << " (" << (throwing_hand_id == 0 ? "LEFT" : "RIGHT") << ")"
                                              << std::endl;
                                    break;
                                }
                            }
                        });
                        
                        DetectedEvent event(DetectedEvent::THROW, ball.logical_id,
                                          throwing_hand_id, timestamp_us, pos);
                        event.evidence = throw_evidence;
                        events.push_back(event);
                        
                        DEBUG_LOG_WRITE({
                            OPEN_DEBUG_LOG(debug_log);
                            debug_log << "[THROW RESULT] Ball " << ball.logical_id
                                      << " thrown by " << (throwing_hand_id == 0 ? "LEFT" : "RIGHT")
                                      << " hand" << std::endl;
                        });
                        
                        // Clear holding_hand_id after throw is confirmed
                        ball.holding_hand_id = -1;
                    }
                }
            }
        }
    }
    
    return events;
}

ThrowCatchDetector::EventEvidence ThrowCatchDetector::evaluateCatchEvidence(
    const PersistentTracker& ball,
    const PersistentTracker& hand,
    const ::Detection* detection,
    float dt
) {
    EventEvidence evidence;
    
    // Ensure both trackers have updated positions
    const_cast<PersistentTracker&>(ball).update_from_kf();
    const_cast<PersistentTracker&>(hand).update_from_kf();
    
    // 1. ML Classification Evidence
    if (detection && detection->class_id == 1) { // ball_held
        evidence.ml_confidence = std::min(1.0f, detection->confidence / config_.ml_confidence_min);
    } else {
        evidence.ml_confidence = 0.0f;
    }
    
    // 2. Proximity Evidence
    float distance = calculateDistance(ball.position, hand.position);
    if (distance < config_.catch_distance) {
        evidence.proximity_score = 1.0f - (distance / config_.catch_distance);
    } else {
        evidence.proximity_score = 0.0f;
    }
    
    // 3. Kinematic Evidence (velocity drop)
    float current_velocity = calculateVelocityMagnitude(ball.velocity);
    Eigen::Vector3d avg_velocity = getAverageVelocity(ball);
    float avg_velocity_mag = calculateVelocityMagnitude(avg_velocity);
    
    if (avg_velocity_mag > 0.1f) { // Only if ball was moving
        float velocity_drop = 1.0f - (current_velocity / avg_velocity_mag);
        if (velocity_drop >= config_.catch_velocity_drop) {
            evidence.kinematic_score = std::min(1.0f, velocity_drop / config_.catch_velocity_drop);
        }
    }
    
    // 4. Relative Velocity Evidence
    Eigen::Vector3d relative_velocity = ball.velocity - hand.velocity;
    float relative_velocity_mag = calculateVelocityMagnitude(relative_velocity);
    
    if (relative_velocity_mag < config_.relative_velocity_catch) {
        evidence.relative_velocity_score = 1.0f - 
            (relative_velocity_mag / config_.relative_velocity_catch);
    }
    
    // Calculate weighted total score
    evidence.total_score = 
        evidence.ml_confidence * config_.ml_weight +
        evidence.proximity_score * config_.proximity_weight +
        evidence.kinematic_score * config_.kinematic_weight +
        evidence.relative_velocity_score * config_.relative_velocity_weight;
    
    return evidence;
}

ThrowCatchDetector::EventEvidence ThrowCatchDetector::evaluateThrowEvidence(
    const PersistentTracker& ball,
    const PersistentTracker& hand,
    float dt
) {
    EventEvidence evidence;
    
    // Ensure both trackers have updated positions
    const_cast<PersistentTracker&>(ball).update_from_kf();
    const_cast<PersistentTracker&>(hand).update_from_kf();
    
    // 1. ML Classification Evidence (ball should be classified as "ball", not "ball_held")
    if (ball.ml_held_confidence < (1.0f - config_.ml_confidence_min)) {
        evidence.ml_confidence = 1.0f - ball.ml_held_confidence;
    } else {
        evidence.ml_confidence = 0.0f;
    }
    
    // 2. Proximity Evidence (ball should be moving away from hand)
    float distance = calculateDistance(ball.position, hand.position);
    if (distance > config_.throw_distance) {
        evidence.proximity_score = std::min(1.0f, 
            (distance - config_.throw_distance) / config_.throw_distance);
    } else {
        evidence.proximity_score = 0.0f;
    }
    
    // 3. Kinematic Evidence (velocity increase)
    float current_velocity = calculateVelocityMagnitude(ball.velocity);
    if (current_velocity > config_.throw_velocity_min) {
        evidence.kinematic_score = std::min(1.0f, 
            current_velocity / (config_.throw_velocity_min * 2.0f));
    }
    
    // 4. Relative Velocity Evidence (ball and hand should have different velocities)
    Eigen::Vector3d relative_velocity = ball.velocity - hand.velocity;
    float relative_velocity_mag = calculateVelocityMagnitude(relative_velocity);
    
    if (relative_velocity_mag > config_.relative_velocity_throw) {
        evidence.relative_velocity_score = std::min(1.0f, 
            relative_velocity_mag / (config_.relative_velocity_throw * 2.0f));
    }
    
    // Calculate weighted total score
    evidence.total_score = 
        evidence.ml_confidence * config_.ml_weight +
        evidence.proximity_score * config_.proximity_weight +
        evidence.kinematic_score * config_.kinematic_weight +
        evidence.relative_velocity_score * config_.relative_velocity_weight;
    
    return evidence;
}

bool ThrowCatchDetector::meetsTemporalRequirement(
    const PersistentTracker& ball, 
    int required_frames
) const {
    return ball.frames_in_current_state >= required_frames;
}

float ThrowCatchDetector::calculateDistance(
    const Eigen::Vector3d& p1, 
    const Eigen::Vector3d& p2
) const {
    return (p1 - p2).norm();
}

float ThrowCatchDetector::calculateVelocityMagnitude(
    const Eigen::Vector3d& velocity
) const {
    return velocity.norm();
}

const ::Detection* ThrowCatchDetector::findMatchingDetection(
    const PersistentTracker& tracker,
    const std::vector<::Detection>& detections
) const {
    const ::Detection* best_match = nullptr;
    float min_distance = 0.3f; // 30cm max distance for matching
    
    for (const auto& det : detections) {
        // Only consider ball detections (class 0 or 1)
        if (det.class_id > 1) continue;
        
        // Calculate distance between tracker position and detection position
        Eigen::Vector3d det_pos(det.world_pos.x, det.world_pos.y, det.world_pos.z);
        float distance = calculateDistance(tracker.position, det_pos);
        
        if (distance < min_distance) {
            min_distance = distance;
            best_match = &det;
        }
    }
    
    return best_match;
}

void ThrowCatchDetector::updateVelocityHistory(PersistentTracker& ball) {
    // Add current velocity to history
    ball.velocity_history.push_back(ball.velocity);
    
    // Keep only the last MAX_VELOCITY_HISTORY frames
    if (ball.velocity_history.size() > PersistentTracker::MAX_VELOCITY_HISTORY) {
        ball.velocity_history.erase(ball.velocity_history.begin());
    }
}

Eigen::Vector3d ThrowCatchDetector::getAverageVelocity(
    const PersistentTracker& ball
) const {
    if (ball.velocity_history.empty()) {
        return ball.velocity;
    }
    
    Eigen::Vector3d sum(0, 0, 0);
    for (const auto& vel : ball.velocity_history) {
        sum += vel;
    }
    
    return sum / static_cast<double>(ball.velocity_history.size());
}

} // namespace juggler