# Ball Tracking System User Guide

**Last Updated:** 2025-10-03

## Table of Contents
1. [Getting Started](#getting-started)
2. [Creating Your First Ball](#creating-your-first-ball)
3. [Multi-Sample Calibration](#multi-sample-calibration)
4. [Activating Balls for Tracking](#activating-balls-for-tracking)
5. [Switching Between Modes](#switching-between-modes)
6. [Troubleshooting](#troubleshooting)
7. [API Reference](#api-reference)

---

## Getting Started

The JuggleHub ball tracking system uses color-based identification to track multiple juggling balls simultaneously. The system supports two modes:

- **Legacy Mode**: Simple HSV color matching (backward compatible)
- **New Mode**: Advanced multi-sample calibration with confidence scoring

### Prerequisites

- JuggleHub engine running with camera access
- JuggleHub Hub application running with API enabled
- At least one juggling ball with distinct color

### Quick Start

1. Start the engine: `./engine/build/juggle_engine`
2. Start the Hub with API: `python hub/main.py --enable-api`
3. Open the ball management interface in the Hub UI
4. Follow the calibration wizard to create your first ball profile

---

## Creating Your First Ball

### Step 1: Access Ball Management

In the Hub UI, navigate to the **Ball Management** section. You'll see:
- List of existing ball profiles
- "Add New Ball" button
- Active/Inactive status for each ball

### Step 2: Start Calibration

1. Click **"Add New Ball"**
2. Enter a descriptive name (e.g., "Red Ball", "Blue Juggling Ball")
3. Choose calibration mode:
   - **Quick Calibration**: Single sample (legacy mode)
   - **Advanced Calibration**: Multi-sample (recommended)

### Step 3: Capture Color Samples

**For Quick Calibration:**
- Hold the ball in front of the camera
- Click "Capture Sample"
- Adjust HSV ranges if needed
- Click "Save"

**For Advanced Calibration (Recommended):**
- Hold the ball in different lighting conditions
- Capture 3-5 samples from various angles
- The system automatically calculates optimal color ranges
- Review the confidence preview
- Click "Save Profile"

### Step 4: Verify Detection

After saving:
1. The ball appears in your ball list
2. Status shows as "Inactive" (not yet tracking)
3. A preview shows detection confidence in real-time
4. Green overlay indicates successful detection

---

## Multi-Sample Calibration

Multi-sample calibration provides robust tracking across varying conditions.

### Best Practices

**Number of Samples:**
- Minimum: 3 samples
- Recommended: 5-7 samples
- Maximum: 10 samples

**Sample Diversity:**
1. **Lighting Variation**: Capture under different light sources
   - Direct overhead light
   - Side lighting
   - Ambient room light
   - Near windows (natural light)

2. **Angle Variation**: Capture from different viewpoints
   - Front view
   - Side view
   - Slightly rotated
   - Different distances

3. **Background Variation**: Capture against different backgrounds
   - Light backgrounds
   - Dark backgrounds
   - Cluttered backgrounds

### Calibration Tips

✅ **DO:**
- Keep the ball steady during capture
- Ensure good focus (not blurry)
- Fill 30-50% of the frame with the ball
- Capture in your typical juggling environment
- Review each sample before proceeding

❌ **DON'T:**
- Capture with motion blur
- Use extreme lighting (too bright/dark)
- Capture with fingers covering the ball
- Rush through samples
- Capture all samples in identical conditions

### Understanding Confidence Scores

The system displays confidence metrics:
- **High Confidence (>0.8)**: Excellent detection, ready to track
- **Medium Confidence (0.5-0.8)**: Good detection, may need refinement
- **Low Confidence (<0.5)**: Poor detection, recalibrate recommended

---

## Activating Balls for Tracking

### Activation Process

1. **Select Ball**: Click on a ball profile in the list
2. **Review Status**: Check detection confidence
3. **Activate**: Click the "Activate" toggle
4. **Verify**: Ball status changes to "Active"

### Active Ball Behavior

When a ball is active:
- Engine receives color profile via UDP
- Ball appears in tracking visualization
- Position data streams to connected applications
- Kalman filter initializes for smooth tracking

### Managing Multiple Balls

**Simultaneous Tracking:**
- Up to 10 balls can be active simultaneously
- Each ball must have distinct color profile
- System automatically assigns tracking IDs

**Switching Active Balls:**
1. Deactivate current ball (click toggle)
2. Wait for confirmation (status updates)
3. Activate new ball
4. Verify tracking in visualization

**Best Practices:**
- Start with one ball to verify system
- Add balls incrementally
- Ensure color separation between balls
- Monitor performance with multiple balls

---

## Switching Between Modes

### Legacy Mode vs. New Mode

**Legacy Mode:**
- Single HSV range per ball
- Faster processing
- Less robust to lighting changes
- Backward compatible with old profiles

**New Mode:**
- Multi-sample calibration
- Confidence-based detection
- Adaptive to lighting variations
- Recommended for new setups

### Switching Modes

**Via API:**
```python
import requests

# Switch to new mode
response = requests.post('http://localhost:5000/api/balls/mode', 
                        json={'mode': 'new'})

# Switch to legacy mode
response = requests.post('http://localhost:5000/api/balls/mode', 
                        json={'mode': 'legacy'})
```

**Via UI:**
1. Open Settings panel
2. Navigate to "Ball Tracking"
3. Select mode from dropdown
4. Click "Apply"
5. System restarts tracking with new mode

### Migration Considerations

When switching from legacy to new mode:
- Existing profiles remain compatible
- Legacy profiles work in new mode (single sample)
- Recalibration recommended for best results
- No data loss during mode switch

---

## Troubleshooting

### Ball Not Detected

**Symptoms:** Ball profile shows low confidence or no detection

**Solutions:**
1. **Recalibrate**: Capture new samples in current lighting
2. **Check Lighting**: Ensure adequate, consistent lighting
3. **Verify Color**: Ensure ball color is distinct from background
4. **Clean Lens**: Check camera lens for smudges
5. **Adjust Ranges**: Manually tune HSV ranges if needed

### Tracking Jumps or Jitters

**Symptoms:** Ball position jumps erratically

**Solutions:**
1. **Reduce Noise**: Improve lighting consistency
2. **Check Confidence**: Verify detection confidence is high
3. **Kalman Tuning**: Adjust filter parameters (advanced)
4. **Frame Rate**: Ensure camera running at target FPS
5. **Reduce Clutter**: Minimize similar colors in background

### Multiple Balls Confused

**Symptoms:** System swaps ball identities

**Solutions:**
1. **Increase Color Separation**: Use more distinct colors
2. **Recalibrate**: Ensure each profile is unique
3. **Check Overlap**: Verify HSV ranges don't overlap
4. **Reduce Active Balls**: Track fewer balls simultaneously
5. **Improve Lighting**: Better lighting reduces ambiguity

### Performance Issues

**Symptoms:** Low frame rate, lag, or delays

**Solutions:**
1. **Reduce Active Balls**: Deactivate unused ball profiles
2. **Lower Resolution**: Reduce camera resolution if possible
3. **Check CPU**: Monitor system resource usage
4. **Optimize Lighting**: Better lighting = faster processing
5. **Update Drivers**: Ensure camera drivers are current

### API Connection Issues

**Symptoms:** Cannot activate/deactivate balls via API

**Solutions:**
1. **Check Hub Running**: Verify Hub is running with `--enable-api`
2. **Verify Port**: Confirm API listening on port 5000
3. **Check Firewall**: Ensure port 5000 is not blocked
4. **Test Connection**: Use `curl http://localhost:5000/api/balls`
5. **Review Logs**: Check Hub logs for error messages

---

## API Reference

### Base URL
```
http://localhost:5000/api
```

### Endpoints

#### List All Balls
```http
GET /api/balls
```

**Response:**
```json
{
  "balls": [
    {
      "id": "ball_001",
      "name": "Red Ball",
      "active": true,
      "confidence": 0.92,
      "mode": "new",
      "samples": 5
    }
  ]
}
```

#### Get Ball Details
```http
GET /api/balls/{ball_id}
```

**Response:**
```json
{
  "id": "ball_001",
  "name": "Red Ball",
  "active": true,
  "color_profile": {
    "mode": "new",
    "samples": 5,
    "hsv_ranges": [...]
  },
  "confidence": 0.92,
  "last_seen": "2025-10-03T11:20:00Z"
}
```

#### Create New Ball
```http
POST /api/balls
Content-Type: application/json

{
  "name": "Blue Ball",
  "mode": "new",
  "samples": [
    {"h": 110, "s": 200, "v": 180},
    {"h": 115, "s": 210, "v": 175}
  ]
}
```

**Response:**
```json
{
  "id": "ball_002",
  "name": "Blue Ball",
  "status": "created"
}
```

#### Activate Ball
```http
POST /api/balls/{ball_id}/activate
```

**Response:**
```json
{
  "id": "ball_001",
  "active": true,
  "status": "activated"
}
```

#### Deactivate Ball
```http
POST /api/balls/{ball_id}/deactivate
```

**Response:**
```json
{
  "id": "ball_001",
  "active": false,
  "status": "deactivated"
}
```

#### Update Ball Profile
```http
PUT /api/balls/{ball_id}
Content-Type: application/json

{
  "name": "Updated Name",
  "samples": [...]
}
```

#### Delete Ball
```http
DELETE /api/balls/{ball_id}
```

**Response:**
```json
{
  "id": "ball_001",
  "status": "deleted"
}
```

#### Switch Tracking Mode
```http
POST /api/balls/mode
Content-Type: application/json

{
  "mode": "new"  // or "legacy"
}
```

**Response:**
```json
{
  "mode": "new",
  "status": "mode_changed",
  "active_balls_reinitialized": true
}
```

### Python Example

```python
import requests
import json

class BallTrackingAPI:
    def __init__(self, base_url="http://localhost:5000/api"):
        self.base_url = base_url
    
    def list_balls(self):
        response = requests.get(f"{self.base_url}/balls")
        return response.json()
    
    def create_ball(self, name, samples):
        data = {
            "name": name,
            "mode": "new",
            "samples": samples
        }
        response = requests.post(f"{self.base_url}/balls", json=data)
        return response.json()
    
    def activate_ball(self, ball_id):
        response = requests.post(f"{self.base_url}/balls/{ball_id}/activate")
        return response.json()
    
    def deactivate_ball(self, ball_id):
        response = requests.post(f"{self.base_url}/balls/{ball_id}/deactivate")
        return response.json()

# Usage
api = BallTrackingAPI()

# List all balls
balls = api.list_balls()
print(f"Found {len(balls['balls'])} balls")

# Activate first ball
if balls['balls']:
    result = api.activate_ball(balls['balls'][0]['id'])
    print(f"Activated: {result['status']}")
```

### JavaScript Example

```javascript
class BallTrackingAPI {
    constructor(baseUrl = 'http://localhost:5000/api') {
        this.baseUrl = baseUrl;
    }
    
    async listBalls() {
        const response = await fetch(`${this.baseUrl}/balls`);
        return await response.json();
    }
    
    async createBall(name, samples) {
        const response = await fetch(`${this.baseUrl}/balls`, {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({name, mode: 'new', samples})
        });
        return await response.json();
    }
    
    async activateBall(ballId) {
        const response = await fetch(`${this.baseUrl}/balls/${ballId}/activate`, {
            method: 'POST'
        });
        return await response.json();
    }
}

// Usage
const api = new BallTrackingAPI();

// List and activate first ball
api.listBalls().then(data => {
    if (data.balls.length > 0) {
        api.activateBall(data.balls[0].id).then(result => {
            console.log('Activated:', result.status);
        });
    }
});
```

---

## Advanced Topics

### Custom Confidence Thresholds

Adjust detection sensitivity by modifying confidence thresholds in the engine configuration:

```cpp
// In ColorTracker.cpp
const float MIN_CONFIDENCE = 0.6f;  // Adjust as needed
```

### Kalman Filter Tuning

For smoother tracking, tune Kalman filter parameters:

```cpp
// In KalmanFilter3D.hpp
const float PROCESS_NOISE = 0.01f;      // Lower = smoother, higher = more responsive
const float MEASUREMENT_NOISE = 0.1f;   // Lower = trust measurements more
```

### Performance Optimization

For high-performance scenarios:
1. Use legacy mode for faster processing
2. Reduce number of active balls
3. Lower camera resolution
4. Increase confidence thresholds
5. Optimize lighting for consistent detection

---

## Support and Resources

- **Documentation**: See `README.md` for system overview
- **Migration Guide**: See `MIGRATION_TO_NEW_TRACKING.md`
- **API Documentation**: See `hub/API_DOCUMENTATION.md`
- **Architecture**: See `APP_LAYER_ARCHITECTURE.md`

For issues or questions, refer to the troubleshooting section or check the project documentation.

---

*This guide is part of the JuggleHub project. Last updated: 2025-10-03*