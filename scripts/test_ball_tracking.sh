#!/bin/bash

# Ball Tracking System Test Script
# This script builds the engine, starts the Hub with API, and runs comprehensive tests
# Last Updated: 2025-10-03

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ENGINE_DIR="$PROJECT_ROOT/engine"
HUB_DIR="$PROJECT_ROOT/hub"
BUILD_DIR="$ENGINE_DIR/build"
VENV_DIR="$HUB_DIR/.venv"
API_PORT=5000
API_BASE_URL="http://localhost:$API_PORT/api"

# Test results
TESTS_PASSED=0
TESTS_FAILED=0
TESTS_TOTAL=0

# Function to print colored output
print_status() {
    local color=$1
    local message=$2
    echo -e "${color}${message}${NC}"
}

print_header() {
    echo ""
    echo "========================================"
    echo "$1"
    echo "========================================"
    echo ""
}

# Function to run a test
run_test() {
    local test_name=$1
    local test_command=$2
    
    TESTS_TOTAL=$((TESTS_TOTAL + 1))
    echo -n "Testing: $test_name... "
    
    if eval "$test_command" > /dev/null 2>&1; then
        print_status "$GREEN" "✓ PASSED"
        TESTS_PASSED=$((TESTS_PASSED + 1))
        return 0
    else
        print_status "$RED" "✗ FAILED"
        TESTS_FAILED=$((TESTS_FAILED + 1))
        return 1
    fi
}

# Function to wait for API to be ready
wait_for_api() {
    local max_attempts=30
    local attempt=0
    
    echo -n "Waiting for API to be ready"
    while [ $attempt -lt $max_attempts ]; do
        if curl -s "$API_BASE_URL/balls" > /dev/null 2>&1; then
            echo ""
            print_status "$GREEN" "API is ready!"
            return 0
        fi
        echo -n "."
        sleep 1
        attempt=$((attempt + 1))
    done
    
    echo ""
    print_status "$RED" "API failed to start within ${max_attempts} seconds"
    return 1
}

# Function to cleanup processes
cleanup() {
    print_header "Cleaning Up"
    
    # Kill Hub process
    if [ ! -z "$HUB_PID" ]; then
        echo "Stopping Hub (PID: $HUB_PID)..."
        kill $HUB_PID 2>/dev/null || true
        wait $HUB_PID 2>/dev/null || true
    fi
    
    # Kill any remaining Hub processes
    pkill -f "python.*hub/main.py" 2>/dev/null || true
    
    print_status "$BLUE" "Cleanup complete"
}

# Set up trap to cleanup on exit
trap cleanup EXIT INT TERM

# Main script
print_header "Ball Tracking System Test Suite"
print_status "$BLUE" "Project Root: $PROJECT_ROOT"

# Step 1: Build the Engine
print_header "Step 1: Building Engine"
cd "$ENGINE_DIR"

if [ -d "$BUILD_DIR" ]; then
    print_status "$YELLOW" "Removing existing build directory..."
    rm -rf "$BUILD_DIR"
fi

print_status "$BLUE" "Running CMake configuration..."
if cmake -B build -S . > /dev/null 2>&1; then
    print_status "$GREEN" "✓ CMake configuration successful"
else
    print_status "$RED" "✗ CMake configuration failed"
    exit 1
fi

print_status "$BLUE" "Building juggle_engine target..."
if cmake --build build --target juggle_engine -j$(nproc) > /dev/null 2>&1; then
    print_status "$GREEN" "✓ Engine build successful"
else
    print_status "$RED" "✗ Engine build failed"
    exit 1
fi

# Verify engine binary exists
if [ -f "$BUILD_DIR/juggle_engine" ]; then
    print_status "$GREEN" "✓ Engine binary created: $BUILD_DIR/juggle_engine"
else
    print_status "$RED" "✗ Engine binary not found"
    exit 1
fi

# Step 2: Start Hub with API
print_header "Step 2: Starting Hub with API"
cd "$PROJECT_ROOT"

# Check if virtual environment exists
if [ ! -d "$VENV_DIR" ]; then
    print_status "$YELLOW" "Virtual environment not found, creating..."
    ./scripts/run_hub.sh --create-venv --install-deps
fi

# Start Hub in background (API is enabled by default)
print_status "$BLUE" "Starting Hub with API enabled..."
source "$VENV_DIR/bin/activate"
cd "$HUB_DIR"
python main.py --no-ui > /tmp/hub_test.log 2>&1 &
HUB_PID=$!

print_status "$GREEN" "Hub started (PID: $HUB_PID)"

# Wait for API to be ready
if ! wait_for_api; then
    print_status "$RED" "Failed to start API, check logs at /tmp/hub_test.log"
    exit 1
fi

# Step 3: Run API Tests
print_header "Step 3: Running API Tests"

# Test 1: List balls endpoint
run_test "GET /api/balls" \
    "curl -s -f '$API_BASE_URL/balls'"

# Test 2: Get system status
run_test "GET /api/status" \
    "curl -s -f '$API_BASE_URL/status'"

# Test 3: Create a test ball
print_status "$BLUE" "Creating test ball..."
BALL_RESPONSE=$(curl -s -X POST "$API_BASE_URL/balls" \
    -H "Content-Type: application/json" \
    -d '{
        "name": "Test Ball",
        "mode": "new",
        "samples": [
            {"h": 170, "s": 200, "v": 180},
            {"h": 175, "s": 210, "v": 175},
            {"h": 168, "s": 195, "v": 185}
        ]
    }' 2>/dev/null)

if echo "$BALL_RESPONSE" | grep -q "id"; then
    BALL_ID=$(echo "$BALL_RESPONSE" | grep -o '"id":"[^"]*"' | cut -d'"' -f4)
    print_status "$GREEN" "✓ Test ball created with ID: $BALL_ID"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    print_status "$RED" "✗ Failed to create test ball"
    TESTS_FAILED=$((TESTS_FAILED + 1))
    BALL_ID=""
fi
TESTS_TOTAL=$((TESTS_TOTAL + 1))

# Test 4: Get ball details (if ball was created)
if [ ! -z "$BALL_ID" ]; then
    run_test "GET /api/balls/$BALL_ID" \
        "curl -s -f '$API_BASE_URL/balls/$BALL_ID'"
    
    # Test 5: Activate ball
    run_test "POST /api/balls/$BALL_ID/activate" \
        "curl -s -f -X POST '$API_BASE_URL/balls/$BALL_ID/activate'"
    
    # Test 6: Check ball is active
    print_status "$BLUE" "Verifying ball activation..."
    BALL_STATUS=$(curl -s "$API_BASE_URL/balls/$BALL_ID" 2>/dev/null)
    if echo "$BALL_STATUS" | grep -q '"active":true'; then
        print_status "$GREEN" "✓ Ball is active"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        print_status "$RED" "✗ Ball activation verification failed"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
    TESTS_TOTAL=$((TESTS_TOTAL + 1))
    
    # Test 7: Deactivate ball
    run_test "POST /api/balls/$BALL_ID/deactivate" \
        "curl -s -f -X POST '$API_BASE_URL/balls/$BALL_ID/deactivate'"
    
    # Test 8: Delete ball
    run_test "DELETE /api/balls/$BALL_ID" \
        "curl -s -f -X DELETE '$API_BASE_URL/balls/$BALL_ID'"
fi

# Test 9: Switch to legacy mode
run_test "POST /api/balls/mode (legacy)" \
    "curl -s -f -X POST '$API_BASE_URL/balls/mode' -H 'Content-Type: application/json' -d '{\"mode\":\"legacy\"}'"

# Test 10: Switch back to new mode
run_test "POST /api/balls/mode (new)" \
    "curl -s -f -X POST '$API_BASE_URL/balls/mode' -H 'Content-Type: application/json' -d '{\"mode\":\"new\"}'"

# Step 4: Test Documentation
print_header "Step 4: Verifying Documentation"

run_test "BALL_TRACKING_USER_GUIDE.md exists" \
    "[ -f '$PROJECT_ROOT/BALL_TRACKING_USER_GUIDE.md' ]"

run_test "MIGRATION_TO_NEW_TRACKING.md exists" \
    "[ -f '$PROJECT_ROOT/MIGRATION_TO_NEW_TRACKING.md' ]"

run_test "README.md contains ball tracking section" \
    "grep -q 'Advanced Ball Tracking System' '$PROJECT_ROOT/README.md'"

# Step 5: Print Summary
print_header "Test Summary"

echo "Total Tests: $TESTS_TOTAL"
print_status "$GREEN" "Passed: $TESTS_PASSED"
print_status "$RED" "Failed: $TESTS_FAILED"
echo ""

if [ $TESTS_FAILED -eq 0 ]; then
    print_status "$GREEN" "✓ ALL TESTS PASSED!"
    echo ""
    print_status "$BLUE" "Ball tracking system is ready to use!"
    echo ""
    echo "Documentation:"
    echo "  - User Guide: BALL_TRACKING_USER_GUIDE.md"
    echo "  - Migration Guide: MIGRATION_TO_NEW_TRACKING.md"
    echo "  - README: See 'Advanced Ball Tracking System' section"
    echo ""
    exit 0
else
    print_status "$RED" "✗ SOME TESTS FAILED"
    echo ""
    print_status "$YELLOW" "Check the following:"
    echo "  - Hub logs: /tmp/hub_test.log"
    echo "  - API endpoint: $API_BASE_URL"
    echo "  - Engine binary: $BUILD_DIR/juggle_engine"
    echo ""
    exit 1
fi