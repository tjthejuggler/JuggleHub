# Ball Management API Documentation

**Created:** 2025-10-03  
**Version:** 1.0  
**Base URL:** `http://localhost:5000/api`

## Overview

The Ball Management API provides HTTP REST endpoints for managing balls through the JuggleHub system. It interfaces with the C++ engine's BallRegistry via ZMQ to handle ball creation, deletion, activation, color calibration, and tracking mode management.

## Table of Contents

- [Getting Started](#getting-started)
- [Authentication](#authentication)
- [Response Format](#response-format)
- [Endpoints](#endpoints)
  - [Health Check](#health-check)
  - [Ball Management](#ball-management)
  - [Color Calibration](#color-calibration)
  - [Tracking Mode](#tracking-mode)
  - [Registry Persistence](#registry-persistence)
- [Error Handling](#error-handling)
- [Examples](#examples)

## Getting Started

### Starting the API Server

The API server is automatically started when running the Hub:

```bash
# Start Hub with API enabled (default)
python3 hub/main.py --no-ui

# Start Hub with custom API port
python3 hub/main.py --no-ui --api-port 8080

# Disable API server
python3 hub/main.py --no-ui --no-api
```

### Testing the API

Use the provided test script:

```bash
python3 hub/test_ball_api.py
```

Or test manually with curl:

```bash
curl http://localhost:5000/api/health
```

## Authentication

Currently, the API does not require authentication. This may be added in future versions.

## Response Format

All API responses follow a consistent JSON format:

### Success Response

```json
{
  "success": true,
  "message": "Operation completed successfully",
  "data": {
    // Optional response data
  }
}
```

### Error Response

```json
{
  "success": false,
  "error": "Error message describing what went wrong"
}
```

## Endpoints

### Health Check

#### `GET /api/health`

Check if the API server is running and healthy.

**Response:**
```json
{
  "success": true,
  "message": "Ball API is healthy"
}
```

---

### Ball Management

#### `POST /api/balls/create`

Create a new ball in the registry.

**Request Body:**
```json
{
  "display_name": "Pink Ball #1"
}
```

**Response:**
```json
{
  "success": true,
  "message": "Ball created successfully",
  "data": {
    "ball_id": "ball_001"
  }
}
```

---

#### `DELETE /api/balls/<ball_id>`

Delete a ball from the registry.

**URL Parameters:**
- `ball_id` (string): Unique ID of the ball to delete

**Response:**
```json
{
  "success": true,
  "message": "Ball ball_001 deleted successfully"
}
```

---

#### `GET /api/balls`

Get all registered balls (active and inactive).

**Response:**
```json
{
  "success": true,
  "message": "Retrieved 3 balls",
  "data": {
    "balls": [
      {
        "id": "ball_001",
        "display_name": "Pink Ball #1",
        "is_active": true,
        "logical_tracker_id": 0,
        "color_samples": [...],
        "frames_tracked": 1234,
        "frames_lost": 56
      }
    ]
  }
}
```

---

#### `GET /api/balls/active`

Get only currently active balls.

**Response:**
```json
{
  "success": true,
  "message": "Retrieved 2 active balls",
  "data": {
    "balls": [...]
  }
}
```

---

#### `POST /api/balls/<ball_id>/activate`

Activate a ball for tracking.

**URL Parameters:**
- `ball_id` (string): Unique ID of the ball to activate

**Response:**
```json
{
  "success": true,
  "message": "Ball ball_001 activated successfully"
}
```

**Error Cases:**
- Ball not found
- Maximum active balls already reached
- Ball already active

---

#### `POST /api/balls/<ball_id>/deactivate`

Deactivate a ball (stop tracking it).

**URL Parameters:**
- `ball_id` (string): Unique ID of the ball to deactivate

**Response:**
```json
{
  "success": true,
  "message": "Ball ball_001 deactivated successfully"
}
```

---

### Color Calibration

#### `POST /api/balls/<ball_id>/samples`

Add a color calibration sample to a ball's profile.

**URL Parameters:**
- `ball_id` (string): Unique ID of the ball

**Request Body:**
```json
{
  "click_x": 320,
  "click_y": 240,
  "lighting": "bright"
}
```

**Parameters:**
- `click_x` (integer, required): X coordinate where user clicked to sample
- `click_y` (integer, required): Y coordinate where user clicked to sample
- `lighting` (string, optional): Lighting condition description (default: "unknown")
  - Suggested values: "bright", "dim", "mixed", "indoor", "outdoor"

**Response:**
```json
{
  "success": true,
  "message": "Color sample added to ball ball_001"
}
```

---

#### `DELETE /api/balls/<ball_id>/samples/<sample_index>`

Remove a color calibration sample from a ball's profile.

**URL Parameters:**
- `ball_id` (string): Unique ID of the ball
- `sample_index` (integer): Index of the sample to remove (0-based)

**Response:**
```json
{
  "success": true,
  "message": "Color sample 0 removed from ball ball_001"
}
```

---

### Tracking Mode

#### `POST /api/tracking/mode`

Set the tracking mode (new system or legacy).

**Request Body:**
```json
{
  "use_new_system": true
}
```

**Parameters:**
- `use_new_system` (boolean, required): 
  - `true` = Use new ball tracking system
  - `false` = Use legacy tracking system

**Response:**
```json
{
  "success": true,
  "message": "Tracking mode set to: new"
}
```

---

#### `GET /api/tracking/mode`

Get the current tracking mode.

**Response:**
```json
{
  "success": true,
  "message": "Success",
  "data": {
    "use_new_system": true,
    "mode": "new"
  }
}
```

---

### Registry Persistence

#### `POST /api/balls/registry/save`

Save the ball registry to a file.

**Request Body (optional):**
```json
{
  "filepath": "custom_registry.json"
}
```

**Parameters:**
- `filepath` (string, optional): Path to save file (default: "ball_registry.json")

**Response:**
```json
{
  "success": true,
  "message": "Ball registry saved to: ball_registry.json"
}
```

---

#### `POST /api/balls/registry/load`

Load the ball registry from a file.

**Request Body (optional):**
```json
{
  "filepath": "custom_registry.json"
}
```

**Parameters:**
- `filepath` (string, optional): Path to load file (default: "ball_registry.json")

**Response:**
```json
{
  "success": true,
  "message": "Ball registry loaded from: ball_registry.json"
}
```

---

## Error Handling

### HTTP Status Codes

- `200 OK`: Request succeeded
- `400 Bad Request`: Invalid request parameters
- `500 Internal Server Error`: Server-side error

### Common Error Responses

**Missing Required Field:**
```json
{
  "success": false,
  "error": "Missing required field: display_name"
}
```

**Ball Not Found:**
```json
{
  "success": false,
  "error": "Command failed: Ball not found"
}
```

**Communication Error:**
```json
{
  "success": false,
  "error": "Communication error: Connection refused"
}
```

---

## Examples

### Complete Workflow Example

```bash
# 1. Check API health
curl http://localhost:5000/api/health

# 2. Create a new ball
curl -X POST http://localhost:5000/api/balls/create \
  -H "Content-Type: application/json" \
  -d '{"display_name": "Red Ball"}'

# Response: {"success": true, "data": {"ball_id": "ball_001"}}

# 3. Add color samples
curl -X POST http://localhost:5000/api/balls/ball_001/samples \
  -H "Content-Type: application/json" \
  -d '{"click_x": 320, "click_y": 240, "lighting": "bright"}'

curl -X POST http://localhost:5000/api/balls/ball_001/samples \
  -H "Content-Type: application/json" \
  -d '{"click_x": 325, "click_y": 245, "lighting": "dim"}'

# 4. Activate the ball for tracking
curl -X POST http://localhost:5000/api/balls/ball_001/activate

# 5. Enable new tracking system
curl -X POST http://localhost:5000/api/tracking/mode \
  -H "Content-Type: application/json" \
  -d '{"use_new_system": true}'

# 6. Get all active balls
curl http://localhost:5000/api/balls/active

# 7. Save registry
curl -X POST http://localhost:5000/api/balls/registry/save

# 8. Deactivate ball when done
curl -X POST http://localhost:5000/api/balls/ball_001/deactivate
```

### Python Example

```python
import requests

BASE_URL = "http://localhost:5000/api"

# Create a ball
response = requests.post(
    f"{BASE_URL}/balls/create",
    json={"display_name": "Blue Ball"}
)
ball_id = response.json()["data"]["ball_id"]

# Add color sample
requests.post(
    f"{BASE_URL}/balls/{ball_id}/samples",
    json={
        "click_x": 320,
        "click_y": 240,
        "lighting": "bright"
    }
)

# Activate ball
requests.post(f"{BASE_URL}/balls/{ball_id}/activate")

# Get active balls
response = requests.get(f"{BASE_URL}/balls/active")
active_balls = response.json()["data"]["balls"]
print(f"Active balls: {len(active_balls)}")
```

### JavaScript/Fetch Example

```javascript
const BASE_URL = "http://localhost:5000/api";

// Create a ball
const createBall = async (displayName) => {
  const response = await fetch(`${BASE_URL}/balls/create`, {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({display_name: displayName})
  });
  const data = await response.json();
  return data.data.ball_id;
};

// Add color sample
const addColorSample = async (ballId, x, y, lighting) => {
  await fetch(`${BASE_URL}/balls/${ballId}/samples`, {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({
      click_x: x,
      click_y: y,
      lighting: lighting
    })
  });
};

// Usage
const ballId = await createBall("Green Ball");
await addColorSample(ballId, 320, 240, "bright");
```

---

## Integration Notes

### With C++ Engine

The API communicates with the C++ engine via ZMQ request-response protocol. The engine must be running and listening on the configured ZMQ port (default: 5565) for the API to function.

### Thread Safety

The API runs in a separate thread from the main Hub application. All operations are thread-safe through the ZMQ communication layer.

### Performance Considerations

- API responses are typically fast (<100ms) for most operations
- Color sample operations may take longer as they process image data
- Registry save/load operations depend on file I/O performance

---

## Future Enhancements

Planned features for future versions:

- Authentication and authorization
- WebSocket support for real-time ball tracking updates
- Batch operations for multiple balls
- Ball grouping and tagging
- Advanced color profile management
- Statistics and analytics endpoints
- Export/import in multiple formats (JSON, CSV, etc.)

---

## Support

For issues or questions:
- Check the test script: `hub/test_ball_api.py`
- Review the implementation: `hub/api_routes.py` and `hub/ball_manager.py`
- Consult the main documentation: `README.md`