#!/bin/bash
# UVRPC Comprehensive Test Suite Runner
# Runs all tests: boundary values, unit tests, and e2e tests

echo "========================================="
echo "UVRPC Comprehensive Test Suite"
echo "========================================="
echo ""

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Set library path
export LD_LIBRARY_PATH=../../dist/lib:$LD_LIBRARY_PATH

# Test results
total_passed=0
total_failed=0

# Test suite 1: Boundary Value Tests
echo "========================================="
echo "Test Suite 1: Boundary Value Tests"
echo "========================================="
echo ""

if [ -f "unit/run_boundary_tests.sh" ]; then
    if unit/run_boundary_tests.sh > /tmp/boundary_tests_$$.log 2>&1; then
        echo -e "${GREEN}✓ PASS${NC}: Boundary Value Tests"
        ((total_passed++))
    else
        echo -e "${RED}✗ FAIL${NC}: Boundary Value Tests"
        ((total_failed++))
        cat /tmp/boundary_tests_$$.log
    fi
else
    echo -e "${YELLOW}⚠ SKIP${NC}: Boundary Value Tests (not found)"
fi
echo ""

# Test suite 2: Stream Response Unit Tests
echo "========================================="
echo "Test Suite 2: Stream Response Unit Tests"
echo "========================================="
echo ""

if [ -f "unit/test_stream_response" ]; then
    if unit/test_stream_response > /tmp/stream_unit_$$.log 2>&1; then
        echo -e "${GREEN}✓ PASS${NC}: Stream Response Unit Tests"
        ((total_passed++))
    else
        echo -e "${RED}✗ FAIL${NC}: Stream Response Unit Tests"
        ((total_failed++))
        cat /tmp/stream_unit_$$.log
    fi
else
    echo -e "${YELLOW}⚠ SKIP${NC}: Stream Response Unit Tests (not compiled)"
fi
echo ""

# Test suite 3: Stream E2E Tests
echo "========================================="
echo "Test Suite 3: Stream E2E Tests"
echo "========================================="
echo ""

if [ -f "run_stream_tests.sh" ]; then
    if run_stream_tests.sh > /tmp/stream_e2e_$$.log 2>&1; then
        echo -e "${GREEN}✓ PASS${NC}: Stream E2E Tests"
        ((total_passed++))
    else
        echo -e "${RED}✗ FAIL${NC}: Stream E2E Tests"
        ((total_failed++))
        cat /tmp/stream_e2e_$$.log
    fi
else
    echo -e "${YELLOW}⚠ SKIP${NC}: Stream E2E Tests (not found)"
fi
echo ""

# Test suite 4: Integration Tests
echo "========================================="
echo "Test Suite 4: Integration Tests"
echo "========================================="
echo ""

if [ -d "integration" ]; then
    # Run TCP test
    if [ -f "integration/test_tcp" ]; then
        echo "Running TCP integration test..."
        if timeout 10 integration/test_tcp > /tmp/tcp_integration_$$.log 2>&1; then
            echo -e "${GREEN}✓ PASS${NC}: TCP Integration Test"
            ((total_passed++))
        else
            echo -e "${RED}✗ FAIL${NC}: TCP Integration Test"
            ((total_failed++))
            cat /tmp/tcp_integration_$$.log
        fi
    else
        echo -e "${YELLOW}⚠ SKIP${NC}: TCP Integration Test (not compiled)"
    fi
    
    # Run IPC test
    if [ -f "integration/test_ipc" ]; then
        echo "Running IPC integration test..."
        if timeout 10 integration/test_ipc > /tmp/ipc_integration_$$.log 2>&1; then
            echo -e "${GREEN}✓ PASS${NC}: IPC Integration Test"
            ((total_passed++))
        else
            echo -e "${RED}✗ FAIL${NC}: IPC Integration Test"
            ((total_failed++))
            cat /tmp/ipc_integration_$$.log
        fi
    else
        echo -e "${YELLOW}⚠ SKIP${NC}: IPC Integration Test (not compiled)"
    fi
    
    # Run INPROC test
    if [ -f "integration/test_inproc" ]; then
        echo "Running INPROC integration test..."
        if timeout 10 integration/test_inproc > /tmp/inproc_integration_$$.log 2>&1; then
            echo -e "${GREEN}✓ PASS${NC}: INPROC Integration Test"
            ((total_passed++))
        else
            echo -e "${RED}✗ FAIL${NC}: INPROC Integration Test"
            ((total_failed++))
            cat /tmp/inproc_integration_$$.log
        fi
    else
        echo -e "${YELLOW}⚠ SKIP${NC}: INPROC Integration Test (not compiled)"
    fi
else
    echo -e "${YELLOW}⚠ SKIP${NC}: Integration Tests (directory not found)"
fi
echo ""

# Print summary
echo "========================================="
echo "Final Test Summary"
echo "========================================="
echo "Total test suites: $((total_passed + total_failed))"
echo -e "Passed: ${GREEN}$total_passed${NC}"
echo -e "Failed: ${RED}$total_failed${NC}"
echo ""

if [ $total_failed -eq 0 ]; then
    echo -e "${GREEN}=========================================${NC}"
    echo -e "${GREEN}All test suites passed!${NC}"
    echo -e "${GREEN}=========================================${NC}"
    exit 0
else
    echo -e "${RED}=========================================${NC}"
    echo -e "${RED}Some test suites failed!${NC}"
    echo -e "${RED}=========================================${NC}"
    exit 1
fi