#pragma once

#include <string>
#include <memory>
#include <atomic>
#include <zmq.hpp>
#include "BallTracker.hpp"
#include "juggler.pb.h"

namespace juggler {

class Engine {
private:
    std::unique_ptr<BallTracker> ball_tracker_;
    zmq::context_t zmq_context_;
    zmq::socket_t zmq_socket_;
    std::atomic<bool> running_;
    std::string zmq_endpoint_;

public:
    explicit Engine(const std::string& zmq_endpoint = "tcp://*:5555");
    ~Engine();
    
    bool initialize();
    void run();
    void stop();
    
    // Command handling
    void handleCommand(const juggler::v1::EngineCommand& command);
    void sendResponse(const juggler::v1::EngineResponse& response);
    
    // Ball tracker access
    BallTracker* getBallTracker() { return ball_tracker_.get(); }
};

} // namespace juggler