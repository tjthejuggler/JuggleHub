cmake_minimum_required(VERSION 3.15)
project(JuggleHubEngine CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

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

# --- Add the ByteTrack Sub-Project ---
# This is a more robust way to include the library.
# It tells CMake to build the bytetrack target from its own CMakeLists.txt.
add_subdirectory(${CMAKE_SOURCE_DIR}/vendor/ByteTrack-cpp)

# --- Protobuf Generation ---
set(PROTOBUF_GENERATE_CPP_APPEND_PATH ON)
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ../api/v1/juggler.proto)

# --- Define the Main Executable ---
add_executable(juggle_engine
    src/main.cpp
    src/Engine.cpp
    src/SimpleBallTracker.cpp
    src/KalmanFilter3D.cpp
    src/modules/UdpBallColorModule.cpp
    src/modules/PositionToRgbModule.cpp
    src/networking/UdpSender.cpp
    src/modules/UdpBallSettingsModule.cpp
    ${PROTO_SRCS} ${PROTO_HDRS}
)

# --- Link All Libraries to the Executable ---
target_link_libraries(juggle_engine PRIVATE
    # Our compiled bytetrack library target
    bytetrack

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
    ${CMAKE_SOURCE_DIR}/vendor/ByteTrack-cpp/include
)
target_include_directories(juggle_engine PRIVATE ${asio_SOURCE_DIR}/include)
target_link_libraries(juggle_engine PUBLIC bytetrack)

# --- Installation (Good Practice) ---
install(TARGETS juggle_engine DESTINATION bin)