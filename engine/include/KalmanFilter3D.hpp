#pragma once

#include <Eigen/Dense>

class KalmanFilter3D {
public:
    // The state vector is [x, y, z, vx, vy, vz]
    using StateVector = Eigen::Matrix<float, 6, 1>;
    // The measurement vector is [x, y, z]
    using MeasurementVector = Eigen::Matrix<float, 3, 1>;

    KalmanFilter3D();

    // Initialize the filter with an initial measurement
    void init(const MeasurementVector& initial_measurement);

    // Predict the next state using constant velocity
    void predict(float dt);

    // Predict the next state using projectile motion model
    void predict_ball(float dt, float gravity = -9.81f);

    // Update the state with a new measurement
    void update(const MeasurementVector& measurement);

    // Get the current estimated state
    StateVector get_state() const;
    StateVector& get_state(); // Non-const version to allow modification
    
    // Get just the position from the state
    Eigen::Vector3f get_position() const;

private:
    // State vector [x, y, z, vx, vy, vz]
    StateVector x_;

    // State transition matrix
    Eigen::Matrix<float, 6, 6> F_;

    // Measurement matrix
    Eigen::Matrix<float, 3, 6> H_;

    // Covariance of the state
    Eigen::Matrix<float, 6, 6> P_;

    // Covariance of the process noise
    Eigen::Matrix<float, 6, 6> Q_;

    // Covariance of the measurement noise
    Eigen::Matrix<float, 3, 3> R_;

    // Identity matrix
    Eigen::Matrix<float, 6, 6> I_;

    bool is_initialized_ = false;
};