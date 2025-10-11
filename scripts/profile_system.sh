#!/bin/bash
# Comprehensive system profiling script
# Profiles both engine and hub, plus system metrics

set -e

echo "=========================================="
echo "JuggleHub System-Wide Profiler"
echo "=========================================="
echo ""
echo "This script will:"
echo "  1. Check system resources (CPU, GPU, Memory)"
echo "  2. Profile the C++ engine with perf"
echo "  3. Profile the Python hub with py-spy"
echo "  4. Generate comprehensive reports"
echo ""
echo "Press Enter to continue or Ctrl+C to cancel..."
read

# Create output directory
PROFILE_DIR="profile_results_$(date +%Y%m%d_%H%M%S)"
mkdir -p "$PROFILE_DIR"
cd "$PROFILE_DIR"

echo ""
echo "=========================================="
echo "Step 1: System Resource Check"
echo "=========================================="

# Check CPU info
echo "CPU Information:" > system_info.txt
lscpu | grep -E "Model name|CPU\(s\)|Thread|Core" >> system_info.txt
echo "" >> system_info.txt

# Check memory
echo "Memory Information:" >> system_info.txt
free -h >> system_info.txt
echo "" >> system_info.txt

# Check GPU
echo "GPU Information:" >> system_info.txt
if command -v nvidia-smi &> /dev/null; then
    nvidia-smi --query-gpu=name,driver_version,memory.total,memory.used,temperature.gpu,utilization.gpu --format=csv >> system_info.txt
elif command -v intel_gpu_top &> /dev/null; then
    echo "Intel GPU detected" >> system_info.txt
    intel_gpu_top -l 1 >> system_info.txt 2>&1 || echo "Could not query Intel GPU" >> system_info.txt
else
    echo "No GPU monitoring tool found" >> system_info.txt
fi
echo "" >> system_info.txt

# Check running processes
echo "Top CPU-consuming processes:" >> system_info.txt
ps aux --sort=-%cpu | head -10 >> system_info.txt
echo "" >> system_info.txt

cat system_info.txt
echo ""

echo "=========================================="
echo "Step 2: Check for Large Log Files"
echo "=========================================="

echo "Searching for log files..." > log_analysis.txt
cd ..
find . -name "*.log" -type f -exec ls -lh {} \; 2>/dev/null | sort -k5 -hr >> "$PROFILE_DIR/log_analysis.txt"
echo "" >> "$PROFILE_DIR/log_analysis.txt"

echo "Searching for recording.log files..." >> "$PROFILE_DIR/log_analysis.txt"
find . -name "recording.log" -type f -exec ls -lh {} \; 2>/dev/null >> "$PROFILE_DIR/log_analysis.txt"

echo "Log file analysis:" 
cat "$PROFILE_DIR/log_analysis.txt"
echo ""

# Calculate total log size
TOTAL_LOG_SIZE=$(find . -name "*.log" -type f -exec du -b {} \; 2>/dev/null | awk '{sum+=$1} END {print sum}')
TOTAL_LOG_SIZE_MB=$((TOTAL_LOG_SIZE / 1024 / 1024))
echo "Total log file size: ${TOTAL_LOG_SIZE_MB} MB" | tee -a "$PROFILE_DIR/log_analysis.txt"
echo ""

if [ $TOTAL_LOG_SIZE_MB -gt 100 ]; then
    echo "WARNING: Log files are using ${TOTAL_LOG_SIZE_MB} MB of disk space!"
    echo "This could impact performance. Consider cleaning up old logs."
    echo ""
fi

echo "=========================================="
echo "Step 3: Profile C++ Engine"
echo "=========================================="
echo ""
echo "This will profile the engine for 30 seconds."
echo "Make sure the engine is doing typical work (tracking balls, etc.)"
echo ""
echo "Press Enter to start engine profiling or Ctrl+C to skip..."
read

cd "$PROFILE_DIR"

# Check if engine exists
ENGINE_BIN="../engine/build/bin/juggle_engine"
if [ -f "$ENGINE_BIN" ]; then
    if command -v perf &> /dev/null; then
        echo "Starting engine with perf profiling..."
        timeout 30 sudo perf record -g -F 99 --call-graph dwarf -o engine_perf.data $ENGINE_BIN 2>&1 | tee engine_profile_log.txt || true
        
        if [ -f "engine_perf.data" ]; then
            echo "Generating engine report..."
            sudo perf report --stdio -i engine_perf.data > engine_profile.txt 2>/dev/null || true
            echo "Engine profile saved to: $PROFILE_DIR/engine_profile.txt"
        fi
    else
        echo "WARNING: perf not installed, skipping engine profiling"
        echo "Install with: sudo apt-get install linux-tools-common linux-tools-generic"
    fi
else
    echo "WARNING: Engine binary not found at $ENGINE_BIN"
    echo "Build it first with: ./scripts/build_engine.sh"
fi

cd ..
echo ""

echo "=========================================="
echo "Step 4: Profile Python Hub"
echo "=========================================="
echo ""
echo "This will profile the hub for 30 seconds."
echo ""
echo "Press Enter to start hub profiling or Ctrl+C to skip..."
read

cd "$PROFILE_DIR"

if command -v py-spy &> /dev/null; then
    echo "Starting hub with py-spy profiling..."
    timeout 30 py-spy record --rate 100 --format flamegraph --output hub_profile.svg -- python -m hub.main 2>&1 | tee hub_profile_log.txt || true
    
    if [ -f "hub_profile.svg" ]; then
        echo "Hub profile saved to: $PROFILE_DIR/hub_profile.svg"
    fi
else
    echo "WARNING: py-spy not installed, skipping hub profiling"
    echo "Install with: pip install py-spy"
fi

cd ..
echo ""

echo "=========================================="
echo "Profile Complete!"
echo "=========================================="
echo ""
echo "All results saved to: $PROFILE_DIR/"
echo ""
echo "Files created:"
ls -lh "$PROFILE_DIR/"
echo ""
echo "Next steps:"
echo "  1. Review system_info.txt for resource usage"
echo "  2. Review log_analysis.txt for large log files"
echo "  3. Review engine_profile.txt for C++ bottlenecks"
echo "  4. Open hub_profile.svg in browser for Python bottlenecks"
echo ""
echo "To view engine profile interactively:"
echo "  cd $PROFILE_DIR"
echo "  sudo perf report -i engine_perf.data"
echo ""