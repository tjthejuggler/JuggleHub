#include "UdpSender.hpp"
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

UdpSender::UdpSender(const std::string& ip, int port) {
    sock_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_ < 0) {
        std::cerr << "Failed to create socket" << std::endl;
    }

    server_addr_.sin_family = AF_INET;
    server_addr_.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &server_addr_.sin_addr);
}

UdpSender::~UdpSender() {
    close(sock_);
}

bool UdpSender::send(const std::vector<unsigned char>& data) {
    ssize_t sent_bytes = sendto(sock_, data.data(), data.size(), 0,
                                (struct sockaddr*)&server_addr_, sizeof(server_addr_));
    if (sent_bytes < 0) {
        std::cerr << "Failed to send data" << std::endl;
        return false;
    }
    return true;
}