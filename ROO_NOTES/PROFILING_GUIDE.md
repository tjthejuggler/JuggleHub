# JuggleHub Performance Profiling Guide

**Created:** 2025-10-11  
**Purpose:** Find and fix performance bottlenecks causing FPS drops

---

## Quick Start

### Option 1: Comprehensive System Profile (Recommended)
```bash
./scripts/profile_system.sh
```

This will:
1. Check system resources (CPU, GPU, Memory)
2. Scan for large log files
3. Profile the C++ engine with `perf`
4. Profile the Python hub with `py-spy`
5. Generate comprehensive reports in a timestamped directory

### Option 2: Profile Engine Only
```bash
./scripts/profile_engine.sh
```

### Option 3: Profile Hub Only
```bash
./scripts/profile_hub.sh
```

---

## Understanding the Results

### System Info (`system_info.txt`)
- **CPU usage**: Should be <80% during normal operation
- **Memory usage**: Check for memory leaks
- **GPU usage**: Should be <50% for smooth operation
- **Temperature**: High temps (>80°C) cause thermal throttling

### Log Analysis (`log_analysis.txt`)
- Lists all `.log` files sorted by size
- **Warning if total >100MB**: Large logs can slow disk I/O
- Check for:
  - `recording.log` files in old recording directories
  - `color_matching.log` growing unbounded
  - Debug logs left enabled in production

### Engine Profile (`engine_profile.txt`)
Shows C++ functions consuming the most CPU time:

```
# Example output:
50.00%  juggle_engine  [.] matchColor()
20.00%  juggle_engine  [.] cv::cvtColor()
15.00%  juggle_engine  [.] updateTrajectory()
```

**What to look for:**
- Functions taking >20% of CPU time
- Unexpected functions in top 10
- GPU conversion functions (should be minimal after HSV caching fix)

### Hub Profile (`hub_profile.svg`)
Interactive flamegraph showing Python function call hierarchy:

**How to read:**
- Width = time spent in function
- Height = call stack depth
- Hover to see function names and percentages

**What to look for:**
- Wide bars = bottlenecks
- Deep stacks = excessive function calls
- UI rendering taking >30% of time
- Network/serialization overhead

---

## Common Bottlenecks and Solutions

### 1. GPU HSV Conversions (Already Fixed)
**Symptom:** `matchColor()` or `cv::cvtColor()` consuming >30% CPU  
**Solution:** HSV frame caching (implemented in [`SimpleBallTracker.cpp`](engine/src/SimpleBallTracker.cpp:934-939))

### 2. Large Log Files
**Symptom:** Disk I/O wait time high, logs >100MB  
**Solution:**
```bash
# Find and remove old recording logs
find . -name "recording.log" -type f -delete

# Truncate active logs
> color_matching.log
> GPU_debug.log
> trajectory_debug.log
```

### 3. Recording Logger Active
**Symptom:** `RecordingLogger::logFrame()` in profile  
**Solution:** Recording logger should only be active during recording sessions. Check if it's being left on.

### 4. Excessive UI Rendering
**Symptom:** UI functions taking >30% in hub profile  
**Solution:** 
- Reduce frame rate for non-critical visualizations
- Use conditional rendering (only update when changed)
- Batch UI updates

### 5. Memory Leaks
**Symptom:** Memory usage growing over time  
**Solution:**
```bash
# Profile memory with valgrind
valgrind --leak-check=full --track-origins=yes ./engine/build/bin/juggle_engine
```

### 6. Thermal Throttling
**Symptom:** Performance degrades over time, high temps  
**Solution:**
- Check cooling system
- Reduce camera FPS (60→30)
- Lower GPU workload

---

## Advanced Profiling

### Interactive Engine Profile
```bash
cd profile_results_YYYYMMDD_HHMMSS/
sudo perf report -i engine_perf.data
```

**Navigation:**
- Arrow keys: Navigate
- Enter: Drill into function
- `+`: Expand call tree
- `-`: Collapse call tree
- `q`: Quit

### Generate Flamegraph
```bash
# Install flamegraph tools
git clone https://github.com/brendangregg/FlameGraph
cd FlameGraph

# Generate from perf data
sudo perf script -i ../profile_results_*/engine_perf.data | ./stackcollapse-perf.pl | ./flamegraph.pl > engine_flamegraph.svg

# Open in browser
firefox engine_flamegraph.svg
```

### Profile Specific Code Section
Add to C++ code:
```cpp
#include <chrono>

auto start = std::chrono::high_resolution_clock::now();

// Code to profile
matchColor(detection, ball);

auto end = std::chrono::high_resolution_clock::now();
auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
std::cout << "matchColor took: " << duration.count() << " µs" << std::endl;
```

### Profile Python with cProfile
```python
import cProfile
import pstats

profiler = cProfile.Profile()
profiler.enable()

# Your code here
run_hub()

profiler.disable()
stats = pstats.Stats(profiler)
stats.sort_stats('cumtime')
stats.print_stats(20)
```

---

## Interpreting FPS Issues

### Expected Performance
- **Target FPS:** 40-60 FPS
- **GPU Compute:** <20%
- **CPU Usage:** <70%
- **Frame time:** <25ms (for 40 FPS)

### Performance Regression Checklist

If FPS drops from 40 to 20:

1. **Check recent code changes**
   - Review git log for changes in last week
   - Look for new loops, conversions, or allocations

2. **Profile before/after**
   - Checkout old commit
   - Profile with `./scripts/profile_system.sh`
   - Compare results

3. **Check system resources**
   - GPU temperature and throttling
   - Background processes
   - Disk I/O (large logs)

4. **Verify fixes are applied**
   - HSV caching enabled
   - Recording logger only active during recording
   - No duplicate UI rendering

---

## Tools Reference

### perf (Linux Performance Analyzer)
```bash
# Record with call graph
sudo perf record -g -F 99 --call-graph dwarf ./program

# View report
sudo perf report

# Show top functions
sudo perf report --stdio --sort comm,dso,symbol
```

### py-spy (Python Profiler)
```bash
# Record flamegraph
py-spy record -o profile.svg -- python script.py

# Record speedscope format
py-spy record -f speedscope -o profile.json -- python script.py

# Profile running process
py-spy record -o profile.svg --pid 12345
```

### valgrind (Memory Profiler)
```bash
# Check for memory leaks
valgrind --leak-check=full ./program

# Profile cache usage
valgrind --tool=cachegrind ./program
```

---

## Troubleshooting

### "perf not found"
```bash
sudo apt-get install linux-tools-common linux-tools-generic linux-tools-$(uname -r)
```

### "py-spy not found"
```bash
pip install py-spy
```

### "Permission denied" for perf
```bash
# Temporarily allow perf for non-root
sudo sysctl -w kernel.perf_event_paranoid=-1

# Or run with sudo
sudo perf record ...
```

### Profile data is empty
- Make sure program runs long enough (30+ seconds)
- Check if program crashes during profiling
- Verify perf/py-spy is recording (check file size)

---

## Related Documentation

- [`FPS_DROP_INVESTIGATION.md`](FPS_DROP_INVESTIGATION.md) - Previous FPS issue and fix
- [`FPS_OPTIMIZATION_*.md`](.) - Historical optimization work
- [`DEBUG_LOGGING_GUIDE.md`](DEBUG_LOGGING_GUIDE.md) - Logging system documentation

---

## Next Steps After Profiling

1. **Identify the bottleneck** from profile reports
2. **Verify it's a real issue** (>20% of time, reproducible)
3. **Research solutions** (caching, batching, optimization)
4. **Implement fix** with performance measurements
5. **Document in FPS_DROP_INVESTIGATION.md**
6. **Re-profile to verify improvement**

---

*Last updated: 2025-10-11*