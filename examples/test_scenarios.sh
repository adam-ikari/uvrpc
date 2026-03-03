#!/bin/bash
# Quick test script for all scenario examples

echo "========================================="
echo "UVRPC Scenario Examples Quick Test"
echo "========================================="
echo ""

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m' # No Color

# Test function
test_scenario() {
    local scenario=$1
    local name=$2
    
    echo "Testing: $name"
    echo "Running: timeout 3 ./$scenario"
    
    timeout 3 ./$scenario > /tmp/scenario_test_$$.log 2>&1
    local exit_code=$?
    
    if [ $exit_code -eq 0 ]; then
        echo -e "${GREEN}✓ PASS${NC}: $name"
        return 0
    elif [ $exit_code -eq 124 ]; then
        echo -e "${RED}✗ TIMEOUT${NC}: $name"
        return 1
    else
        echo -e "${RED}✗ FAIL${NC}: $name (exit code: $exit_code)"
        return 1
    fi
}

# Set library path
export LD_LIBRARY_PATH=./dist/lib:$LD_LIBRARY_PATH

# Count results
passed=0
failed=0

# Test all scenarios
test_scenario "scenario_1_simple_request_response" "Scenario 1: Simple Request-Response"
if [ $? -eq 0 ]; then ((passed++)); else ((failed++)); fi

test_scenario "scenario_2_streaming_data_transfer" "Scenario 2: Streaming Data Transfer"
if [ $? -eq 0 ]; then ((passed++)); else ((failed++)); fi

test_scenario "scenario_3_oneway_logging" "Scenario 3: Oneway Logging"
if [ $? -eq 0 ]; then ((passed++)); else ((failed++)); fi

test_scenario "scenario_4_broadcast_mode" "Scenario 4: Broadcast Mode"
if [ $? -eq 0 ]; then ((passed++)); else ((failed++)); fi

test_scenario "scenario_5_mixed_mode_api" "Scenario 5: Mixed Mode API"
if [ $? -eq 0 ]; then ((passed++)); else ((failed++)); fi

echo ""
echo "========================================="
echo "Test Summary"
echo "========================================="
echo "Total: 5"
echo -e "Passed: ${GREEN}$passed${NC}"
echo -e "Failed: ${RED}$failed${NC}"
echo ""

if [ $failed -eq 0 ]; then
    echo -e "${GREEN}All tests passed!${NC}"
    exit 0
else
    echo -e "${RED}Some tests failed!${NC}"
    exit 1
fi