#pragma once

#include "juggler.pb.h"
#include <functional>

using CommandCallback = std::function<void(const juggler::v1::CommandRequest&)>;

class ModuleBase {
public:
    virtual ~ModuleBase() = default;
    virtual void setup() = 0;
    virtual void update(const juggler::v1::FrameData& frame_data, const CommandCallback& command_callback) = 0;
    virtual void cleanup() = 0;
    virtual void processCommand(const juggler::v1::CommandRequest& command) = 0;
};