#include "Engine.hpp"
#include <iostream>

int main(int argc, char* argv[]) {
    Engine::OutputFormat format = Engine::OutputFormat::DEFAULT;
    bool use_dnn_tracker = false;
    bool verbose = false;
    std::string device_name = "CPU"; // Default to CPU

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
            std::cout << "Options:" << std::endl;
            std::cout << "  --help                  Show this help message" << std::endl;
            std::cout << "  --use-dnn-tracker       Enable the DNN tracker" << std::endl;
            std::cout << "  --verbose               Enable verbose output" << std::endl;
            std::cout << "  --device=<device>       Set the inference device (e.g., CPU, GPU, NPU, AUTO)" << std::endl;
            return EXIT_SUCCESS;
        } else if (arg == "--output-format=simple") {
            format = Engine::OutputFormat::SIMPLE;
        } else if (arg == "--output-format=legacy") {
            format = Engine::OutputFormat::LEGACY;
        } else if (arg == "--use-dnn-tracker") {
            use_dnn_tracker = true;
        } else if (arg == "--verbose") {
            verbose = true;
        } else if (arg.rfind("--device=", 0) == 0) {
            device_name = arg.substr(9);
        }
    }

    try {
        Engine engine("config.json", device_name, format, use_dnn_tracker, verbose);
        engine.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}