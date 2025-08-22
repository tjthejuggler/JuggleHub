#pragma once

#include <string>
#include <vector>
#include <netinet/in.h>

class UdpSender {
public:
    UdpSender(const std::string& ip, int port);
    ~UdpSender();

    bool send(const std::vector<unsigned char>& data);

private:
    int sock_;
    struct sockaddr_in server_addr_;
};