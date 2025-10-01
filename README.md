# JuggleHub - Advanced Juggling Analysis System

A high-performance monorepo combining C++ real-time ball tracking with Python-based analysis and visualization.

**Last Updated:** 2025-10-01 12:27:00 UTC

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
    ├── run_hub.sh           # Run Python hub
    └── add_prefix.py        # Add prefix to files and directories
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
- **Intel OpenVINO 2025.2.0+** (for DNN-based tracking)
- **Eigen3** (for mathematical operations)

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
   
   # Eigen3 (for mathematical operations)
   sudo apt install libeigen3-dev
   
   # Asio (for UDP communication)
   sudo apt install libasio-dev

   # Python development
   sudo apt install python3-dev python3-pip python3-venv
   ```

3. **Install Intel OpenVINO (Required for DNN Tracking):**
   ```bash
   # Download and install OpenVINO 2025.2.0
   wget https://storage.openvinotoolkit.org/repositories/openvino/packages/2025.2/linux/l_openvino_toolkit_ubuntu20_2025.2.0.16993.5a9b9c1ca0e_x86_64.tgz
   tar -xzf l_openvino_toolkit_ubuntu20_2025.2.0.16993.5a9b9c1ca0e_x86_64.tgz
   sudo mv l_openvino_toolkit_ubuntu20_2025.2.0.16993.5a9b9c1ca0e /opt/intel/openvino_2025.2.0
   
   # Set up environment variables (add to ~/.bashrc)
   echo 'source /opt/intel/openvino_2025.2.0/setupvars.sh' >> ~/.bashrc
   source ~/.bashrc
   ```

4. **Build the C++ engine:**
   ```bash
   ./scripts/build_engine.sh
   ```

5. **Set up Python environment:**
   ```bash
   ./scripts/run_hub.sh --create-venv --install-deps
   ```

   **Note:** If you encounter an "externally-managed-environment" error when installing dependencies, the script will automatically create and use a virtual environment to avoid conflicts with system Python packages.

### Running the System

1. **Start the system:**
   ```bash
   # If using virtual environment (recommended)
   ./scripts/run_hub.sh --use-venv
   
   # Or without virtual environment (if system allows)
   ./scripts/run_hub.sh
   ```
   This will start both the C++ engine and the Python hub.

   **Troubleshooting:** If you see "Missing required dependencies: protobuf pyzmq", run:
   ```bash
   ./scripts/run_hub.sh --create-venv --install-deps
   ```

2. **Using custom camera settings:**
   ```bash
   # Use default camera settings (auto exposure enabled)
   ./scripts/run_hub.sh --use-venv --camera-settings default.json
   
   # Use no-blur settings (manual exposure for consistent lighting)
   ./scripts/run_hub.sh --use-venv --camera-settings no_blur.json
   ```

3. **Selecting inference device:**
   ```bash
   # Use NPU for power-efficient AI inference (Intel Core Ultra processors)
   ./scripts/run_hub.sh --use-venv --device NPU
   
   # Use GPU for maximum performance (Intel integrated/discrete graphics)
   ./scripts/run_hub.sh --use-venv --device GPU
   
   # Use CPU for compatibility (default fallback)
   ./scripts/run_hub.sh --use-venv --device CPU
   
   # Let OpenVINO automatically select the best device
   ./scripts/run_hub.sh --use-venv --device AUTO
   ```

4. **Selecting Model:**
   ```bash
   # Use yolo11s model
   ./scripts/run_hub.sh --use-venv --model yolo11s
   
   # Use yolo11n model (default)
   ./scripts/run_hub.sh --use-venv --model yolo11n
   ```

3. **Live Camera Settings Control:**
  
  Once the hub is running, you can dynamically change camera settings without restarting:
  
  - Open the **Calibration Mode** in the hub UI
  - Use the **Camera Settings** dropdown to select different presets
  - Click **"Stop Camera"** to pause the video feed
  - Click **"Start Camera"** to resume with the selected settings
  
  This allows you to quickly switch between different camera configurations (like default auto-exposure vs. no-blur manual settings) during your juggling session without interrupting the application.

## 📊 Features

### C++ Engine Features
- **Dual Tracking Systems**:
  - **Color-based tracking** of colored juggling balls (pink, orange, green, yellow)
  - **DNN-based tracking** using YOLOv11 + ByteTrack for robust object detection and tracking
- **Advanced AI Tracking**:
  - **YOLOv11 object detection** optimized with Intel OpenVINO for real-time inference
  - **YOLO-Pose estimation** for hand and body tracking
  - **ByteTrack multi-object tracking** for consistent ball ID assignment across frames
  - **Automatic model loading** from OpenVINO IR format (.xml/.bin files)
- **Camera Settings Management**: Load custom camera settings from JSON files for optimal performance
- **Interactive calibration** with click-to-calibrate functionality
- **Persistent calibration settings** - automatically saves and loads settings between sessions
- **Smart occlusion handling** - merges nearby detections
- **High-performance streaming** at up to 90 FPS
- **Protocol Buffers output** for type-safe data exchange
- **Pluggable Real-Time Modules**: Run different interactive applications (games, controllers, etc.) as sandboxed C++ modules for maximum performance, controlled by the Python hub

### Python Hub Features
- **Real-time visualization** with PyQt6 GUI
- **Menu bar interface** with File and Help menus for easy access to settings and information
- **Persistent calibration settings** - automatically saves settings on exit and loads on startup
- **Save/Load settings** - export and import calibration settings via file dialogs (Ctrl+S / Ctrl+O)
- **About dialog** - comprehensive application information and feature overview
- **Probabilistic State Estimator**: Fuses data from multiple sources to determine the physical state of juggling balls (held vs. unheld).
- **Color Profile Persistence**: Saves and loads color profiles for juggling balls, allowing for consistent identification across sessions.
- **Kalman Filter Ball Tracker**: Manages ball tracks and applies a Kalman Filter to each one for smoothed physics data.
- **Enhanced Visualization**: Large, solid, color-matched ball trackers and thick, highly visible hand trackers for clear and intuitive visual feedback.
- **Camera settings selection** with dropdown interface for easy configuration switching
- **Live camera control** with stop/start functionality for seamless settings switching
- **SQLite database logging** for session analysis (runtime data files are automatically ignored by git)
- **ZeroMQ data streaming** with automatic reconnection
- **Console mode** for headless operation
- **Performance monitoring** and statistics
- **Extensible architecture** for custom analysis
### Enhanced Color Tracking System

**Last Updated:** 2025-10-01 01:16:00 UTC

JuggleHub features an advanced color tracking system with persistent color profiles and intelligent tracking behavior:

#### Named Color Profiles
- **Interactive Calibration**: Click on a ball during calibration mode to create a named color profile
- **Persistent Database**: Color profiles are saved to `hub/ball_color_profiles.json` and persist across sessions
- **Manual Naming**: Each profile gets a custom name (e.g., "Red Ball", "Blue Ball") for easy identification
- **Profile Matching**: Only balls matching saved color profiles are tracked - false positives are automatically filtered out

#### Zero-Lag Tracking
- **Direct YOLO Positioning**: When YOLO detects a ball, the color tracker uses the raw detection position directly (no smoothing lag)
- **Kalman Prediction Fallback**: When YOLO loses sight of a ball, Kalman filtering predicts its position
- **Instant Hand Association**: When a ball disappears near a wrist (within 15cm), it's instantly associated with that hand
- **Smart Occlusion Handling**: Balls held in hands are automatically positioned at the wrist location

#### How to Use
1. **Enter Calibration Mode** in the hub UI
2. **Click "Calibrate Ball 0"** (or 1, 2, etc.)
3. **Click on the ball** in the video feed
4. **Enter a name** for the color profile (e.g., "Red Ball")
5. **Repeat** for each ball you want to track

The system will now only track balls that match your saved color profiles, eliminating false positives from hands or other objects.


### Heuristic Intelligence Layer
- **Robust Hand Tracking**: Enforces a two-hand limit and maintains persistent left/right identity.
- **Ball-in-Hand Occlusion Model**: Intelligently infers when a ball is held, linking its state to the corresponding hand to prevent track loss.

### API Features
- **Type-safe communication** via Protocol Buffers
- **Versioned API** for backward compatibility
- **Rich data structures** for balls, hands, IMU data
- **System status monitoring**
- **Command/response patterns** for engine control

### Tracking Control

The hub UI provides real-time control over the DNN-based tracking system. These settings can be adjusted in the "Calibration Mode" panel.

#### DNN Tracker Settings
- **Confidence Threshold**: Adjusts the minimum confidence level for a detected object to be considered a valid detection. Higher values reduce false positives but may miss less clear objects.
- **NMS Threshold**: Controls the "non-maximum suppression" threshold, which merges overlapping bounding boxes. Higher values allow more overlap, while lower values are stricter.

#### ByteTrack Settings
- **Track Buffer (Frames)**: Defines how many frames a "lost" track is kept in memory before being deleted. Increasing this value helps maintain consistent IDs for objects that are temporarily occluded.
- **Track Threshold**: The confidence threshold required to initiate a new track.
- **High Confidence Threshold**: The confidence threshold for the first association step, where high-confidence detections are matched to existing tracks.
- **Match Threshold**: The IoU (Intersection over Union) threshold for associating detections with existing tracks. A lower value makes it easier to associate a detection with a track, even if they don't perfectly overlap.

## 🤖 DNN-Based Tracking System

JuggleHub now features a state-of-the-art deep neural network tracking system that provides robust, AI-powered ball detection and tracking capabilities alongside the traditional color-based tracking.

### Architecture Overview

The DNN tracking system consists of three main components:

1. **YOLOv11 Object Detection**: Uses a pre-trained YOLOv11 model optimized with Intel OpenVINO for real-time inference
2. **YOLO-Pose Estimation**: Uses a pre-trained pose estimation model to track hand and body keypoints.
3. **ByteTrack Multi-Object Tracking**: Maintains consistent object IDs across frames, handling occlusions and temporary disappearances

### Key Components

#### DNNTracker Class ([`engine/include/DNNTracker.hpp`](engine/include/DNNTracker.hpp))
- **OpenVINO Integration**: Loads and runs YOLOv11 and YOLO-Pose models in OpenVINO IR format
- **Preprocessing Pipeline**: Handles image resizing, normalization, and format conversion
- **Postprocessing**: Converts model outputs to detection boxes with confidence scores
- **ByteTrack Integration**: Maintains object tracking across frames

#### Model Files
- **YOLOv11 Ball Detection Model**: [`engine/models/yolo11n.xml`](engine/models/yolo11n.xml) (OpenVINO IR format)
- **YOLOv11 Ball Detection Weights**: [`engine/models/yolo11n.bin`](engine/models/yolo11n.bin) (Binary weights)
- **YOLO-Pose Model**: [`engine/models/yolo11n-pose.xml`](engine/models/yolo11n-pose.xml) (OpenVINO IR format)
- **YOLO-Pose Weights**: [`engine/models/yolo11n-pose.bin`](engine/models/yolo11n-pose.bin) (Binary weights)
- **Metadata**: [`engine/models/metadata.yaml`](engine/models/metadata.yaml) (Model configuration)

### Technical Specifications

#### Model Configuration
- **Input Resolution**: 640x640 pixels
- **Model Type**: YOLOv11n (nano variant for speed)
- **Inference Backend**: Intel OpenVINO 2025.2.0
- **Confidence Threshold**: 0.45 (configurable)
- **NMS Threshold**: 0.5 (configurable)

#### Performance Characteristics
- **Inference Speed**: ~10-15ms per frame on modern CPUs
- **Detection Accuracy**: High precision for juggling balls
- **Tracking Consistency**: Maintains object IDs across occlusions
- **Memory Usage**: ~50MB additional for model and tracking state

### Usage

#### Enabling DNN Tracking
The DNN tracker is automatically available when the engine is built with OpenVINO support. The engine will detect and load the YOLOv11 model from the `engine/models/` directory.

```bash
# Build with DNN tracking support (requires OpenVINO)
./scripts/build_engine.sh

# Run engine with DNN tracking enabled
./engine/build/juggle_engine --use-dnn-tracker
```

#### Model Requirements
Ensure the following files are present in `engine/models/`:
- `yolo11n.xml` - OpenVINO IR model file
- `yolo11n.bin` - Model weights
- `metadata.yaml` - Model metadata (optional)

### Integration with Existing System

The DNN tracker integrates seamlessly with the existing JuggleHub architecture:

#### Engine Integration
- **Dual Mode Operation**: Can run alongside color-based tracking
- **Unified Output**: Produces the same `TrackedObject` data structure
- **Module Compatibility**: Works with all existing real-time modules
- **Protocol Buffer Support**: Outputs tracking data via the same API

#### Build System
- **CMake Integration**: Automatically detects OpenVINO installation
- **Dependency Management**: Links required OpenVINO libraries
- **Cross-Platform**: Supports Linux and macOS (where OpenVINO is available)

### Dependencies

#### Required Libraries
- **Intel OpenVINO 2025.2.0+**: Core inference engine
- **OpenCV 4.x**: Image processing and computer vision
- **Eigen3**: Mathematical operations and linear algebra
- **ByteTrack-cpp**: Multi-object tracking library (included as submodule)

#### Installation Verification
```bash
# Verify OpenVINO installation
source /opt/intel/openvino_2025.2.0/setupvars.sh
python3 -c "import openvino; print(f'OpenVINO version: {openvino.__version__}')"

# Verify model files
ls -la engine/models/
# Should show: yolo11n.xml, yolo11n.bin, metadata.yaml
```

### Troubleshooting

#### Common Issues

**"OpenVINO model not found"**
- Ensure model files are in `engine/models/` directory
- Check file permissions and accessibility
- Verify model file integrity

**"OpenVINO library not found"**
- Verify OpenVINO installation: `source /opt/intel/openvino_2025.2.0/setupvars.sh`
- Check CMake can find OpenVINO: `pkg-config --exists openvino`
- Rebuild with clean build: `./scripts/build_engine.sh --clean`

**"Low DNN tracking performance"**
- Reduce input resolution in DNNTracker configuration
- Use CPU optimization flags during OpenVINO installation
- Consider using GPU backend if available

#### Debug Information
```bash
# Enable verbose DNN tracking output
./engine/build/juggle_engine --use-dnn-tracker --verbose

# Check OpenVINO device capabilities
python3 -c "
import openvino as ov
core = ov.Core()
print('Available devices:', core.available_devices)
"
```

### Future Enhancements

The DNN tracking system provides a foundation for advanced features:

- **Custom Model Training**: Train YOLOv11 models on specific juggling ball datasets
- **Multi-Class Detection**: Detect different types of juggling objects (balls, clubs, rings)
- **Hardware Acceleration**: Utilize Intel GPU or VPU for faster inference
- **Model Optimization**: Quantization and pruning for embedded deployment

### Device Selection for Inference

JuggleHub supports multiple compute devices for running the DNN-based tracking:

- **CPU**: Default option, works on all systems
- **GPU**: Accelerated inference using Intel integrated or discrete graphics (default)
- **NPU**: Neural Processing Unit acceleration on supported hardware
- **AUTO**: Let OpenVINO automatically select the best available device

You can specify the device using the `--device` command-line argument:

```bash
# Use NPU for power-efficient inference
./scripts/run_hub.sh --use-venv --device NPU

# Use GPU for maximum performance (default)
./scripts/run_hub.sh --use-venv --device GPU

# Use CPU for compatibility
./scripts/run_hub.sh --use-venv --device CPU

# Let OpenVINO choose automatically
./scripts/run_hub.sh --use-venv --device AUTO
```
## 🧍 Full Body Pose Estimation

JuggleHub now includes comprehensive full-body pose estimation using YOLO-Pose, providing real-time tracking of 17 COCO keypoints for advanced juggling analysis and body mechanics understanding.

### Pose Estimation Overview

The pose estimation system runs alongside ball detection, providing synchronized tracking of:
- **17 COCO Keypoints**: Complete body skeleton including head, torso, arms, and legs
- **3D Position Data**: All keypoints are deprojected to 3D world coordinates using depth information
- **Hand Tracking**: Specialized wrist tracking (left and right) for juggling-specific analysis
- **Real-time Performance**: Optimized inference running at 20-30 FPS on modern hardware

### COCO Keypoint Format

The system tracks all 17 standard COCO keypoints in order:

| Index | Keypoint Name | Description |
|-------|--------------|-------------|
| 0 | Nose | Center of face |
| 1 | Left Eye | Left eye position |
| 2 | Right Eye | Right eye position |
| 3 | Left Ear | Left ear position |
| 4 | Right Ear | Right ear position |
| 5 | Left Shoulder | Left shoulder joint |
| 6 | Right Shoulder | Right shoulder joint |
| 7 | Left Elbow | Left elbow joint |
| 8 | Right Elbow | Right elbow joint |
| 9 | **Left Wrist** | **Left hand position** |
| 10 | **Right Wrist** | **Right hand position** |
| 11 | Left Hip | Left hip joint |
| 12 | Right Hip | Right hip joint |
| 13 | Left Knee | Left knee joint |
| 14 | Right Knee | Right knee joint |
| 15 | Left Ankle | Left ankle position |
| 16 | Right Ankle | Right ankle position |

### Technical Implementation

#### Architecture
- **Model**: YOLOv11-Pose (nano variant for speed)
- **Input Resolution**: 640x640 pixels
- **Output Format**: Person bounding boxes + 17 keypoints (x, y, confidence) per person
- **3D Deprojection**: Uses RealSense depth data to convert 2D keypoints to 3D world coordinates
- **Confidence Filtering**: Only keypoints with >0.5 confidence are deprojected

#### Data Structure

The [`TrackedHand`](engine/include/DNNTracker.hpp:43-48) structure contains:
```cpp
struct TrackedHand {
    cv::Point3f wrist_pos_3d;           // Primary wrist position in 3D
    float confidence;                    // Detection confidence
    int id;                             // 0 for left hand, 1 for right hand
    std::vector<cv::Point3f> keypoints; // All 17 keypoints in 3D
};
```

#### Key Features

**Robust Hand Tracking**
- Automatic left/right hand identification based on skeletal structure
- Persistent hand IDs (0=left, 1=right) maintained across frames
- Wrist positions used as primary hand location for ball-hand interaction analysis

**Full Body Context**
- Access to complete body pose for advanced analysis
- Shoulder, elbow, and wrist positions for arm trajectory analysis
- Hip and leg positions for body mechanics and balance analysis
- Head position for gaze direction estimation

**3D World Coordinates**
- All keypoints converted from 2D image space to 3D world space
- Depth-based filtering (0.2m - 3.0m range) for reliable measurements
- Synchronized with ball tracking in the same coordinate system

### Usage

#### Enabling Pose Estimation

Pose estimation is enabled by default when using the DNN tracker:

```bash
# Run with pose estimation (default)
./scripts/run_hub.sh --use-venv

# Explicitly enable/disable via settings
# (Can be toggled at runtime through the hub UI)
```

#### Accessing Pose Data

The pose data is available through the Protocol Buffer API in the `FrameData` message:

```python
# Python example - accessing pose data
for hand in frame_data.hands:
    print(f"Hand {hand.id}: Wrist at ({hand.wrist_pos_3d.x}, {hand.wrist_pos_3d.y}, {hand.wrist_pos_3d.z})")
    print(f"Confidence: {hand.confidence}")
    
    # Access all 17 keypoints
    for i, keypoint in enumerate(hand.keypoints):
        print(f"  Keypoint {i}: 3D=({keypoint.pos_3d.x}, {keypoint.pos_3d.y}, {keypoint.pos_3d.z})")
        print(f"             2D=({keypoint.pos_2d.x}, {keypoint.pos_2d.y})")
        print(f"             Confidence={keypoint.confidence}")
```

### Performance Characteristics

#### Inference Speed
- **CPU**: 15-25ms per frame (YOLOv11n-pose)
- **GPU**: 8-12ms per frame (with OpenVINO GPU plugin)
- **NPU**: 10-15ms per frame (Intel Core Ultra processors)

#### Accuracy
- **Keypoint Detection**: High precision for visible body parts
- **3D Accuracy**: ±2-5cm for keypoints within optimal depth range (0.5m - 2.0m)
- **Hand Tracking**: Robust wrist detection even during rapid juggling movements

#### Resource Usage
- **Memory**: Additional ~30MB for pose model
- **CPU Load**: +5-10% compared to ball detection only
- **Combined System**: ~30-40% CPU usage on modern quad-core systems

### Applications

The full-body pose estimation enables advanced features:

**Juggling Analysis**
- **Arm Trajectory Tracking**: Analyze throwing and catching motions using shoulder-elbow-wrist chains
- **Body Mechanics**: Study posture, balance, and body positioning during juggling
- **Pattern Recognition**: Correlate body movements with juggling patterns
- **Technique Coaching**: Compare body positions against ideal form

**Interactive Applications**
- **Gesture Control**: Use body poses to control applications or LED balls
- **Pose-Based Games**: Create games that respond to specific body positions
- **Virtual Coaching**: Real-time feedback on body positioning and technique
- **Motion Capture**: Record full-body juggling performances for analysis

**Research Applications**
- **Biomechanics Studies**: Analyze the physics of juggling movements
- **Learning Progression**: Track how body mechanics improve with practice
- **Injury Prevention**: Identify potentially harmful movement patterns
- **Performance Optimization**: Find the most efficient body positions for different patterns

### Configuration

#### Model Settings

The pose estimation can be configured through the DNNTracker settings:

```bash
# Adjust confidence thresholds
# pose_confidence_threshold: 0.3 (person detection)
# keypoint_confidence_threshold: 0.5 (individual keypoints)
```

#### Runtime Control

Pose estimation can be toggled at runtime:
- Through the hub UI: Settings → "Enable Pose Model"
- Via command: `SET_POSE_MODEL_ENABLED` command in Protocol Buffer API

### Troubleshooting

**"No hands detected"**
- Ensure hands are visible in the camera frame
- Check that person is within depth range (0.5m - 2.5m)
- Verify pose model files exist in `engine/models/`
- Increase lighting for better detection

**"Inconsistent hand IDs"**
- This is expected when person moves significantly
- The system re-identifies left/right based on skeletal structure each frame
- For persistent tracking, use the wrist positions with Kalman filtering

**"Low pose estimation performance"**
- Reduce camera resolution
- Use NPU or GPU acceleration
- Consider using pose estimation only when needed
- Disable pose estimation if only ball tracking is required

### Future Enhancements

Planned improvements for pose estimation:

- **Multi-Person Support**: Track multiple jugglers simultaneously
- **Temporal Smoothing**: Apply Kalman filtering to keypoints for smoother tracking
- **Pose Classification**: Automatic recognition of juggling tricks and patterns
- **Custom Keypoints**: Train models with juggling-specific keypoints (finger positions, etc.)
- **Skeleton Visualization**: Real-time overlay of detected skeleton on video feed

**Last Updated:** 2025-09-30 18:15:00 UTC


The NPU option is particularly useful for systems with Intel Core Ultra processors and dedicated neural processing hardware, offering significant performance improvements while reducing CPU load and power consumption.

## 🧠 NPU (Neural Processing Unit) Support

JuggleHub supports Intel NPU acceleration for AI inference on compatible hardware, providing significant performance improvements and reduced power consumption.

### NPU Hardware Requirements

- **Supported CPUs**: Intel Core Ultra processors (Meteor Lake architecture)
  - Intel Core Ultra 5, 7, and 9 series
  - Examples: Core Ultra 9 185H, Core Ultra 7 155H, etc.
- **Operating System**: Linux with kernel 5.15+ (Ubuntu 22.04+ recommended)
- **Memory**: Additional 2GB RAM recommended for NPU operations

### NPU Setup and Installation

#### 1. Verify NPU Hardware Support

First, check if your system has NPU hardware:

```bash
# Check CPU model
lscpu | grep -i "Model name"
# Should show Intel Core Ultra processor

# Check for NPU driver
lsmod | grep intel_vpu
# Should show: intel_vpu

# Check NPU device
ls -la /dev/accel*
# Should show: /dev/accel0
```

#### 2. Install NPU Drivers

If NPU drivers are not installed, install them via Snap:

```bash
# Install Intel NPU driver package
sudo snap install intel-npu-driver

# Verify installation
intel-npu-driver.npu-umd-test
# Should show successful NPU tests
```

#### 3. Configure NPU Environment

JuggleHub provides scripts to automatically configure the NPU environment:

```bash
# Setup NPU environment and verify availability
./scripts/setup_npu_env.sh

# Should output:
# Available devices: ['CPU', 'GPU', 'NPU']
# ✅ NPU is available and ready to use!
```

### Using NPU with JuggleHub

#### Method 1: Using the NPU-Enabled Script (Recommended)

```bash
# Run engine with automatic NPU setup
./scripts/run_engine_with_npu.sh --device=NPU --use-dnn-tracker --verbose

# The script will:
# 1. Set up NPU environment automatically
# 2. Verify NPU availability
# 3. Run the engine with NPU acceleration
```

#### Method 2: Manual NPU Setup

```bash
# Set up environment manually
export LD_LIBRARY_PATH="/snap/intel-npu-driver/10/usr/lib/x86_64-linux-gnu:$LD_LIBRARY_PATH"
source /opt/intel/openvino_2025.2.0/setupvars.sh

# Run engine with NPU
./engine/build/juggle_engine --device=NPU --use-dnn-tracker
```

#### Method 3: Using the Hub with NPU

```bash
# Run hub with NPU acceleration
./scripts/run_hub.sh --use-venv --device NPU
```

### NPU Performance Benefits

#### Performance Comparison

| Device | Inference Time | Power Usage | CPU Load |
|--------|---------------|-------------|----------|
| **CPU** | 15-20ms | High | 25-40% |
| **GPU** | 5-10ms | Medium | 15-25% |
| **NPU** | 8-12ms | **Low** | **5-10%** |

#### Key Advantages

- **Power Efficiency**: Up to 70% lower power consumption vs CPU inference
- **Reduced CPU Load**: Frees up CPU resources for other tasks
- **Consistent Performance**: Dedicated hardware provides stable inference times
- **Thermal Efficiency**: Lower heat generation compared to CPU/GPU inference

### Troubleshooting NPU Issues

#### Common Problems and Solutions

**"NPU not found in available devices"**

This is the most common issue. The solution is to add the Level Zero libraries to your environment:

```bash
# Add NPU libraries to library path
export LD_LIBRARY_PATH="/snap/intel-npu-driver/10/usr/lib/x86_64-linux-gnu:$LD_LIBRARY_PATH"

# Source OpenVINO environment
source /opt/intel/openvino_2025.2.0/setupvars.sh

# Verify NPU is now available
python3 -c "
import openvino as ov
core = ov.Core()
print('Available devices:', core.get_available_devices())
"
```

**"intel_vpu driver not loaded"**

```bash
# Check if driver is loaded
lsmod | grep intel_vpu

# If not loaded, the driver may need to be installed
# Follow the NPU driver installation steps above
```

**"Permission denied accessing /dev/accel0"**

```bash
# Check if user is in render group
groups $USER | grep render

# If not in render group, add user
sudo usermod -a -G render $USER
# Log out and log back in for changes to take effect
```

**"NPU compilation failed"**

```bash
# Test NPU with a simple model first
./test_npu

# Check OpenVINO NPU plugin
ls /opt/intel/openvino_2025.2.0/runtime/lib/intel64/ | grep npu
# Should show: libopenvino_intel_npu_plugin.so
```

#### Debug Commands

```bash
# Test NPU availability
./scripts/setup_npu_env.sh

# Run NPU test program
./test_npu

# Check system NPU status
sudo dmesg | grep -i -E "(npu|vpu)"

# Verify Level Zero libraries
find /snap/intel-npu-driver -name "*ze*" | head -5
```

### NPU Technical Details

#### Architecture Integration

- **OpenVINO Integration**: Uses OpenVINO's NPU plugin for inference
- **Level Zero Backend**: Leverages Intel's Level Zero API for NPU communication
- **Model Compatibility**: Supports standard OpenVINO IR models (.xml/.bin)
- **Memory Management**: Efficient memory allocation between system RAM and NPU

#### Supported Models

- **YOLOv11**: Full support for all YOLOv11 variants (n, s, m, l, x)
- **YOLOv11**: Compatible with YOLOv11 models
- **Custom Models**: Any OpenVINO-compatible detection model
- **Quantization**: Supports INT8 quantized models for optimal NPU performance

#### Environment Variables

```bash
# Core NPU environment setup
export LD_LIBRARY_PATH="/snap/intel-npu-driver/10/usr/lib/x86_64-linux-gnu:$LD_LIBRARY_PATH"

# Optional NPU optimizations
export OV_NPU_THREADS=4                    # NPU thread count
export OV_NPU_MEMORY_POOL_SIZE=512         # Memory pool size (MB)
export OV_NPU_LOG_LEVEL=INFO               # Debug logging
```

### Integration with Existing Workflows

The NPU support is fully integrated with JuggleHub's existing features:

- **Real-time Modules**: All modules work with NPU acceleration
- **Recording**: NPU inference works with both recording modes
- **Hub Integration**: Full compatibility with Python hub
- **Protocol Buffers**: Same data structures and API
- **Multi-device**: Can fall back to CPU/GPU if NPU unavailable

**Last Updated:** 2025-09-08 19:25:00 UTC

##  Configuration

### Engine Configuration

The engine supports various command-line options:

```bash
# High-performance mode
./engine/build/juggle_engine --high-fps

# Custom resolution
./engine/build/juggle_engine --width 1280 --height 720 --fps 60

# With timestamps
./engine/build/juggle_engine --timestamp

# Enable DNN-based tracking (requires OpenVINO)
./engine/build/juggle_engine --use-dnn-tracker

# Enable hand tracking (if compiled with support)
./engine/build/juggle_engine --track-hands

# Select a specific model (e.g., yolo11s)
./engine/build/juggle_engine --use-dnn-tracker --model yolo11s

# Combine DNN tracking with high performance
./engine/build/juggle_engine --use-dnn-tracker --high-fps

# Custom output formats
./engine/build/juggle_engine --output-format=simple
./engine/build/juggle_engine --output-format=legacy
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

**Last Updated:** 2025-08-23 14:45:40 UTC

## 📋 Command Reference

This section provides a comprehensive list of commonly used commands for quick reference.

### Core System Commands

#### Starting the Hub
```bash
# Basic hub startup
./scripts/run_hub.sh

# Hub with virtual environment
./scripts/run_hub.sh --use-venv

# Hub with NPU acceleration
./scripts/run_hub.sh --use-venv --device NPU

# Hub with IMU streaming from watch
./scripts/run_hub.sh --use-venv --watch-ips 10.54.136.205

# Hub with multiple watches and NPU
./scripts/run_hub.sh --use-venv --device NPU --watch-ips 10.54.136.205 10.54.136.206

# Hub in console mode (no GUI)
./scripts/run_hub.sh --no-ui

# Hub with debug output
./scripts/run_hub.sh --debug
```

#### Building the System
```bash
# Build C++ engine
./scripts/build_engine.sh

# Build with debug symbols
./scripts/build_engine.sh --debug --verbose

# Generate Protocol Buffer files
./scripts/generate_protos.sh

# Create Python virtual environment and install dependencies
./scripts/run_hub.sh --create-venv --install-deps
```

### Real-Time Module Commands

#### Loading Modules
```bash
# Load PositionToRgbModule - tracks green ball position and controls LED ball color
PYTHONPATH=$(pwd)/hub python3 scripts/load_module.py PositionToRgbModule --ip 10.54.136.205

# Load UdpBallColorModule - enables direct ball color control
PYTHONPATH=$(pwd)/hub python3 scripts/load_module.py UdpBallColorModule --ip 10.54.136.205

# Load module with custom port
PYTHONPATH=$(pwd)/hub python3 scripts/load_module.py PositionToRgbModule --ip 10.54.136.205 --port 41412
```

### Ball Control Commands

#### Direct Color Control
```bash
# Make ball 205 blue (using hub interactive mode)
cd hub && source .venv/bin/activate && echo -e "load\ncolor 205 0 0 255\nquit" | python3 main.py

# Make ball 201 red
cd hub && source .venv/bin/activate && echo -e "load\ncolor 201 255 0 0\nquit" | python3 main.py

# Make ball 202 green
cd hub && source .venv/bin/activate && echo -e "load\ncolor 202 0 255 0\nquit" | python3 main.py

# Make ball 203 white
cd hub && source .venv/bin/activate && echo -e "load\ncolor 203 255 255 255\nquit" | python3 main.py
```

### Engine Commands

#### Direct Engine Usage
```bash
# Run engine with high performance
./engine/build/juggle_engine --high-fps

# Run engine with DNN tracking (AI-powered detection)
./engine/build/juggle_engine --use-dnn-tracker

# Run engine with DNN tracking and high performance
./engine/build/juggle_engine --use-dnn-tracker --high-fps

# Run engine with custom resolution
./engine/build/juggle_engine --width 1280 --height 720 --fps 60

# Run engine with timestamps
./engine/build/juggle_engine --timestamp

# Run engine with hand tracking (if compiled with support)
./engine/build/juggle_engine --track-hands

# Run engine in calibration mode
./engine/build/juggle_engine calibrate
```

#### Engine Output Formats
```bash
# Simple CSV output: timestamp_us,color_name,world_x,world_y,world_z,center_x,center_y,confidence
./engine/build/juggle_engine --output-format=simple

# Legacy CSV output: color_name,world_x,world_y,world_z,timestamp_us
./engine/build/juggle_engine --output-format=legacy

# Default human-readable output
./engine/build/juggle_engine --output-format=default

# DNN tracking with simple output format
./engine/build/juggle_engine --use-dnn-tracker --output-format=simple
```

### Development & Testing Commands

#### IMU Testing
```bash
# Start IMU simulator for testing
./scripts/imu_simulator.py --watch-id left_watch

# Connect hub to IMU simulator
./scripts/run_hub.sh --watch-ips 127.0.0.1
```

#### Debugging
```bash
# Engine debug output
./engine/build/bin/juggle_engine 2>engine_debug.log

# Hub debug mode
./scripts/run_hub.sh --debug

# Test ZMQ connection
python3 scripts/test_zmq.py
```

#### Testing Ball Communication
```bash
# Test UDP ball communication
python3 test_ball_udp.py

# Test blue ball specifically
python3 test_blue_ball.py
```

### Network Configuration

#### Default IP Ranges
- **Balls**: `10.54.136.X` where X is the ball ID (e.g., 205 for ball 205)
- **Watches**: Any IP address, commonly `10.54.136.X` or `192.168.1.X`
- **Port**: `41412` (UDP for balls), `8080` (WebSocket for watches)

#### Common IP Examples
```bash
# Ball IDs and their corresponding IPs
# Ball 201: 10.54.136.201
# Ball 202: 10.54.136.202
# Ball 203: 10.54.136.203
# Ball 205: 10.54.136.205

# Watch examples
# Left watch: 10.54.136.205 or 192.168.1.101
# Right watch: 10.54.136.206 or 192.168.1.102
```

### Quick Reference Summary

| Command Purpose | Command |
|----------------|---------|
| **Start hub with watch** | `./scripts/run_hub.sh --use-venv --watch-ips 10.54.136.205` |
| **Start hub with NPU** | `./scripts/run_hub.sh --use-venv --device NPU` |
| **Load position-to-color module** | `PYTHONPATH=$(pwd)/hub python3 scripts/load_module.py PositionToRgbModule --ip 10.54.136.205` |
| **Make ball blue** | `cd hub && source .venv/bin/activate && echo -e "load\ncolor 205 0 0 255\nquit" \| python3 main.py` |
| **Build engine** | `./scripts/build_engine.sh` |
| **Run with DNN tracking** | `./engine/build/juggle_engine --use-dnn-tracker` |
| **Calibrate colors** | `./engine/build/juggle_engine calibrate` |
| **Debug mode** | `./scripts/run_hub.sh --debug` |

**Last Updated:** 2025-08-23 14:45:40 UTC

## ⌚ Real-Time IMU Streaming

JuggleHub now supports real-time data streaming from smartwatches, providing synchronized IMU data alongside the video-based ball tracking. This enables the development of advanced applications that fuse motion data from the user's wrists with the positions of the juggling balls.

### Architecture

- **Protocol**: Data is streamed from the smartwatches to the Python hub using **WebSockets** for low-latency, persistent connections.
- **Format**: The IMU data (accelerometer and gyroscope) is sent as **JSON** objects.
- **Component**: A new, asynchronous `IMUListener` component (`hub/components/imu_listener.py`) runs in a separate thread to handle the WebSocket connections and data parsing without blocking the main application.

### Running with IMU Streaming

To run the hub with IMU streaming enabled, provide the IP addresses of your smartwatches using the `--watch-ips` command-line argument.

```bash
# Run with one watch
./scripts/run_hub.sh --watch-ips 192.168.1.101

# Run with two watches
./scripts/run_hub.sh --watch-ips 192.168.1.101 192.168.1.102
```

### Testing with the IMU Simulator

For development and testing without physical watches, you can use the included IMU simulator. The simulator runs a WebSocket server that mimics the behavior of a watch.

1.  **Start the simulator in a separate terminal:**
    ```bash
    # Make the script executable
    chmod +x scripts/imu_simulator.py

    # Run the simulator for a mock "left_watch"
    ./scripts/imu_simulator.py --watch-id left_watch
    ```
    You can run multiple instances of the simulator on different ports or with different watch IDs if needed.

2.  **Run the JuggleHub:**
    In another terminal, start the hub and connect to the simulator using the loopback address (`127.0.0.1`).
    ```bash
    ./scripts/run_hub.sh --watch-ips 127.0.0.1
    ```
    The hub will connect to the simulator, and you will see the IMU data being processed in the debug output.

## 🧪 Development Workflow

This project is designed for rapid development of new real-time applications without sacrificing performance.

### Building with Debug Info

To build the engine with debug symbols and verbose output:
```bash
./scripts/build_engine.sh --debug --verbose
```

### Workflow: Creating a New Real-Time Module

Here is the step-by-step process for adding a new interactive feature (e.g., a new game that colors the balls based on their height).

1.  **(API) Define Data Structures:** If your new module needs to send or receive unique data, add the new message types to [`api/v1/juggler.proto`](api/v1/juggler.proto).

2.  **(C++) Create the Module:**
    -   Create a new file in [`engine/src/modules/`](engine/src/modules/), for example, `HeightColorModule.cpp`.
    -   In this file, create a class that inherits from the `ModuleBase` interface.
    -   Implement the `setup()`, `update()`, and `cleanup()` methods. The `update()` function will contain your real-time logic that runs every frame.

3.  **(C++) Register the Module:**
    -   Open [`engine/src/Engine.cpp`](engine/src/Engine.cpp) (or a dedicated module factory file).
    -   Include the header for your new module.
    -   Add your new module to the list of available modules so the engine knows it exists and can be loaded.

4.  **(Build) Recompile the Engine:** Run `./scripts/build_engine.sh`. This will compile your new module and also regenerate the Protobuf files for both C++ and Python, ensuring everything is in sync.

5.  **(Python) Add Control Logic:**
    -   In the Python [`hub/`](hub/), modify the UI or application logic to send a command to the C++ engine to load your new module.

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

1. **Modify the API**: Update [`api/v1/juggler.proto`](api/v1/juggler.proto)
2. **Rebuild**: Run build scripts to regenerate Protocol Buffer files
3. **Update Engine**: Modify C++ code in [`engine/src/`](engine/src/)
4. **Update Hub**: Modify Python code in [`hub/components/`](hub/components/)

## 🗺️ Roadmap & Future Vision

This architecture provides a foundation for many advanced features:

-   **[ ] Advanced Coaching Modules**: Implement C++ modules for pattern analysis, consistency scoring, and real-time feedback.
-   **[x] Wearable Sensor Integration**: Add support for IMU data from wearables (gloves, watches) to the Protobuf API and fuse it with vision data in the engine.
-   **[ ] Web Interface**: Create a web-based dashboard using the Python hub as a backend to view juggling sessions from any device.
-   **[ ] FPGA/Hardware Acceleration**: Explore offloading parts of the vision pipeline (like color thresholding) to hardware like a Xilinx Kria for even lower latency.
-   **[ ] ROS Integration**: Add a ROS 2 compatibility layer to allow the engine to publish data as standard ROS topics.

## 📈 Performance

### Typical Performance Metrics

#### Color-Based Tracking
- **Engine**: 60-90 FPS at 848x480, 30-60 FPS at 1280x720
- **Hub**: Real-time processing with <10ms latency
- **Memory**: ~100MB engine, ~50MB hub
- **CPU**: 15-25% on modern quad-core systems

#### DNN-Based Tracking
- **Engine**: 30-60 FPS at 640x640 (model input), 20-45 FPS at higher resolutions
- **Inference Time**: 10-15ms per frame (YOLOv11n on modern CPU)
- **Memory**: ~150MB engine (additional 50MB for model), ~50MB hub
- **CPU**: 25-40% on modern quad-core systems
- **GPU Acceleration**: 2-5ms per frame (when available with OpenVINO GPU plugin)

### Optimization Tips

#### General Optimization
- Use `--high-fps` for maximum frame rate
- Reduce resolution for better performance
- Use `--downscale 0.5` for processing optimization
- Enable hardware acceleration where available

#### DNN Tracking Optimization
- Use YOLOv11n (nano) model for fastest inference
- Enable OpenVINO optimizations: `export OV_CPU_THREADS_NUM=4`
- Use GPU acceleration if available: Install OpenVINO GPU plugin
- Reduce model input resolution for speed vs accuracy trade-off
- Consider model quantization for embedded deployment

## 🔍 Troubleshooting

### Common Issues

**"Missing required dependencies: protobuf pyzmq"**
- This occurs when Python dependencies are not installed
- Solution: Run `./scripts/run_hub.sh --create-venv --install-deps`
- For subsequent runs, use `./scripts/run_hub.sh --use-venv`
- The script automatically handles virtual environment creation to avoid system package conflicts

**"No RealSense device found"**
- Ensure camera is connected to USB 3.0 port
- Check RealSense SDK installation
- Try different USB ports

**"Protocol Buffer files not found"**
- Run `./scripts/build_engine.sh` to generate files
- Check that `protoc` is installed and in PATH

**"OpenVINO model not found" or "DNN tracking failed"**
- Verify OpenVINO installation: `source /opt/intel/openvino_2025.2.0/setupvars.sh`
- Check model files exist: `ls -la engine/models/yolo11n.*`
- Rebuild with clean build: `./scripts/build_engine.sh --clean`
- Verify OpenVINO can load model: Test with sample OpenVINO applications

**"ZMQ connection failed"**
- We've identified an issue with ZMQ in some environments. If you're having trouble with ZMQ, you can switch to the standard I/O communication method. See the "Development Workflow" section for more details.

**Low frame rate**
- Close other camera applications
- Use lower resolution settings
- Check USB bandwidth limitations
- For DNN tracking: Reduce model input resolution or use GPU acceleration

**"Excessive debug output / console spam"**
- Debug logging is now controlled by the `JUGGLEHUB_DEBUG` environment variable
- By default, only important messages (INFO, WARN, ERROR) are shown
- Enable detailed debug logging: `JUGGLEHUB_DEBUG=1 ./scripts/run_hub.sh`
- See [`DEBUG_LOGGING_GUIDE.md`](DEBUG_LOGGING_GUIDE.md) for complete documentation

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

## 📹 Recording Features

JuggleHub includes two built-in recording features for capturing juggling sessions:

1. **5-Second Clip Recording**: Captures the last 5 seconds of frames when triggered
2. **Continuous Recording**: Start/stop recording that captures frames for any duration

Both recording systems save frames with consistent naming that includes timestamps for easy identification and organization.

### 5-Second Clip Recording

The 5-second recording feature maintains a rolling buffer of the last 150 frames (approximately 5 seconds at 30 FPS). When a recording is triggered via the `RECORD_START` command or by pressing the 'R' key in the UI, all buffered frames are saved to disk.

### Continuous Recording

The continuous recording feature allows you to start and stop recording at any time, capturing frames for any duration. This is perfect for recording entire juggling sessions or specific practice segments.

#### Using Continuous Recording

**Via UI:**
- Click the "Start Recording" button in the hub interface
- The button will change to "Stop Recording" and turn red
- The status indicator will show "● Recording" in red
- Click "Stop Recording" to end the session and save all frames

**Via Keyboard:**
- Press 'R' key for 5-second clip recording (existing feature)
- Use the UI buttons for continuous recording

#### How Continuous Recording Works

- Maintains a separate buffer that grows during recording (no frame limit)
- Frames are continuously added to the buffer while recording is active
- When stopped, all frames in the buffer are saved to disk
- Buffer is cleared when recording starts to ensure clean sessions

**Important Memory Considerations:**
- Continuous recording captures ALL frames from start to stop
- Memory usage grows with recording duration (~3MB per second at 30fps)
- For long recordings, monitor system memory and stop recording when needed
- Each frame uses approximately 100KB of memory (640x480 JPEG)

### File Organization

Recordings are saved in the `engine/data/1_raw_recordings/` directory with the following structure:

```
engine/data/1_raw_recordings/
├── rs455_2025-09-05_17-18-29/           # 5-second clip recording
│   ├── rs455_2025-09-05_17-18-29_frame_0.jpg
│   ├── rs455_2025-09-05_17-18-29_frame_1.jpg
│   └── ... (up to 150 frames)
├── continuous_2025-09-05_17-25-14/      # Continuous recording session
│   ├── continuous_2025-09-05_17-25-14_frame_0.jpg
│   ├── continuous_2025-09-05_17-25-14_frame_1.jpg
│   └── ... (variable number of frames)
└── continuous_2025-09-05_17-31-37/      # Another continuous session
    ├── continuous_2025-09-05_17-31-37_frame_0.jpg
    ├── continuous_2025-09-05_17-31-37_frame_1.jpg
    └── ...
```

### File Naming Convention

#### 5-Second Clip Recording
- **Directory**: `rs455_YYYY-MM-DD_HH-MM-SS` (rs455 prefix + timestamp when recording was triggered)
- **Files**: `rs455_YYYY-MM-DD_HH-MM-SS_frame_N.jpg` (rs455 prefix + same timestamp + frame number)

#### Continuous Recording
- **Directory**: `continuous_YYYY-MM-DD_HH-MM-SS` (continuous prefix + timestamp when recording started)
- **Files**: `continuous_YYYY-MM-DD_HH-MM-SS_frame_N.jpg` (continuous prefix + same timestamp + frame number)

This naming convention ensures:
- Easy chronological sorting
- Clear association between directory and contained files
- Unique identifiers for each frame across all recordings
- Consistent format for automated processing
- 'rs455_' prefix for easy identification and organization

### Triggering Recordings

Recordings can be triggered through the ZMQ command interface:

```bash
# Using the hub's command system
echo -e "record\nquit" | python3 hub/main.py
```

Or programmatically via the Protocol Buffer API by sending a `RECORD_START` command to the engine.

### Frame Naming Migration

**Note:** If you have existing recordings with the old naming format (`frame_N.jpg`), you can use the provided migration script to update them to the new format:

```bash
# Run the frame naming fix script
python3 scripts/fix_frame_names.py

# Or specify a custom data directory
python3 scripts/fix_frame_names.py /path/to/custom/data
```

The script will:
- Process all subdirectories in the data folder
- Rename files from `frame_N.jpg` to `rs455_YYYY-MM-DD_HH-MM-SS_frame_N.jpg`
- Skip files that are already in the correct format
- Provide detailed progress output

**Last Updated:** 2025-09-05 15:08:00 UTC

## 🤖 Dataset Preparation for AI Training

JuggleHub includes a comprehensive dataset preparation script that automatically splits your annotated images into training and validation sets using the industry-standard 80/20 rule for machine learning model training.

### The 80/20 Split Rule

The script follows the widely recommended best practice:
- **80% of images** go into the `train/` folder for model learning
- **20% of images** go into the `valid/` folder for model validation

This ratio provides the optimal balance between giving the model enough material to learn from while reserving a statistically significant number of images for reliable performance evaluation.

### Dataset Size Examples

| Total Annotated Images | Train Folder (80%) | Valid Folder (20%) |
|------------------------|-------------------|-------------------|
| 300 (V1 Goal)         | 240 images        | 60 images         |
| 500 (V2 Goal)         | 400 images        | 100 images        |
| 1,000 (Ambitious)     | 800 images        | 200 images        |

### Directory Structure

The script expects and creates the following structure:

```
JuggleHub/
├── 2_tagged_and_annotated/          # Source: Your annotated images
│   ├── good_lighting/               # Tag-based organization
│   │   ├── image001.jpg
│   │   ├── image001.txt             # YOLO format annotations
│   │   └── ...
│   ├── multiple_balls/
│   ├── high_contrast/
│   └── ...
└── 3_training_datasets/             # Output: Ready-to-train datasets
    ├── V1_generalist/
    │   ├── train/
    │   │   ├── images/              # 80% of images
    │   │   └── labels/              # 80% of annotations
    │   ├── valid/
    │   │   ├── images/              # 20% of images
    │   │   └── labels/              # 20% of annotations
    │   └── dataset.yaml             # YOLOv11 configuration
    └── V2_specialized/
        └── ...
```

### Usage Examples

#### Basic Usage
```bash
# Create V1 generalist dataset from specific tags
python3 scripts/prepare_dataset.py --dataset-name V1_generalist --tags good_lighting clear_balls multiple_balls

# Create V2 specialized dataset from high-quality tags
python3 scripts/prepare_dataset.py --dataset-name V2_specialized --tags perfect_lighting high_contrast professional_setup

# Include all available tags
python3 scripts/prepare_dataset.py --dataset-name complete_dataset --tags all
```

#### Advanced Options
```bash
# Custom split ratio (70/30 instead of 80/20)
python3 scripts/prepare_dataset.py --dataset-name custom_split --tags all --train-split 0.7

# Dry run to see what would happen without copying files
python3 scripts/prepare_dataset.py --dataset-name test_dataset --tags all --dry-run

# Custom directories and multiple classes
python3 scripts/prepare_dataset.py \
  --source-dir /path/to/annotations \
  --output-dir /path/to/datasets \
  --dataset-name multi_class \
  --tags all \
  --class-names ball club ring
```

### Key Features

#### Intelligent Processing
- **Random Shuffling**: Ensures both train and validation sets contain representative samples from all conditions
- **Tag-Based Selection**: Choose specific quality tags or include all available data
- **Reproducible Results**: Uses fixed random seed for consistent splits across runs
- **Safety Checks**: Validates input parameters and warns about missing files

#### Professional Output
- **YOLOv11 Compatible**: Creates proper directory structure and `dataset.yaml` configuration
- **Progress Tracking**: Shows detailed progress during file copying
- **Comprehensive Summary**: Reports final statistics and directory structure
- **Error Handling**: Clear error messages and validation

#### Flexible Configuration
- **Custom Split Ratios**: Not limited to 80/20 - use any ratio between 10/90 and 90/10
- **Multiple Classes**: Support for multi-class datasets beyond just "ball"
- **Custom Paths**: Specify custom source and output directories
- **Dry Run Mode**: Preview operations without making changes

### Script Output Example

```
Dataset Preparation Script
========================================
Dataset Name: V1_generalist
Source Directory: 2_tagged_and_annotated
Output Directory: 3_training_datasets/V1_generalist
Tags: good_lighting, clear_balls, multiple_balls
Train/Valid Split: 0.8/0.2
Random Seed: 42

Processing 3 tag directories:
  - good_lighting
  - clear_balls
  - multiple_balls

Scanning good_lighting...
  Found 120 image/annotation pairs
Scanning clear_balls...
  Found 95 image/annotation pairs
Scanning multiple_balls...
  Found 85 image/annotation pairs

Found 300 total image/annotation pairs

Splitting 300 pairs:
  Training: 240 pairs (80%)
  Validation: 60 pairs (20%)

Copying training files...
  Copied 240/240 training pairs
Copying validation files...
  Copied 60/60 validation pairs

Created dataset configuration: 3_training_datasets/V1_generalist/dataset.yaml

============================================================
DATASET PREPARATION COMPLETE
============================================================
Dataset Name: V1_generalist
Output Directory: 3_training_datasets/V1_generalist
Source Tags: good_lighting, clear_balls, multiple_balls

Dataset Statistics:
  Total Images: 300
  Training: 240 (80.0%)
  Validation: 60 (20.0%)

Directory Structure:
  3_training_datasets/V1_generalist/
  ├── train/
  │   ├── images/ (240 files)
  │   └── labels/ (240 files)
  ├── valid/
  │   ├── images/ (60 files)
  │   └── labels/ (60 files)
  └── dataset.yaml

Ready for YOLOv11 training!
Use: yolo train data=3_training_datasets/V1_generalist/dataset.yaml model=yolo11n.pt
```

### Integration with YOLOv11

The script creates datasets that are immediately ready for YOLOv11 training:

```bash
# Train a YOLOv11 model with your prepared dataset
yolo train data=3_training_datasets/V1_generalist/dataset.yaml model=yolo11n.pt epochs=100 imgsz=640

# Train with custom parameters
yolo train data=3_training_datasets/V2_specialized/dataset.yaml model=yolo11s.pt epochs=200 imgsz=640 batch=16
```

### Best Practices

1. **Quality Over Quantity**: Focus on high-quality, diverse annotations rather than just volume
2. **Tag Organization**: Use descriptive tag names that reflect image characteristics
3. **Balanced Representation**: Ensure all important scenarios are represented in your tags
4. **Validation**: Always run with `--dry-run` first to verify your selection
5. **Reproducibility**: Keep the same random seed for consistent results across experiments

**Last Updated:** 2025-08-31 09:04:00 UTC

## 🔧 Utility Scripts

### File Prefix Script

JuggleHub includes a utility script for adding prefixes to files and directories, useful for organizing datasets or preparing files for specific processing workflows.

#### [`scripts/add_prefix.py`](scripts/add_prefix.py)

A simple script that adds the 'rs455_' prefix (or any custom prefix) to the beginning of every file and directory name in a given directory.

**Features:**
- Add custom prefix to files and directories
- Optional recursive processing of subdirectories
- Dry-run mode to preview changes
- Skip files that already have the prefix
- Safe handling of naming conflicts

**Usage Examples:**

```bash
# Basic usage - add 'rs455_' prefix to all items in a directory
python3 scripts/add_prefix.py /path/to/directory

# Recursive processing - include all subdirectories
python3 scripts/add_prefix.py /path/to/directory --recursive

# Dry run - see what would be renamed without making changes
python3 scripts/add_prefix.py /path/to/directory --dry-run

# Custom prefix
python3 scripts/add_prefix.py /path/to/directory --prefix "custom_prefix_"

# Combine options
python3 scripts/add_prefix.py /path/to/directory --recursive --dry-run --prefix "dataset_v2_"
```

**Command Line Options:**
- `directory`: Directory path to process (required)
- `-r, --recursive`: Process subdirectories recursively
- `--prefix PREFIX`: Custom prefix to add (default: rs455_)
- `--dry-run`: Show what would be renamed without actually renaming

**Example Output:**
```
Processing directory: /tmp/test_directory
Prefix: 'rs455_'
Recursive: True
Dry run: False
--------------------------------------------------
Renamed: 'file1.txt' -> 'rs455_file1.txt'
Renamed: 'subdir1' -> 'rs455_subdir1'
Processing subdirectory: /tmp/test_directory/rs455_subdir1
Renamed: 'nested_file.txt' -> 'rs455_nested_file.txt'
--------------------------------------------------
Successfully renamed 3 items.
```

**Safety Features:**
- Skips files that already have the specified prefix
- Warns about naming conflicts and skips problematic renames
- Provides detailed progress output
- Supports dry-run mode for safe testing

## 🗃️ Database and Runtime Files

JuggleHub automatically creates SQLite database files during operation to store juggling session data, including ball tracking information, IMU data, and performance metrics. These files are automatically excluded from version control.

### Database Files

The following database files are created during runtime and are automatically ignored by git:

- **`hub/juggling_data.db`**: Main SQLite database containing session data
- **`hub/juggling_data.db-shm`**: Shared memory file for SQLite WAL mode
- **`hub/juggling_data.db-wal`**: Write-ahead log file for SQLite transactions

### Why Database Files Are Ignored

Database files are excluded from version control because they:

- **Contain runtime data**: Generated during application usage, not source code
- **Can grow very large**: May exceed GitHub's 100MB file size limit
- **Are user-specific**: Contain personal juggling session data
- **Are regenerated**: Created automatically when the application runs

### Database Location and Configuration

By default, the database is created as `juggling_data.db` in the hub directory. You can specify a custom location using the `--database-path` argument:

```bash
# Use custom database location
./scripts/run_hub.sh --use-venv --database-path /path/to/custom/juggling_data.db

# Disable database logging entirely
./scripts/run_hub.sh --use-venv --no-logging
```

### Data Persistence

While database files are not tracked in git, your juggling session data persists locally. To backup or share session data:

```bash
# Backup your database
cp hub/juggling_data.db ~/backups/juggling_data_$(date +%Y%m%d).db

# View session statistics
sqlite3 hub/juggling_data.db "SELECT session_id, start_time, total_frames, total_balls FROM sessions;"
```

**Last Updated:** 2025-09-22 10:29:00 UTC