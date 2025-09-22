# NOTE: THIS IS JUST THE OLD VERSION OF OUR VISION FILE, IT IS OUTDATED JUST KEPT AROUND FOR THE MEMORY

# JuggleHub Project Vision

**Last Updated:** 2025-09-07 13:30:00 UTC

This document outlines the core mission, technical architecture, and long-term goals for the JuggleHub project. It serves as a guiding star for development, community contributions, and strategic decisions, blending a high-level creative vision with a detailed technical foundation verified against the core source code.

## 1. Mission Statement

To create a foundational and modular platform that monitors and analyzes juggling in real-time, empowering developers, artists, and jugglers to build a universe of creative and practical applications. JuggleHub provides the robust groundwork for tools ranging from progress tracking and coaching to interactive performances and artistic installations, fostering limitless innovation on a solid technological base.

## 2. Guiding Principles & Core Philosophy

Our decisions are guided by a commitment to creating a powerful, reliable, and extensible platform.

*   **Accuracy Above All:** The integrity of our tracking data is paramount. We prioritize precision and reliability to ensure that all applications built on JuggleHub are founded on trustworthy analytics.
*   **Real-Time Performance is Key:** Low-latency processing is a non-negotiable requirement. The system is engineered to provide immediate feedback, enabling a vast "playground" of interactive possibilities without being constrained by technical limitations.
*   **A Foundation for Creativity:** We provide the essential tools for tracking and analysis, but the platform is designed to be a canvas. We empower others to create, experiment, and push the boundaries of what's possible with juggling and technology.
*   **Modularity over Monolith:** The system is architected as a set of interoperable components. The C++ engine hosts pluggable, real-time "Modules," and the Python hub is built from distinct, independently functioning components.
*   **API-Driven Design:** Communication between major components is governed by a strict, versioned API defined with Protocol Buffers (`juggler.proto`). This is the single source of truth for all data structures, preventing integration errors and ensuring stability.

## 3. Target Audience

JuggleHub is built for the ambitious and creative members of the juggling community. Our primary audience consists of:

*   **Dedicated Jugglers:** Individuals driven to achieve the highest level of skill, who can use the platform for detailed feedback, progress tracking, and identifying areas for improvement.
*   **Creative Artists and Performers:** Jugglers who see their craft as an art form and want to fuse it with technology to create unique, interactive experiences, from controlling smart props to driving generative visuals or music.
*   **Developers & Researchers:** Technologists and academics who require a stable, high-performance platform for building novel human-computer interaction systems or for conducting precise research on motor skills and biomechanics.

## 4. Technical Architecture: A Deep Dive

JuggleHub is built on a high-performance, hybrid architecture that separates low-latency C++ processing from high-level Python management. This separation of concerns is fundamental to achieving our goals of real-time performance and creative flexibility.

### 4.1. System Overview

```
+---------------------------------+      +--------------------------------+
|       C++ Real-Time Engine      |      |          Python Hub            |
|---------------------------------|      |--------------------------------|
| - Intel RealSense Camera I/O    |      | - ZMQClient (Data Receiver)    |
| - Color & DNN Ball Tracking     |      | - IMUListener (WebSocket)      |
| - Real-Time Module Execution    |      | - JuggleHubUI (PyQt6)          |
| - ZMQ PUB (Data) & REP (Control)|      | - DatabaseLogger (SQLite)      |
| - UDP Control for Smart Props   |      | - Headless Operation Capable   |
+---------------------------------+      +--------------------------------+
           ^                |                      ^                |
           |                | Protobuf via ZeroMQ  |                |
           | Control        +--------------------->| Data           |
           | Commands       |<---------------------+                |
           v                |                      v                v
+---------------------------------+      +--------------------------------+
|          Hardware             |      |       User & External Systems  |
|---------------------------------|      |--------------------------------|
| - RealSense Depth Camera        |      | - User Interface               |
| - Smart Juggling Balls (UDP)    |      | - Projection Mapping, Music    |
| - Wearable IMU Sensors(WebSocket)|      | - Research Databases           |
+---------------------------------+      +--------------------------------+
```

### 4.2. The C++ Real-Time Engine (`Engine.cpp`)

The C++ engine is the heart of the system, responsible for all tasks where per-frame, low-latency processing is critical.

*   **Responsibilities**:
    *   Directly managing the Intel RealSense camera, including sophisticated control over settings, resolution, and frame rates via JSON configuration files.
    *   Performing ball tracking using one of two selectable systems: a legacy color-based tracker or a modern DNN-based tracker.
    *   Executing sandboxed, real-time "Modules" (e.g., `PositionToRgbModule`) for interactive applications like smart ball control.
    *   Streaming serialized `FrameData` Protobuf messages via a ZeroMQ `PUB` socket.
    *   Receiving and responding to `CommandRequest` Protobuf messages on a separate ZeroMQ `REP` socket in a dedicated command processing thread.

*   **Tracking Subsystems**:
    1.  **DNN-Powered Tracking**: The primary, state-of-the-art pipeline.
        *   **Model**: YOLOv8n for fast and accurate object detection.
        *   **Inference Backend**: Intel's OpenVINO toolkit for high-performance inference on CPUs, GPUs, and NPUs.
        *   **Tracker**: ByteTrack for maintaining consistent object IDs across frames.
    2.  **Color-Based Tracking**: A highly efficient, legacy system for controlled lighting conditions.

### 4.3. The Python Hub: Mission Control (`main.py`)

The Python hub is the flexible, user-facing brain of the operation. It is built on a multithreaded, component-based architecture to ensure a responsive user experience.

*   **Core Components**:
    *   **`ZMQClient`**: Connects to the C++ engine's `PUB` socket to receive the stream of `FrameData`.
    *   **`IMUListener`**: Runs in a separate thread, managing WebSocket connections to wearable sensors and parsing incoming IMU data.
    *   **`DatabaseLogger`**: Logs all incoming data to a SQLite database for post-session analysis.
    *   **`JuggleHubUI`**: The main graphical interface, built with PyQt6, for real-time visualization and system control.

*   **Data Fusion & Processing Loop**: A dedicated background thread continuously:
    1.  Receives the latest `FrameData` (vision) from the `ZMQClient`.
    2.  Polls the `IMUListener` for the most recent sensor data.
    3.  **Fuses** the two data sources by augmenting the `FrameData` message with the latest `IMUData`.
    4.  Dispatches the combined data packet to the UI for visualization and to the `DatabaseLogger` for storage.

*   **Headless Operation**: The hub can be run without the UI (`--no-ui`), making it suitable for server deployments, data logging bots, or automated tasks.

### 4.4. The API (`juggler.proto`)

The API definition is the rigid contract that ensures seamless communication.

*   **Key Data Structures**:
    *   **`FrameData`**: The main message type, a container for all data related to a single moment in time. It includes lists of `Ball`, `Hand`, and `IMUData` messages, as well as system status and camera intrinsics.
    *   **`Ball`**: Contains the 3D position and 2D bounding box of a tracked ball.
    *   **`IMUData`**: Contains accelerometer and gyroscope data from a wearable sensor.
    *   **`CommandRequest`**: A versatile message for controlling the engine, with an enum for actions like `LOAD_MODULE`, `CAMERA_START`, and `RECORD_CONTINUOUS_START`.

## 5. Long-term Goals & Future Vision

We envision JuggleHub evolving from a specialized analysis tool into a versatile creative and scientific hub.

*   **Seamless Integration with Creative Technologies:** Develop robust, well-documented interfaces (e.g., OSC, MIDI, WebSockets) to connect JuggleHub with a wide array of external systems.
*   **Expand Hardware Support:** Abstract the camera interface to support other hardware beyond the Intel RealSense, including standard webcams and other depth sensors.
*   **Advanced Pattern Recognition:** Implement machine learning models within the Python hub to recognize juggling patterns in real-time.
*   **Become a Tool for Scientific Research:** Continue to enhance the precision and data-logging capabilities of the platform to make it a valuable tool for academic and research institutions.
*   **Foster a Community of Innovators:** Cultivate a community of developers and artists who build and share new modules, applications, and creative projects.