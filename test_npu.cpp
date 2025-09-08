#include <openvino/openvino.hpp>
#include <iostream>
#include <exception>

int main() {
    try {
        std::cout << "=== OpenVINO NPU Test ===" << std::endl;
        
        // Initialize OpenVINO Core
        ov::Core core;
        
        // List all available devices
        std::cout << "Available devices:" << std::endl;
        auto devices = core.get_available_devices();
        bool npu_found = false;
        
        for (const auto& device : devices) {
            std::cout << "  - " << device << std::endl;
            if (device == "NPU" || device.find("NPU") != std::string::npos) {
                npu_found = true;
            }
        }
        
        if (!npu_found) {
            std::cout << "❌ NPU not found in available devices" << std::endl;
            
            // Try to manually register NPU plugin
            std::cout << "Attempting to manually register NPU plugin..." << std::endl;
            try {
                core.register_plugin("/opt/intel/openvino_2025.2.0/runtime/lib/intel64/libopenvino_intel_npu_plugin.so", "NPU");
                std::cout << "✅ NPU plugin registered manually" << std::endl;
                
                // Check devices again
                auto new_devices = core.get_available_devices();
                std::cout << "Devices after manual registration:" << std::endl;
                for (const auto& device : new_devices) {
                    std::cout << "  - " << device << std::endl;
                    if (device == "NPU" || device.find("NPU") != std::string::npos) {
                        npu_found = true;
                    }
                }
            } catch (const std::exception& e) {
                std::cout << "❌ Failed to register NPU plugin: " << e.what() << std::endl;
            }
        }
        
        if (npu_found) {
            std::cout << "✅ NPU device found!" << std::endl;
            
            // Test NPU compilation with your model
            std::cout << "Testing NPU compilation with yolo11n.xml..." << std::endl;
            try {
                auto model = core.read_model("engine/models/yolo11n.xml");
                auto compiled_model = core.compile_model(model, "NPU");
                std::cout << "✅ NPU compilation successful!" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "❌ NPU compilation failed: " << e.what() << std::endl;
            }
        } else {
            std::cout << "❌ NPU device still not available" << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cout << "❌ Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}