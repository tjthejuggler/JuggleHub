#pragma once

// Global flag to control debug logging
// Set via --debug-log command line flag in main.cpp
extern bool g_enable_debug_log;

// Macro to conditionally write to debug log
#define DEBUG_LOG_WRITE(code) \
    do { \
        if (g_enable_debug_log) { \
            code \
        } \
    } while(0)
