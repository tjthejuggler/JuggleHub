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
    const cv::Point3f& acceleration,
    const TrajectoryPredictionParams& params
) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    std::vector<cv::Point3f> result;
    
    if (!gpu_enabled_) {
        result = predictTrajectoryCpu(initial_pos, initial_vel, acceleration, params);
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
                    float t2 = t * t;
                    
                    // General kinematic equation with fitted acceleration
                    // p(t) = p₀ + v₀t + ½at²
                    float x = initial_pos.x + initial_vel.x * t + 0.5f * acceleration.x * t2;
                    float y = initial_pos.y + initial_vel.y * t + 0.5f * acceleration.y * t2;
                    float z = initial_pos.z + initial_vel.z * t + 0.5f * acceleration.z * t2;
                    
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
            result = predictTrajectoryCpu(initial_pos, initial_vel, acceleration, params);
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
    const cv::Point3f& acceleration,
    const TrajectoryPredictionParams& params
) {
    int num_points = std::min(params.max_points,
                             static_cast<int>(params.max_time / params.time_step));
    
    std::vector<cv::Point3f> trajectory;
    trajectory.reserve(num_points);
    
    for (int i = 0; i < num_points; i++) {
        float t = i * params.time_step;
        float t2 = t * t;
        
        // General kinematic equation with fitted acceleration
        // p(t) = p₀ + v₀t + ½at²
        float x = initial_pos.x + initial_vel.x * t + 0.5f * acceleration.x * t2;
        float y = initial_pos.y + initial_vel.y * t + 0.5f * acceleration.y * t2;
        float z = initial_pos.z + initial_vel.z * t + 0.5f * acceleration.z * t2;
        
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

GpuTrajectoryPredictor::TrajectoryInitialConditions GpuTrajectoryPredictor::estimateInitialConditions(
    const std::vector<TrajectoryPoint>& points,
    float gravity
) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    if (points.size() < 3) {
        // Not enough points for estimation
        return {cv::Point3f(0, 0, 0), cv::Point3f(0, 0, 0)};
    }
    
    // Use CPU for least-squares fitting (small matrix operations)
    TrajectoryInitialConditions result = estimateInitialConditionsCpu(points, gravity);
    
    // Update statistics
    auto end_time = std::chrono::high_resolution_clock::now();
    double elapsed_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
    
    stats_.velocity_estimations++;
    stats_.total_estimation_time_ms += elapsed_ms;
    
    return result;
}

// NEW: Estimate velocity at CURRENT position (last point)
cv::Point3f GpuTrajectoryPredictor::estimateCurrentVelocity(
    const std::vector<TrajectoryPoint>& points,
    float gravity
) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    if (points.size() < 2) {
        // Not enough points for estimation
        return cv::Point3f(0, 0, 0);
    }
    
    // Use CPU for velocity estimation (small operation)
    cv::Point3f result = estimateCurrentVelocityCpu(points, gravity);
    
    // Update statistics
    auto end_time = std::chrono::high_resolution_clock::now();
    double elapsed_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
    
    stats_.velocity_estimations++;
    stats_.total_estimation_time_ms += elapsed_ms;
    
    return result;
}

// DEPRECATED: Kept for backward compatibility
cv::Point3f GpuTrajectoryPredictor::estimateInitialVelocity(
    const std::vector<TrajectoryPoint>& points,
    float gravity
) {
    auto conditions = estimateInitialConditions(points, gravity);
    return conditions.second;  // Return velocity only
}

GpuTrajectoryPredictor::TrajectoryInitialConditions GpuTrajectoryPredictor::estimateInitialConditionsCpu(
    const std::vector<TrajectoryPoint>& points,
    float gravity [[maybe_unused]]
) {
    int n = points.size();
    
    const int MAX_FIT_POINTS = 10;
    int start_idx = std::max(0, n - static_cast<int>(MAX_FIT_POINTS));
    
    std::vector<double> times;
    std::vector<double> x_vals, y_vals, z_vals;
    
    // Use the timestamp of the first point in our window as t=0
    uint64_t t0 = points[start_idx].timestamp;
    
    for (int i = start_idx; i < n; i++) {
        // Only use verified points for fitting
        if (!points[i].verified) continue;
        
        double t = (points[i].timestamp - t0) / 1000000.0; // Convert to seconds
        times.push_back(t);
        x_vals.push_back(points[i].position.x);
        y_vals.push_back(points[i].position.y);
        z_vals.push_back(points[i].position.z);
    }
    
    if (times.size() < 3) {
        return {cv::Point3f(0, 0, 0), cv::Point3f(0, 0, 0)};
    }
    
    // --- FIT LINEAR MODEL FOR X and Y ---
    // This part was correct and remains the same.
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
    
    if (sum_t_dev_sq < 1e-9) {
        return {cv::Point3f(0, 0, 0), cv::Point3f(0, 0, 0)};
    }
    
    float v0x = static_cast<float>(sum_tx_dev / sum_t_dev_sq);
    float v0y = static_cast<float>(sum_ty_dev / sum_t_dev_sq);
    float p0x = static_cast<float>(x_mean - v0x * t_mean);
    float p0y = static_cast<float>(y_mean - v0y * t_mean);

    // --- FIT PARABOLA TO Z: z(t) = a*t^2 + b*t + c ---
    // This is the corrected section using cv::solve
    double sum_t = 0.0, sum_t2 = 0.0, sum_t3 = 0.0, sum_t4 = 0.0;
    double sum_z = 0.0, sum_tz = 0.0, sum_t2z = 0.0;
    
    for (size_t i = 0; i < times.size(); i++) {
        double t = times[i];
        double t2 = t * t;
        double z = z_vals[i];
        
        sum_t += t;
        sum_t2 += t2;
        sum_t3 += t2 * t;
        sum_t4 += t2 * t2;
        sum_z += z;
        sum_tz += t * z;
        sum_t2z += t2 * z;
    }
    
    double n_pts = static_cast<double>(times.size());

    // Build the normal equations matrix A and vector B for Ax = B
    // x = [a, b, c]'
    cv::Mat A = (cv::Mat_<double>(3, 3) <<
        sum_t4, sum_t3, sum_t2,
        sum_t3, sum_t2, sum_t,
        sum_t2, sum_t,  n_pts);
        
    cv::Mat B = (cv::Mat_<double>(3, 1) << sum_t2z, sum_tz, sum_z);
    
    cv::Mat coeffs;
    // Solve the system for the coefficients [a, b, c]
    if (!cv::solve(A, B, coeffs, cv::DECOMP_LU)) {
        // Fallback to linear fit if matrix is singular
        double z_mean = sum_z / n_pts;
        double sum_tz_dev = 0.0;
        for (size_t i = 0; i < times.size(); i++) {
            sum_tz_dev += (times[i] - t_mean) * (z_vals[i] - z_mean);
        }
        float v0z = static_cast<float>(sum_tz_dev / sum_t_dev_sq);
        float p0z = static_cast<float>(z_mean - v0z * t_mean);
        return {{p0x, p0y, p0z}, {v0x, v0y, v0z}};
    }

    // From z(t) = a*t^2 + b*t + c:
    // a = coeffs.at<double>(0)
    // b = v0z = initial vertical velocity
    // c = p0z = initial vertical position
    float v0z = static_cast<float>(coeffs.at<double>(1));
    float p0z = static_cast<float>(coeffs.at<double>(2));

    cv::Point3f p0(p0x, p0y, p0z);
    cv::Point3f v0(v0x, v0y, v0z);
    
    return {p0, v0};
}

// NEW: General parabolic fit for all three axes
GpuTrajectoryPredictor::ParabolicFitResult GpuTrajectoryPredictor::estimateCurrentStateCpu(
    const std::vector<TrajectoryPoint>& points
) {
    // --- 1. Data Preparation ---
    const int MAX_FIT_POINTS = 10;
    int n = points.size();
    int start_idx = std::max(0, n - MAX_FIT_POINTS);

    std::vector<double> t_vals;
    std::vector<cv::Point3d> pos_vals;

    int64_t t_current = static_cast<int64_t>(points.back().timestamp);

    for (int i = start_idx; i < n; i++) {
        if (!points[i].verified) continue;

        int64_t timestamp_i = static_cast<int64_t>(points[i].timestamp);
        double t = (timestamp_i - t_current) / 1000000.0;
        
        t_vals.push_back(t);
        pos_vals.push_back(cv::Point3d(points[i].position));
    }

    // We need at least 3 points to fit a parabola
    if (t_vals.size() < 3) {
        return {cv::Point3f(0,0,0), cv::Point3f(0,0,0), cv::Point3f(0,0,0), false};
    }

    // --- 2. Calculate the Sums for the Normal Equations ---
    double sum_t = 0.0, sum_t2 = 0.0, sum_t3 = 0.0, sum_t4 = 0.0;
    cv::Point3d sum_pos(0,0,0), sum_t_pos(0,0,0), sum_t2_pos(0,0,0);
    
    for (size_t i = 0; i < t_vals.size(); ++i) {
        double t = t_vals[i];
        double t2 = t * t;
        const auto& pos = pos_vals[i];

        sum_t  += t;
        sum_t2 += t2;
        sum_t3 += t2 * t;
        sum_t4 += t2 * t2;

        sum_pos    += pos;
        sum_t_pos  += pos * t;
        sum_t2_pos += pos * t2;
    }

    double num_pts = static_cast<double>(t_vals.size());

    // --- 3. Build and Solve the System of Equations ---
    cv::Mat A = (cv::Mat_<double>(3, 3) <<
        sum_t4, sum_t3, sum_t2,
        sum_t3, sum_t2, sum_t,
        sum_t2, sum_t,  num_pts);
    
    cv::Mat coeffs_x, coeffs_y, coeffs_z;

    cv::Mat Bx = (cv::Mat_<double>(3, 1) << sum_t2_pos.x, sum_t_pos.x, sum_pos.x);
    cv::Mat By = (cv::Mat_<double>(3, 1) << sum_t2_pos.y, sum_t_pos.y, sum_pos.y);
    cv::Mat Bz = (cv::Mat_<double>(3, 1) << sum_t2_pos.z, sum_t_pos.z, sum_pos.z);
    
    if (!cv::solve(A, Bx, coeffs_x, cv::DECOMP_LU) ||
        !cv::solve(A, By, coeffs_y, cv::DECOMP_LU) ||
        !cv::solve(A, Bz, coeffs_z, cv::DECOMP_LU))
    {
        return {cv::Point3f(0,0,0), cv::Point3f(0,0,0), cv::Point3f(0,0,0), false};
    }

    // --- 4. Interpret the Coefficients ---
    // p(t) = c₂t² + c₁t + c₀
    // Comparing with p(t) = p₀ + v₀t + ½at²:
    // c₀ = p₀, c₁ = v₀, c₂ = ½a (so a = 2*c₂)

    ParabolicFitResult result;
    result.position.x = coeffs_x.at<double>(2);
    result.position.y = coeffs_y.at<double>(2);
    result.position.z = coeffs_z.at<double>(2);

    result.velocity.x = coeffs_x.at<double>(1);
    result.velocity.y = coeffs_y.at<double>(1);
    result.velocity.z = coeffs_z.at<double>(1);

    result.acceleration.x = 2.0 * coeffs_x.at<double>(0);
    result.acceleration.y = 2.0 * coeffs_y.at<double>(0);
    result.acceleration.z = 2.0 * coeffs_z.at<double>(0);

    result.success = true;
    return result;
}

cv::Point3f GpuTrajectoryPredictor::estimateCurrentVelocityCpu(
    const std::vector<TrajectoryPoint>& points,
    float gravity
) {
    int n = points.size();
    
    if (n < 2) {
        return cv::Point3f(0, 0, 0);
    }
    
    // Attempt the full parabolic fit first
    ParabolicFitResult fit = estimateCurrentStateCpu(points);

    if (fit.success) {
        return fit.velocity;
    }
    
    // --- FALLBACK: If parabolic fit fails (e.g., only 2 points) ---
    if (n == 2) {
        const TrajectoryPoint& p1 = points[0];
        const TrajectoryPoint& p2 = points[1];
        
        // CRITICAL: Use signed integers to avoid overflow
        int64_t ts1 = static_cast<int64_t>(p1.timestamp);
        int64_t ts2 = static_cast<int64_t>(p2.timestamp);
        double dt = (ts2 - ts1) / 1000000.0;  // µs to seconds
        
        if (dt < 0.001) {
            return cv::Point3f(0, 0, 0);
        }
        
        // Simple velocity calculation: v = Δp / Δt
        cv::Point3f velocity;
        velocity.x = (p2.position.x - p1.position.x) / dt;
        velocity.y = (p2.position.y - p1.position.y) / dt;
        velocity.z = (p2.position.z - p1.position.z) / dt;
        
        return velocity;
    }
    
    // For 3+ points: Use least-squares fitting for better noise resistance
    // Key difference: Set t=0 at the LAST point (current time), not first point
    
    const int MAX_FIT_POINTS = 10;
    int start_idx = std::max(0, n - MAX_FIT_POINTS);
    
    std::vector<double> times;
    std::vector<double> x_vals, y_vals, z_vals;
    
    // CRITICAL: Use the timestamp of the LAST point as t=0 (current time)
    // MUST use signed integers to handle negative time differences!
    int64_t t_current = static_cast<int64_t>(points[n - 1].timestamp);
    
    for (int i = start_idx; i < n; i++) {
        // Only use verified points for fitting
        if (!points[i].verified) {
            continue;
        }
        
        // Time is NEGATIVE for past points, 0 for current point
        // CRITICAL: Cast to signed int64_t before subtraction to avoid overflow!
        int64_t timestamp_i = static_cast<int64_t>(points[i].timestamp);
        double t = (timestamp_i - t_current) / 1000000.0;  // Convert to seconds
        
        times.push_back(t);
        x_vals.push_back(points[i].position.x);
        y_vals.push_back(points[i].position.y);
        z_vals.push_back(points[i].position.z);
    }
    
    if (times.size() < 2) {
        // Fall back to two-point method with last 2 points
        const TrajectoryPoint& p1 = points[n - 2];
        const TrajectoryPoint& p2 = points[n - 1];
        
        // CRITICAL: Use signed integers to avoid overflow
        int64_t ts1 = static_cast<int64_t>(p1.timestamp);
        int64_t ts2 = static_cast<int64_t>(p2.timestamp);
        double dt = (ts2 - ts1) / 1000000.0;
        
        if (dt < 0.001) {
            return cv::Point3f(0, 0, 0);
        }
        
        cv::Point3f velocity;
        velocity.x = (p2.position.x - p1.position.x) / dt;
        velocity.y = (p2.position.y - p1.position.y) / dt;
        velocity.z = (p2.position.z - p1.position.z) / dt;
        
        return velocity;
    }
    
    // --- FIT LINEAR MODEL FOR X and Y ---
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
    
    // CRITICAL FIX: Check if variance is essentially zero (all points at same time)
    // Use a very small threshold since we're working with small time differences
    // When t=0 at last point, times range from ~-0.3s to 0s, giving variance ~0.01
    if (sum_t_dev_sq < 1e-20) {
        return cv::Point3f(0, 0, 0);
    }
    
    // Velocity is the slope of the linear fit: v = sum(t_dev * pos_dev) / sum(t_dev^2)
    float v0x = static_cast<float>(sum_tx_dev / sum_t_dev_sq);
    float v0y = static_cast<float>(sum_ty_dev / sum_t_dev_sq);
    
    // --- FIT Z VELOCITY WITH PHYSICS CONSTRAINT ---
    // CRITICAL: We know z(t) = z0 + v_z0*t - 0.5*g*t^2
    // So we constrain a = -g/2 and only fit for b (velocity) and c (position)
    // This gives: z(t) = (-g/2)*t^2 + b*t + c
    // Rearranging: z(t) + (g/2)*t^2 = b*t + c
    // Let z_adjusted = z + (g/2)*t^2, then fit: z_adjusted = b*t + c
    
    double sum_t = 0.0, sum_t2 = 0.0;
    double sum_z_adj = 0.0, sum_tz_adj = 0.0;
    
    for (size_t i = 0; i < times.size(); i++) {
        double t = times[i];
        double t2 = t * t;
        double z = z_vals[i];
        
        // Adjust z by adding back the gravity term: z_adj = z + (g/2)*t^2
        double z_adjusted = z + 0.5 * gravity * t2;
        
        sum_t += t;
        sum_t2 += t2;
        sum_z_adj += z_adjusted;
        sum_tz_adj += t * z_adjusted;
    }
    
    double n_pts = static_cast<double>(times.size());
    
    // Build the normal equations for linear fit: z_adj = b*t + c
    // Matrix form: [sum_t2, sum_t  ] [b] = [sum_tz_adj]
    //              [sum_t,  n_pts  ] [c]   [sum_z_adj ]
    cv::Mat A = (cv::Mat_<double>(2, 2) <<
        sum_t2, sum_t,
        sum_t,  n_pts);
        
    cv::Mat B = (cv::Mat_<double>(2, 1) << sum_tz_adj, sum_z_adj);
    
    cv::Mat coeffs;
    // Solve the system for [b, c]
    if (!cv::solve(A, B, coeffs, cv::DECOMP_LU)) {
        // Fallback to linear fit if matrix is singular
        double z_mean = 0.0;
        for (double z : z_vals) z_mean += z;
        z_mean /= n_pts;
        
        double sum_tz_dev = 0.0;
        for (size_t i = 0; i < times.size(); i++) {
            sum_tz_dev += (times[i] - t_mean) * (z_vals[i] - z_mean);
        }
        float v0z = static_cast<float>(sum_tz_dev / sum_t_dev_sq);
        return cv::Point3f(v0x, v0y, v0z);
    }
    
    // Extract velocity and position
    // z(t) = (-g/2)*t^2 + b*t + c
    // At t=0: v_z = b (the derivative at t=0)
    double b_coeff = coeffs.at<double>(0);  // velocity at t=0
    float v0z = static_cast<float>(b_coeff);
    
    return cv::Point3f(v0x, v0y, v0z);
}

// DEPRECATED: Kept for backward compatibility
cv::Point3f GpuTrajectoryPredictor::estimateInitialVelocityCpu(
    const std::vector<TrajectoryPoint>& points,
    float gravity
) {
    auto conditions = estimateInitialConditionsCpu(points, gravity);
    return conditions.second;  // Return velocity only
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
        refined.initial_velocity = estimateCurrentVelocity(
            trajectory.points, trajectory.gravity
        );
        
        // Recompute predicted trajectory with standard gravity acceleration
        TrajectoryPredictionParams params;
        params.gravity = trajectory.gravity;
        cv::Point3f acceleration(0, 0, -params.gravity);  // Standard gravity
        refined.predicted_path = predictTrajectory(
            refined.initial_position,
            refined.initial_velocity,
            acceleration,
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