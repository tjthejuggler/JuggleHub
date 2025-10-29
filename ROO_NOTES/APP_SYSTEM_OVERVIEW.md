# JuggleHub App System - Quick Overview

**Last Updated:** 2025-10-08 22:11:00 CEST

## 🎯 What is the App System?

The JuggleHub App System allows you to build independent applications that use the juggling tracking engine. Apps run in separate windows, can be placed on different monitors, and have full control over which engine features are active.

## 🚀 Quick Start for Users

### Launching Apps

1. **From the App Menu:**
   - Click `App` in the menu bar
   - Select an app from "Recent Apps"
   - Or click "App Manager..." to see all apps

2. **From the App Manager:**
   - Press `Ctrl+M` or select `App → App Manager...`
   - Browse available apps in a visual grid
   - Click "Launch" on any app card

### Using the Catch Counter App

The first app is a simple catch counter:

```
┌─────────────────────────┐
│  🎯 Catch Counter       │
├─────────────────────────┤
│                         │
│         42              │  ← Large counter display
│       catches           │
│                         │
│   [Restart Counter]     │  ← Reset button
│                         │
└─────────────────────────┘
```

- **Automatic Counting:** Catches are detected and counted automatically
- **Restart:** Click the button to reset the counter to 0
- **Independent Window:** Move to any monitor, minimize separately

## 🏗️ System Architecture (Simplified)

```
┌─────────────────────────────────────────────────────────┐
│                    Your Computer                         │
│                                                          │
│  ┌──────────────────┐         ┌──────────────────┐     │
│  │  JuggleHub       │         │  Catch Counter   │     │
│  │  Main Window     │         │  App Window      │     │
│  │                  │         │                  │     │
│  │  [App Menu]      │────────▶│  Catches: 42     │     │
│  │  • Recent Apps   │ Launch  │  [Restart]       │     │
│  │  • App Manager   │         │                  │     │
│  └──────────────────┘         └──────────────────┘     │
│         │                              │                │
│         │                              │                │
│         └──────────────┬───────────────┘                │
│                        │                                │
│                        ▼                                │
│              ┌──────────────────┐                       │
│              │  Tracking Engine │                       │
│              │  (C++ Process)   │                       │
│              │                  │                       │
│              │  • Camera        │                       │
│              │  • Ball Tracking │                       │
│              │  • Catch Detection│                      │
│              └──────────────────┘                       │
└─────────────────────────────────────────────────────────┘
```

## 🎨 App Manager Interface

The App Manager shows all available apps in a beautiful grid:

```
┌────────────────────────────────────────────────────────────┐
│  🚀 JuggleHub Apps                                         │
├────────────────────────────────────────────────────────────┤
│                                                            │
│  ┌──────────────────┐    ┌──────────────────┐            │
│  │ 🎯 Catch Counter │    │ 🔢 Siteswap ID   │            │
│  │                  │    │                  │            │
│  │ Simple catch     │    │ Identifies       │            │
│  │ counting app     │    │ siteswap patterns│            │
│  │                  │    │ from throws      │            │
│  │ v1.0.0 • analytics│   │ v1.0.0 • analytics│           │
│  │                  │    │                  │            │
│  │    [Launch]      │    │    [Launch]      │            │
│  └──────────────────┘    └──────────────────┘            │
│                                                            │
│  ┌──────────────────┐    ┌──────────────────┐            │
│  │ 🎮 LED Ball      │    │ 📈 Performance   │            │
│  │    Controller    │    │    Metrics       │            │
│  │                  │    │                  │            │
│  │ Control ball     │    │ Track your       │            │
│  │ colors in real   │    │ progress         │            │
│  │ time             │    │                  │            │
│  │ v1.0.0 • control │    │ v1.0.0 • training│            │
│  │                  │    │                  │            │
│  │    [Launch]      │    │    [Launch]      │            │
│  └──────────────────┘    └──────────────────┘            │
│                                                            │
│                                          [Close]           │
└────────────────────────────────────────────────────────────┘
```

## 🔧 How Apps Work

### 1. Apps are Independent
- Each app runs in its own window
- Can be moved to different monitors
- Can be minimized/closed independently
- Multiple apps can run at the same time

### 2. Apps Control Engine Features
- Apps tell the engine which features they need
- Example: Catch Counter needs "throw_catch_detection"
- Engine only runs the features that apps request
- This minimizes latency and maximizes performance

### 3. Apps Receive Real-Time Data
- Engine sends data to apps via ZMQ (fast messaging)
- Apps get ball positions, catch events, hand tracking, etc.
- Data arrives in real-time (< 10ms latency)
- Apps process data and update their UI

## 📊 Data Flow Example (Catch Counter)

```
1. User juggles
   │
   ▼
2. Camera captures video
   │
   ▼
3. Engine detects balls and catches
   │
   ▼
4. Engine sends catch event via ZMQ
   │
   ▼
5. Catch Counter receives event
   │
   ▼
6. Counter increments: 41 → 42
   │
   ▼
7. UI updates to show "42"
```

**Total latency:** < 50ms from catch to display update

## 🎓 For Developers

### Creating a New App

1. **Create app directory:**
   ```bash
   mkdir -p hub/apps/my_app
   ```

2. **Create metadata.json:**
   ```json
   {
     "id": "my_app",
     "name": "My Awesome App",
     "version": "1.0.0",
     "description": "Does something cool",
     "icon": "🚀",
     "category": "training",
     "required_features": ["throw_catch_detection"],
     "entry_point": "hub.apps.my_app.app:MyApp"
   }
   ```

3. **Create app.py:**
   ```python
   from hub.apps.base import BaseApp
   from PyQt6.QtWidgets import QMainWindow, QLabel
   
   class MyApp(BaseApp):
       def get_metadata(self):
           return {...}  # Return metadata
       
       def initialize(self):
           pass  # Initialize app state
       
       def create_window(self):
           window = QMainWindow()
           # Build your UI here
           return window
       
       def on_frame_data(self, frame_data):
           # Process incoming data
           pass
   ```

4. **Test your app:**
   ```bash
   ./scripts/run_hub.sh --use-venv
   # Open App Manager and launch your app
   ```

See [`APP_LAYER_ARCHITECTURE.md`](APP_LAYER_ARCHITECTURE.md) for complete details.

## 🎯 Available Engine Features

Apps can enable/disable these features:

| Feature | Description | Use Case |
|---------|-------------|----------|
| `dnn_tracking` | YOLO-based ball detection | High accuracy tracking |
| `color_tracking` | Color-based ball tracking | Fast, simple tracking |
| `pose_estimation` | Full body pose tracking | Body mechanics analysis |
| `throw_catch_detection` | Detect throws and catches | Catch counting, pattern analysis |
| `hand_tracking` | Track hand positions | Hand-ball interaction |
| `pattern_recognition` | Identify juggling patterns | Pattern analysis (future) |

## 📈 Performance Characteristics

| Metric | Value | Notes |
|--------|-------|-------|
| **App Launch Time** | < 1 second | From click to window open |
| **Data Latency** | < 10ms | Engine to app |
| **UI Update Rate** | 30-60 FPS | Smooth, responsive |
| **Memory per App** | ~50MB | Lightweight |
| **Max Concurrent Apps** | 10+ | Limited by system resources |

## 🔮 Future Enhancements

### Planned Features:
- **App Marketplace:** Download apps from community
- **App Settings:** Persistent configuration per app
- **Inter-App Communication:** Apps can talk to each other
- **Mobile Apps:** Control apps from phone/tablet
- **Cloud Sync:** Sync app data across devices

### Available Apps:
- **Catch Counter:** Simple catch counting for tracking progress
- **Siteswap ID:** Identifies siteswap patterns from throw/catch sequences

### Planned Apps:
- **Pattern Analyzer:** Advanced pattern analysis and visualization
- **Training Coach:** Real-time feedback and coaching
- **Performance Metrics:** Track progress over time
- **LED Ball Controller:** Control ball colors based on tracking
- **Video Recorder:** Record sessions with overlays

## 🤝 Contributing

Want to build an app? Check out:
1. [`APP_LAYER_ARCHITECTURE.md`](APP_LAYER_ARCHITECTURE.md) - Complete architecture
2. [`APP_IMPLEMENTATION_PLAN.md`](APP_IMPLEMENTATION_PLAN.md) - Implementation guide
3. `docs/APP_DEVELOPMENT_GUIDE.md` - Developer tutorial (coming soon)

## 📞 Support

- **Issues:** Open an issue on GitHub
- **Questions:** Check the documentation first
- **Ideas:** We'd love to hear your app ideas!

---

**Built with ❤️ for the juggling community**