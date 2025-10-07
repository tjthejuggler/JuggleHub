#pragma once

#include "ModuleBase.hpp"
#include "SimpleBallTracker.hpp"
#include <memory>
#include <thread>
#include <asio.hpp>

namespace juggler {
namespace modules {

class UdpBallSettingsModule : public ModuleBase {
public:
    UdpBallSettingsModule(std::shared_ptr<SimpleBallTracker> simple_tracker);
    ~UdpBallSettingsModule();

    // ModuleBase interface
    void setup() override;
    void update(const juggler::v1::FrameData&, const CommandCallback&) override;
    void cleanup() override;
    void processCommand(const juggler::v1::CommandRequest&) override;

private:
    void UdpListen();

    std::shared_ptr<SimpleBallTracker> simple_tracker_;

    std::unique_ptr<std::thread> listener_thread_;
    asio::io_context io_context_;
    asio::ip::udp::socket socket_;
    asio::ip::udp::endpoint remote_endpoint_;
    std::array<char, 1024> recv_buffer_;
    bool stop_listening_ = false;
};

}
}