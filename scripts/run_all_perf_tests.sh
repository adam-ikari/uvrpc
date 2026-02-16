#!/bin/bash
# Comprehensive performance test for all transport layers

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}UVRPC All Transport Layers Performance Test${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

# Test parameters
REQUESTS=10000

# Results file
RESULT_FILE="/tmp/uvrpc_perf_results_$(date +%Y%m%d_%H%M%S).txt"
echo "Transport,Requests,Time(s),Ops/s" > $RESULT_FILE

# Function to run test
run_test() {
    local transport=$1
    local address=$2

    echo -e "${YELLOW}Testing $transport...${NC}"

    # Kill any existing processes
    pkill -9 simple_server 2>/dev/null || true
    pkill -9 simple_client 2>/dev/null || true
    sleep 1

    # Start server
    dist/bin/simple_server "$address" > /tmp/server_${transport}.log 2>&1 &
    SERVER_PID=$!
    sleep 2

    # Check if server started
    if ! kill -0 $SERVER_PID 2>/dev/null; then
        echo -e "${RED}ERROR: Server failed to start${NC}"
        cat /tmp/server_${transport}.log
        return 1
    fi

    # Run client and measure
    START_TIME=$(date +%s.%N)
    dist/bin/simple_client "$address" > /tmp/client_${transport}.log 2>&1
    EXIT_CODE=$?
    END_TIME=$(date +%s.%N)

    # Cleanup
    kill $SERVER_PID 2>/dev/null || true
    timeout 3 bash -c "wait $SERVER_PID 2>/dev/null" || true

    if [ $EXIT_CODE -ne 0 ]; then
        echo -e "${RED}ERROR: Client failed with exit code $EXIT_CODE${NC}"
        cat /tmp/client_${transport}.log
        return 1
    fi

    # Calculate metrics
    ELAPSED=$(echo "$END_TIME - $START_TIME" | bc)
    OPS_PER_SEC=$(echo "scale=2; $REQUESTS / $ELAPSED" | bc)

    echo -e "${GREEN}SUCCESS: $transport${NC}"
    echo -e "  Time: ${ELAPSED}s"
    echo -e "  Throughput: ${OPS_PER_SEC} ops/s"
    echo ""

    # Save results
    echo "$transport,$REQUESTS,$ELAPSED,$OPS_PER_SEC" >> $RESULT_FILE
}

# Test TCP
echo -e "${BLUE}=== TCP Transport ===${NC}"
run_test "TCP" "127.0.0.1:5555"

# Test IPC
echo -e "${BLUE}=== IPC Transport ===${NC}"
run_test "IPC" "/tmp/uvrpc_test.sock"

# Test UDP
echo -e "${BLUE}=== UDP Transport ===${NC}"
run_test "UDP" "udp://127.0.0.1:8765"

# Test INPROC
echo -e "${BLUE}=== INPROC Transport ===${NC}"
run_test "INPROC" "inproc://test"

# Print summary
echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}Summary${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""
cat $RESULT_FILE | column -t -s,
echo ""
echo "Results saved to: $RESULT_FILE"
echo ""

# Cleanup
pkill -9 simple_server 2>/dev/null || true
pkill -9 simple_client 2>/dev/null || true

echo -e "${GREEN}All tests completed!${NC}"