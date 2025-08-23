#pragma once

#include "ModuleBase.hpp"
#include <string> // Required for std::string

class PositionToRgbModule : public ModuleBase {
public:
    PositionToRgbModule();
    
    void setup() override;
    void update(const juggler::v1::FrameData& frame_data, const CommandCallback& command_callback) override;
    void cleanup() override;
    void processCommand(const juggler::v1::CommandRequest& command) override;

private:
    std::string target_ball_id_; // Stores the ID of the LED ball to control
};