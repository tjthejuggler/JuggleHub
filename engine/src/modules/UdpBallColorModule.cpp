#include "UdpBallColorModule.hpp"
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>

UdpBallColorModule::UdpBallColorModule() = default;

void UdpBallColorModule::setup() {
    // For now, we'll hardcode the IP and port.
    // This should be made configurable later.
    udp_sender_ = std::make_unique<UdpSender>("10.54.136.255", 41412);
    std::cout << "UdpBallColorModule setup" << std::endl;
}

void UdpBallColorModule::update(const juggler::v1::FrameData& frame_data) {
    for (const auto& ball : frame_data.balls()) {
        // Construct the UDP packet to send a green color
        // Create UDP header using proper struct packing (matches working Python code)
        std::vector<unsigned char> packet;
        
        // Add header: byte(66), uint32(0), byte(0), uint16(0)
        packet.push_back(66);                    // byte: 66
        packet.push_back(0); packet.push_back(0); packet.push_back(0); packet.push_back(0); // uint32: 0 (big-endian)
        packet.push_back(0);                     // byte: 0
        packet.push_back(0); packet.push_back(0); // uint16: 0 (big-endian)
        
        // Add color data for blue: struct.pack("!BBBB", 0x0a, 0, 0, 255)
        packet.push_back(0x0a);
        packet.push_back(0);    // R
        packet.push_back(0);    // G
        packet.push_back(255);  // B

        // The ball ID is assumed to be the last octet of the IP address.
        // This is a simplification and may need to be revisited.
        std::string ball_ip = "10.54.136." + ball.id();
        
        // The UdpSender needs to be re-initialized for each ball's IP.
        // This is not ideal, but it's a start.
        // Send brightness command FIRST (like in working Python code)
        // Python: brightness_data = struct.pack("!BB", 0x10, 7)
        std::vector<unsigned char> brightness_packet;
        
        // Add header: byte(66), uint32(0), byte(0), uint16(0)
        brightness_packet.push_back(66);                    // byte: 66
        brightness_packet.push_back(0); brightness_packet.push_back(0); brightness_packet.push_back(0); brightness_packet.push_back(0); // uint32: 0
        brightness_packet.push_back(0);                     // byte: 0
        brightness_packet.push_back(0); brightness_packet.push_back(0); // uint16: 0
        
        // Add brightness data: 0x10 (brightness command), 7 (brightness level)
        brightness_packet.push_back(0x10);
        brightness_packet.push_back(7);  // Maximum brightness level
        
        // Send brightness first with separate sender (like Python code)
        UdpSender brightness_sender(ball_ip, 41412);
        brightness_sender.send(brightness_packet);
        
        // Small delay between commands (like Python code has time.sleep(1))
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // Then send color command with separate sender
        UdpSender color_sender(ball_ip, 41412);
        color_sender.send(packet);
    }
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