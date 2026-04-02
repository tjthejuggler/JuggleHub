#include "Engine.hpp"
#include <iostream>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <signal.h>
#include <execinfo.h>
#include <unistd.h>

// Global flag to control debug logging
bool g_enable_debug_log = false;

// Global debug log file for engine_debug.log (not GPU_debug.log)
std::ofstream g_engine_debug_log;

void writeDebugLog(const std::string& message) {
    // Only write if debug logging is enabled AND file is open
    if (g_enable_debug_log && g_engine_debug_log.is_open()) {
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        auto timer = std::chrono::system_clock::to_time_t(now);
        std::tm bt = *std::localtime(&timer);
        
        g_engine_debug_log << std::put_time(&bt, "%H:%M:%S") << "."
                        << std::setfill('0') << std::setw(3) << ms.count()
                        << " | " << message << std::endl;
        g_engine_debug_log.flush();
    }
}

void signalHandler(int sig) {
    writeDebugLog("=== SIGNAL CAUGHT: " + std::to_string(sig) + " ===");
    
    void* array[20];
    size_t size = backtrace(array, 20);
    
    writeDebugLog("Stack trace:");
    char** strings = backtrace_symbols(array, size);
    for (size_t i = 0; i < size; i++) {
        writeDebugLog(std::string(strings[i]));
    }
    free(strings);
    
    g_engine_debug_log.close();
    exit(1);
}

int main(int argc, char* argv[]) {
    // Install signal handlers
    signal(SIGSEGV, signalHandler);  // Segmentation fault
    signal(SIGABRT, signalHandler);  // Abort
    signal(SIGFPE, signalHandler);   // Floating point exception
    signal(SIGILL, signalHandler);   // Illegal instruction
    
    Engine::OutputFormat format = Engine::OutputFormat::DEFAULT;
    bool use_dnn_tracker = false;
    bool verbose = false;
    bool simple_tracking = true;  // Default to simple tracking (depth+color)
    std::string device_name = "CPU"; // Default to CPU
    std::string camera_settings_path = ""; // Path to camera settings JSON file
    std::string model_name = "yolo11n"; // Default to yolo11n
    std::string pose_model_name = "yolo11n-pose"; // Default to yolo11n-pose

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help") {
            std::cout << "JuggleHub Engine" << std::endl;
            std::cout << "  --simple-tracking     Use depth+color ball tracking (DEFAULT, no YOLO ball model)" << std::endl;
            std::cout << "  --yolo-tracking       Use YOLO ball detection model (loads all trackers)" << std::endl;
            std::cout << "  --device=<device>     OpenVINO device (CPU, GPU, NPU, AUTO)" << std::endl;
            std::cout << "  --model=<name>        YOLO ball model name (default: yolo11n)" << std::endl;
            std::cout << "  --pose-model=<name>   YOLO pose model name (default: yolo11n-pose)" << std::endl;
            std::cout << "  --camera-settings=<f> Camera settings JSON file path" << std::endl;
            std::cout << "  --debug-log           Enable debug logging to engine_debug.log" << std::endl;
            std::cout << "  --verbose             Enable verbose output" << std::endl;
            return EXIT_SUCCESS;
        } else if (arg == "--output-format=simple") {
            format = Engine::OutputFormat::SIMPLE;
        } else if (arg == "--output-format=legacy") {
            format = Engine::OutputFormat::LEGACY;
        } else if (arg == "--use-dnn-tracker" || arg == "--yolo-tracking") {
            use_dnn_tracker = true;
            simple_tracking = false;
        } else if (arg == "--simple-tracking") {
            simple_tracking = true;
        } else if (arg == "--verbose") {
            verbose = true;
        } else if (arg == "--debug-log") {
            g_enable_debug_log = true;
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
    
    // Only open debug log file if debug logging is enabled
    if (g_enable_debug_log) {
        g_engine_debug_log.open("engine_debug.log", std::ios::out | std::ios::trunc);
        if (!g_engine_debug_log.is_open()) {
            std::cerr << "Failed to open engine_debug.log" << std::endl;
            return EXIT_FAILURE;
        }
        
        writeDebugLog("=== ENGINE STARTUP ===");
        writeDebugLog("Parsing command line arguments...");
        writeDebugLog("Command line arguments parsed successfully");
        writeDebugLog("Device: " + device_name);
        writeDebugLog("Model: " + model_name);
        writeDebugLog("Pose Model: " + pose_model_name);
        writeDebugLog("Camera Settings: " + (camera_settings_path.empty() ? "none" : camera_settings_path));
        writeDebugLog("Use DNN Tracker: " + std::string(use_dnn_tracker ? "true" : "false"));
        writeDebugLog("Verbose: " + std::string(verbose ? "true" : "false"));
        writeDebugLog("Simple Tracking: " + std::string(simple_tracking ? "true" : "false"));
        
        writeDebugLog("=== ENGINE STARTED ===");
        writeDebugLog("Build: 3D MATCHING - 2025-10-03");
        writeDebugLog("╔════════════════════════════════════════╗");
        writeDebugLog("║  ENGINE WITH 3D MATCHING - BUILD 2025 ║");
        writeDebugLog("║  If you see this, new code is loaded  ║");
        writeDebugLog("╚════════════════════════════════════════╝");
    }

    try {
        if (g_enable_debug_log) {
            writeDebugLog("Creating Engine instance...");
        }
        Engine engine(camera_settings_path, device_name, model_name, pose_model_name, format, use_dnn_tracker, verbose, simple_tracking);
        if (g_enable_debug_log) {
            writeDebugLog("Engine instance created successfully");
            writeDebugLog("Starting engine.run()...");
        }
        engine.run();
        if (g_enable_debug_log) {
            writeDebugLog("engine.run() completed normally");
        }
    } catch (const std::exception& e) {
        if (g_enable_debug_log) {
            writeDebugLog("EXCEPTION CAUGHT: " + std::string(e.what()));
            g_engine_debug_log.close();
        }
        return EXIT_FAILURE;
    } catch (...) {
        if (g_enable_debug_log) {
            writeDebugLog("UNKNOWN EXCEPTION CAUGHT");
            g_engine_debug_log.close();
        }
        return EXIT_FAILURE;
    }
    
    if (g_enable_debug_log) {
        writeDebugLog("=== ENGINE SHUTDOWN ===");
        g_engine_debug_log.close();
    }
    return EXIT_SUCCESS;
}