#include "Engine.hpp"
#include <iostream>

int main(int argc, char* argv[]) {
    Engine::OutputFormat format = Engine::OutputFormat::DEFAULT;
    if (argc > 1) {
        std::string arg = argv[1];
        if (arg == "--output-format=simple") {
            format = Engine::OutputFormat::SIMPLE;
        } else if (arg == "--output-format=legacy") {
            format = Engine::OutputFormat::LEGACY;
        }
    }

    try {
        Engine engine("config.json", format);
        engine.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}