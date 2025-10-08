#!/bin/bash

# Performance analysis script for JuggleHub Engine
# This script provides various ways to analyze perf.data files

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}📊 JuggleHub Engine Performance Analyzer${NC}"

# Default values
PERF_DATA="perf.data"
ANALYSIS_TYPE="report"

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --input)
            PERF_DATA="$2"
            shift 2
            ;;
        --type)
            ANALYSIS_TYPE="$2"
            shift 2
            ;;
        -h|--help)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --input FILE            Input perf data file (default: perf.data)"
            echo "  --type TYPE             Analysis type (default: report)"
            echo "  -h, --help              Show this help message"
            echo ""
            echo "Analysis Types:"
            echo "  report                  Interactive performance report (default)"
            echo "  top                     Show top functions by overhead"
            echo "  annotate                Line-by-line source code analysis"
            echo "  stat                    Show performance statistics"
            echo "  script                  Dump raw trace data"
            echo "  flamegraph              Generate flame graph (requires flamegraph tools)"
            echo "  summary                 Quick text summary of hotspots"
            echo ""
            echo "Examples:"
            echo "  $0                                    # Interactive report"
            echo "  $0 --type top                         # Show top functions"
            echo "  $0 --type annotate                    # Annotate source code"
            echo "  $0 --input profile_gpu.data --type summary"
            exit 0
            ;;
        *)
            echo -e "${RED}❌ Unknown option: $1${NC}"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

# Check if perf is installed
if ! command -v perf &> /dev/null; then
    echo -e "${RED}❌ Error: perf not found${NC}"
    echo "Please install perf tools:"
    echo "  sudo apt update"
    echo "  sudo apt install linux-tools-common linux-tools-\$(uname -r)"
    exit 1
fi

# Check if perf data file exists
if [ ! -f "$PERF_DATA" ]; then
    echo -e "${RED}❌ Error: Perf data file not found: $PERF_DATA${NC}"
    echo "Please run the profiler first:"
    echo "  ./scripts/profile_engine.sh"
    exit 1
fi

echo -e "${BLUE}📁 Analyzing: $PERF_DATA${NC}"
echo ""

# Perform analysis based on type
case $ANALYSIS_TYPE in
    report)
        echo -e "${GREEN}📈 Opening interactive performance report...${NC}"
        echo -e "${YELLOW}Navigation:${NC}"
        echo -e "  - Use arrow keys to navigate"
        echo -e "  - Press Enter to drill down into a function"
        echo -e "  - Press 'a' to annotate a function"
        echo -e "  - Press 'q' to quit"
        echo ""
        sleep 2
        sudo perf report -i "$PERF_DATA"
        ;;
    
    top)
        echo -e "${GREEN}🔝 Top 20 functions by CPU overhead:${NC}"
        echo ""
        sudo perf report -i "$PERF_DATA" --stdio --sort overhead,symbol | head -n 40
        ;;
    
    annotate)
        echo -e "${GREEN}📝 Source code annotation${NC}"
        echo -e "${YELLOW}This will show line-by-line performance data${NC}"
        echo ""
        sleep 2
        sudo perf annotate -i "$PERF_DATA"
        ;;
    
    stat)
        echo -e "${GREEN}📊 Performance statistics:${NC}"
        echo ""
        sudo perf report -i "$PERF_DATA" --stdio --header
        ;;
    
    script)
        echo -e "${GREEN}📜 Raw trace data:${NC}"
        echo -e "${YELLOW}This shows the raw sampling data${NC}"
        echo ""
        sudo perf script -i "$PERF_DATA" | head -n 100
        echo ""
        echo -e "${BLUE}💡 Showing first 100 lines. For full output, run:${NC}"
        echo -e "  sudo perf script -i $PERF_DATA > trace.txt"
        ;;
    
    flamegraph)
        echo -e "${GREEN}🔥 Generating flame graph...${NC}"
        
        # Check if flamegraph tools are available
        if ! command -v stackcollapse-perf.pl &> /dev/null || ! command -v flamegraph.pl &> /dev/null; then
            echo -e "${YELLOW}⚠️ Flame graph tools not found${NC}"
            echo "To install flame graph tools:"
            echo "  git clone https://github.com/brendangregg/FlameGraph"
            echo "  sudo cp FlameGraph/*.pl /usr/local/bin/"
            echo ""
            echo "Alternatively, generating flame graph data for manual processing..."
            OUTPUT_FILE="flamegraph_data.txt"
            sudo perf script -i "$PERF_DATA" > "$OUTPUT_FILE"
            echo -e "${GREEN}✅ Flame graph data saved to: $OUTPUT_FILE${NC}"
            echo "Process it with: stackcollapse-perf.pl $OUTPUT_FILE | flamegraph.pl > flamegraph.svg"
        else
            OUTPUT_FILE="flamegraph.svg"
            sudo perf script -i "$PERF_DATA" | stackcollapse-perf.pl | flamegraph.pl > "$OUTPUT_FILE"
            echo -e "${GREEN}✅ Flame graph generated: $OUTPUT_FILE${NC}"
            echo "Open it in a web browser to visualize the call stacks"
        fi
        ;;
    
    summary)
        echo -e "${GREEN}📋 Performance Summary${NC}"
        echo ""
        
        # Get total samples
        TOTAL_SAMPLES=$(sudo perf report -i "$PERF_DATA" --stdio | grep "# Total Lost Samples" -A 1 | tail -n 1 | awk '{print $NF}' || echo "N/A")
        
        echo -e "${BLUE}=== Top 10 Hotspots ===${NC}"
        echo ""
        sudo perf report -i "$PERF_DATA" --stdio --sort overhead,symbol --percent-limit 1 | grep -A 15 "# Overhead" | tail -n 15
        
        echo ""
        echo -e "${BLUE}=== Performance Insights ===${NC}"
        echo ""
        
        # Extract top function
        TOP_FUNC=$(sudo perf report -i "$PERF_DATA" --stdio --sort overhead,symbol | grep -v "^#" | grep -v "^$" | head -n 1)
        if [ -n "$TOP_FUNC" ]; then
            OVERHEAD=$(echo "$TOP_FUNC" | awk '{print $1}')
            FUNC_NAME=$(echo "$TOP_FUNC" | awk '{print $NF}')
            echo -e "🔥 Biggest hotspot: ${RED}$FUNC_NAME${NC} (${OVERHEAD} of CPU time)"
        fi
        
        # Count unique functions
        FUNC_COUNT=$(sudo perf report -i "$PERF_DATA" --stdio --sort symbol | grep -v "^#" | grep -v "^$" | wc -l)
        echo -e "📊 Total unique functions sampled: $FUNC_COUNT"
        
        echo ""
        echo -e "${BLUE}=== Recommendations ===${NC}"
        echo ""
        echo "1. Focus optimization on the top 3-5 functions"
        echo "2. Use 'perf annotate' to see line-by-line hotspots"
        echo "3. Consider algorithmic improvements for high-overhead functions"
        echo "4. Profile again after optimizations to measure impact"
        
        echo ""
        echo -e "${BLUE}💡 For detailed analysis, run:${NC}"
        echo -e "  $0 --input $PERF_DATA --type report"
        ;;
    
    *)
        echo -e "${RED}❌ Unknown analysis type: $ANALYSIS_TYPE${NC}"
        echo "Use --help to see available types"
        exit 1
        ;;
esac

echo ""
echo -e "${GREEN}✅ Analysis complete${NC}"