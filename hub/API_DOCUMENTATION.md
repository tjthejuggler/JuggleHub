# JuggleHub API Documentation

**Status:** ARCHIVED - Ball Management API Removed
**Last Updated:** 2025-10-03

## Overview

The Ball Management API has been removed from JuggleHub. The system now uses only legacy color tracking without REST API endpoints.

## Removed Endpoints

The following endpoints were part of the Ball Management API and have been removed:

- `GET /api/balls` - List all balls
- `GET /api/balls/active` - List active balls
- `POST /api/balls` - Create new ball
- `DELETE /api/balls/<ball_id>` - Delete ball
- `POST /api/balls/<ball_id>/activate` - Activate ball
- `POST /api/balls/<ball_id>/deactivate` - Deactivate ball
- `POST /api/balls/<ball_id>/calibrate` - Add color sample
- `DELETE /api/balls/<ball_id>/samples/<index>` - Remove color sample
- `GET /api/tracking/mode` - Get tracking mode
- `POST /api/tracking/mode` - Set tracking mode

## Current Communication

JuggleHub now uses only ZeroMQ for communication between the C++ engine and Python hub:

### ZeroMQ Sockets
- **Publisher:** `tcp://127.0.0.1:5555` - Frame data from engine to hub
- **Commander:** `tcp://127.0.0.1:5565` - Commands from hub to engine

### Protocol Buffers
Communication uses Protocol Buffers defined in `juggler.proto`:
- `FrameData` - Video frames with tracking data
- `CommandRequest` - Commands to engine
- `CommandResponse` - Responses from engine

## Color Calibration

Color calibration is now done through the UI only:
1. Click on a ball in the video feed
2. System captures HSV values
3. Color range is saved to `ball_settings.json`

No API endpoints are available for programmatic calibration.