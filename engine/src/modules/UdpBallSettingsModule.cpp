#include "UdpBallSettingsModule.hpp"
#include <iostream>
#include <sstream>

namespace juggler {
namespace modules {

UdpBallSettingsModule::UdpBallSettingsModule(std::shared_ptr<SimpleBallTracker> simple_tracker, bool* use_dnn_tracker_ptr)
    : simple_tracker_(simple_tracker),
      use_dnn_tracker_ptr_(use_dnn_tracker_ptr),
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
                            // Handle use_dnn_tracker setting
                            if (key == "use_dnn_tracker" && use_dnn_tracker_ptr_) {
                                bool enabled = (value == "1" || value == "true");
                                *use_dnn_tracker_ptr_ = enabled;
                                std::cout << "✅ YOLO ball detection " << (enabled ? "enabled" : "disabled") << std::endl;
                            }
                            // Forward other settings to tracker
                            else if (simple_tracker_) {
                                simple_tracker_->updateSetting(key, value);
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