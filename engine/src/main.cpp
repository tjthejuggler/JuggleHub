#include "Engine.hpp"
#include <iostream>

int main(int argc, char* argv[]) {
    Engine::OutputFormat format = Engine::OutputFormat::DEFAULT;
    bool use_dnn_tracker = false;
    bool verbose = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--output-format=simple") {
            format = Engine::OutputFormat::SIMPLE;
        } else if (arg == "--output-format=legacy") {
            format = Engine::OutputFormat::LEGACY;
        } else if (arg == "--use-dnn-tracker") {
            use_dnn_tracker = true;
        } else if (arg == "--verbose") {
            verbose = true;
        }
    }

    try {
        Engine engine("config.json", format, use_dnn_tracker, verbose);
        engine.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}