# Siteswap ID App

**Version:** 1.0.0  
**Category:** Analytics  
**Last Updated:** 2025-10-08

## Overview

The Siteswap ID app identifies siteswap patterns by analyzing throw and catch sequences from the JuggleHub tracking engine. It tracks which hands throw which balls, calculates timing information, and attempts to identify the siteswap notation being performed.

## Features

### Pattern Detection
- **Vanilla Siteswap Analysis**: Detects asynchronous siteswap patterns (e.g., 3, 441, 531)
- **Pattern Recognition**: Identifies repeating patterns in throw sequences
- **Real-time Updates**: Pattern updates as you juggle

### Statistics Tracking
- **Throw/Catch Counts**: Total throws and catches per hand
- **Flight Time**: Average time balls spend in the air
- **Dwell Time**: Average time balls spend in hands
- **Ball Count**: Number of balls being tracked

### Event Logging
- **Real-time Event Log**: Shows recent 20 throw/catch events
- **Color-Coded Events**: Events are colored based on ball color
  - **Throws**: Brighter colors (e.g., bright pink for pink ball throw)
  - **Catches**: Darker colors (e.g., dark pink for pink ball catch)
- **Timestamps**: Precise timing for each event
- **Hand Identification**: Left (L) or Right (R) hand
- **Confidence Scores**: Detection confidence for each event

## How It Works

### Siteswap Detection Algorithm

1. **Event Collection**: Collects throw and catch events from the engine
2. **Flight Time Calculation**: Measures time between throw and next catch
3. **Height Estimation**: Converts flight time to throw height (beats)
   - Each ~200ms of flight time ≈ 1 beat
   - Heights are clamped to 0-9 for standard siteswap notation
4. **Pattern Recognition**: Searches for repeating sequences in throw heights
5. **Pattern Display**: Shows detected pattern in siteswap notation

### Data Flow

```
Engine → Throw/Catch Events → Analyzer → Pattern Detection → UI Update
                                    ↓
                              Statistics Calculation
```

## Usage

### Launching the App

1. Open JuggleHub
2. Click `App → App Manager...` (or press Ctrl+M)
3. Find "Siteswap ID" in the app grid
4. Click "Launch"

### Using the App

1. **Start Juggling**: Begin your juggling pattern
2. **Watch Detection**: The app will analyze throws and catches
3. **View Pattern**: Detected siteswap appears in large text
4. **Check Statistics**: Review timing and count statistics
5. **Monitor Events**: See real-time event log at bottom
6. **Reset**: Click "Reset Analysis" to clear and start fresh

### Tips for Best Results

- **Consistent Pattern**: Juggle a consistent, repeating pattern
- **Clear Throws**: Make distinct throws and catches
- **Stable Rhythm**: Maintain steady timing between throws
- **Good Lighting**: Ensure proper camera visibility
- **Calibrated System**: Make sure tracking is working well

## Technical Details

### Required Engine Features
- `throw_catch_detection`: Detects throw and catch events

### Data Processing
- **Thread-Safe**: Uses Qt signals for UI updates from background thread
- **Event History**: Maintains rolling buffer of last 100 events
- **Pattern Analysis**: Analyzes last 10 throws for pattern detection
- **Color Tracking**: Maps ball IDs to colors from ColorTrackedBall data

### Color Coding
- **Ball Color Detection**: Automatically detects ball colors from tracking data
- **Throw Brightness**: Throws are displayed in brighter versions of ball color (1.3x brightness)
- **Catch Darkness**: Catches are displayed in darker versions of ball color (0.7x brightness)
- **Supported Colors**: red, orange, yellow, green, blue, purple, pink, white

### Timing Calculations

**Flight Time**: Time from throw to catch
```
flight_time = catch_timestamp - throw_timestamp
```

**Dwell Time**: Time from catch to throw
```
dwell_time = throw_timestamp - catch_timestamp
```

**Beat Estimation**: Convert flight time to siteswap height
```
height = round(flight_time_ms / 200ms)
```

## Limitations

### Current Version (1.0.0)

- **Vanilla Siteswap Only**: Currently only detects asynchronous patterns
- **Synchronous Patterns**: Not yet implemented (e.g., (4,4), (6x,4))
- **Multiplex**: Does not detect multiplex patterns
- **Pattern Length**: Best results with patterns of 3-7 throws
- **Timing Sensitivity**: Requires consistent throw timing

### Future Enhancements

- Synchronous siteswap detection
- Crossing throw detection (x notation)
- Multiplex pattern support
- Pattern visualization
- Historical pattern tracking
- Export pattern data
- Custom beat time calibration

## Troubleshooting

### Pattern Shows "Detecting..."
- **Cause**: Not enough data or inconsistent pattern
- **Solution**: Continue juggling with consistent rhythm

### Incorrect Pattern Detected
- **Cause**: Timing variations or missed detections
- **Solution**: Click "Reset Analysis" and try again with more consistent throws

### No Events in Log
- **Cause**: Throw/catch detection not working
- **Solution**: Check main hub tracking, ensure hands and balls are visible

### Statistics Not Updating
- **Cause**: No events being received
- **Solution**: Verify engine is running and publishing data

## Development

### File Structure
```
hub/apps/siteswap_id/
├── __init__.py          # Package marker
├── metadata.json        # App metadata
├── app.py              # Main application code
└── README.md           # This file
```

### Key Classes

- **`SiteswapIDApp`**: Main app class, inherits from BaseApp
- **`SiteswapAnalyzer`**: Pattern detection and analysis logic
- **`ThrowCatchSequence`**: Event sequence tracking and timing
- **`SiteswapSignal`**: Qt signal emitter for thread-safe UI updates

### Extending the App

To add new features:

1. **Add Analysis Method**: Implement in `SiteswapAnalyzer` class
2. **Update UI**: Add display elements in `create_window()`
3. **Connect Signals**: Wire up new signals in `initialize()`
4. **Process Events**: Update `_on_event()` to call new analysis

## References

### Siteswap Notation
- [Siteswap on Wikipedia](https://en.wikipedia.org/wiki/Siteswap)
- [Juggling Lab](https://jugglinglab.org/) - Pattern simulator
- [Library of Juggling](https://libraryofjuggling.com/) - Pattern database

### JuggleHub Documentation
- [`APP_DEVELOPER_GUIDE.md`](../../APP_DEVELOPER_GUIDE.md) - App development guide
- [`APP_SYSTEM_OVERVIEW.md`](../../APP_SYSTEM_OVERVIEW.md) - System overview
- [`api/v1/juggler.proto`](../../../api/v1/juggler.proto) - Protocol buffer definitions

## License

Part of the JuggleHub project. See main project LICENSE for details.

## Contributing

Contributions welcome! Ideas for improvements:
- Better pattern detection algorithms
- Support for more siteswap types
- Pattern visualization
- Machine learning for pattern recognition
- Export/import pattern data

---

**Built with ❤️ for the juggling community**