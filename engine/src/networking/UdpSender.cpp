#include "UdpSender.hpp"
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

UdpSender::UdpSender() : sock_(-1) {
    // Default constructor, socket not yet connected.
}

UdpSender::UdpSender(const std::string& ip, int port) : sock_(-1) {
    connect(ip, port);
}

UdpSender::~UdpSender() {
    if (sock_ >= 0) {
        close(sock_);
    }
}

void UdpSender::connect(const std::string& ip, int port) {
    if (sock_ >= 0) {
        close(sock_); // Close existing socket if open
    }
    
    sock_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_ < 0) {
        std::cerr << "Failed to create socket" << std::endl;
        return;
    }

    server_addr_.sin_family = AF_INET;
    server_addr_.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &server_addr_.sin_addr);
}

bool UdpSender::send(const std::vector<unsigned char>& data) {
    if (sock_ < 0) {
        std::cerr << "UDP Sender not connected." << std::endl;
        return false;
    }

    ssize_t sent_bytes = sendto(sock_, data.data(), data.size(), 0,
                                (struct sockaddr*)&server_addr_, sizeof(server_addr_));
    if (sent_bytes < 0) {
        std::cerr << "Failed to send data" << std::endl;
        return false;
    }
    return true;
}

bool UdpSender::send_rgb(uint8_t r, uint8_t g, uint8_t b) {
    std::vector<unsigned char> packet;
    
    // Add header: byte(66), uint32(0), byte(0), uint16(0)
    packet.push_back(66);                    // byte: 66
    packet.push_back(0); packet.push_back(0); packet.push_back(0); packet.push_back(0); // uint32: 0 (big-endian)
    packet.push_back(0);                     // byte: 0
    packet.push_back(0); packet.push_back(0); // uint16: 0 (big-endian)
    
    // Add color data: 0x0a (color command), R, G, B
    packet.push_back(0x0a);
    packet.push_back(r);    // R
    packet.push_back(g);    // G
    packet.push_back(b);    // B

    return send(packet);
}