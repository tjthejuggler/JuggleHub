cmake_minimum_required(VERSION 3.15)
project(JuggleEngine)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZMQ REQUIRED libzmq)
pkg_check_modules(realsense2 REQUIRED realsense2)
find_package(OpenCV REQUIRED)
# Find OpenVINO development package
# This requires the setupvars.sh script to have been sourced
find_package(OpenVINO REQUIRED)
# Find the Eigen3 library dependency
find_package(Eigen3 REQUIRED)

# Define the location of the ByteTrack-cpp project we just built
set(BYTETRACK_PROJECT_DIR ${CMAKE_SOURCE_DIR}/vendor/ByteTrack-cpp)
find_package(Protobuf REQUIRED)

# Generate Protocol Buffer files
set(PROTO_FILES "${CMAKE_SOURCE_DIR}/../api/v1/juggler.proto")
set(PROTO_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY ${PROTO_GENERATED_DIR})

# Custom command to generate protobuf files
add_custom_command(
    OUTPUT "${PROTO_GENERATED_DIR}/juggler.pb.cc" "${PROTO_GENERATED_DIR}/juggler.pb.h"
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
    ARGS --cpp_out=${PROTO_GENERATED_DIR}
         --proto_path=${CMAKE_SOURCE_DIR}/../api/v1
         ${PROTO_FILES}
    DEPENDS ${PROTO_FILES}
    COMMENT "Generating Protocol Buffer files"
)

# Create a library for generated protobuf files
add_library(juggler_proto
    "${PROTO_GENERATED_DIR}/juggler.pb.cc"
    "${PROTO_GENERATED_DIR}/juggler.pb.h"
)

target_link_libraries(juggler_proto
    ${Protobuf_LIBRARIES}
)

target_include_directories(juggler_proto PUBLIC
    ${PROTO_GENERATED_DIR}
    ${Protobuf_INCLUDE_DIRS}
)

# Add an option to enable/disable hand tracking
option(ENABLE_HAND_TRACKING "Enable MediaPipe hand tracking feature" OFF)

# Create the main executable
add_executable(juggle_engine
    src/main.cpp
    src/BallTracker.cpp
    src/Engine.cpp
    src/DNNTracker.cpp
    src/networking/UdpSender.cpp
    src/modules/UdpBallColorModule.cpp
    src/modules/PositionToRgbModule.cpp
)

# Include directories
target_include_directories(juggle_engine PRIVATE
    include
    src/modules
    src/networking
    ${realsense2_INCLUDE_DIRS}
    ${OpenCV_INCLUDE_DIRS}
    ${Protobuf_INCLUDE_DIRS}
    ${PROTO_GENERATED_DIR}
    ${ZMQ_INCLUDE_DIRS}
    ${BYTETRACK_PROJECT_DIR}/include
    ${EIGEN3_INCLUDE_DIRS}
)

# Tell our juggle_engine where to find the compiled .so library file
target_link_directories(juggle_engine PRIVATE
    ${BYTETRACK_PROJECT_DIR}/build
)

# Link libraries
target_link_libraries(juggle_engine PRIVATE
    ${realsense2_LIBRARIES}
    OpenCV::opencv_core
    OpenCV::opencv_imgproc
    OpenCV::opencv_dnn
    openvino
    bytetrack
    ${Protobuf_LIBRARIES}
    ${ZMQ_LIBRARIES}
    juggler_proto
)

# Compiler definitions
target_compile_definitions(juggle_engine PRIVATE
    ${realsense2_CFLAGS_OTHER}
)

if(ENABLE_HAND_TRACKING)
    message(STATUS "Hand tracking feature is ENABLED")
    target_compile_definitions(juggle_engine PRIVATE ENABLE_HAND_TRACKING)
    
    # Add hand tracking source if it exists
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/hand_tracker.cpp")
        target_sources(juggle_engine PRIVATE src/hand_tracker.cpp)
    endif()
else()
    message(STATUS "Hand tracking feature is DISABLED")
endif()

# Compiler-specific optimizations
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(juggle_engine PRIVATE -O3 -march=native)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(juggle_engine PRIVATE /O2 /arch:AVX2)
endif()

# Set output directory
set_target_properties(juggle_engine PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Print build configuration
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "Output directory: ${CMAKE_BINARY_DIR}/bin")
message(STATUS "Protocol Buffer files will be generated in: ${PROTO_GENERATED_DIR}")

# Install targets
install(TARGETS juggle_engine
    RUNTIME DESTINATION bin
)

# Install protocol buffer headers for development
install(FILES "${PROTO_GENERATED_DIR}/juggler.pb.h"
    DESTINATION include
)