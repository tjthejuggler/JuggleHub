#include "KalmanFilter3D.hpp"

KalmanFilter3D::KalmanFilter3D() {
    // State vector [x, y, z, vx, vy, vz]
    x_ = StateVector::Zero();

    // State transition matrix (constant velocity model)
    F_ = Eigen::Matrix<float, 6, 6>::Identity();
    // We will update this matrix in the predict step based on dt

    // Measurement matrix (we only measure position)
    H_ = Eigen::Matrix<float, 3, 6>::Zero();
    H_(0, 0) = 1; // x
    H_(1, 1) = 1; // y
    H_(2, 2) = 1; // z

    // Covariance of the state (initial uncertainty)
    P_ = Eigen::Matrix<float, 6, 6>::Identity() * 1000;

    // Covariance of the process noise
    // This needs tuning. A higher value means the model is less certain.
    Q_ = Eigen::Matrix<float, 6, 6>::Identity();
    Q_.bottomRightCorner<3, 3>() *= 0.1; // Lower uncertainty for velocity

    // Covariance of the measurement noise
    // This should be based on the sensor's accuracy.
    R_ = Eigen::Matrix<float, 3, 3>::Identity() * 0.1;

    I_ = Eigen::Matrix<float, 6, 6>::Identity();
}

void KalmanFilter3D::init(const MeasurementVector& initial_measurement) {
    // Set initial position from the first measurement, and assume initial velocity is zero
    x_.head<3>() = initial_measurement;
    x_.tail<3>() = Eigen::Vector3f::Zero();
    is_initialized_ = true;
}

void KalmanFilter3D::predict(float dt) {
    if (!is_initialized_) return;

    // Update the state transition matrix for constant velocity
    F_ = Eigen::Matrix<float, 6, 6>::Identity();
    F_(0, 3) = dt;
    F_(1, 4) = dt;
    F_(2, 5) = dt;

    // Predict the next state
    x_ = F_ * x_;
    P_ = F_ * P_ * F_.transpose() + Q_;
}

void KalmanFilter3D::predict_ball(float dt, float gravity) {
    if (!is_initialized_) return;

    // Update the state transition matrix for constant acceleration (gravity)
    F_ = Eigen::Matrix<float, 6, 6>::Identity();
    F_(0, 3) = dt;
    F_(1, 4) = dt;
    F_(2, 5) = dt;
    F_(4, 4) = 1; // vy = vy_prev + g*dt

    // Control vector for gravity
    Eigen::Matrix<float, 6, 1> B;
    B.setZero();
    B(4) = 0.5f * gravity * dt * dt; // y = y_prev + vy*dt + 0.5*g*dt^2
    
    StateVector u;
    u.setZero();
    u(4) = gravity * dt; // Change in velocity due to gravity

    // Predict the next state
    x_ = F_ * x_;
    x_(4) += u(4); // Apply gravitational acceleration to vy
    x_(1) += 0.5 * u(4) * dt; // Apply gravitational acceleration to y

    P_ = F_ * P_ * F_.transpose() + Q_;
}


void KalmanFilter3D::update(const MeasurementVector& measurement) {
    if (!is_initialized_) {
        init(measurement);
        return;
    }

    // Measurement residual
    MeasurementVector y = measurement - H_ * x_;

    // Residual covariance
    Eigen::Matrix<float, 3, 3> S = H_ * P_ * H_.transpose() + R_;

    // Kalman gain
    Eigen::Matrix<float, 6, 3> K = P_ * H_.transpose() * S.inverse();

    // Update the state
    x_ = x_ + K * y;

    // Update the covariance
    P_ = (I_ - K * H_) * P_;
}

KalmanFilter3D::StateVector KalmanFilter3D::get_state() const {
    return x_;
}

Eigen::Vector3f KalmanFilter3D::get_position() const {
    return x_.head<3>();
}