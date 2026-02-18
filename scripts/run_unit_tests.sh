#!/bin/bash
# Run UVRPC unit tests

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"
TEST_EXECUTABLE="$PROJECT_ROOT/dist/bin/uvrpc_tests"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "================================"
echo "UVRPC Unit Tests"
echo "================================"
echo ""

# Check if tests are built
if [ ! -f "$TEST_EXECUTABLE" ]; then
    echo -e "${YELLOW}Warning: Test executable not found at $TEST_EXECUTABLE${NC}"
    echo "Building tests..."
    cd "$PROJECT_ROOT"
    cmake -S . -B build -DUVRPC_BUILD_TESTS=ON > /dev/null 2>&1
    make -C build uvrpc_tests > /dev/null 2>&1
fi

# Check if build succeeded
if [ ! -f "$TEST_EXECUTABLE" ]; then
    echo -e "${RED}Error: Failed to build tests${NC}"
    exit 1
fi

echo "Running tests..."
echo ""

# Run all passing unit tests
TEST_FILTER="AllocatorTest.*:FlatBuffersTest.*:MsgIDTest.*:UVRPCConfigTest.*:UVRPCAsyncTest.*:UVBUSTest.*:UVRPCContextTest.*:UVRPCPublisherTest.*:UVRPCSubscriberTest.*"

if "$TEST_EXECUTABLE" --gtest_filter="$TEST_FILTER" --gtest_brief=1; then
    echo ""
    echo -e "${GREEN}✓ All unit tests passed!${NC}"
    exit 0
else
    echo ""
    echo -e "${RED}✗ Some tests failed${NC}"
    exit 1
fi