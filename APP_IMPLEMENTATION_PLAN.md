# JuggleHub App Layer Implementation Plan

**Last Updated:** 2025-10-02 18:44:00 CEST

## 📋 Implementation Phases

This document outlines the step-by-step implementation plan for the JuggleHub App Layer system.

## Phase 1: Protocol Buffer Extensions ✅ DESIGN COMPLETE

### Tasks:
1. **Extend `api/v1/juggler.proto`** with app-related messages
   - Add `ThrowCatchEvent` message for catch/throw events
   - Add `AppFeatureControl` message for feature management
   - Extend `CommandRequest` with `ENABLE_FEATURE` and `DISABLE_FEATURE` types
   - Add `throw_catch_events` field to `FrameData`
   - Add `feature_name` field to `CommandRequest`

2. **Regenerate Protocol Buffer files**
   - Run `./scripts/generate_protos.sh`
   - Verify C++ and Python files are generated correctly

### Files to Modify:
- `api/v1/juggler.proto`

### Estimated Time: 1-2 hours

---

## Phase 2: Core App Infrastructure ✅ DESIGN COMPLETE

### Tasks:
1. **Create app directory structure**
   ```bash
   mkdir -p hub/apps/catch_counter
   mkdir -p hub/components
   ```

2. **Implement BaseApp abstract class** (`hub/apps/base.py`)
   - Abstract methods: `get_metadata()`, `initialize()`, `create_window()`, `on_frame_data()`
   - Lifecycle management: `start()`, `stop()`
   - Feature control integration

3. **Implement AppAPI** (`hub/apps/api.py`)
   - ZMQ subscriber for frame data
   - ZMQ commander for engine control
   - Methods: `subscribe_to_data()`, `enable_feature()`, `disable_feature()`, `send_command()`
   - Background thread for data reception

4. **Implement AppManager** (`hub/apps/manager.py`)
   - App discovery and registry management
   - App lifecycle (launch, close)
   - Recent apps tracking
   - Window management

5. **Create app registry system**
   - `hub/apps/registry.json` for persistent storage
   - Auto-discovery of apps in `hub/apps/` directory

### Files to Create:
- `hub/apps/__init__.py`
- `hub/apps/base.py`
- `hub/apps/api.py`
- `hub/apps/manager.py`
- `hub/apps/registry.json`

### Estimated Time: 4-6 hours

---

## Phase 3: Engine Feature Control ✅ DESIGN COMPLETE

### Tasks:
1. **Add feature control to Engine** (`engine/src/Engine.cpp`)
   - Implement `ENABLE_FEATURE` command handler
   - Implement `DISABLE_FEATURE` command handler
   - Feature flags for: DNN tracking, color tracking, pose estimation, throw/catch detection
   - Dynamic feature toggling without restart

2. **Update command processing**
   - Add feature name mapping
   - Validate feature requests
   - Return success/failure responses

### Files to Modify:
- `engine/src/Engine.cpp`
- `engine/include/Engine.hpp`

### Estimated Time: 3-4 hours

---

## Phase 4: Catch Counter App ✅ DESIGN COMPLETE

### Tasks:
1. **Create app metadata** (`hub/apps/catch_counter/metadata.json`)
   - App ID, name, version, description
   - Required features: throw_catch_detection
   - Entry point specification

2. **Implement CatchCounterApp** (`hub/apps/catch_counter/app.py`)
   - Inherit from BaseApp
   - Implement all abstract methods
   - Catch event handling
   - Counter state management

3. **Create app UI** (`hub/apps/catch_counter/ui.py` or inline)
   - QMainWindow with counter display
   - Large, readable counter (72pt font)
   - Restart button
   - Dark theme styling

4. **Test catch counter**
   - Launch app independently
   - Verify catch counting works
   - Test restart functionality
   - Verify window can be moved to different monitor

### Files to Create:
- `hub/apps/catch_counter/__init__.py`
- `hub/apps/catch_counter/metadata.json`
- `hub/apps/catch_counter/app.py`

### Estimated Time: 3-4 hours

---

## Phase 5: Main Hub UI Integration ✅ DESIGN COMPLETE

### Tasks:
1. **Add App menu to main window** (`hub/components/ui.py`)
   - Create "App" menu in menu bar
   - Add "Recent Apps" submenu
   - Add "App Manager..." action (Ctrl+M)
   - Populate recent apps dynamically

2. **Create AppManagerDialog** (`hub/components/app_manager_dialog.py`)
   - Grid layout for app cards
   - AppCard widget for each app
   - Launch buttons
   - Scrollable area for many apps
   - Dark theme styling

3. **Integrate AppManager into Hub** (`hub/main.py`)
   - Initialize AppManager in Hub class
   - Pass ZMQ context to AppManager
   - Handle app lifecycle

### Files to Modify:
- `hub/components/ui.py`

### Files to Create:
- `hub/components/app_manager_dialog.py`

### Estimated Time: 4-5 hours

---

## Phase 6: Testing & Documentation ✅ DESIGN COMPLETE

### Tasks:
1. **Integration testing**
   - Test app discovery
   - Test app launching from menu
   - Test app launching from App Manager
   - Test multiple apps running simultaneously
   - Test feature control (enable/disable)
   - Test app window independence (move to different monitor)
   - Test recent apps tracking

2. **Create developer documentation** (`docs/APP_DEVELOPMENT_GUIDE.md`)
   - How to create a new app
   - BaseApp API reference
   - AppAPI reference
   - Metadata format
   - Best practices
   - Example app walkthrough

3. **Update README.md**
   - Add App Layer section
   - Document App menu
   - Document App Manager
   - List available apps
   - Link to developer guide

### Files to Create:
- `docs/APP_DEVELOPMENT_GUIDE.md`

### Files to Modify:
- `README.md`

### Estimated Time: 3-4 hours

---

## Phase 7: Future Enhancements (Post-MVP)

### Planned Features:
1. **App Settings Persistence**
   - Save/load app-specific settings
   - Window position/size memory

2. **App Communication**
   - Inter-app messaging
   - Shared state management

3. **App Marketplace**
   - Remote app discovery
   - One-click installation
   - Version management

4. **Advanced Apps**
   - Pattern Analyzer app
   - Training Coach app
   - Performance Metrics app
   - LED Ball Controller app

5. **App Permissions**
   - Fine-grained feature access control
   - Security model for third-party apps

---

## 🎯 Success Criteria

The app layer implementation is considered complete when:

1. ✅ Apps can be launched from the App menu
2. ✅ Apps run in independent windows
3. ✅ Apps can enable/disable engine features
4. ✅ Apps receive real-time frame data via ZMQ
5. ✅ Catch Counter app successfully counts catches
6. ✅ Recent apps are tracked and displayed
7. ✅ App Manager shows all available apps
8. ✅ Multiple apps can run simultaneously
9. ✅ Documentation is complete and clear
10. ✅ System is stable and performant

---

## 📊 Estimated Total Time

- **Phase 1:** 1-2 hours
- **Phase 2:** 4-6 hours
- **Phase 3:** 3-4 hours
- **Phase 4:** 3-4 hours
- **Phase 5:** 4-5 hours
- **Phase 6:** 3-4 hours

**Total: 18-25 hours** (approximately 3-4 working days)

---

## 🚀 Getting Started

To begin implementation, start with Phase 1 and work sequentially through each phase. Each phase builds on the previous one, so it's important to complete them in order.

### Quick Start Commands:

```bash
# Phase 1: Extend Protocol Buffers
vim api/v1/juggler.proto
./scripts/generate_protos.sh

# Phase 2: Create app infrastructure
mkdir -p hub/apps/catch_counter
touch hub/apps/{__init__.py,base.py,api.py,manager.py,registry.json}

# Phase 4: Create catch counter app
touch hub/apps/catch_counter/{__init__.py,metadata.json,app.py}

# Phase 5: Create app manager dialog
touch hub/components/app_manager_dialog.py

# Test the system
./scripts/run_hub.sh --use-venv
```

---

## 🔄 Iterative Development

This plan supports iterative development:

1. **MVP (Phases 1-4):** Basic app system with catch counter
2. **Polish (Phase 5):** UI integration and user experience
3. **Documentation (Phase 6):** Developer guides and testing
4. **Future (Phase 7):** Advanced features and ecosystem

Each phase can be tested independently before moving to the next.

---

## 📝 Notes

- **Latency Optimization:** The ZMQ subscriber in AppAPI runs in a background thread to avoid blocking the UI
- **Thread Safety:** Use Qt signals for cross-thread communication (see CatchCounterSignals)
- **Feature Control:** Apps should enable required features on start and disable on stop to optimize performance
- **Window Management:** Each app is a separate QMainWindow, allowing independent positioning and minimization
- **Error Handling:** All app operations should have try-except blocks with clear error messages

---

## 🎨 Architecture Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                    JuggleHub Ecosystem                       │
│                                                              │
│  ┌──────────────┐         ┌──────────────┐                 │
│  │  Main Hub    │◄────────┤  App Manager │                 │
│  │  Window      │         └──────────────┘                 │
│  └──────────────┘                │                          │
│         │                         │                          │
│         │                         ▼                          │
│         │              ┌─────────────────────┐              │
│         │              │  App 1: Catch       │              │
│         │              │  Counter            │              │
│         │              └─────────────────────┘              │
│         │                         │                          │
│         │              ┌─────────────────────┐              │
│         │              │  App 2: Pattern     │              │
│         │              │  Analyzer           │              │
│         │              └─────────────────────┘              │
│         │                         │                          │
│         └─────────────────────────┴──────────────┐          │
│                                                   │          │
│                                                   ▼          │
│                                        ┌──────────────────┐ │
│                                        │  AppAPI          │ │
│                                        │  (ZMQ Interface) │ │
│                                        └──────────────────┘ │
│                                                   │          │
└───────────────────────────────────────────────────┼──────────┘
                                                    │
                                                    ▼
                                         ┌──────────────────┐
                                         │  C++ Engine      │
                                         │  (ZMQ Publisher) │
                                         └──────────────────┘
```

This architecture ensures:
- **Loose coupling** between apps and engine
- **High performance** with direct ZMQ communication
- **Flexibility** to add new apps without modifying core system
- **Scalability** to support many concurrent apps