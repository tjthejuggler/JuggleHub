#include "UdpBallSettingsModule.hpp"
#include <iostream>
#include <sstream>

namespace juggler {
namespace modules {

UdpBallSettingsModule::UdpBallSettingsModule(std::shared_ptr<SimpleBallTracker> simple_tracker)
    : simple_tracker_(simple_tracker),
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
                            if (simple_tracker_) {
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