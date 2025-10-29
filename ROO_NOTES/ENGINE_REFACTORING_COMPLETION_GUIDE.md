# Engine.cpp Refactoring Completion Guide

**Date:** 2025-10-29  
**Status:** Infrastructure Complete - Integration Pending  
**Current Engine.cpp Size:** 2714 lines  
**Target:** Reduce to ~500-800 lines by delegating to specialized classes

---

## 🎯 OBJECTIVE

Complete the refactoring of `engine/src/Engine.cpp` by moving existing implementations into the newly created specialized classes and updating Engine.cpp to use them. **NO FUNCTIONALITY MUST BE LOST.**

---

## 📋 CURRENT STATE

### ✅ Completed Infrastructure

The following files have been created and are ready:

1. **`engine/include/CameraIntrinsics.hpp`** - Shared camera intrinsics type
2. **`engine/include/CameraManager.hpp`** + **`engine/src/CameraManager.cpp`** (240 lines)
3. **`engine/include/RecordingManager.hpp`** + **`engine/src/RecordingManager.cpp`** (250 lines)
4. **`engine/include/PlaybackController.hpp`** + **`engine/src/PlaybackController.cpp`** (195 lines)
5. **`engine/include/CommandProcessor.hpp`** + **`engine/src/CommandProcessor.cpp`** (424 lines)
6. **`engine/include/VisualizationRenderer.hpp`** + **`engine/src/VisualizationRenderer.cpp`** (177 lines)
7. **`engine/include/Engine.hpp`** - Updated with new class members (lines 66-70)
8. **`engine/CMakeLists.txt`** - Updated to compile new source files

### ⚠️ Current Problem

The new classes contain **STUB IMPLEMENTATIONS ONLY**. They have the structure but not the actual code from Engine.cpp. Engine.cpp still contains all 2714 lines of original code.

---

## 🔧 WHAT NEEDS TO BE DONE

### Phase 1: Move Camera Operations to CameraManager

**Files to modify:**
- `engine/src/CameraManager.cpp`
- `engine/src/Engine.cpp`

**Methods to move from Engine.cpp to CameraManager.cpp:**

1. **`initializeCamera()`** (lines 1272-1287 in Engine.cpp)
   - Move to `CameraManager::initialize()`
   - Handles: Loading settings, configuring streams

2. **`loadCameraSettingsFromJson()`** (lines 1289-1305 in Engine.cpp)
   - Move to `CameraManager::loadSettingsFromJson()`
   - Handles: Reading JSON settings file

3. **`applyCameraSettings()`** (lines 1307-1328 in Engine.cpp)
   - Move to `CameraManager::applySettings()`
   - Handles: Applying JSON settings to active pipeline

4. **`startCamera()`** (lines 1344-1405 in Engine.cpp)
   - Move to `CameraManager::start()`
   - Handles: Starting pipeline, getting intrinsics, enabling IR projector

5. **`stopCamera()`** (lines 1330-1342 in Engine.cpp)
   - Move to `CameraManager::stop()`
   - Handles: Stopping pipeline

6. **`startCameraWithSettings()`** (lines 1407-1443 in Engine.cpp)
   - Move to `CameraManager::startWithSettings()`
   - Handles: Reconfiguring and restarting camera

**Member variables to move:**
```cpp
// From Engine.hpp (currently missing, need to add to Engine.cpp):
rs2::pipeline pipe_;
rs2::config rs_config_;
rs2::align align_to_color_;
std::string camera_settings_path_;
std::string json_content_;
bool camera_running_;
bool ir_projector_active_;
uint32_t camera_width_;
uint32_t camera_height_;
uint32_t camera_fps_;
CameraIntrinsics camera_intrinsics_;
```

**Frame capture logic** (lines 171-199 in Engine.cpp):
```cpp
// This code in run() method:
rs2::frameset frames = pipe_.wait_for_frames(5000);
rs2::frameset aligned_frames = align_to_color_.process(frames);
// ... convert to cv::Mat
```
Move to `CameraManager::captureFrame(cv::Mat& color, cv::Mat& depth)`

---

### Phase 2: Move Recording Operations to RecordingManager

**Files to modify:**
- `engine/src/RecordingManager.cpp`
- `engine/src/Engine.cpp`

**Methods to move:**

1. **`saveRecording()`** (lines 995-1113 in Engine.cpp)
   - Move to `RecordingManager::savePreRecordingBuffer()`
   - Handles: Saving the 150-frame pre-recording buffer

2. **`startContinuousRecording()`** (lines 1115-1137 in Engine.cpp)
   - Move to `RecordingManager::startContinuous()`
   - Handles: Starting continuous recording session

3. **`stopContinuousRecording()`** (lines 1139-1270 in Engine.cpp)
   - Move to `RecordingManager::stopContinuous()`
   - Handles: Stopping and saving continuous recording

**Member variables to move:**
```cpp
std::deque<RecordingFrame> frame_buffer_;  // Pre-recording buffer (150 frames)
std::mutex frame_buffer_mutex_;
std::deque<RecordingFrame> continuous_frame_buffer_;  // Active recording buffer
std::mutex continuous_frame_buffer_mutex_;
bool continuous_recording_;
std::string continuous_recording_session_;
RecordingLogger recording_logger_;
```

**Frame buffer management** (lines 452-466 in Engine.cpp):
```cpp
// Add to frame buffers
RecordingFrame rec_frame = {...};
{
    std::lock_guard<std::mutex> lock(frame_buffer_mutex_);
    frame_buffer_.push_back(rec_frame);
    if (frame_buffer_.size() > 150) {
        frame_buffer_.pop_front();
    }
}
if (continuous_recording_) {
    std::lock_guard<std::mutex> lock(continuous_frame_buffer_mutex_);
    continuous_frame_buffer_.push_back(rec_frame);
}
```
Move to `RecordingManager::addFrame()`

---

### Phase 3: Move Playback Operations to PlaybackController

**Files to modify:**
- `engine/src/PlaybackController.cpp`
- `engine/src/Engine.cpp`

**Methods to move:**

1. **`startPlayback()`** (lines 2604-2625 in Engine.cpp)
   - Move to `PlaybackController::start()`
   - Handles: Loading recording and entering playback mode

2. **`stopPlayback()`** (lines 2627-2640 in Engine.cpp)
   - Move to `PlaybackController::stop()`
   - Handles: Exiting playback mode

3. **`stepPlaybackForward()`** (lines 2642-2663 in Engine.cpp)
   - Move to `PlaybackController::stepForward()`
   - Handles: Advancing one frame

4. **`stepPlaybackBackward()`** (lines 2665-2686 in Engine.cpp)
   - Move to `PlaybackController::stepBackward()`
   - Handles: Going back one frame

5. **`setPlaybackSpeed()`** (lines 2688-2695 in Engine.cpp)
   - Move to `PlaybackController::setSpeed()`
   - Handles: Adjusting playback speed

6. **`pausePlayback()`** (lines 2697-2704 in Engine.cpp)
   - Move to `PlaybackController::pause()`

7. **`resumePlayback()`** (lines 2706-2714 in Engine.cpp)
   - Move to `PlaybackController::resume()`

**Member variables to move:**
```cpp
std::unique_ptr<PlaybackManager> playback_manager_;
bool playback_mode_;
std::chrono::steady_clock::time_point last_playback_frame_time_;
```

**Playback frame logic** (lines 128-170 in Engine.cpp):
```cpp
if (playback_mode_ && playback_manager_->isLoaded()) {
    if (!playback_manager_->isPaused()) {
        // Calculate frame timing...
        if (playback_manager_->getNextFrame(color_image, depth_image)) {
            // ...
        }
    }
}
```
Move to `PlaybackController::getNextFrame()`

---

### Phase 4: Move Command Processing to CommandProcessor

**Files to modify:**
- `engine/src/CommandProcessor.cpp`
- `engine/src/Engine.cpp`

**Methods to move:**

1. **`processCommands()`** (lines 691-977 in Engine.cpp)
   - Move to `CommandProcessor::processCommands()`
   - This is the LARGEST method - 286 lines
   - Handles: All ZMQ command processing

2. **`sendCommand()`** (lines 979-982 in Engine.cpp)
   - Move to `CommandProcessor::sendCommand()`
   - Handles: Internal command queueing

3. **`create_module()`** (lines 984-993 in Engine.cpp)
   - Move to `CommandProcessor::createModule()`
   - Handles: Module factory

**Member variables to move:**
```cpp
zmq::socket_t zmq_commander_;  // Note: zmq_publisher_ stays in Engine
std::queue<juggler::v1::CommandRequest> command_queue_;
std::mutex command_queue_mutex_;
std::unique_ptr<ModuleBase> active_module_;
```

**Command processing includes:**
- LOAD_MODULE / UNLOAD_MODULE / CONFIGURE_MODULE
- RECORD_START / RECORD_CONTINUOUS_START / RECORD_CONTINUOUS_STOP
- CAMERA_STOP / CAMERA_START
- CALIBRATE_OBJECT / CALIBRATE_COLOR
- SET_POSE_MODEL_ENABLED / SET_VIDEO_FEED_ENABLED
- SET_TRACKER_TYPE / SET_DEPTH_SENSOR_ENABLED
- RELOAD_COLOR_PROFILES
- PLAYBACK_START / PLAYBACK_STOP / PLAYBACK_STEP_FORWARD / PLAYBACK_STEP_BACKWARD
- PLAYBACK_SET_SPEED / PLAYBACK_PAUSE / PLAYBACK_RESUME
- SET_VISUALIZATION_STATES
- ENABLE_FEATURE / DISABLE_FEATURE

---

### Phase 5: Move Visualization Rendering to VisualizationRenderer

**Files to modify:**
- `engine/src/VisualizationRenderer.cpp`
- `engine/src/Engine.cpp`

**Methods to move:**

1. **`renderVisualizationsOnFrame()`** (lines 1444-2554 in Engine.cpp)
   - Move to `VisualizationRenderer::render()`
   - This is the SECOND LARGEST method - 1110 lines!
   - Handles: All visualization overlays for recordings

**This method draws:**
- Raw YOLO detections (dark red boxes)
- Filtered YOLO detections (bright red boxes)
- YOLO color calibration squares
- Hand tracking (wrist circles, skeleton)
- Color-tracked balls (colored letters)
- Final tracker visualization
- Trajectory points and predictions
- Hand threshold circles
- Hand velocity zones
- Throw/catch event info
- Info panel with text wrapping

---

### Phase 6: Update Engine.cpp to Use New Classes

**File to modify:**
- `engine/src/Engine.cpp`

**Constructor changes** (lines 24-86):
```cpp
// REMOVE old member variable initializations:
camera_settings_path_(camera_settings_path),
align_to_color_(RS2_STREAM_COLOR),
camera_running_(false),
ir_projector_active_(false),
camera_width_(640),
camera_height_(480),
camera_fps_(60),
playback_manager_(std::make_unique<PlaybackManager>()),
playback_mode_(false)

// ADD new class initializations:
camera_manager_(std::make_unique<CameraManager>(camera_settings_path)),
recording_manager_(std::make_unique<RecordingManager>()),
playback_controller_(std::make_unique<PlaybackController>()),
command_processor_(std::make_unique<CommandProcessor>(...)),
visualization_renderer_(std::make_unique<VisualizationRenderer>())
```

**run() method changes** (lines 92-682):

Replace direct calls with delegated calls:
```cpp
// OLD:
initializeCamera();
startCamera();

// NEW:
camera_manager_->initialize();
camera_manager_->start();

// OLD:
if (camera_running_) {
    rs2::frameset frames = pipe_.wait_for_frames(5000);
    // ...
}

// NEW:
if (camera_manager_->isRunning()) {
    camera_manager_->captureFrame(color_image, depth_image);
}

// OLD:
std::thread command_thread(&Engine::processCommands, this);

// NEW:
std::thread command_thread([this]() {
    command_processor_->processCommands();
});

// OLD:
RecordingFrame rec_frame = {...};
frame_buffer_.push_back(rec_frame);

// NEW:
recording_manager_->addFrame(...);
```

**Remove old method implementations:**
- Delete lines 691-977 (processCommands)
- Delete lines 979-982 (sendCommand)
- Delete lines 984-993 (create_module)
- Delete lines 995-1113 (saveRecording)
- Delete lines 1115-1137 (startContinuousRecording)
- Delete lines 1139-1270 (stopContinuousRecording)
- Delete lines 1272-1287 (initializeCamera)
- Delete lines 1289-1305 (loadCameraSettingsFromJson)
- Delete lines 1307-1328 (applyCameraSettings)
- Delete lines 1330-1342 (stopCamera)
- Delete lines 1344-1405 (startCamera)
- Delete lines 1407-1443 (startCameraWithSettings)
- Delete lines 1444-2554 (renderVisualizationsOnFrame)
- Delete lines 2604-2625 (startPlayback)
- Delete lines 2627-2640 (stopPlayback)
- Delete lines 2642-2663 (stepPlaybackForward)
- Delete lines 2665-2686 (stepPlaybackBackward)
- Delete lines 2688-2695 (setPlaybackSpeed)
- Delete lines 2697-2704 (pausePlayback)
- Delete lines 2706-2714 (resumePlayback)

**Keep in Engine.cpp:**
- Constructor / Destructor
- run() method (but simplified with delegated calls)
- stop() method
- setTrackerType() method (lines 2556-2602)
- Tracker management (simple_tracker_, simple_2d_tracker_, new_3d_tracker_)
- ZMQ publisher (zmq_publisher_)
- Module system (color_module_, settings_module_)
- Frame tracking (frame_counter_, last_raw_detections_, last_tracked_objects_)

---

## 🔍 CRITICAL DEPENDENCIES TO MAINTAIN

### 1. Camera Intrinsics Flow
```
CameraManager::start() 
  → Gets intrinsics from RealSense
  → Stores in camera_intrinsics_
  → Engine::run() calls camera_manager_->getIntrinsics()
  → Passes to tracker_->update(color, depth, intrinsics)
```

### 2. Recording Flow
```
Engine::run() 
  → Captures frame
  → Tracker processes frame
  → recording_manager_->addFrame(...)
  → Adds to pre-recording buffer (always)
  → Adds to continuous buffer (if recording)
```

### 3. Playback Flow
```
playback_controller_->start(directory)
  → Loads PlaybackManager
  → Sets playback_mode_ = true
Engine::run()
  → Checks playback_controller_->isInPlaybackMode()
  → Gets frames from playback_controller_->getNextFrame()
```

### 4. Command Flow
```
ZMQ command arrives
  → command_processor_->processCommands()
  → Calls camera_manager_->start()
  → Calls recording_manager_->startContinuous()
  → Calls playback_controller_->start()
  → Sends ZMQ response
```

### 5. Visualization Flow
```
recording_manager_->stopContinuous()
  → Saves frames without visualizations
  → If visualizations enabled:
    → For each frame:
      → visualization_renderer_->render(frame, rec_frame)
      → Saves frame with visualizations
```

---

## ⚠️ CRITICAL WARNINGS

### 1. Thread Safety
- `frame_buffer_` and `continuous_frame_buffer_` are accessed from multiple threads
- MUST maintain mutex locks when moving to RecordingManager
- Command processing runs in separate thread

### 2. Shared State
- `camera_intrinsics_` is used by tracker, recording, and visualization
- `visualization_states_` is used by recording and real-time feed
- `tracker_` pointer is used by command processor and main loop

### 3. Initialization Order
- Camera MUST be initialized before tracker can run
- Settings module MUST be created after tracker
- Command processor MUST have access to all other managers

### 4. ZMQ Sockets
- `zmq_publisher_` stays in Engine (publishes frame data)
- `zmq_commander_` moves to CommandProcessor (receives commands)
- Both use same `zmq_context_`

---

## 📝 STEP-BY-STEP EXECUTION PLAN

### Step 1: Move Camera Operations (Safest First)
1. Copy camera methods from Engine.cpp to CameraManager.cpp
2. Update method signatures to match CameraManager.hpp
3. Move member variables to CameraManager
4. Update Engine.cpp constructor to initialize CameraManager
5. Update Engine::run() to call camera_manager_->methods()
6. Remove old camera methods from Engine.cpp
7. **COMPILE AND TEST**

### Step 2: Move Recording Operations
1. Copy recording methods from Engine.cpp to RecordingManager.cpp
2. Move frame buffer member variables
3. Update Engine::run() to call recording_manager_->addFrame()
4. Update command processor calls (will do in Step 4)
5. Remove old recording methods from Engine.cpp
6. **COMPILE AND TEST**

### Step 3: Move Playback Operations
1. Copy playback methods from Engine.cpp to PlaybackController.cpp
2. Move PlaybackManager member variable
3. Update Engine::run() playback logic
4. Update command processor calls (will do in Step 4)
5. Remove old playback methods from Engine.cpp
6. **COMPILE AND TEST**

### Step 4: Move Command Processing (Most Complex)
1. Copy processCommands() from Engine.cpp to CommandProcessor.cpp
2. Update to call camera_manager_, recording_manager_, playback_controller_
3. Move zmq_commander_ and command queue
4. Update Engine::run() to start command_processor_ thread
5. Remove old command methods from Engine.cpp
6. **COMPILE AND TEST**

### Step 5: Move Visualization Rendering (Largest Method)
1. Copy renderVisualizationsOnFrame() to VisualizationRenderer.cpp
2. Update signature to accept all needed parameters
3. Update RecordingManager to call visualization_renderer_->render()
4. Remove old renderVisualizationsOnFrame() from Engine.cpp
5. **COMPILE AND TEST**

### Step 6: Final Cleanup
1. Remove all unused member variables from Engine.hpp
2. Remove unused includes from Engine.cpp
3. Verify Engine.cpp is now ~500-800 lines
4. **FINAL COMPILE AND FULL SYSTEM TEST**

---

## 🧪 TESTING CHECKLIST

After each step, verify:
- [ ] Project compiles without errors
- [ ] Camera starts and captures frames
- [ ] Tracker detects balls and hands
- [ ] Recording saves frames correctly
- [ ] Playback loads and plays recordings
- [ ] Commands are processed and responded to
- [ ] Visualizations render correctly in recordings
- [ ] No memory leaks (valgrind if possible)
- [ ] No crashes during normal operation

---

## 📚 FILES TO REFERENCE

### Primary Files to Modify:
1. `engine/src/CameraManager.cpp` - Add camera implementations
2. `engine/src/RecordingManager.cpp` - Add recording implementations
3. `engine/src/PlaybackController.cpp` - Add playback implementations
4. `engine/src/CommandProcessor.cpp` - Add command processing
5. `engine/src/VisualizationRenderer.cpp` - Add rendering implementation
6. `engine/src/Engine.cpp` - Remove old code, add delegated calls

### Reference Files (Don't Modify):
1. `engine/include/CameraManager.hpp` - Camera interface
2. `engine/include/RecordingManager.hpp` - Recording interface
3. `engine/include/PlaybackController.hpp` - Playback interface
4. `engine/include/CommandProcessor.hpp` - Command interface
5. `engine/include/VisualizationRenderer.hpp` - Rendering interface
6. `engine/include/Engine.hpp` - Engine interface (already updated)
7. `engine/include/CameraIntrinsics.hpp` - Shared type
8. `engine/include/IBallTracker.hpp` - Tracker interface
9. `engine/include/SimpleBallTracker.hpp` - Tracker implementation

### Supporting Files:
1. `engine/CMakeLists.txt` - Already updated
2. `engine/include/PlaybackManager.hpp` - Used by PlaybackController
3. `engine/include/RecordingLogger.hpp` - Used by RecordingManager
4. `engine/src/modules/ModuleBase.hpp` - Used by CommandProcessor

---

## 💡 TIPS FOR SUCCESS

1. **Work incrementally** - Complete one phase at a time
2. **Compile frequently** - After every major change
3. **Keep git commits small** - One phase per commit
4. **Test after each phase** - Don't accumulate untested changes
5. **Preserve exact behavior** - Copy-paste code, don't rewrite
6. **Maintain thread safety** - Keep all mutex locks
7. **Watch for shared state** - Pass by reference where needed
8. **Check includes** - Add missing headers as needed
9. **Verify line numbers** - They may shift as you edit
10. **Use search** - Find all usages before removing code

---

## 🎯 SUCCESS CRITERIA

When complete:
- ✅ Engine.cpp is 500-800 lines (down from 2714)
- ✅ All functionality works identically
- ✅ Project compiles without warnings
- ✅ All tests pass
- ✅ Code is more maintainable and modular
- ✅ Each class has a single, clear responsibility

---

## 📞 QUESTIONS TO ASK IF STUCK

1. "Does this method access any Engine member variables?"
   - If yes, those variables need to be passed or moved

2. "Is this method called from multiple places?"
   - If yes, ensure all call sites are updated

3. "Does this method need to be thread-safe?"
   - If yes, maintain or add mutex locks

4. "What does this method depend on?"
   - Ensure dependencies are available in new class

5. "What depends on this method?"
   - Ensure callers can still access it

---

**Good luck! This is a significant refactoring but the infrastructure is solid. Take it one phase at a time and test frequently.**