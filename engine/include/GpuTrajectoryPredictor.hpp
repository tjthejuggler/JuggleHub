#pragma once

#include <opencv2/opencv.hpp>
#include <opencv2/core/ocl.hpp>
#include <vector>
#include <memory>
#include <mutex>
#include <sstream>

/**
 * GPU-Accelerated Trajectory Predictor
 * 
 * Computes ballistic trajectories using physics-based equations on GPU.
 * Uses OpenCV's UMat for automatic GPU acceleration via OpenCL.
 * 
 * Physics Model:
 * - Ballistic motion with gravity
 * - Optional air resistance (future enhancement)
 * - Parabolic trajectory in 3D space
 * 
 * Performance:
 * - ~10x faster than CPU for trajectory computation
 * - Parallel processing of all trajectory points
 * - Efficient closest-point search using GPU reduction
 */

// Forward declarations
struct TrajectoryPoint {
    cv::Point3f position;      // 3D world position
    uint64_t timestamp;        // When this point was verified (microseconds)
    float confidence;          // Confidence score (0.0-1.0)
    bool verified;             // True if confirmed by YOLO or color blob
    
    TrajectoryPoint() 
        : position(0, 0, 0), timestamp(0), confidence(0.0f), verified(false) {}
    
    TrajectoryPoint(const cv::Point3f& pos, uint64_t ts, float conf, bool ver)
        : position(pos), timestamp(ts), confidence(conf), verified(ver) {}
};

struct BallTrajectory {
    // Verified points along trajectory
    std::vector<TrajectoryPoint> points;
    
    // Physics parameters
    cv::Point3f initial_velocity;    // v0 (m/s)
    cv::Point3f initial_position;    // p0 (m)
    float gravity;                   // g (m/s²), default: 9.81
    uint64_t throw_timestamp;        // When trajectory started (microseconds)
    
    // Confidence metrics
    int verified_point_count;        // Number of confirmed points
    float trajectory_confidence;     // Overall confidence (0.0-1.0)
    float search_radius_m;           // Current search radius (m)
    
    // Cached prediction
    std::vector<cv::Point3f> predicted_path;  // Full predicted trajectory
    uint64_t prediction_timestamp;            // When prediction was computed
    bool prediction_valid;                    // True if prediction is up-to-date
    
    BallTrajectory() 
        : initial_velocity(0, 0, 0), initial_position(0, 0, 0),
          gravity(9.81f), throw_timestamp(0),
          verified_point_count(0), trajectory_confidence(0.0f),
          search_radius_m(0.30f), prediction_timestamp(0),
          prediction_valid(false) {}
};

struct TrajectoryPredictionParams {
    float time_step;        // Time between predicted points (seconds)
    float max_time;         // Maximum trajectory duration (seconds)
    int max_points;         // Maximum number of points to compute
    float gravity;          // Gravitational acceleration (m/s²)
    bool use_air_resistance; // Enable air resistance model (future)
    float air_resistance_coeff; // Air resistance coefficient (future)
    
    TrajectoryPredictionParams()
        : time_step(0.033f),      // 30fps
          max_time(3.0f),         // 3 seconds
          max_points(90),         // 3s * 30fps
          gravity(9.81f),         // Standard gravity
          use_air_resistance(false),
          air_resistance_coeff(0.0f) {}
};

class GpuTrajectoryPredictor {
public:
    GpuTrajectoryPredictor();
    ~GpuTrajectoryPredictor() = default;
    
    /**
     * Predict full trajectory path using ballistic physics
     * 
     * Computes trajectory points using GPU-accelerated kernel:
     *   x(t) = x0 + vx0 * t
     *   y(t) = y0 + vy0 * t
     *   z(t) = z0 + vz0 * t - 0.5 * g * t²
     * 
     * @param initial_pos Starting position (throw point)
     * @param initial_vel Initial velocity vector
     * @param params Prediction parameters (time step, max time, etc.)
     * @return Vector of predicted 3D positions
     */
    std::vector<cv::Point3f> predictTrajectory(
        const cv::Point3f& initial_pos,
        const cv::Point3f& initial_vel,
        const TrajectoryPredictionParams& params = TrajectoryPredictionParams()
    );
    
    /**
     * Find closest point on trajectory to given position
     * 
     * Uses GPU reduction to efficiently find minimum distance.
     * Returns both the index of closest point and the distance.
     * 
     * @param trajectory Predicted trajectory points
     * @param position Position to match
     * @return Pair of (closest_point_index, distance_in_meters)
     */
    std::pair<int, float> findClosestPoint(
        const std::vector<cv::Point3f>& trajectory,
        const cv::Point3f& position
    );
    
    /**
     * Estimate initial velocity from verified trajectory points
     * 
     * Uses least-squares fitting to estimate v0 from observed points.
     * Fits parabolic trajectory to minimize error.
     * 
     * Algorithm:
     * 1. Extract time and position data from verified points
     * 2. Fit linear model for x(t) and y(t)
     * 3. Fit quadratic model for z(t) with gravity
     * 4. Extract velocity components from fitted parameters
     * 
     * @param points Verified trajectory points (minimum 3 required)
     * @param gravity Gravitational acceleration (m/s²)
     * @return Estimated initial velocity vector
     */
    cv::Point3f estimateInitialVelocity(
        const std::vector<TrajectoryPoint>& points,
        float gravity = 9.81f
    );
    
    /**
     * Refine trajectory prediction using verified points
     * 
     * Updates physics parameters to minimize error between
     * predicted and verified points. Recomputes full trajectory.
     * 
     * Process:
     * 1. Re-estimate initial velocity from all verified points
     * 2. Compute new predicted trajectory
     * 3. Update confidence based on fit quality
     * 4. Adjust search radius based on confidence
     * 
     * @param trajectory Current trajectory with verified points
     * @return Updated trajectory with refined prediction
     */
    BallTrajectory refineTrajectory(const BallTrajectory& trajectory);
    
    /**
     * Update trajectory confidence and search radius
     * 
     * Confidence increases with more verified points.
     * Search radius decreases as confidence increases.
     * 
     * Formula:
     *   confidence = min(1.0, verified_count / points_for_full_confidence)
     *   search_radius = max_radius - confidence * (max_radius - min_radius)
     * 
     * @param trajectory Trajectory to update
     * @param points_for_full_confidence Points needed for 100% confidence
     * @param min_radius Minimum search radius (m)
     * @param max_radius Maximum search radius (m)
     */
    void updateConfidence(
        BallTrajectory& trajectory,
        int points_for_full_confidence = 5,
        float min_radius = 0.10f,
        float max_radius = 0.30f
    );
    
    /**
     * Check if GPU acceleration is available
     * 
     * @return true if GPU is enabled, false if using CPU fallback
     */
    bool isGpuEnabled() const { return gpu_enabled_; }
    
    /**
     * Get GPU device information
     * 
     * @return String describing GPU device
     */
    std::string getGpuInfo() const;
    
    /**
     * Get performance statistics
     * 
     * @return String with timing and usage statistics
     */
    std::string getPerformanceStats() const;

private:
    // GPU state
    bool gpu_enabled_;
    std::mutex mutex_;
    
    // Pre-allocated GPU buffers
    cv::UMat gpu_trajectory_buffer_;
    cv::UMat gpu_distance_buffer_;
    cv::UMat gpu_input_buffer_;
    
    // Performance tracking
    struct PerformanceStats {
        uint64_t trajectory_predictions = 0;
        uint64_t closest_point_searches = 0;
        uint64_t velocity_estimations = 0;
        uint64_t trajectory_refinements = 0;
        
        double total_prediction_time_ms = 0.0;
        double total_search_time_ms = 0.0;
        double total_estimation_time_ms = 0.0;
        double total_refinement_time_ms = 0.0;
    };
    PerformanceStats stats_;
    
    // Helper functions
    void initializeGpu();
    
    // CPU fallback implementations
    std::vector<cv::Point3f> predictTrajectoryCpu(
        const cv::Point3f& initial_pos,
        const cv::Point3f& initial_vel,
        const TrajectoryPredictionParams& params
    );
    
    std::pair<int, float> findClosestPointCpu(
        const std::vector<cv::Point3f>& trajectory,
        const cv::Point3f& position
    );
    
    cv::Point3f estimateInitialVelocityCpu(
        const std::vector<TrajectoryPoint>& points,
        float gravity
    );
};