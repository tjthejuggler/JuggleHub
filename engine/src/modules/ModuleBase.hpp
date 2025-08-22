#pragma once

#include "juggler.pb.h"

class ModuleBase {
public:
    virtual ~ModuleBase() = default;
    virtual void setup() = 0;
    virtual void update(const juggler::v1::FrameData& frame_data) = 0;
    virtual void cleanup() = 0;
    virtual void processCommand(const juggler::v1::CommandRequest& command) = 0;
};