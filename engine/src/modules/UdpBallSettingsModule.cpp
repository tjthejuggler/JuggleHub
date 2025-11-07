#include "UdpBallSettingsModule.hpp"
#include "CameraManager.hpp"
#include <iostream>
#include <sstream>

namespace juggler {
namespace modules {

UdpBallSettingsModule::UdpBallSettingsModule(std::shared_ptr<IBallTracker> tracker, bool* use_dnn_tracker_ptr, CameraManager* camera_manager)
    : tracker_(tracker),
      use_dnn_tracker_ptr_(use_dnn_tracker_ptr),
      camera_manager_(camera_manager),
      socket_(io_context_, asio::ip::udp::endpoint(asio::ip::udp::v4(), 12346)) {
}

UdpBallSettingsModule::~UdpBallSettingsModule() {
    cleanup();
}

void UdpBallSettingsModule::setup() {
    std::cout << "UdpBallSettingsModule initialized. Listening on port 12346..." << std::endl;
    stop_listening_ = false;
    listener_thread_ = std::make_unique<std::thread>(&UdpBallSettingsModule::UdpListen, this);
}

void UdpBallSettingsModule::update(const juggler::v1::FrameData&, const CommandCallback&) {
    // Nothing to do here
}

void UdpBallSettingsModule::cleanup() {
    stop_listening_ = true;
    io_context_.stop();
    if (listener_thread_ && listener_thread_->joinable()) {
        listener_thread_->join();
    }
}

void UdpBallSettingsModule::processCommand(const juggler::v1::CommandRequest&) {
    // Nothing to do here
}

void UdpBallSettingsModule::setTracker(std::shared_ptr<IBallTracker> tracker) {
    tracker_ = tracker;
    std::cout << "UdpBallSettingsModule: Tracker pointer updated" << std::endl;
}

void UdpBallSettingsModule::setCameraManager(CameraManager* camera_manager) {
    camera_manager_ = camera_manager;
    std::cout << "UdpBallSettingsModule: CameraManager pointer updated" << std::endl;
}

void UdpBallSettingsModule::UdpListen() {
    while (!stop_listening_) {
        try {
            socket_.async_receive_from(
                asio::buffer(recv_buffer_), remote_endpoint_,
                [this](std::error_code ec, std::size_t bytes_recvd) {
                    if (!ec && bytes_recvd > 0) {
                        std::string message(recv_buffer_.data(), bytes_recvd);
                        std::cout << "Received settings message: " << message << std::endl;
                        
                        std::istringstream iss(message);
                        std::string key, value;
                        if (std::getline(iss, key, '=') && std::getline(iss, value)) {
                            // Handle camera_auto_exposure - forward to CameraManager
                            if (key == "camera_auto_exposure" && camera_manager_) {
                                try {
                                    bool enabled = (value == "1" || value == "true");
                                    camera_manager_->setAutoExposure(enabled);
                                    std::cout << "✅ Camera auto exposure " << (enabled ? "enabled" : "disabled") << std::endl;
                                } catch (const std::exception& e) {
                                    std::cerr << "❌ Error setting camera auto exposure: " << e.what() << std::endl;
                                }
                            }
                            // Handle camera_exposure - forward to CameraManager
                            else if (key == "camera_exposure" && camera_manager_) {
                                try {
                                    int exposure = std::stoi(value);
                                    camera_manager_->setExposure(exposure);
                                    std::cout << "✅ Camera exposure set to " << exposure << " microseconds" << std::endl;
                                } catch (const std::exception& e) {
                                    std::cerr << "❌ Error setting camera exposure: " << e.what() << std::endl;
                                }
                            }
                            // Handle camera_gain - forward to CameraManager
                            else if (key == "camera_gain" && camera_manager_) {
                                try {
                                    int gain = std::stoi(value);
                                    camera_manager_->setGain(gain);
                                    std::cout << "✅ Camera gain set to " << gain << std::endl;
                                } catch (const std::exception& e) {
                                    std::cerr << "❌ Error setting camera gain: " << e.what() << std::endl;
                                }
                            }
                            // Handle camera_auto_white_balance - forward to CameraManager
                            else if (key == "camera_auto_white_balance" && camera_manager_) {
                                try {
                                    bool enabled = (value == "1" || value == "true");
                                    camera_manager_->setAutoWhiteBalance(enabled);
                                    std::cout << "✅ Camera auto white balance " << (enabled ? "enabled" : "disabled") << std::endl;
                                } catch (const std::exception& e) {
                                    std::cerr << "❌ Error setting camera auto white balance: " << e.what() << std::endl;
                                }
                            }
                            // Handle camera_white_balance - forward to CameraManager
                            else if (key == "camera_white_balance" && camera_manager_) {
                                try {
                                    int white_balance = std::stoi(value);
                                    camera_manager_->setWhiteBalance(white_balance);
                                    std::cout << "✅ Camera white balance set to " << white_balance << " K" << std::endl;
                                } catch (const std::exception& e) {
                                    std::cerr << "❌ Error setting camera white balance: " << e.what() << std::endl;
                                }
                            }
                            // Handle enable_ball_detection - forward to tracker to control ball inference
                            else if (key == "enable_ball_detection" && tracker_) {
                                tracker_->updateSetting(key, value);
                                bool enabled = (value == "1" || value == "true");
                                std::cout << "✅ YOLO ball detection " << (enabled ? "enabled" : "disabled") << std::endl;
                            }
                            // Handle legacy use_dnn_tracker setting (kept for backward compatibility)
                            else if (key == "use_dnn_tracker" && use_dnn_tracker_ptr_) {
                                bool enabled = (value == "1" || value == "true");
                                *use_dnn_tracker_ptr_ = enabled;
                                std::cout << "✅ YOLO tracker " << (enabled ? "enabled" : "disabled") << std::endl;
                            }
                            // Forward other settings to tracker
                            else if (tracker_) {
                                tracker_->updateSetting(key, value);
                            }
                        } else {
                            std::cerr << "Warning: Malformed settings message received." << std::endl;
                        }
                    }
                });
            io_context_.run();
            io_context_.restart();
        } catch (const std::exception& e) {
            std::cerr << "Error in UDP listener: " << e.what() << std::endl;
        }
    }
    std::cout << "UDP listener thread stopped." << std::endl;
}

} 
}