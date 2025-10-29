# Rebuild Engine After Held Circle Offset Changes

## Issue
The held circle offset visualization is not updating because the C++ engine needs to be recompiled after code changes.

## Solution
You need to rebuild the C++ engine to see the changes take effect.

### Quick Rebuild Command

```bash
cd engine
mkdir -p build
cd build
cmake ..
make -j$(nproc)
cd ../..
```

### Or use the build script (if available):

```bash
./scripts/build_engine.sh
```

### After Rebuilding

1. Restart the engine
2. The held circle offset setting should now work
3. Adjust the slider in Settings → New 3D Tracker → Physics & Kalman Filter → "Held Circle Offset (cm)"
4. The yellow circle around hands should move based on the offset value

## What Changed

The C++ code now:
1. Calculates forearm direction from elbow to wrist keypoints
2. Offsets the held circle center along this direction
3. Shows both the offset circle (yellow) and wrist dot (small yellow dot)

## Verification

After rebuilding, you should see:
- Yellow circle at offset position (not just at wrist)
- Small yellow dot at wrist position for reference
- Circle moves when you adjust the "Held Circle Offset" slider