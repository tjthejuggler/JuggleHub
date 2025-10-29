# Ball Tracking System - Visual Diagrams
**Date:** 2025-01-04  
**Purpose:** Visual representation of the color-dominated tracking system

---

## System Architecture Comparison

### OLD SYSTEM (Prediction-Based)
```mermaid
graph TD
    A[YOLO Detection] --> B[Depth Filter]
    B --> C[Kalman Predict]
    C --> D[Build Cost Matrix<br/>3D Distance]
    D --> E[Greedy Assignment]
    E --> F[Kalman Update]
    F --> G[Color Validation<br/>Secondary]
    G --> H[Output Tracks]
    
    style C fill:#ff9999
    style D fill:#ff9999
    style E fill:#ff9999
    style G fill:#ffcc99
```

### NEW SYSTEM (Color-Dominated)
```mermaid
graph TD
    A[YOLO Detection] --> B[Depth Filter]
    B --> C[Color Dominance<br/>Scoring]
    C --> D[Color-Based<br/>Assignment]
    D --> E[Kalman Update<br/>Smoothing Only]
    E --> F[Output Tracks]
    
    style C fill:#99ff99
    style D fill:#99ff99
    style E fill:#99ccff
```

---

## Frame Processing Pipeline

```mermaid
flowchart TB
    subgraph Frame_N["Frame N Processing"]
        A[Capture Frame] --> B[YOLO Inference]
        B --> C[Extract Detections]
        C --> D{Valid Depth?}
        D -->|Yes| E[Compute Color Scores]
        D -->|No| Z[Discard]
        
        E --> F[Detection Pool]
        
        subgraph ColorAssignment["Color-Based Assignment"]
            G[Get Enabled Colors] --> H{For Each Color}
            H --> I[Find Highest Score]
            I --> J{Score > Threshold?}
            J -->|Yes| K[Assign to Tracker]
            J -->|No| L[Tracker = LOST]
            K --> M[Remove from Pool]
            M --> H
        end
        
        F --> G
        
        K --> N[Kalman Update]
        L --> O[No Update]
        
        N --> P[Extract Position]
        O --> P
        P --> Q[Output Results]
    end
    
    style E fill:#99ff99
    style I fill:#99ff99
    style K fill:#99ccff
```

---

## Color Dominance Scoring

```mermaid
graph LR
    subgraph Detection["Detection Bbox"]
        A[Center Point] --> B[Sample 5x5 Region]
    end
    
    B --> C[Convert to HSV]
    
    subgraph Scoring["Color Scoring"]
        C --> D[Compute Avg H,S,V]
        D --> E{Hue Range?}
        E -->|60-90| F[Green Score]
        E -->|5-20| G[Orange Score]
        E -->|160-180,0-10| H[Pink Score]
        E -->|20-40| I[Yellow Score]
        
        F --> J[Score = S*V/255²]
        G --> J
        H --> J
        I --> J
    end
    
    J --> K[ColorScores Object]
    
    style D fill:#ffcc99
    style J fill:#99ff99
```

---

## Assignment Algorithm

```mermaid
flowchart TD
    A[Start] --> B[Get Enabled Colors]
    B --> C{For Each Enabled Color}
    
    C --> D[Initialize best_score = 0.1]
    D --> E{For Each Detection}
    
    E --> F{Already Assigned?}
    F -->|Yes| E
    F -->|No| G[Get Score for Color]
    
    G --> H{Score > best_score?}
    H -->|Yes| I[Update best_score<br/>Update best_detection]
    H -->|No| E
    
    I --> E
    E -->|Done| J{Found Match?}
    
    J -->|Yes| K[Assign Detection to Tracker]
    J -->|No| L[Mark Tracker as LOST]
    
    K --> M[Mark Detection as Assigned]
    M --> C
    L --> C
    
    C -->|Done| N[End]
    
    style G fill:#99ff99
    style K fill:#99ccff
```

---

## Tracker Lifecycle

```mermaid
stateDiagram-v2
    [*] --> LOST: Initialize
    
    LOST --> TRACKED: Detection Found<br/>(Color Match)
    
    TRACKED --> TRACKED: Detection Found<br/>Each Frame
    TRACKED --> LOST: No Detection<br/>(Immediate)
    
    LOST --> [*]: Color Disabled
    
    note right of TRACKED
        Kalman filter updates
        Position smoothing
        Velocity estimation
    end note
    
    note right of LOST
        No prediction-based
        tracking
        Wait for color match
    end note
```

---

## Color Profile Integration

```mermaid
graph TB
    subgraph Settings["Tracker Settings"]
        A[Color Profiles] --> B{Green Enabled?}
        A --> C{Orange Enabled?}
        A --> D{Pink Enabled?}
        A --> E{Yellow Enabled?}
    end
    
    subgraph Trackers["Active Trackers"]
        B -->|Yes| F[Green Tracker]
        C -->|Yes| G[Orange Tracker]
        D -->|Yes| H[Pink Tracker]
        E -->|Yes| I[Yellow Tracker]
        
        B -->|No| J[No Tracker]
        C -->|No| J
        D -->|No| J
        E -->|No| J
    end
    
    subgraph Assignment["Assignment"]
        F --> K[Claim Most Green]
        G --> L[Claim Most Orange]
        H --> M[Claim Most Pink]
        I --> N[Claim Most Yellow]
    end
    
    style F fill:#99ff99
    style G fill:#ffaa66
    style H fill:#ff99cc
    style I fill:#ffff99
```

---

## Example: 3-Ball Juggling

```mermaid
sequenceDiagram
    participant YOLO
    participant ColorScorer
    participant GreenTracker
    participant OrangeTracker
    participant PinkTracker
    
    YOLO->>ColorScorer: 3 Detections
    
    Note over ColorScorer: Detection 1<br/>Green: 0.8<br/>Orange: 0.1<br/>Pink: 0.0
    Note over ColorScorer: Detection 2<br/>Green: 0.1<br/>Orange: 0.7<br/>Pink: 0.2
    Note over ColorScorer: Detection 3<br/>Green: 0.0<br/>Orange: 0.1<br/>Pink: 0.9
    
    ColorScorer->>GreenTracker: Assign Detection 1<br/>(Highest Green)
    ColorScorer->>OrangeTracker: Assign Detection 2<br/>(Highest Orange)
    ColorScorer->>PinkTracker: Assign Detection 3<br/>(Highest Pink)
    
    GreenTracker->>GreenTracker: Kalman Update
    OrangeTracker->>OrangeTracker: Kalman Update
    PinkTracker->>PinkTracker: Kalman Update
```

---

## Edge Case: Ambiguous Colors

```mermaid
graph TD
    A[Detection] --> B[Color Scores]
    B --> C{Orange: 0.6<br/>Yellow: 0.5}
    
    C --> D[Check Enabled Colors]
    D --> E{Orange Enabled?}
    E -->|Yes| F[Orange Claims First]
    E -->|No| G{Yellow Enabled?}
    G -->|Yes| H[Yellow Claims]
    G -->|No| I[No Assignment]
    
    F --> J[Detection Assigned]
    H --> J
    I --> K[Detection Ignored]
    
    style F fill:#ffaa66
    style H fill:#ffff99
```

---

## Performance Comparison

```mermaid
graph LR
    subgraph Old["OLD SYSTEM"]
        A1[O(N*M) Cost Matrix] --> A2[O(N*M) Assignment]
        A2 --> A3[Total: O(N²)]
    end
    
    subgraph New["NEW SYSTEM"]
        B1[O(N) Color Scoring] --> B2[O(C*N) Assignment]
        B2 --> B3[Total: O(N)]
    end
    
    A3 -.->|Slower| C[Performance]
    B3 -.->|Faster| C
    
    style A3 fill:#ff9999
    style B3 fill:#99ff99
```

Where:
- N = Number of detections
- M = Number of trackers
- C = Number of enabled colors (typically 2-4)

---

## Data Flow

```mermaid
flowchart LR
    subgraph Input
        A[Color Frame<br/>640x480]
        B[Depth Frame<br/>640x480]
    end
    
    subgraph Processing
        C[YOLO Model] --> D[Detections<br/>N balls]
        D --> E[Color Scorer]
        E --> F[Assignment<br/>Algorithm]
        F --> G[Kalman Filters<br/>C trackers]
    end
    
    subgraph Output
        H[Tracked Balls<br/>with IDs]
        I[3D Positions]
        J[Velocities]
    end
    
    A --> C
    B --> D
    G --> H
    G --> I
    G --> J
    
    style E fill:#99ff99
    style F fill:#99ff99
```

---

## Memory Layout

```mermaid
graph TB
    subgraph Trackers["Tracker Array"]
        T1[Tracker 0<br/>Green]
        T2[Tracker 1<br/>Orange]
        T3[Tracker 2<br/>Pink]
    end
    
    subgraph Detection["Detection Pool"]
        D1[Detection 0<br/>Scores: G=0.8, O=0.1, P=0.0]
        D2[Detection 1<br/>Scores: G=0.1, O=0.7, P=0.2]
        D3[Detection 2<br/>Scores: G=0.0, O=0.1, P=0.9]
    end
    
    T1 -.->|Claims| D1
    T2 -.->|Claims| D2
    T3 -.->|Claims| D3
    
    style T1 fill:#99ff99
    style T2 fill:#ffaa66
    style T3 fill:#ff99cc
```

---

## Conclusion

The new color-dominated system is:
- **Simpler**: Linear algorithm instead of quadratic
- **More robust**: Color identity is stable
- **More predictable**: 1:1 mapping between colors and trackers
- **Faster**: O(N) instead of O(N²)

The diagrams above illustrate how the system leverages color as the primary identity mechanism, with Kalman filters used only for smoothing and velocity estimation.