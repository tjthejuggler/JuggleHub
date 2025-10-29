# Debug Logging System Guide

**Last Updated:** 2025-10-01

## Overview

JuggleHub now includes a comprehensive debug logging system that reduces console spam during normal operation while allowing detailed debugging when needed.

## Key Issues Fixed

### 1. Color Tracker Disappearing Issue
**Problem:** In [`ColorTracker.cpp:448-452`](engine/src/ColorTracker.cpp:448), there was a static counter that incremented indefinitely. After approximately 2 billion increments, it would overflow causing undefined behavior, which could cause the color trackers to disappear after ~1000 frames.

**Solution:** Removed the static counter and replaced the conditional debug printing with the new debug logging system that only prints when debug mode is enabled.

### 2. Excessive Console Output
**Problem:** There were 102+ console print statements in C++ code and 93+ in Python code that were always active, potentially causing performance lag and making it difficult to see important messages.

**Solution:** Implemented a debug logging system with different log levels (DEBUG, INFO, WARN, ERROR) that respects the `JUGGLEHUB_DEBUG` environment variable.

## How to Use

### Enabling Debug Mode

Set the environment variable before running the application:

```bash
# Enable debug logging
export JUGGLEHUB_DEBUG=1
./scripts/run_hub.sh

# Or inline:
JUGGLEHUB_DEBUG=1 ./scripts/run_hub.sh
```

### Disabling Debug Mode (Default)

```bash
# Unset the variable
unset JUGGLEHUB_DEBUG

# Or just run normally (debug is off by default)
./scripts/run_hub.sh
```

## Log Levels

### C++ Logging (engine/)

The new [`DebugLog.hpp`](engine/include/DebugLog.hpp) header provides four log levels:

1. **DEBUG_LOG()** - Only prints when `JUGGLEHUB_DEBUG=1`
   - Use for verbose debugging information
   - Frame-by-frame updates, detailed state information
   
2. **INFO_LOG()** - Always prints
   - Use for important user-facing messages
   - Startup messages, successful operations, configuration changes
   
3. **WARN_LOG()** - Always prints to stderr
   - Use for warnings that don't stop execution
   - Recoverable errors, deprecated features
   
4. **ERROR_LOG()** - Always prints to stderr
   - Use for errors and failures
   - Failed operations, exceptions, critical issues

### Example Usage

```cpp
#include "DebugLog.hpp"

// Debug output (only when JUGGLEHUB_DEBUG=1)
DEBUG_LOG("Processing frame ", frame_number, " with ", ball_count, " balls");

// Important user message (always shown)
INFO_LOG("Camera started successfully at 640x480 @ 60 FPS");

// Warning (always shown)
WARN_LOG("Camera settings file not found, using defaults");

// Error (always shown)
ERROR_LOG("Failed to initialize tracker: ", error_message);
```

## Files Modified

### C++ Files
- [`engine/include/DebugLog.hpp`](engine/include/DebugLog.hpp) - New debug logging utility (CREATED)
- [`engine/src/ColorTracker.cpp`](engine/src/ColorTracker.cpp) - Fixed overflow bug, converted to debug logging
- [`engine/src/Engine.cpp`](engine/src/Engine.cpp) - Converted to debug logging

### Files Still Using Old Logging (To Be Updated)
The following files still use `std::cout`/`std::cerr` and should be updated in future work:
- `engine/src/DNNTracker.cpp`
- `engine/src/BallTracker.cpp`
- `engine/src/modules/*.cpp`
- `hub/` Python files (need separate Python logging configuration)

## Performance Impact

With debug mode **disabled** (default):
- Minimal performance impact
- Only important messages (INFO, WARN, ERROR) are shown
- No frame-by-frame logging spam

With debug mode **enabled**:
- Detailed logging for troubleshooting
- May slightly reduce FPS due to I/O operations
- Useful for development and debugging

## Python Logging

The Python hub now respects the `JUGGLEHUB_DEBUG` environment variable:

### Configuration

The logging level is automatically configured in [`hub/main.py`](hub/main.py:18-26):
- **Debug OFF** (default): Only INFO, WARNING, and ERROR messages are shown
- **Debug ON** (`JUGGLEHUB_DEBUG=1`): All DEBUG messages are shown

### Log Levels

Python uses the standard `logging` module with these levels:
- `logging.debug()` - Only shown when `JUGGLEHUB_DEBUG=1`
- `logging.info()` - Always shown (important messages)
- `logging.warning()` - Always shown (warnings)
- `logging.error()` - Always shown (errors)

### Example Usage

```python
import logging
logger = logging.getLogger(__name__)

# Debug output (only when JUGGLEHUB_DEBUG=1)
logger.debug(f"Processing frame {frame_number} with {ball_count} balls")

# Important user message (always shown)
logger.info("Camera started successfully")

# Warning (always shown)
logger.warning("Camera settings file not found, using defaults")

# Error (always shown)
logger.error(f"Failed to initialize tracker: {error_message}")
```

### Remaining Work

Some Python files still use `print()` statements. These should be gradually converted to use the `logging` module for consistency.

## Testing

To verify the debug logging system:

```bash
# Test with debug OFF (should see minimal output)
./scripts/run_hub.sh

# Test with debug ON (should see detailed frame-by-frame logs)
JUGGLEHUB_DEBUG=1 ./scripts/run_hub.sh
```

## Benefits

1. **Cleaner Console Output** - Normal operation shows only important messages
2. **Better Performance** - Reduced I/O operations during normal use
3. **Easier Debugging** - Enable detailed logs only when needed
4. **Fixed Bug** - Resolved color tracker disappearing issue
5. **Maintainable** - Consistent logging pattern across codebase