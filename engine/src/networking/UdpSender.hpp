#pragma once

#include <string>
#include <vector>
#include <netinet/in.h>

class UdpSender {
public:
    UdpSender(); // Default constructor
    UdpSender(const std::string& ip, int port);
    ~UdpSender();

    void connect(const std::string& ip, int port);

    bool send(const std::vector<unsigned char>& data);
    bool send_rgb(uint8_t r, uint8_t g, uint8_t b);

private:
    int sock_;
    struct sockaddr_in server_addr_;
};