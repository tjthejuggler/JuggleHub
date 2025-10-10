#include "GpuTrajectoryPredictor.hpp"
#include <chrono>
#include <iostream>
#include <cmath>
#include <algorithm>

GpuTrajectoryPredictor::GpuTrajectoryPredictor() 
    : gpu_enabled_(false) {
    initializeGpu();
}

void GpuTrajectoryPredictor::initializeGpu() {
    // Check if OpenCL is available
    if (!cv::ocl::haveOpenCL()) {
        std::cout << "[GpuTrajectoryPredictor] OpenCL not available, using CPU fallback" 
                  << std::endl;
        gpu_enabled_ = false;
        return;
    }
    
    // Initialize OpenCL context
    cv::ocl::Context context;
    if (!context.create(cv::ocl::Device::TYPE_GPU)) {
        std::cout << "[GpuTrajectoryPredictor] Failed to create GPU context, using CPU fallback" 
                  << std::endl;
        gpu_enabled_ = false;
        return;
    }
    
    // Set default device
    cv::ocl::Device device = cv::ocl::Device::getDefault();
    cv::ocl::setUseOpenCL(true);
    
    gpu_enabled_ = true;
    
    std::cout << "[GpuTrajectoryPredictor] GPU acceleration enabled" << std::endl;
    std::cout << "  Device: " << device.name() << std::endl;
    std::cout << "  Vendor: " << device.vendorName() << std::endl;
    std::cout << "  Version: " << device.version() << std::endl;
    
    // Pre-allocate GPU buffers (will be resized as needed)
    gpu_trajectory_buffer_.create(1, 100, CV_32FC3);  // 100 points initially
    gpu_distance_buffer_.create(1, 100, CV_32F);
}

std::string GpuTrajectoryPredictor::getGpuInfo() const {
    if (!gpu_enabled_) {
        return "GPU: Disabled (using CPU fallback)";
    }
    
    cv::ocl::Device device = cv::ocl::Device::getDefault();
    return "GPU: " + device.name() + " (" + device.vendorName() + ")";
}

// ============================================================================
// Trajectory Prediction
// ============================================================================

std::vector<cv::Point3f> GpuTrajectoryPredictor::predictTrajectory(
    const cv::Point3f& initial_pos,
    const cv::Point3f& initial_vel,
    const TrajectoryPredictionParams& params
) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    std::vector<cv::Point3f> result;
    
    if (!gpu_enabled_) {
        result = predictTrajectoryCpu(initial_pos, initial_vel, params);
    } else {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Calculate number of points
        int num_points = std::min(params.max_points, 
                                 static_cast<int>(params.max_time / params.time_step));
        
        try {
            // Prepare output buffer
            cv::UMat gpu_output;
            gpu_output.create(num_points, 1, CV_32FC3);
            
            // Use OpenCV's parallel_for_ with UMat for GPU acceleration
            // This leverages OpenCL automatically
            cv::parallel_for_(cv::Range(0, num_points), [&](const cv::Range& range) {
                cv::Mat cpu_output = gpu_output.getMat(cv::ACCESS_WRITE);
                
                for (int i = range.start; i < range.end; i++) {
                    float t = i * params.time_step;
                    
                    // Ballistic trajectory equations
                    // x(t) = x0 + vx0 * t
                    // y(t) = y0 + vy0 * t
                    // z(t) = z0 + vz0 * t - 0.5 * g * t²
                    float x = initial_pos.x + initial_vel.x * t;
                    float y = initial_pos.y + initial_vel.y * t;
                    float z = initial_pos.z + initial_vel.z * t - 0.5f * params.gravity * t * t;
                    
                    cpu_output.at<cv::Vec3f>(i, 0) = cv::Vec3f(x, y, z);
                }
            });
            
            // Download results from GPU
            cv::Mat cpu_result = gpu_output.getMat(cv::ACCESS_READ);
            result.reserve(num_points);
            
            for (int i = 0; i < num_points; i++) {
                cv::Vec3f point = cpu_result.at<cv::Vec3f>(i, 0);
                result.push_back(cv::Point3f(point[0], point[1], point[2]));
            }
        } catch (const cv::Exception& e) {
            std::cerr << "[GpuTrajectoryPredictor] GPU prediction failed: " << e.what() << std::endl;
            std::cerr << "[GpuTrajectoryPredictor] Falling back to CPU" << std::endl;
            result = predictTrajectoryCpu(initial_pos, initial_vel, params);
        }
    }
    
    // Update statistics
    auto end_time = std::chrono::high_resolution_clock::now();
    double elapsed_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
    
    stats_.trajectory_predictions++;
    stats_.total_prediction_time_ms += elapsed_ms;
    
    return result;
}

std::vector<cv::Point3f> GpuTrajectoryPredictor::predictTrajectoryCpu(
    const cv::Point3f& initial_pos,
    const cv::Point3f& initial_vel,
    const TrajectoryPredictionParams& params
) {
    int num_points = std::min(params.max_points, 
                             static_cast<int>(params.max_time / params.time_step));
    
    std::vector<cv::Point3f> trajectory;
    trajectory.reserve(num_points);
    
    for (int i = 0; i < num_points; i++) {
        float t = i * params.time_step;
        
        // Ballistic trajectory equations
        float x = initial_pos.x + initial_vel.x * t;
        float y = initial_pos.y + initial_vel.y * t;
        float z = initial_pos.z + initial_vel.z * t - 0.5f * params.gravity * t * t;
        
        trajectory.push_back(cv::Point3f(x, y, z));
    }
    
    return trajectory;
}

// ============================================================================
// Closest Point Search
// ============================================================================

std::pair<int, float> GpuTrajectoryPredictor::findClosestPoint(
    const std::vector<cv::Point3f>& trajectory,
    const cv::Point3f& position
) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    std::pair<int, float> result;
    
    if (!gpu_enabled_ || trajectory.size() < 10) {
        // Use CPU for small trajectories
        result = findClosestPointCpu(trajectory, position);
    } else {
        std::lock_guard<std::mutex> lock(mutex_);
        
        try {
            // Upload trajectory to GPU
            cv::Mat cpu_trajectory(trajectory.size(), 1, CV_32FC3);
            for (size_t i = 0; i < trajectory.size(); i++) {
                cpu_trajectory.at<cv::Vec3f>(i, 0) = cv::Vec3f(
                    trajectory[i].x, trajectory[i].y, trajectory[i].z
                );
            }
            
            cv::UMat gpu_trajectory = cpu_trajectory.getUMat(cv::ACCESS_READ);
            cv::UMat gpu_distances;
            gpu_distances.create(trajectory.size(), 1, CV_32F);
            
            // Compute distances on GPU using parallel_for_
            cv::parallel_for_(cv::Range(0, trajectory.size()), [&](const cv::Range& range) {
                cv::Mat traj = gpu_trajectory.getMat(cv::ACCESS_READ);
                cv::Mat dist = gpu_distances.getMat(cv::ACCESS_WRITE);
                
                for (int i = range.start; i < range.end; i++) {
                    cv::Vec3f point = traj.at<cv::Vec3f>(i, 0);
                    float dx = point[0] - position.x;
                    float dy = point[1] - position.y;
                    float dz = point[2] - position.z;
                    dist.at<float>(i, 0) = std::sqrt(dx*dx + dy*dy + dz*dz);
                }
            });
            
            // Find minimum on CPU (small operation)
            cv::Mat cpu_distances = gpu_distances.getMat(cv::ACCESS_READ);
            double min_dist;
            cv::Point min_loc;
            cv::minMaxLoc(cpu_distances, &min_dist, nullptr, &min_loc, nullptr);
            
            result = {min_loc.y, static_cast<float>(min_dist)};
        } catch (const cv::Exception& e) {
            std::cerr << "[GpuTrajectoryPredictor] GPU search failed: " << e.what() << std::endl;
            std::cerr << "[GpuTrajectoryPredictor] Falling back to CPU" << std::endl;
            result = findClosestPointCpu(trajectory, position);
        }
    }
    
    // Update statistics
    auto end_time = std::chrono::high_resolution_clock::now();
    double elapsed_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
    
    stats_.closest_point_searches++;
    stats_.total_search_time_ms += elapsed_ms;
    
    return result;
}

std::pair<int, float> GpuTrajectoryPredictor::findClosestPointCpu(
    const std::vector<cv::Point3f>& trajectory,
    const cv::Point3f& position
) {
    int closest_idx = 0;
    float min_distance = std::numeric_limits<float>::max();
    
    for (size_t i = 0; i < trajectory.size(); i++) {
        float dx = trajectory[i].x - position.x;
        float dy = trajectory[i].y - position.y;
        float dz = trajectory[i].z - position.z;
        float distance = std::sqrt(dx*dx + dy*dy + dz*dz);
        
        if (distance < min_distance) {
            min_distance = distance;
            closest_idx = i;
        }
    }
    
    return {closest_idx, min_distance};
}

// ============================================================================
// Velocity Estimation (Least-Squares Fitting)
// ============================================================================

cv::Point3f GpuTrajectoryPredictor::estimateInitialVelocity(
    const std::vector<TrajectoryPoint>& points,
    float gravity
) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    if (points.size() < 3) {
        // Not enough points for estimation
        return cv::Point3f(0, 0, 0);
    }
    
    // Use CPU for least-squares fitting (small matrix operations)
    cv::Point3f result = estimateInitialVelocityCpu(points, gravity);
    
    // Update statistics
    auto end_time = std::chrono::high_resolution_clock::now();
    double elapsed_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
    
    stats_.velocity_estimations++;
    stats_.total_estimation_time_ms += elapsed_ms;
    
    return result;
}

cv::Point3f GpuTrajectoryPredictor::estimateInitialVelocityCpu(
    const std::vector<TrajectoryPoint>& points,
    float gravity
) {
    int n = points.size();
    
    // Extract time and position data
    std::vector<double> times;
    std::vector<double> x_vals, y_vals, z_vals;
    
    uint64_t t0 = points[0].timestamp;
    
    for (const auto& point : points) {
        if (!point.verified) continue;
        
        double t = (point.timestamp - t0) / 1000000.0;  // Convert to seconds
        times.push_back(t);
        x_vals.push_back(point.position.x);
        y_vals.push_back(point.position.y);
        z_vals.push_back(point.position.z);
    }
    
    if (times.size() < 3) {
        return cv::Point3f(0, 0, 0);
    }
    
    // Fit linear model for x and y: p(t) = p0 + v0*t
    // Use least-squares: v0 = sum((t - t_mean) * (p - p_mean)) / sum((t - t_mean)^2)
    
    double t_mean = 0.0, x_mean = 0.0, y_mean = 0.0;
    for (size_t i = 0; i < times.size(); i++) {
        t_mean += times[i];
        x_mean += x_vals[i];
        y_mean += y_vals[i];
    }
    t_mean /= times.size();
    x_mean /= times.size();
    y_mean /= times.size();
    
    double sum_t_dev_sq = 0.0;
    double sum_tx_dev = 0.0, sum_ty_dev = 0.0;
    
    for (size_t i = 0; i < times.size(); i++) {
        double t_dev = times[i] - t_mean;
        sum_t_dev_sq += t_dev * t_dev;
        sum_tx_dev += t_dev * (x_vals[i] - x_mean);
        sum_ty_dev += t_dev * (y_vals[i] - y_mean);
    }
    
    float vx0 = static_cast<float>(sum_tx_dev / sum_t_dev_sq);
    float vy0 = static_cast<float>(sum_ty_dev / sum_t_dev_sq);
    
    // Fit quadratic model for z: z(t) = z0 + vz0*t - 0.5*g*t^2
    // Rearrange: z(t) + 0.5*g*t^2 = z0 + vz0*t
    // Then fit linear model to (z + 0.5*g*t^2) vs t
    
    std::vector<double> z_adjusted;
    for (size_t i = 0; i < times.size(); i++) {
        z_adjusted.push_back(z_vals[i] + 0.5 * gravity * times[i] * times[i]);
    }
    
    double z_adj_mean = 0.0;
    for (double z : z_adjusted) {
        z_adj_mean += z;
    }
    z_adj_mean /= z_adjusted.size();
    
    double sum_tz_dev = 0.0;
    for (size_t i = 0; i < times.size(); i++) {
        double t_dev = times[i] - t_mean;
        sum_tz_dev += t_dev * (z_adjusted[i] - z_adj_mean);
    }
    
    float vz0 = static_cast<float>(sum_tz_dev / sum_t_dev_sq);
    
    return cv::Point3f(vx0, vy0, vz0);
}

// ============================================================================
// Trajectory Refinement
// ============================================================================

BallTrajectory GpuTrajectoryPredictor::refineTrajectory(
    const BallTrajectory& trajectory
) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    BallTrajectory refined = trajectory;
    
    // Re-estimate initial velocity from all verified points
    if (trajectory.verified_point_count >= 3) {
        refined.initial_velocity = estimateInitialVelocity(
            trajectory.points, trajectory.gravity
        );
        
        // Recompute predicted trajectory
        TrajectoryPredictionParams params;
        params.gravity = trajectory.gravity;
        refined.predicted_path = predictTrajectory(
            refined.initial_position,
            refined.initial_velocity,
            params
        );
        
        refined.prediction_timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        refined.prediction_valid = true;
    }
    
    // Update confidence
    updateConfidence(refined);
    
    // Update statistics
    auto end_time = std::chrono::high_resolution_clock::now();
    double elapsed_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
    
    stats_.trajectory_refinements++;
    stats_.total_refinement_time_ms += elapsed_ms;
    
    return refined;
}

void GpuTrajectoryPredictor::updateConfidence(
    BallTrajectory& trajectory,
    int points_for_full_confidence,
    float min_radius,
    float max_radius
) {
    // Confidence increases with verified points
    trajectory.trajectory_confidence = std::min(
        1.0f,
        static_cast<float>(trajectory.verified_point_count) / points_for_full_confidence
    );
    
    // Search radius decreases with confidence
    trajectory.search_radius_m = max_radius - 
        trajectory.trajectory_confidence * (max_radius - min_radius);
}

// ============================================================================
// Performance Statistics
// ============================================================================

std::string GpuTrajectoryPredictor::getPerformanceStats() const {
    std::stringstream ss;
    
    ss << "=== GPU Trajectory Predictor Statistics ===" << std::endl;
    ss << "GPU Enabled: " << (gpu_enabled_ ? "Yes" : "No") << std::endl;
    ss << std::endl;
    
    ss << "Operations:" << std::endl;
    ss << "  Trajectory Predictions: " << stats_.trajectory_predictions << std::endl;
    ss << "  Closest Point Searches: " << stats_.closest_point_searches << std::endl;
    ss << "  Velocity Estimations: " << stats_.velocity_estimations << std::endl;
    ss << "  Trajectory Refinements: " << stats_.trajectory_refinements << std::endl;
    ss << std::endl;
    
    if (stats_.trajectory_predictions > 0) {
        ss << "Average Times:" << std::endl;
        ss << "  Prediction: " << (stats_.total_prediction_time_ms / stats_.trajectory_predictions) 
           << " ms" << std::endl;
    }
    
    if (stats_.closest_point_searches > 0) {
        ss << "  Search: " << (stats_.total_search_time_ms / stats_.closest_point_searches) 
           << " ms" << std::endl;
    }
    
    if (stats_.velocity_estimations > 0) {
        ss << "  Estimation: " << (stats_.total_estimation_time_ms / stats_.velocity_estimations) 
           << " ms" << std::endl;
    }
    
    if (stats_.trajectory_refinements > 0) {
        ss << "  Refinement: " << (stats_.total_refinement_time_ms / stats_.trajectory_refinements) 
           << " ms" << std::endl;
    }
    
    return ss.str();
}