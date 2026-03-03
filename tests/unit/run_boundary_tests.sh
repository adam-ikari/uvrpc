#!/bin/bash
# Run all UVRPC boundary value tests

echo "========================================="
echo "UVRPC Boundary Value Test Suite"
echo "========================================="
echo ""

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m' # No Color

# Set library path
export LD_LIBRARY_PATH=../../dist/lib:$LD_LIBRARY_PATH

# Test results
passed=0
failed=0

# Test 1: Simplified boundary tests
echo "Test 1: Simplified Boundary Value Tests"
echo "----------------------------------------"
if ./test_boundary_simple > /tmp/boundary_simple_$$.log 2>&1; then
    echo -e "${GREEN}✓ PASS${NC}: Simplified Boundary Value Tests"
    ((passed++))
else
    echo -e "${RED}✗ FAIL${NC}: Simplified Boundary Value Tests"
    ((failed++))
    cat /tmp/boundary_simple_$$.log
fi
echo ""

# Test 2: Stream response unit tests
echo "Test 2: Stream Response Unit Tests"
echo "----------------------------------------"
if ./test_stream_response > /tmp/stream_response_$$.log 2>&1; then
    echo -e "${GREEN}✓ PASS${NC}: Stream Response Unit Tests"
    ((passed++))
else
    echo -e "${RED}✗ FAIL${NC}: Stream Response Unit Tests"
    ((failed++))
    cat /tmp/stream_response_$$.log
fi
echo ""

# Print summary
echo "========================================="
echo "Test Summary"
echo "========================================="
echo "Total: 2"
echo -e "Passed: ${GREEN}$passed${NC}"
echo -e "Failed: ${RED}$failed${NC}"
echo ""

if [ $failed -eq 0 ]; then
    echo -e "${GREEN}All boundary value tests passed!${NC}"
    exit 0
else
    echo -e "${RED}Some tests failed!${NC}"
    exit 1
fi