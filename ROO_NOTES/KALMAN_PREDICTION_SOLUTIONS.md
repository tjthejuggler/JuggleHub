Root Cause Analysis
Looking at the 3D matching code in DNNTracker.cpp:223-442, I can identify several potential causes:

1. Greedy Assignment is Not Optimal (Lines 82-129)
The current greedy algorithm finds the minimum cost pair iteratively, but this is not globally optimal:

// Greedy: repeatedly find minimum cost unassigned pair
for (int iter = 0; iter < std::min(n_trackers, n_detections); ++iter) {
    float min_cost = max_cost_threshold;
    int best_tracker = -1;
    int best_detection = -1;
    
    for (int i = 0; i < n_trackers; ++i) {
        if (tracker_assigned[i]) continue;
        for (int j = 0; j < n_detections; ++j) {
            if (detection_assigned[j]) continue;
            if (cost_matrix[i][j] < min_cost) {
                min_cost = cost_matrix[i][j];
                best_tracker = i;
                best_detection = j;
            }
        }
    }
    // ... assign best pair
}

Problem: This can make locally optimal choices that prevent globally optimal assignments.

Example Scenario:

Ball A is 10cm from Detection 1, 25cm from Detection 2
Ball B is 15cm from Detection 1, 20cm from Detection 2
Greedy picks: A→1 (10cm), then B→2 (20cm) = Total 30cm
Optimal would be: A→2 (25cm), B→1 (15cm) = Total 40cm... wait, greedy wins here
But with 3+ balls, greedy can fail badly
2. No Temporal Consistency Bonus (Lines 351-366)
The cost matrix uses ONLY 3D distance:

for (size_t i = 0; i < ball_trackers_list.size(); ++i) {
    for (size_t j = 0; j < valid_detections.size(); ++j) {
        float dist = calculate_distance(predicted_pos, valid_detections[j]->world_pos);
        cost_matrix[i][j] = dist;  // ← ONLY distance, no history
    }
}

Problem: No penalty for switching IDs. If two balls cross paths, the algorithm might swap their IDs because it only looks at current distance.

3. 30cm Threshold May Be Too Large (Line 369)
const float MAX_ASSOCIATION_DISTANCE = 0.30f;  // 30cm

Problem: At 60 FPS with fast juggling, balls can move significantly between frames. A 30cm threshold might allow ambiguous matches when balls are close together.

4. No Velocity Matching
The matching doesn't consider velocity consistency. A ball moving right shouldn't suddenly match with a detection moving left.

Solutions (In Priority Order)
🔥 SOLUTION 1: Implement Hungarian Algorithm (HIGH IMPACT)
Replace the greedy assignment with the Hungarian algorithm for globally optimal matching:

// Use Hungarian algorithm instead of greedy
#include <vector>
#include <limits>

// Hungarian algorithm implementation (or use a library)
std::vector<std::pair<int, int>> hungarian_assignment(
    const std::vector<std::vector<float>>& cost_matrix,
    float max_cost_threshold
) {
    // Implement Hungarian algorithm
    // Returns optimal assignments
}

Impact: Prevents ID swaps caused by suboptimal greedy choices.

🔥 SOLUTION 2: Add Temporal Consistency Bonus (HIGH IMPACT)
Modify the cost function to penalize ID changes:

// Build cost matrix with temporal consistency
for (size_t i = 0; i < ball_trackers_list.size(); ++i) {
    auto* tracker = ball_trackers_list[i];
    
    for (size_t j = 0; j < valid_detections.size(); ++j) {
        float spatial_dist = calculate_distance(predicted_pos, valid_detections[j]->world_pos);
        
        // Add temporal consistency bonus
        float temporal_bonus = 0.0f;
        if (tracker->last_matched_detection_index == j) {
            temporal_bonus = -0.05f;  // 5cm bonus for same detection
        }
        
        cost_matrix[i][j] = spatial_dist + temporal_bonus;
    }
}

Impact: Encourages stable ID assignments across frames.

🔥 SOLUTION 3: Add Velocity Consistency Check (MEDIUM IMPACT)
Include velocity in the cost function:

// Get tracker velocity
Eigen::Vector3f tracker_vel = tracker->kf.get_state().tail<3>();

// Estimate detection velocity (if we have history)
Eigen::Vector3f detection_vel = estimate_detection_velocity(j);

// Velocity difference
float vel_diff = (tracker_vel - detection_vel).norm();

// Combined cost
cost_matrix[i][j] = spatial_dist + 0.1f * vel_diff;  // Weight velocity at 10%

Impact: Prevents matching balls with incompatible motion directions.

⚡ SOLUTION 4: Adaptive Distance Threshold (MEDIUM IMPACT)
Use tighter thresholds when balls are close together:

// Calculate minimum inter-ball distance
float min_ball_separation = calculate_min_ball_separation(valid_detections);

// Adaptive threshold
float adaptive_threshold = 0.30f;
if (min_ball_separation < 0.20f) {  // Balls within 20cm
    adaptive_threshold = 0.15f;  // Use tighter threshold
}

Impact: Reduces ambiguous matches when balls are close.

⚡ SOLUTION 5: Gating with Mahalanobis Distance (ADVANCED)
Use the Kalman filter's covariance for statistically-informed gating:

// Mahalanobis distance considers uncertainty
float mahalanobis_dist = calculate_mahalanobis_distance(
    tracker->kf.get_state(),
    tracker->kf.get_covariance(),
    detection_pos
);

// Use chi-squared test for gating
if (mahalanobis_dist > chi_squared_threshold) {
    cost_matrix[i][j] = std::numeric_limits<float>::max();  // Reject
}

Impact: More principled rejection of impossible matches.

Recommended Implementation Order
Start with Solution 2 (Temporal Consistency) - Easiest to implement, immediate impact
Add Solution 3 (Velocity Consistency) - Natural extension
Implement Solution 1 (Hungarian Algorithm) - More work but optimal
Fine-tune with Solution 4 (Adaptive Threshold) - Polish
Quick Test
To verify this is the issue, add debug logging to track ID changes:

// After assignments
for (const auto& [tracker_idx, detection_idx] : assignments) {
    auto* tracker = ball_trackers_list[tracker_idx];
    if (tracker->last_matched_detection_index != detection_idx && 
        tracker->last_matched_detection_index >= 0) {
        debug_log << "[ID SWAP] Tracker " << tracker->logical_id 
                  << " switched from detection " << tracker->last_matched_detection_index
                  << " to " << detection_idx << std::endl;
    }
    tracker->last_matched_detection_index = detection_idx;
}

This will show you exactly when and how often IDs are swapping, confirming the issue.

Bottom Line: The ID swapping is caused by the greedy assignment algorithm lacking temporal consistency. Adding a temporal bonus to the cost function (Solution 2) is the quickest fix with high impact.