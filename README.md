# JuggleHub - Advanced Juggling Analysis System

A high-performance monorepo combining C++ real-time ball tracking with Python-based analysis and visualization.

**Last Updated:** 2025-08-23 07:35:00 UTC

## 🎯 Overview

JuggleHub is a complete system for the real-time analysis and interactive control of juggling patterns. It is built on a high-performance, hybrid architecture that separates low-latency C++ processing from high-level Python management.

### Core Philosophy

This project is guided by a few key principles:

-   **Real-Time First:** For interactive applications (games, coaching feedback, ball control), logic that must run *per-frame* belongs in the C++ engine to guarantee the lowest possible latency.
-   **Python for Flexibility:** The Python hub is the "mission control" for the system. It is used for configuration, data logging, visualization, and running any analysis that is not strictly real-time.
-   **API-Driven:** The C++ engine and Python hub communicate through a strict, versioned API defined with Protocol Buffers. This is the single source of truth for all data structures, preventing integration errors.
-   **Modularity over Monolith:** The C++ engine is not a single application, but a host for pluggable, real-time "Modules". This allows new interactive applications to be developed in C++ without ever modifying the core tracking code.

## 🏗️ Architecture

```
JuggleHub/
├── api/v1/                    # Protocol Buffers API definition
│   └── juggler.proto         # Single source of truth for data structures
├── engine/                   # C++ real-time tracking engine
│   ├── src/                  # Source files
│   ├── include/              # Header files
│   └── CMakeLists.txt        # Build configuration
├── hub/                      # Python analysis and UI
│   ├── main.py              # Main hub application
│   ├── components/          # Core components
│   └── requirements.txt     # Python dependencies
└── scripts/                 # Build and run scripts
    ├── build_engine.sh      # Build C++ engine
    └── run_hub.sh           # Run Python hub
```

## 🚀 Quick Start

### Prerequisites

**System Requirements:**
- Linux (Ubuntu 20.04+ recommended) or macOS
- Intel RealSense D400 series camera
- USB 3.0 port
- Modern CPU with AVX2 support

**Dependencies:**
- C++ compiler (GCC 7+ or Clang 6+)
- CMake 3.15+
- Python 3.8+
- Protocol Buffers compiler
- Intel RealSense SDK 2.0
- OpenCV 4.x
- ZeroMQ

### Installation

1. **Clone the repository:**
   ```bash
   git clone <repository-url>
   cd JuggleHub
   ```

2. **Install system dependencies (Ubuntu/Debian):**
   ```bash
   # Build tools
   sudo apt update
   sudo apt install build-essential cmake pkg-config
   
   # Protocol Buffers
   sudo apt install protobuf-compiler libprotobuf-dev
   
   # Intel RealSense SDK
   sudo apt-key adv --keyserver keyserver.ubuntu.com --recv-key F6E65AC044F831AC80A06380C8B3A55A6F3EFCDE
   sudo add-apt-repository "deb https://librealsense.intel.com/Debian/apt-repo $(lsb_release -cs) main"
   sudo apt update
   sudo apt install librealsense2-devel
   
   # OpenCV
   sudo apt install libopencv-dev
   
   # ZeroMQ
   sudo apt install libzmq3-dev
   
   # Python development
   sudo apt install python3-dev python3-pip python3-venv
   ```

3. **Build the C++ engine:**
   ```bash
   ./scripts/build_engine.sh
   ```

4. **Set up Python environment:**
   ```bash
   ./scripts/run_hub.sh --create-venv --install-deps
   ```

### Running the System

1. **Start the system:**
   ```bash
   ./scripts/run_hub.sh
   ```
   This will start both the C++ engine and the Python hub.

## 📊 Features

### C++ Engine Features
- **Real-time 3D tracking** of colored juggling balls (pink, orange, green, yellow)
- **Interactive calibration** with click-to-calibrate functionality
- **Smart occlusion handling** - merges nearby detections
- **High-performance streaming** at up to 90 FPS
- **Protocol Buffers output** for type-safe data exchange
- **Optional hand tracking** with MediaPipe integration
- **Pluggable Real-Time Modules**: Run different interactive applications (games, controllers, etc.) as sandboxed C++ modules for maximum performance, controlled by the Python hub

### Python Hub Features
- **Real-time visualization** with PyQt6 GUI
- **SQLite database logging** for session analysis
- **ZeroMQ data streaming** with automatic reconnection
- **Console mode** for headless operation
- **Performance monitoring** and statistics
- **Extensible architecture** for custom analysis

### API Features
- **Type-safe communication** via Protocol Buffers
- **Versioned API** for backward compatibility
- **Rich data structures** for balls, hands, IMU data
- **System status monitoring**
- **Command/response patterns** for engine control

## 🔧 Configuration

### Engine Configuration

The engine supports various command-line options:

```bash
# High-performance mode
./engine/build/bin/juggle_engine --high-fps

# Custom resolution
./engine/build/bin/juggle_engine --width 1280 --height 720 --fps 60

# With timestamps
./engine/build/bin/juggle_engine --timestamp

# Enable hand tracking (if compiled with support)
./engine/build/bin/juggle_engine --track-hands

# Custom output formats
./engine/build/bin/juggle_engine --output-format=simple
./engine/build/bin/juggle_engine --output-format=legacy
```

The `--output-format` option controls how ball detection data is printed to the console:
-   `default` (default): A human-readable, formatted output with labels for each field.
-   `simple`: A comma-separated format with all available data: `timestamp_us,color_name,world_x,world_y,world_z,center_x,center_y,confidence`.
-   `legacy`: A comma-separated format designed for compatibility with older tools: `color_name,world_x,world_y,world_z,timestamp_us`.

### Hub Configuration

The hub can be configured via command-line arguments:

```bash
# Console mode (no GUI)
./scripts/run_hub.sh --no-ui

# Debug mode
./scripts/run_hub.sh --debug

# Custom ZMQ endpoint
./scripts/run_hub.sh --zmq-endpoint tcp://192.168.1.100:5555

# Disable database logging
python3 hub/main.py --no-logging
```

### Color Calibration

1. Run the engine in calibration mode:
   ```bash
   ./engine/build/bin/juggle_engine calibrate
   ```

2. Follow the on-screen instructions:
   - Press '1', '2', '3', '4' to select colors
   - Click on balls to calibrate
   - Press 's' to save settings
   - Press 'q' to exit

## 🎨 Ball Color Control

The system includes a UDP-based ball color control module that can send color commands to smart juggling balls over the network.

### Quick Color Commands

To send color commands to specific balls, use the hub's interactive command interface:

```bash
# Navigate to hub directory and activate virtual environment
cd hub && source .venv/bin/activate

# Send a color command to a specific ball
echo -e "load\ncolor BALL_ID R G B\nquit" | python3 main.py
```

### Examples

```bash
# Make ball 205 (IP 10.54.136.205) blue
cd hub && source .venv/bin/activate && echo -e "load\ncolor 205 0 0 255\nquit" | python3 main.py

# Make ball 201 red
cd hub && source .venv/bin/activate && echo -e "load\ncolor 201 255 0 0\nquit" | python3 main.py

# Make ball 202 green
cd hub && source .venv/bin/activate && echo -e "load\ncolor 202 0 255 0\nquit" | python3 main.py

# Make ball 203 white
cd hub && source .venv/bin/activate && echo -e "load\ncolor 203 255 255 255\nquit" | python3 main.py
```

### Command Format

- `load`: Loads the UdpBallColorModule
- `color BALL_ID R G B`: Sends color command to ball
 - `BALL_ID`: The last octet of the ball's IP address (e.g., 205 for 10.54.136.205)
 - `R G B`: Red, Green, Blue values (0-255 each)
- `quit`: Exits the program

### Network Configuration

- **Default IP Range**: `10.54.136.X` where X is the ball ID
- **Port**: `41412` (UDP)
- **Protocol**: Custom UDP packets with brightness and color commands

### Interactive Mode

You can also run the hub in interactive mode for multiple commands:

```bash
cd hub && source .venv/bin/activate && python3 main.py
```

Then type commands interactively:
```
> load
> color 205 0 0 255
> color 201 255 0 0
> quit
```

**Last Updated:** 2025-08-22 22:44:00 UTC

## 🧪 Development Workflow

This project is designed for rapid development of new real-time applications without sacrificing performance.

### Building with Debug Info

To build the engine with debug symbols and verbose output:
```bash
./scripts/build_engine.sh --debug --verbose
```

### Workflow: Creating a New Real-Time Module

Here is the step-by-step process for adding a new interactive feature (e.g., a new game that colors the balls based on their height).

1.  **(API) Define Data Structures:** If your new module needs to send or receive unique data, add the new message types to [`api/v1/juggler.proto`](api/v1/juggler.proto:1).

2.  **(C++) Create the Module:**
    -   Create a new file in [`engine/src/modules/`](engine/src/modules/:1), for example, `HeightColorModule.cpp`.
    -   In this file, create a class that inherits from the `ModuleBase` interface.
    -   Implement the `setup()`, `update()`, and `cleanup()` methods. The `update()` function will contain your real-time logic that runs every frame.

3.  **(C++) Register the Module:**
    -   Open [`engine/src/Engine.cpp`](engine/src/Engine.cpp:1) (or a dedicated module factory file).
    -   Include the header for your new module.
    -   Add your new module to the list of available modules so the engine knows it exists and can be loaded.

4.  **(Build) Recompile the Engine:** Run `./scripts/build_engine.sh`. This will compile your new module and also regenerate the Protobuf files for both C++ and Python, ensuring everything is in sync.

5.  **(Python) Add Control Logic:**
    -   In the Python [`hub/`](hub/:1), modify the UI or application logic to send a command to the C++ engine to load your new module.

6.  **Run and Test!**

### Running Tests

```bash
# Verify Protocol Buffer definitions compile for Python
make generate-proto

# Run Python unit tests
cd hub && python3 -m pytest

# Test the hub components
cd hub
python3 -m pytest
```

### Adding New Features

1. **Modify the API**: Update [`api/v1/juggler.proto`](api/v1/juggler.proto:1)
2. **Rebuild**: Run build scripts to regenerate Protocol Buffer files
3. **Update Engine**: Modify C++ code in [`engine/src/`](engine/src/:1)
4. **Update Hub**: Modify Python code in [`hub/components/`](hub/components/:1)

## 🗺️ Roadmap & Future Vision

This architecture provides a foundation for many advanced features:

-   **[ ] Advanced Coaching Modules**: Implement C++ modules for pattern analysis, consistency scoring, and real-time feedback.
-   **[ ] Wearable Sensor Integration**: Add support for IMU data from wearables (gloves, watches) to the Protobuf API and fuse it with vision data in the engine.
-   **[ ] Web Interface**: Create a web-based dashboard using the Python hub as a backend to view juggling sessions from any device.
-   **[ ] FPGA/Hardware Acceleration**: Explore offloading parts of the vision pipeline (like color thresholding) to hardware like a Xilinx Kria for even lower latency.
-   **[ ] ROS Integration**: Add a ROS 2 compatibility layer to allow the engine to publish data as standard ROS topics.

## 📈 Performance

### Typical Performance Metrics
- **Engine**: 60-90 FPS at 848x480, 30-60 FPS at 1280x720
- **Hub**: Real-time processing with <10ms latency
- **Memory**: ~100MB engine, ~50MB hub
- **CPU**: 15-25% on modern quad-core systems

### Optimization Tips
- Use `--high-fps` for maximum frame rate
- Reduce resolution for better performance
- Use `--downscale 0.5` for processing optimization
- Enable hardware acceleration where available

## 🔍 Troubleshooting

### Common Issues

**"No RealSense device found"**
- Ensure camera is connected to USB 3.0 port
- Check RealSense SDK installation
- Try different USB ports

**"Protocol Buffer files not found"**
- Run `./scripts/build_engine.sh` to generate files
- Check that `protoc` is installed and in PATH

**"ZMQ connection failed"**
- We've identified an issue with ZMQ in some environments. If you're having trouble with ZMQ, you can switch to the standard I/O communication method. See the "Development Workflow" section for more details.

**Low frame rate**
- Close other camera applications
- Use lower resolution settings
- Check USB bandwidth limitations

### Debug Mode

Enable debug output for detailed information:

```bash
# Engine debug (via stderr)
./engine/build/bin/juggle_engine 2>engine_debug.log

# Hub debug
./scripts/run_hub.sh --debug
```

## 🤝 Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Test thoroughly with real hardware
5. Update documentation
6. Submit a pull request

### Code Style
- **C++**: Follow Google C++ Style Guide
- **Python**: Follow PEP 8
- **Commits**: Use conventional commit messages

## 📄 License

This project is provided as-is for educational and development purposes.

## 🙏 Acknowledgments

- Intel RealSense team for excellent depth camera SDK
- OpenCV community for computer vision tools
- Protocol Buffers team for efficient serialization
- ZeroMQ community for high-performance messaging
- PyQt team for cross-platform GUI framework

---

**Built with ❤️ for the juggling community**

For questions, issues, or contributions, please open an issue on the repository.