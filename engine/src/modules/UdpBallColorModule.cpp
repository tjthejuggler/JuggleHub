#include "UdpBallColorModule.hpp"
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>

UdpBallColorModule::UdpBallColorModule() = default;

void UdpBallColorModule::setup() {
    udp_sender_ = std::make_unique<UdpSender>("10.54.136.205", 41412); // Use correct IP
    std::cout << "UdpBallColorModule setup" << std::endl;
}

void UdpBallColorModule::update(const juggler::v1::FrameData& frame_data, const CommandCallback& command_callback) {
    // This module doesn't do anything in the update loop.
    // It only responds to commands.
}

void UdpBallColorModule::cleanup() {
    std::cout << "UdpBallColorModule cleanup" << std::endl;
}

void UdpBallColorModule::processCommand(const juggler::v1::CommandRequest& command) {
    if (command.type() == juggler::v1::CommandRequest::SEND_COLOR_COMMAND) {
        const auto& color_command = command.color_command();
        std::string ball_ip = "10.54.136." + color_command.ball_id();
        
        // Create UDP header using proper struct packing (matches working Python code)
        // Python: struct.pack("!bIBH", 66, 0, 0, 0)
        std::vector<unsigned char> packet;
        
        // Add header: byte(66), uint32(0), byte(0), uint16(0)
        packet.push_back(66);                    // byte: 66
        packet.push_back(0); packet.push_back(0); packet.push_back(0); packet.push_back(0); // uint32: 0 (big-endian)
        packet.push_back(0);                     // byte: 0
        packet.push_back(0); packet.push_back(0); // uint16: 0 (big-endian)
        
        // Add color data: struct.pack("!BBBB", 0x0a, R, G, B)
        packet.push_back(0x0a);
        packet.push_back(static_cast<unsigned char>(color_command.color().r()));
        packet.push_back(static_cast<unsigned char>(color_command.color().g()));
        packet.push_back(static_cast<unsigned char>(color_command.color().b()));

        UdpSender sender(ball_ip, 41412);
        sender.send(packet);
    }
}