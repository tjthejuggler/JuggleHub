#pragma once

#include "KalmanFilter3D.hpp"
#include <string>
#include <opencv2/core/types.hpp> // For cv::Rect_

// Enum to represent the current state of a persistent tracker
enum class TrackerStatus {
    TRACKED,    // The object is currently being seen and updated by a live detection.
    PREDICTED,  // The object is not seen; its position is based on Kalman filter prediction.
    OCCLUDED,   // The object is presumed to be hidden by another object (e.g., a ball in a hand).
    LOST        // The object has been unseen for an extended period.
};

// A structure to hold the complete state of a single logical object (ball or hand)
// that persists across frames, regardless of temporary occlusions.
struct PersistentTracker {
    // --- Core Identity ---
    int logical_id;                 // A fixed ID for this tracker (e.g., 0, 1, 2 for balls).
    int last_seen_bytetrack_id = -1;  // The most recent raw ByteTrack ID associated with this tracker.
    std::string class_name;           // The object's class (e.g., "ball", "hand").

    // --- State & Position ---
    TrackerStatus status = TrackerStatus::LOST; // Current state of the tracker.
    KalmanFilter3D kf;                          // The Kalman filter for this object, providing smoothed and predicted states.
    Eigen::Vector3d position;                   // The current best estimate of 3D position (from KF).
    Eigen::Vector3d velocity;                   // The current best estimate of 3D velocity (from KF).
    cv::Rect_<float> box_2d;                    // The last known 2D bounding box.

    // --- Occlusion & Lifetime Management ---
    int frames_since_seen = 0;      // Counter for how many frames the object has been unobserved.
    int parent_id = -1;             // If OCCLUDED, this stores the logical_id of the object it's hidden by (e.g., a hand).

    // --- Heuristics for Hand Tracking ---
    bool is_left_hand = false;      // Flag to identify the left hand persistently.

    // --- Physics State ---
    bool is_in_freefall = false;    // True if gravity should be applied.

    // Constructor
    PersistentTracker(int id, std::string name) : logical_id(id), class_name(std::move(name)) {}

    // Method to update the tracker's state from its Kalman Filter
    void update_from_kf() {
        auto state = kf.get_state();
        // State is [x, y, z, vx, vy, vz]
        position = Eigen::Vector3d(state(0), state(1), state(2));
        velocity = Eigen::Vector3d(state(3), state(4), state(5));
    }
};