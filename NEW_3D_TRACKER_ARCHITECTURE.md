# New 3D Tracker Architecture

This document outlines the architecture of the `New3DTracker`, a physics-based tracking system using a 6-state Kalman filter (`x, y, z, vx, vy, vz`) for robust 3D ball tracking.

## Core Concepts

### Persistent Ball Objects
The fundamental design principle is the use of **persistent ball objects**. The system initializes one permanent `New3DBall` object for each enabled color profile at startup. These objects are never deleted; they simply transition between states and are marked as seen or unseen.

### State Machine
Each ball is managed by a simple state machine:
- **`HELD` State**: The ball's position is locked to the associated hand's wrist. The Kalman filter tracks the wrist's movement to estimate velocity for throws.
- **`IN_FLIGHT` State**: The ball's trajectory is predicted by the Kalman filter, incorporating gravity, and is corrected by new 3D measurements from YOLO detections.

## Definitive Tracking Flow Diagram

The following diagram provides a complete, step-by-step visualization of the entire per-frame tracking loop. It details the specific logic and conditions checked for both `HELD` and `IN_FLIGHT` balls at every stage of the process.

```mermaid
graph TD
    subgraph "Frame Start & Detection"
        A[Start Frame] --> B[Run YOLO Ball & Pose Detection];
        B --> C[3D Detections & 3D Hand Positions];
    end

    subgraph "Step 1: Prediction"
        C --> P1{For each ball};
        P1 --> P2{State?};
        P2 -- HELD --> P3["predictHeldBall: KF tracks hand wrist, predicts position = wrist_pos"];
        P2 -- IN_FLIGHT --> P4["predictInFlightBall: Apply gravity to KF velocity, then predict new position"];
        P3 & P4 --> P_OUT[Predicted Ball Positions];
    end

    subgraph "Step 2: Association"
        P_OUT & C --> A1["Associate Detections to Predictions via Greedy Match (Cost = Distance + Color Penalty)"];
        A1 --> A_OUT{Outputs};
        A_OUT --> M[Matched Pairs];
        A_OUT --> UM_B[Unmatched Balls];
        A_OUT --> UM_D[Unmatched Detections];
    end

    subgraph "Step 3: Update Matched Balls"
        M --> U1{For each matched pair...};
        U1 --> U2{Ball State?};
        
        U2 -- HELD --> U3{"handleHeldStateUpdate"};
        U3 --> U4{Is holding hand visible?};
        U4 -- No --> U5["THROW: State -> IN_FLIGHT"];
        U4 -- Yes --> U6{"dist_to_hand > held_radius AND relative_vel > throw_thresh?"};
        U6 -- Yes --> U5;
        U6 -- No --> U7["NO THROW: State remains HELD"];
        
        U2 -- IN_FLIGHT --> U8{"handleInFlightStateUpdate"};
        U8 --> U9["Correct KF with detection measurement"];
        U9 --> U10{dist_to_any_hand < held_radius?};
        U10 -- Yes --> U11["CATCH: State -> HELD"];
        U10 -- No --> U12["NO CATCH: State remains IN_FLIGHT"];
    end

    subgraph "Step 4, 5, 6: Handle Unmatched & Re-acquire"
        UM_B & UM_D --> R1{"Step 4: createNewTracks"};
        R1 --> R2{"Match Unseen Ball to Unmatched Detection by Color?"};
        R2 -- Yes --> R3["RE-ACQUIRE: Update ball with detection data, check proximity to hands to set HELD/IN_FLIGHT state"];
        R2 -- No --> R4[Remaining Unmatched Balls];
        
        R4 --> R5{"Step 5: reacquireHeldBallsByProximity"};
        R5 --> R6{"Unmatched IN_FLIGHT ball predicted position is near a hand?"};
        R6 -- Yes --> R7["RE-ACQUIRE (PROXIMITY CATCH): State -> HELD"];
        R6 -- No --> R8[Remaining Unmatched Balls];

        R8 --> R9{"Step 6: handleUnmatchedBalls"};
        R9 --> R10{State?};
        R10 -- HELD --> R11{Is holding hand still visible?};
        R11 -- Yes --> R12["HELD (OCCLUDED): Keep state, position follows hand"];
        R11 -- No --> R13["HELD (HAND LOST): State -> IN_FLIGHT"];
        R10 -- IN_FLIGHT --> R14["IN_FLIGHT (UNSEEN): Increment frames_since_seen counter"];
    end
    
    subgraph "Step 7: Finalize & Output"
        U5 & U7 & U11 & U12 & R3 & R7 & R12 & R13 & R14 --> F1[Collect Final States of All Balls];
        F1 --> F2{For each ball};
        F2 --> F3{Final State?};
        F3 -- HELD --> F4["Final Position = Associated Hand's Wrist Position"];
        F3 -- IN_FLIGHT --> F5["Final Position = Kalman Predicted Position"];
        F4 & F5 --> F_OUT[Output: Final Tracked Balls & Events];
    end

    style A fill:#d4edda,stroke:#c3e6cb
    style F_OUT fill:#f8d7da,stroke:#f5c6cb
    style U5 fill:#ffc107,stroke:#ff9800
    style U11 fill:#ffc107,stroke:#ff9800
    style R3 fill:#28a745,stroke:#28a745
    style R7 fill:#28a745,stroke:#28a745
    style R13 fill:#ffc107,stroke:#ff9800