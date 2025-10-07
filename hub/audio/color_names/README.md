# Color Name Audio Files

This directory contains audio files for announcing ball colors during throw/catch events.

## Required Files

Please add MP3 files for each of the following colors:

- `pink.mp3` - Audio saying "pink"
- `orange.mp3` - Audio saying "orange"
- `yellow.mp3` - Audio saying "yellow"
- `green.mp3` - Audio saying "green"
- `red.mp3` - Audio saying "red"
- `blue.mp3` - Audio saying "blue"
- `purple.mp3` - Audio saying "purple"
- `white.mp3` - Audio saying "white"

## File Format

- **Format**: MP3
- **Naming**: Lowercase color name (e.g., `red.mp3`, not `Red.mp3`)
- **Duration**: Keep files short (0.5-1.5 seconds recommended)
- **Quality**: Any reasonable quality is fine (128kbps or higher)

## How to Create Audio Files

You can create these files in several ways:

1. **Record your own voice**: Use any audio recording software
2. **Text-to-Speech**: Use online TTS services like:
   - Google Text-to-Speech
   - Amazon Polly
   - Microsoft Azure TTS
3. **Voice synthesis tools**: Use tools like Audacity with TTS plugins

## Usage

Once the files are in place, enable the feature in the JuggleHub UI:
- Go to **Tracking Settings** → **Ball State Detection** section
- Enable **"Name on Catches"** to hear color names when balls are caught
- Enable **"Name on Throws"** to hear color names when balls are thrown
- Use the **Test** buttons to verify the audio files work correctly

## Linux Audio Requirements

On Linux, the system uses `mpg123` or `ffplay` for MP3 playback. Install with:
```bash
sudo apt install mpg123
```

Or if you prefer ffplay:
```bash
sudo apt install ffmpeg
```

---
*Last updated: 2025-01-07*