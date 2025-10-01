#pragma once

#include <iostream>
#include <string>
#include <cstdlib>

namespace juggler {

/**
 * Debug logging utility that respects the JUGGLEHUB_DEBUG environment variable.
 * Set JUGGLEHUB_DEBUG=1 to enable debug logging.
 */
class DebugLog {
public:
    static bool isDebugEnabled() {
        static bool initialized = false;
        static bool debug_enabled = false;
        
        if (!initialized) {
            const char* env_debug = std::getenv("JUGGLEHUB_DEBUG");
            debug_enabled = (env_debug != nullptr && std::string(env_debug) == "1");
            initialized = true;
            
            if (debug_enabled) {
                std::cout << "🐛 Debug logging enabled (JUGGLEHUB_DEBUG=1)" << std::endl;
            }
        }
        
        return debug_enabled;
    }
    
    // Debug output - only prints if debug is enabled
    template<typename... Args>
    static void debug(Args&&... args) {
        if (isDebugEnabled()) {
            (std::cout << ... << args) << std::endl;
        }
    }
    
    // Info output - always prints (for important user-facing messages)
    template<typename... Args>
    static void info(Args&&... args) {
        (std::cout << ... << args) << std::endl;
    }
    
    // Warning output - always prints
    template<typename... Args>
    static void warn(Args&&... args) {
        (std::cerr << ... << args) << std::endl;
    }
    
    // Error output - always prints
    template<typename... Args>
    static void error(Args&&... args) {
        (std::cerr << ... << args) << std::endl;
    }
};

// Convenience macros
#define DEBUG_LOG(...) juggler::DebugLog::debug(__VA_ARGS__)
#define INFO_LOG(...) juggler::DebugLog::info(__VA_ARGS__)
#define WARN_LOG(...) juggler::DebugLog::warn(__VA_ARGS__)
#define ERROR_LOG(...) juggler::DebugLog::error(__VA_ARGS__)

} // namespace juggler