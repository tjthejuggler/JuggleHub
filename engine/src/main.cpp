#include "Engine.hpp"
#include <iostream>
#include <fstream>

int main(int argc, char* argv[]) {
    // Create debug log file at startup
    std::ofstream debug_log("engine_debug.log", std::ios::out | std::ios::trunc);
    debug_log << "=== ENGINE STARTED ===" << std::endl;
    debug_log << "Build: 3D MATCHING - 2025-10-03" << std::endl;
    debug_log << "╔════════════════════════════════════════╗" << std::endl;
    debug_log << "║  ENGINE WITH 3D MATCHING - BUILD 2025 ║" << std::endl;
    debug_log << "║  If you see this, new code is loaded  ║" << std::endl;
    debug_log << "╚════════════════════════════════════════╝" << std::endl;
    debug_log.close();

    Engine::OutputFormat format = Engine::OutputFormat::DEFAULT;
    bool use_dnn_tracker = false;
    bool verbose = false;
    std::string device_name = "CPU"; // Default to CPU
    std::string camera_settings_path = ""; // Path to camera settings JSON file
    std::string model_name = "yolo11n"; // Default to yolo11n
    std::string pose_model_name = "yolo11n-pose"; // Default to yolo11n-pose

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
            std::cout << "Options:" << std::endl;
            std::cout << "  --help                        Show this help message" << std::endl;
            std::cout << "  --use-dnn-tracker             Enable the DNN tracker" << std::endl;
            std::cout << "  --verbose                     Enable verbose output" << std::endl;
            std::cout << "  --device=<device>             Set the inference device (e.g., CPU, GPU, NPU, AUTO)" << std::endl;
            std::cout << "  --camera-settings=<path>      Path to camera settings JSON file" << std::endl;
            std::cout << "  --model=<model_name>          Specify the model name (e.g., yolo11s)" << std::endl;
            std::cout << "  --pose-model=<model_name>     Specify the pose model name (e.g., yolo-pose)" << std::endl;
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
        } else if (arg.rfind("--camera-settings=", 0) == 0) {
            camera_settings_path = arg.substr(18);
        } else if (arg.rfind("--model=", 0) == 0) {
            model_name = arg.substr(8);
        } else if (arg.rfind("--pose-model=", 0) == 0) {
            pose_model_name = arg.substr(13);
        }
    }

    try {
        Engine engine(camera_settings_path, device_name, model_name, pose_model_name, format, use_dnn_tracker, verbose);
        engine.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}