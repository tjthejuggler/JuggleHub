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

+---------------------------------+      +--------------------------------+
|       C++ Real-Time Engine      |      |          Python Hub            |
|---------------------------------|      |--------------------------------|
| - Intel RealSense Camera I/O    |      | - ZMQClient (Data Receiver)    |
| - DNN Ball & Hand Tracking      |      | - Pattern Recognition Pipeline |
| - Real-Time Module Execution    |      | - IMUListener (WebSocket)      |
| - ZMQ PUB (Data) & REP (Control)|      | - JuggleHubUI (PyQt6)          |
| - UDP Control for Smart Props   |      | - DatabaseLogger (SQLite)      |
|                                 |      | - Headless Operation Capable   |
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

    Core Components:

        ZMQClient: Connects to the C++ engine's PUB socket to receive the stream of FrameData.

        IMUListener: Runs in a separate thread, managing WebSocket connections to wearable sensors and parsing incoming IMU data.

        DatabaseLogger: Logs all incoming data—from raw coordinates to final pattern analysis—to a SQLite database for post-session review.

        JuggleHubUI: The main graphical interface, built with PyQt6, for real-time visualization and system control.

    The Two-Tiered Pattern Recognition Pipeline: A dedicated background thread executes a sophisticated, hierarchical analysis pipeline on the incoming data. This "glass-box" approach is designed for accuracy, interpretability, and real-time performance on local hardware.

        Tier 1: Event-Based Grammar Analysis

            Purpose: To deconstruct the continuous motion into a sequence of discrete, meaningful physical events.

            Mechanism:

                State Estimation: A Kalman Filter is applied to each ball's trajectory. This physics-based model smooths the noisy coordinate data from the tracker and accurately estimates the ball's true position and velocity.

                Event Detection: By monitoring the Kalman filter's "innovation" signal (the difference between its prediction and the real measurement), the system detects moments where balls deviate from a ballistic path. It then classifies the cause of this deviation:

                    A spike correlated with hand proximity is a THROW or CATCH event.

                    Simultaneous spikes for two or more balls at the same location, without hand proximity, are classified as an intentional mid-air COLLISION event.

                Siteswap Calculation: For patterns composed only of throws and catches, the system measures the flight time between each throw and its corresponding catch, normalizes it by the juggler's tempo, and derives the pattern's Siteswap notation (e.g., '3', '441'). For patterns involving other events like collisions, this step is bypassed in favor of a direct event sequence analysis in Tier 2.

            Resource Profile: This tier consists of efficient algorithms, not neural networks. It runs with minimal load on the CPU.

        Tier 2: Spatial Execution Analysis (Pattern Description & Classification)

            Purpose: To analyze the spatial execution or "flavor" of the pattern. This tier deconstructs how each event is performed, allowing it to first describe any pattern and then classify it if it's a known one.

            Mechanism:

                Event Atomization: The system analyzes the trajectory and context of each individual event detected by Tier 1, classifying it as a fundamental "atom of motion" (e.g., THROW_TYPE_NORMAL, THROW_TYPE_OVER_THE_TOP, HAND_CROSSING, or the COLLISION event itself).

                Pattern Descriptor Generation: It assembles these atoms into a structured Pattern Descriptor—a detailed, symbolic representation of the pattern's execution over a repeating cycle (e.g., Siteswap: 3, Right_Hand_Throws: [NORMAL], Events: [COLLISION]).

                Classification via Library Matching: This complete descriptor is then compared against a library of known patterns. If a match is found, it returns the common name (e.g., 'Mills Mess'). If no match is found, it returns the full descriptor, effectively "explaining" the new, unclassified pattern it is seeing.

            Resource Profile: This analysis relies on lightweight, specialized models (e.g., small Random Forests, SVMs, or tiny neural networks for event atomization). It is ideal for offloading to the NPU (if available) or running with negligible impact on the CPU. It does not compete for iGPU resources.

### 4.4. The API (`juggler.proto`)

The API definition is the rigid contract that ensures seamless communication. It has been extended to include rich analytical data from the pattern recognition pipeline.

    Key Data Structures:

        FrameData: The main message type, a container for all data related to a single moment in time. It includes lists of Ball, Hand, and IMUData messages, as well as system status, camera intrinsics, and the new PatternData message.

        Ball: Contains the 3D position, 2D bounding box, and smoothed Kalman filter state of a tracked ball.

        JugglingEvent: Represents a discrete event detected in the time-series. This message is crucial for the event-based analysis pipeline.
        code Protobuf

IGNORE_WHEN_COPYING_START
IGNORE_WHEN_COPYING_END

    
message JugglingEvent {
  enum EventType {
    UNKNOWN = 0;
    THROW = 1;
    CATCH = 2;
    COLLISION = 3; // Event for intentional mid-air collisions
  }
  EventType type = 1;
  uint64 timestamp_us = 2;
  
  // A list of ball IDs involved in the event.
  // Usually one for throws/catches, but multiple for collisions.
  repeated int32 ball_ids = 3; 

  // ... other contextual data like 3D position, associated hand ID, etc.
}

  

PatternData: A message containing the high-level analysis output from the Python hub. This is the primary result of the pattern recognition pipeline.
code Protobuf
IGNORE_WHEN_COPYING_START
IGNORE_WHEN_COPYING_END

    
message PatternData {
  // The human-readable name of the classified pattern.
  // e.g., "3-Ball Cascade", "Mills Mess", "Center Collision"
  string pattern_name = 1;

  // The derived Siteswap notation for the current pattern.
  // May be "N/A" for non-Siteswap patterns (e.g., with collisions).
  // e.g., "3", "441"
  string derived_siteswap = 2;

  // A score from 0.0 to 1.0 indicating pattern stability and consistency.
  float consistency_score = 3;

  // The juggler's current tempo, derived from the rhythm of events.
  float beats_per_minute = 4;
}

  

CommandRequest: A versatile message for controlling the engine, with an enum for actions like LOAD_MODULE, CAMERA_START, and RECORD_CONTINUOUS_START.

### 4.5. Core AI Strategy: Dual-Model Specialization for Object Tracking

To achieve the highest performance in both real-time analysis and offline data processing, JuggleHub adopts a dual-model strategy for its core object tracking. A single, generalist model cannot optimally serve the distinct domains of clean, consistent lab footage and varied, "in-the-wild" video. Therefore, we develop and maintain two specialized, fine-tuned YOLOv8 models:

*   **Model A: The "Live Engine" Model**
    *   **Purpose:** Real-time, low-latency tracking.
    *   **Training Data:** Fined-tuned exclusively on high-quality video captured directly from the project's Intel RealSense camera setup.
    *   **Characteristics:** Optimized for the specific lighting, camera angle, resolution, and objects used in the live system. This specialization ensures the highest possible accuracy and speed, providing a pristine coordinate data stream to the downstream analysis pipeline.
    *   **Deployment:** This is the model that is integrated into the C++ Real-Time Engine.

*   **Model B: The "Data Miner" Model**
    *   **Purpose:** Offline, large-scale data extraction for training the pattern recognition engine.
    *   **Training Data:** Fine-tuned on a diverse and challenging dataset composed of thousands of juggling videos from varied sources (e.g., YouTube).
    *   **Characteristics:** Optimized for robustness over raw speed. It is designed to handle a wide range of lighting conditions, compression artifacts, camera angles, and juggler styles.
    *   **Deployment:** This model is a critical development tool used in our data preparation scripts. It is not part of the real-time engine.

This separation of concerns is fundamental, allowing us to build a hyper-accurate real-time system without sacrificing the ability to learn from the vast amount of juggling knowledge available publicly.

### 4.6. Development Workflow for the Pattern Recognition Engine

The sophisticated "glass-box" pattern recognition pipeline is built through a structured, three-stage development process that combines automated data processing with targeted, efficient human supervision.

*   **Stage 1: Automated Trajectory Extraction**
    *   The entire library of training videos (e.g., thousands of YouTube clips) is processed by a batch script. This script uses the **"Data Miner" Model (Model B)** to perform object detection on every frame.
    *   The output of this stage is a massive dataset of structured trajectory files (e.g., JSON), where each file contains the time-series coordinate data for a single, complete juggling performance. This automated process transforms unstructured video into the foundational data for all subsequent analysis.

*   **Stage 2: Supervised "Throw Atom" Labeling & Training**
    *   This is the core supervised learning step. We do not label entire videos or individual frames, which is inefficient. Instead, we label the fundamental building blocks of juggling: individual throws.
    *   A specialized labeling tool first uses the Tier 1 event detection logic (Kalman filters) to automatically identify every throw in a trajectory file. It then presents a visualization of each throw to a human expert, who assigns it a categorical label (e.g., `NORMAL`, `OVER_THE_TOP`, `CROSS_UNDER`).
    *   This labeled dataset of individual throw segments is used to train a small, efficient classifier (the **"Throw Atomizer"** model). This model's sole purpose is to classify the type of any given throw based on its trajectory data.

*   **Stage 3: Pattern Definition and Library Creation**
    *   The final system is assembled by integrating the trained "Throw Atomizer" into the real-time Tier 2 pipeline.
    *   Known juggling patterns are then defined in a human-readable "Pattern Library" (e.g., a YAML file). This library acts as a recipe book, defining each pattern by its Siteswap and the required sequence of "throw atoms."
    *   In real-time, the system generates a "Pattern Descriptor" and compares it to this library. A match results in a named classification, while a non-match results in a detailed description of the novel pattern being performed.

## 5. Long-term Goals & Future Vision

With a robust, interpretable pattern recognition system at its core, we envision JuggleHub evolving from an analysis tool into an intelligent juggling partner.

*   **Quantitative Skill Assessment & AI Coaching:** Move beyond simple pattern labels to provide actionable, data-driven feedback. The hierarchical pipeline naturally produces metrics like throw height variance, dwell time consistency, spatial accuracy, and rhythmic precision. A future "Coaching Module" will use this data to offer specific advice, such as "Your left-handed throws in the '441' pattern are consistently 10% lower than your right."
*   **Seamless Integration with Creative Technologies:** Develop robust, well-documented interfaces (e.g., OSC, MIDI, WebSockets) to connect JuggleHub's rich analytical output (like individual throw events or pattern consistency scores) with a wide array of external systems for music generation, projection mapping, and interactive art.
*   **Advanced Multi-Modal Sensor Fusion:** Formally integrate wearable IMU data into the event detection pipeline. Use the high-G acceleration spikes from wrist sensors to corroborate or even replace the Kalman innovation signal for throw/catch detection, creating an unparalleled level of event-detection accuracy.
*   **Generative Pattern Discovery:** Leverage the Siteswap derivation engine to identify and analyze non-standard or user-created juggling patterns in real-time. This turns the system into a tool for exploring and formalizing new juggling sequences.
*   **Expand Hardware Support:** Abstract the camera interface to support other hardware beyond the Intel RealSense, including standard webcams (with 2D analysis) and other depth sensors.
*   **Foster a Community of Innovators:** Cultivate a community of developers and artists who build and share new analysis modules, coaching applications, and creative projects on top of the JuggleHub platform.
*   **Advanced Event and Error Analysis:** Extend the event detection vocabulary beyond throws and catches to include and analyze more complex juggling primitives like intentional mid-air collisions, stalls, and contact juggling moves. This will enable the system to provide feedback on a much wider range of modern juggling styles and automatically identify errors like fumbles or drops.
