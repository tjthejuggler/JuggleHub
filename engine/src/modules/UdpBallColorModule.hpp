#pragma once

#include "ModuleBase.hpp"
#include "../networking/UdpSender.hpp"
#include <memory>

class UdpBallColorModule : public ModuleBase {
public:
    UdpBallColorModule();
    void setup() override;
    void update(const juggler::v1::FrameData& frame_data) override;
    void cleanup() override;
    void processCommand(const juggler::v1::CommandRequest& command) override;

private:
    std::unique_ptr<UdpSender> udp_sender_;
};