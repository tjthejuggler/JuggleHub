#include "PositionToRgbModule.hpp"
#include <vector>
#include <algorithm> // For std::clamp
#include <iostream>

PositionToRgbModule::PositionToRgbModule() : target_ball_id_("201") {} // Default to ball 201

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

    // --- Coordinate to RGB Mapping Logic ---
    // The raw 3D position values (x, y, z) are in meters, relative to the camera.
    // We need to map these to an RGB range (0-255).
    //
    // Current assumption: The relevant juggling space for X, Y, Z is roughly between
    // -0.5 meters and +0.5 meters.
    //
    // To map this range [-0.5, 0.5] to [0, 1]: (value + 0.5)
    // Then to map [0, 1] to [0, 255]: * 255.0
    //
    // NOTE: These ranges (-0.5 to 0.5) are an initial estimate and may need to be
    // calibrated or adjusted based on the actual physical juggling volume.
    // If the ball goes outside this range, the color will be clamped to 0 or 255.

    double raw_x = green_ball->position_3d().x();
    double raw_y = green_ball->position_3d().y();
    double raw_z = green_ball->position_3d().z();

    // Map to [0, 1] range for 0-255 scaling
    double norm_x = (raw_x + 0.5); // Example: -0.5 -> 0, 0.5 -> 1
    double norm_y = (raw_y + 0.5);
    double norm_z = (raw_z + 0.5);
    
    // Clamp to [0, 1] in case raw coordinates are outside the assumed range
    norm_x = std::clamp(norm_x, 0.0, 1.0);
    norm_y = std::clamp(norm_y, 0.0, 1.0);
    norm_z = std::clamp(norm_z, 0.0, 1.0);

    // Scale to [0, 255] for RGB components
    uint8_t r = static_cast<uint8_t>(norm_x * 255.0);
    uint8_t g = static_cast<uint8_t>(norm_y * 255.0);
    uint8_t b = static_cast<uint8_t>(norm_z * 255.0);

    // --- Send Color Command ---
    juggler::v1::CommandRequest command;
    command.set_type(juggler::v1::CommandRequest::SEND_COLOR_COMMAND);
    auto* color_command = command.mutable_color_command();
    
    // Use the configurable target_ball_id_
    color_command->set_ball_id(target_ball_id_);
    
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
    if (command.type() == juggler::v1::CommandRequest::CONFIGURE_MODULE) {
        // Look for "target_ball_id" argument
        auto it = command.module_args().find("target_ball_id");
        if (it != command.module_args().end()) {
            target_ball_id_ = it->second;
            std::cout << "PositionToRgbModule configured with target_ball_id: " << target_ball_id_ << std::endl;
        } else {
            std::cout << "PositionToRgbModule received CONFIGURE_MODULE command without 'target_ball_id' argument." << std::endl;
        }
    }
}