cmake_minimum_required(VERSION 3.15)
project(JuggleHubEngine CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set default build type to RelWithDebInfo if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Choose the type of build (Debug, Release, RelWithDebInfo, MinSizeRel)" FORCE)
endif()

# Compiler flags for different build types
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
# FPS OPTIMIZATION: Aggressive optimization with CPU-specific instructions
# -O3: Maximum optimization level (includes vectorization, loop unrolling, etc.)
# -march=native: Generate code optimized for the current CPU architecture
# -mtune=native: Tune code scheduling for the current CPU
# -ffast-math: Allow aggressive floating-point optimizations (safe for our use case)
# Expected gain: 5-10% performance improvement from better CPU utilization
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -mtune=native -ffast-math -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g -DNDEBUG")
set(CMAKE_CXX_FLAGS_MINSIZEREL "-Os -DNDEBUG")

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# --- Find All Dependencies ---
include(FetchContent)

find_package(OpenCV REQUIRED)
include_directories(/opt/intel/openvino_2025.2.0/runtime/include)
link_directories(/opt/intel/openvino_2025.2.0/runtime/lib/intel64)
find_package(Eigen3 REQUIRED)
find_package(Protobuf REQUIRED)
find_package(realsense2 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZeroMQ REQUIRED libzmq)

# Fetch and configure Asio
FetchContent_Declare(
  asio
  GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
  GIT_TAG asio-1-28-1 # Use a specific tag for stability
)
FetchContent_MakeAvailable(asio)

# --- Protobuf Generation ---
set(PROTOBUF_GENERATE_CPP_APPEND_PATH ON)
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ../api/v1/juggler.proto)

# --- Define the Main Executable ---
add_executable(juggle_engine
    src/main.cpp
    src/Engine.cpp
    src/SimpleBallTracker.cpp
    src/Simple2DBallTracker.cpp
    src/GpuHsvConverter.cpp
    src/GpuTrajectoryPredictor.cpp
    src/modules/UdpBallColorModule.cpp
    src/modules/PositionToRgbModule.cpp
    src/networking/UdpSender.cpp
    src/modules/UdpBallSettingsModule.cpp
    ${PROTO_SRCS} ${PROTO_HDRS}
)

# --- Link All Libraries to the Executable ---
target_link_libraries(juggle_engine PRIVATE
    # OpenVINO and OpenCV
    openvino
    ${OpenCV_LIBS}

    # Other dependencies
    realsense2::realsense2
    protobuf::libprotobuf
    ${ZeroMQ_LIBRARIES}
    Eigen3::Eigen
)

# Tell the executable where to find headers
target_include_directories(juggle_engine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include # For our own headers
    ${CMAKE_BINARY_DIR} # For generated proto headers
    ${ZeroMQ_INCLUDE_DIRS}
    /opt/intel/openvino_2025.2.0/runtime/include
)
target_include_directories(juggle_engine PRIVATE ${asio_SOURCE_DIR}/include)

# --- Installation (Good Practice) ---
install(TARGETS juggle_engine DESTINATION bin)