#include "PositionToRgbModule.hpp"
#include <vector>
#include <algorithm> // For std::clamp
#include <iostream>

PositionToRgbModule::PositionToRgbModule() = default;

void PositionToRgbModule::setup() {
    std::cout << "PositionToRgbModule setup complete." << std::endl;
}

void PositionToRgbModule::update(const juggler::v1::FrameData& frame_data, const CommandCallback& command_callback) {
        const juggler::v1::Ball* green_ball = nullptr;
        for (const auto& ball : frame_data.balls()) {
            if (ball.color_name() == "green") {
                green_ball = &ball;
                break;
            }
        }

    if (!green_ball) {
        return;
    }

    double norm_x = (green_ball->position_3d().x() + 0.5);
    double norm_y = (green_ball->position_3d().y() + 0.5);
    double norm_z = (green_ball->position_3d().z() + 0.5);

    uint8_t r = static_cast<uint8_t>(std::clamp(norm_x * 255.0, 0.0, 255.0));
    uint8_t g = static_cast<uint8_t>(std::clamp(norm_y * 255.0, 0.0, 255.0));
    uint8_t b = static_cast<uint8_t>(std::clamp(norm_z * 255.0, 0.0, 255.0));

    juggler::v1::CommandRequest command;
    command.set_type(juggler::v1::CommandRequest::SEND_COLOR_COMMAND);
    auto* color_command = command.mutable_color_command();
    color_command->set_ball_id("all");
    auto* color = color_command->mutable_color();
    color->set_r(r);
    color->set_g(g);
    color->set_b(b);

    if (command_callback) {
        command_callback(command);
    }
}

void PositionToRgbModule::cleanup() {
    // Since we are no longer managing UDP directly,
    // we don't send a black color on cleanup.
    // The controlling module (UdpBallColorModule) should handle this.
    std::cout << "PositionToRgbModule cleaned up." << std::endl;
}

void PositionToRgbModule::processCommand(const juggler::v1::CommandRequest& command) {
    // This module doesn't process any specific commands yet.
}