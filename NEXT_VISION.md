# JuggleHub Project Vision

**Last Updated:** 2025-09-07 13:30:00 UTC

This document outlines the core mission, technical architecture, and long-term goals for the JuggleHub project. It serves as a guiding star for development, community contributions, and strategic decisions, blending a high-level creative vision with a detailed technical foundation verified against the core source code.

## 1. Mission Statement

To create a foundational and modular platform that monitors and analyzes juggling in real-time, empowering developers, artists, and jugglers to build a universe of creative and practical applications. JuggleHub provides the robust groundwork for tools ranging from progress tracking and **AI-powered coaching** to interactive performances and artistic installations, fostering limitless innovation on a solid, **interpretable** technological base.

## 2. Guiding Principles & Core Philosophy

Our decisions are guided by a commitment to creating a powerful, reliable, and extensible platform.

*   **Accuracy Above All:** The integrity of our tracking data is paramount. We prioritize precision and reliability to ensure that all applications built on JuggleHub are founded on trustworthy analytics.
*   **Real-Time Performance is Key:** Low-latency processing is a non-negotiable requirement. The system is engineered for immediate feedback, distributing computational loads across available hardware (CPU, iGPU, NPU) to enable a vast "playground" of interactive possibilities without technical constraints.
*   **Interpretability as a Feature:** We reject "black-box" solutions where possible. Our analysis systems, particularly for pattern recognition, are designed to be "glass-box" models. The output should be not just a label, but a verifiable, descriptive model of the performance, providing deeper insights and building user trust.
*   **A Foundation for Creativity:** We provide the essential tools for tracking and analysis, but the platform is designed to be a canvas. We empower others to create, experiment, and push the boundaries of what's possible with juggling and technology.
*   **Modularity over Monolith:** The system is architected as a set of interoperable components. The C++ engine hosts pluggable, real-time "Modules," and the Python hub is built from distinct, independently functioning components, including a multi-tiered analysis pipeline.
*   **API-Driven Design:** Communication between major components is governed by a strict, versioned API defined with Protocol Buffers (`juggler.proto`). This is the single source of truth for all data structures, preventing integration errors and ensuring stability.

## 3. Target Audience

JuggleHub is built for the ambitious and creative members of the juggling community. Our primary audience consists of:

*   **Dedicated Jugglers:** Individuals driven to achieve the highest level of skill, who can use the platform for detailed, **quantitative feedback** (e.g., throw consistency, dwell time), progress tracking, and identifying specific areas for improvement.
*   **Creative Artists and Performers:** Jugglers who see their craft as an art form and want to fuse it with technology to create unique, interactive experiences, from controlling smart props to driving generative visuals or music.
*   **Developers & Researchers:** Technologists and academics who require a stable, high-performance, and **interpretable platform** for building novel human-computer interaction systems or for conducting precise research on motor skills and biomechanics.

## 4. Technical Architecture: A Deep Dive

JuggleHub is built on a high-performance, hybrid architecture that separates low-latency C++ processing from high-level Python management. This separation of concerns is fundamental to achieving our goals of real-time performance and creative flexibility.

### 4.1. System Overview

```
+---------------------------------+      +--------------------------------+
|       C++ Real-Time Engine      |      |          Python Hub            |
|---------------------------------|      |--------------------------------|
| - Intel RealSense Camera I/O    |      | - ZMQClient (Data Receiver)    |
| - Color & DNN Ball Tracking     |      | - Pattern Recognition Pipeline |
| - Real-Time Module Execution    |      | - IMUListener (WebSocket)      |
| - ZMQ PUB (Data) & REP (Control)|      | - JuggleHubUI (PyQt6)          |
| - UDP Control for Smart Props   |      | - DatabaseLogger (SQLite)      |
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
    *   Performing ball and hand tracking using a fine-tuned, high-performance YOLOv8n model.
    *   Executing sandboxed, real-time "Modules" (e.g., `PositionToRgbModule`) for interactive applications like smart ball control.
    *   Streaming serialized `FrameData` Protobuf messages via a ZeroMQ `PUB` socket.
    *   Receiving and responding to `CommandRequest` Protobuf messages on a separate ZeroMQ `REP` socket in a dedicated command processing thread.

*   **Tracking Subsystems**:
    1.  **DNN-Powered Tracking (Primary):** The state-of-the-art pipeline for object detection.
        *   **Model:** A fine-tuned YOLOv8n model, specifically optimized for the consistent conditions of the RealSense camera setup to provide the highest quality coordinate data for the downstream analysis pipeline.
        *   **Inference Backend:** Intel's OpenVINO toolkit for high-performance inference, intelligently utilizing available hardware like iGPUs or NPUs.
        *   **Tracker:** ByteTrack for maintaining consistent object IDs across frames.
    2.  **Color-Based Tracking (Legacy):** A highly efficient system for controlled lighting conditions, maintained for specific use cases or as a fallback.

### 4.3. The Python Hub: Analysis & Mission Control (`main.py`)

The Python hub is the flexible, user-facing brain of the operation. It is built on a multithreaded, component-based architecture to receive low-level tracking data from the C++ engine and transform it into high-level, interpretable insights.

*   **Core Components**:
    *   **`ZMQClient`**: Connects to the C++ engine's `PUB` socket to receive the stream of `FrameData`.
    *   **`IMUListener`**: Runs in a separate thread, managing WebSocket connections to wearable sensors and parsing incoming IMU data.
    *   **`DatabaseLogger`**: Logs all incoming data—from raw coordinates to final pattern analysis—to a SQLite database for post-session review.
    *   **`JuggleHubUI`**: The main graphical interface, built with PyQt6, for real-time visualization and system control.

*   **The Two-Tiered Pattern Recognition Pipeline**: A dedicated background thread executes a sophisticated, hierarchical analysis pipeline on the incoming data. This "glass-box" approach is designed for accuracy, interpretability, and real-time performance on local hardware.

    *   **Tier 1: Temporal Grammar Analysis (Siteswap Derivation)**
        *   **Purpose:** To determine the fundamental *temporal rhythm* and structure of the pattern.
        *   **Mechanism:**
            1.  **State Estimation:** A **Kalman Filter** is applied to each ball's trajectory. This physics-based model smooths the noisy coordinate data from the tracker and accurately estimates the ball's true position and velocity.
            2.  **Event Detection:** By monitoring the Kalman filter's "innovation" signal (the difference between its prediction and the real measurement), the system robustly detects discrete **throw and catch events**. A large innovation spike signifies an external force (a hand) has acted on the ball.
            3.  **Siteswap Calculation:** The system measures the flight time between each throw and its corresponding catch, normalizes it by the juggler's tempo, and derives the pattern's **Siteswap notation** (e.g., '3', '441').
        *   **Resource Profile:** This tier consists of efficient algorithms, not neural networks. It runs with minimal load on the **CPU**.

    *   **Tier 2: Spatial Execution Analysis (Pattern Description & Classification)**
        *   **Purpose:** To analyze the *spatial execution* or "flavor" of the pattern. This tier deconstructs *how* each throw is performed, allowing it to first describe any pattern and then classify it if it's a known one. This is how it differentiates patterns sharing the same Siteswap (e.g., Cascade vs. Mills Mess).
        *   **Mechanism:**
            1.  **Throw Atomization:** The system analyzes the trajectory and context of each individual throw detected by Tier 1, classifying it as a fundamental "atom of motion" (e.g., `THROW_TYPE_NORMAL`, `THROW_TYPE_OVER_THE_TOP`, `HAND_CROSSING`).
            2.  **Pattern Descriptor Generation:** It assembles these atoms into a structured **Pattern Descriptor**—a detailed, symbolic representation of the pattern's execution over a repeating cycle (e.g., `Siteswap: 3`, `Right_Hand_Throws: [NORMAL]`, `Left_Hand_Throws: [NORMAL, OVER_THE_TOP]`).
            3.  **Classification via Library Matching:** This complete descriptor is then compared against a library of known patterns. If a match is found, it returns the common name (e.g., 'Mills Mess'). If no match is found, it returns the full descriptor, effectively "explaining" the new, unclassified pattern it is seeing.
        *   **Resource Profile:** This analysis relies on lightweight, specialized models (e.g., small Random Forests, SVMs, or tiny neural networks for throw atomization). This makes it ideal for offloading to the **NPU** (if available) or running with negligible impact on the **CPU**. It does not compete for iGPU resources.

### 4.4. The API (`juggler.proto`)

The API definition is the rigid contract that ensures seamless communication. It has been extended to include rich analytical data from the pattern recognition pipeline.

*   **Key Data Structures**:
    *   **`FrameData`**: The main message type, a container for all data related to a single moment in time. It includes lists of `Ball`, `Hand`, and `IMUData` messages, as well as system status, camera intrinsics, and the new `PatternData` message.
    *   **`Ball`**: Contains the 3D position, 2D bounding box, and smoothed Kalman filter state of a tracked ball.
    *   **`JugglingEvent`**: Represents a discrete event detected in the time-series, such as a throw or a catch, with associated timestamps and object IDs.
    *   **`PatternData`**: A message containing the high-level analysis output from the Python hub.
        ```protobuf
        message PatternData {
          // The human-readable name of the classified pattern.
          // e.g., "3-Ball Cascade", "Mills Mess"
          string pattern_name = 1;

          // The derived Siteswap notation for the current pattern.
          // e.g., "3", "441"
          string derived_siteswap = 2;

          // A score from 0.0 to 1.0 indicating pattern stability.
          float consistency_score = 3;

          // The juggler's current tempo.
          float beats_per_minute = 4;
        }
        ```
    *   **`CommandRequest`**: A versatile message for controlling the engine, with an enum for actions like `LOAD_MODULE`, `CAMERA_START`, and `RECORD_CONTINUOUS_START`.

## 5. Long-term Goals & Future Vision

With a robust, interpretable pattern recognition system at its core, we envision JuggleHub evolving from an analysis tool into an intelligent juggling partner.

*   **Quantitative Skill Assessment & AI Coaching:** Move beyond simple pattern labels to provide actionable, data-driven feedback. The hierarchical pipeline naturally produces metrics like throw height variance, dwell time consistency, spatial accuracy, and rhythmic precision. A future "Coaching Module" will use this data to offer specific advice, such as "Your left-handed throws in the '441' pattern are consistently 10% lower than your right."
*   **Seamless Integration with Creative Technologies:** Develop robust, well-documented interfaces (e.g., OSC, MIDI, WebSockets) to connect JuggleHub's rich analytical output (like individual throw events or pattern consistency scores) with a wide array of external systems for music generation, projection mapping, and interactive art.
*   **Advanced Multi-Modal Sensor Fusion:** Formally integrate wearable IMU data into the event detection pipeline. Use the high-G acceleration spikes from wrist sensors to corroborate or even replace the Kalman innovation signal for throw/catch detection, creating an unparalleled level of event-detection accuracy.
*   **Generative Pattern Discovery:** Leverage the Siteswap derivation engine to identify and analyze non-standard or user-created juggling patterns in real-time. This turns the system into a tool for exploring and formalizing new juggling sequences.
*   **Expand Hardware Support:** Abstract the camera interface to support other hardware beyond the Intel RealSense, including standard webcams (with 2D analysis) and other depth sensors.
*   **Foster a Community of Innovators:** Cultivate a community of developers and artists who build and share new analysis modules, coaching applications, and creative projects on top of the JuggleHub platform.
*   **Advanced Event and Error Analysis:** Extend the event detection vocabulary beyond throws and catches to include and analyze more complex juggling primitives like intentional mid-air collisions, stalls, and contact juggling moves. This will enable the system to provide feedback on a much wider range of modern juggling styles and automatically identify errors like fumbles or drops.