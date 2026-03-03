#!/bin/bash
# UVRPC Stream Response Test Runner
# Runs unit tests and e2e tests for stream response functionality

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

echo "========================================"
echo "UVRPC Stream Response Test Suite"
echo "========================================"
echo ""

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m' # No Color

# Build tests
echo "[BUILD] Building tests..."
cd "$PROJECT_ROOT"
gcc -Iinclude -Itests -Ideps/libuv/include \
    -Ldist/lib -Ldeps/libuv/build \
    -pthread -o tests/unit/test_stream_response \
    tests/unit/test_stream_response.c -luvrpc -luv

gcc -Iinclude -Itests -Ideps/libuv/include \
    -Ldist/lib -Ldeps/libuv/build \
    -pthread -o tests/e2e/test_stream_e2e \
    tests/e2e/test_stream_e2e.c -luvrpc -luv

echo -e "${GREEN}✓ Build complete${NC}"
echo ""

# Run unit tests
echo "========================================"
echo "Unit Tests"
echo "========================================"
LD_LIBRARY_PATH=dist/lib:deps/libuv/build tests/unit/test_stream_response
UNIT_RESULT=$?
echo ""

# Run e2e tests
echo "========================================"
echo "End-to-End Tests"
echo "========================================"
LD_LIBRARY_PATH=dist/lib:deps/libuv/build timeout 15 tests/e2e/test_stream_e2e
E2E_RESULT=$?
echo ""

# Summary
echo "========================================"
echo "Test Summary"
echo "========================================"
if [ $UNIT_RESULT -eq 0 ]; then
    echo -e "${GREEN}✓ Unit tests: PASSED${NC}"
else
    echo -e "${RED}✗ Unit tests: FAILED${NC}"
fi

if [ $E2E_RESULT -eq 0 ]; then
    echo -e "${GREEN}✓ E2E tests: PASSED${NC}"
else
    echo -e "${RED}✗ E2E tests: FAILED${NC}"
fi
echo "========================================"

# Exit with error if any test failed
if [ $UNIT_RESULT -ne 0 ] || [ $E2E_RESULT -ne 0 ]; then
    exit 1
fi

echo -e "${GREEN}All tests passed!${NC}"
exit 0