#pragma once

#include "ModuleBase.hpp"
#include "IBallTracker.hpp"
#include <memory>
#include <thread>
#include <asio.hpp>

namespace juggler {
namespace modules {

class UdpBallSettingsModule : public ModuleBase {
public:
    UdpBallSettingsModule(std::shared_ptr<IBallTracker> tracker, bool* use_dnn_tracker_ptr = nullptr);
    ~UdpBallSettingsModule();

    // ModuleBase interface
    void setup() override;
    void update(const juggler::v1::FrameData&, const CommandCallback&) override;
    void cleanup() override;
    void processCommand(const juggler::v1::CommandRequest&) override;
    
    // Update the tracker pointer (used when switching trackers)
    void setTracker(std::shared_ptr<IBallTracker> tracker);

private:
    void UdpListen();

    std::shared_ptr<IBallTracker> tracker_;  // Pointer to tracker (works with both SimpleBallTracker and Simple2DBallTracker)
    bool* use_dnn_tracker_ptr_;  // Pointer to Engine's use_dnn_tracker_ flag

    std::unique_ptr<std::thread> listener_thread_;
    asio::io_context io_context_;
    asio::ip::udp::socket socket_;
    asio::ip::udp::endpoint remote_endpoint_;
    std::array<char, 1024> recv_buffer_;
    bool stop_listening_ = false;
};

}
}