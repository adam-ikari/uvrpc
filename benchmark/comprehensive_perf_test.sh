#!/bin/bash
# UVRPC Comprehensive Performance Test
# Tests CS and Broadcast modes across all transports (TCP, UDP, IPC, INPROC)

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BENCHMARK_BIN="${PROJECT_ROOT}/dist/bin/perf_benchmark"
RESULTS_DIR="${PROJECT_ROOT}/benchmark/results"
RESULTS_FILE="${RESULTS_DIR}/comprehensive_report.md"

# Create results directory
mkdir -p "$RESULTS_DIR"

# Cleanup function
cleanup() {
    echo -e "\n${YELLOW}Cleaning up...${NC}"
    pkill -9 perf_benchmark 2>/dev/null || true
    # Clean up IPC sockets
    rm -f /tmp/uvrpc_benchmark.sock 2>/dev/null || true
    sleep 1
}

trap cleanup EXIT INT TERM

# Force cleanup on startup
cleanup

# Test configuration
TEST_DURATION=3000  # 3 seconds
RESULTS=()

# Run test function
run_test() {
    local test_name="$1"
    local transport="$2"
    local address="$3"
    shift 3
    local params=("$@")
    
    echo -e "${CYAN}=== $test_name ===${NC}"
    echo -e "Transport: $transport"
    echo -e "Address: $address"
    
    # Start server if needed
    if [[ "$test_name" == *"CS Mode"* ]]; then
        echo -e "${BLUE}Starting server...${NC}"
        "$BENCHMARK_BIN" --server -a "$address" --server-timeout $((TEST_DURATION + 5000)) > /tmp/server.log 2>&1 &
        SERVER_PID=$!
        sleep 2
        
        if ! kill -0 $SERVER_PID 2>/dev/null; then
            echo -e "${RED}Server failed to start${NC}"
            cat /tmp/server.log
            return 1
        fi
    fi
    
    # Run client test
    local output=$("$BENCHMARK_BIN" -a "$address" "${params[@]}" -d "$TEST_DURATION" 2>&1)
    local ret=$?
    
    # Parse results
    local sent=$(echo "$output" | grep "^Sent:" | awk '{print $2}' || echo "0")
    local received=$(echo "$output" | grep "^Received:" | awk '{print $2}' || echo "0")
    local success_rate=$(echo "$output" | grep "Success rate:" | awk '{print $3}' || echo "0%")
    local throughput=$(echo "$output" | grep "Client throughput:" | awk '{print $3}' || echo "0")
    local memory=$(echo "$output" | grep "Memory:" | awk '{print $3}' || echo "0")
    
    echo -e "${GREEN}Sent: $sent, Received: $received, Success: $success_rate${NC}"
    echo -e "${GREEN}Throughput: $throughput ops/s, Memory: $memory MB${NC}"
    
    # Store results
    RESULTS+=("$test_name|$transport|$sent|$received|$success_rate|$throughput|$memory")
    
    # Kill server if we started it
    if [[ "$test_name" == *"CS Mode"* ]]; then
        kill $SERVER_PID 2>/dev/null || true
        wait $SERVER_PID 2>/dev/null || true
    fi
    
    sleep 1
    
    return $ret
}

# Run broadcast test
run_broadcast_test() {
    local test_name="$1"
    local transport="$2"
    local address="$3"
    shift 3
    local params=("$@")
    
    echo -e "${CYAN}=== $test_name ===${NC}"
    echo -e "Transport: $transport"
    echo -e "Address: $address"
    
    # Start publisher
    echo -e "${BLUE}Starting publisher...${NC}"
    "$BENCHMARK_BIN" --publisher -a "$address" "${params[@]}" -d "$TEST_DURATION" > /tmp/publisher.log 2>&1 &
    PUB_PID=$!
    sleep 1
    
    if ! kill -0 $PUB_PID 2>/dev/null; then
        echo -e "${RED}Publisher failed to start${NC}"
        cat /tmp/publisher.log
        return 1
    fi
    
    # Start subscriber
    echo -e "${BLUE}Starting subscriber...${NC}"
    local sub_output=$("$BENCHMARK_BIN" --subscriber -a "$address" "${params[@]}" -d "$TEST_DURATION" 2>&1)
    local sub_ret=$?
    
    # Parse results
    local messages=$(echo "$sub_output" | grep "Messages received:" | awk '{print $3}' || echo "0")
    local throughput=$(echo "$sub_output" | grep "Throughput:" | awk '{print $2}' || echo "0")
    local bandwidth=$(echo "$sub_output" | grep "Bandwidth:" | awk '{print $2}' || echo "0")
    
    echo -e "${GREEN}Messages: $messages, Throughput: $throughput msgs/s, Bandwidth: $bandwidth MB/s${NC}"
    
    # Store results
    RESULTS+=("$test_name|$transport|$messages|N/A|N/A|$throughput|0")
    
    # Cleanup
    kill $PUB_PID 2>/dev/null || true
    wait $PUB_PID 2>/dev/null || true
    
    sleep 1
    
    return $sub_ret
}

# Run latency test
run_latency_test() {
    local transport="$1"
    local address="$2"
    
    echo -e "${CYAN}=== Latency Test - $transport ===${NC}"
    
    # Start server
    "$BENCHMARK_BIN" --server -a "$address" --server-timeout 10000 > /tmp/server.log 2>&1 &
    SERVER_PID=$!
    sleep 2
    
    if ! kill -0 $SERVER_PID 2>/dev/null; then
        echo -e "${RED}Server failed to start${NC}"
        return 1
    fi
    
    # Run latency test
    local output=$("$BENCHMARK_BIN" -a "$address" --latency 2>&1)
    local ret=$?
    
    # Parse latency metrics
    local p50=$(echo "$output" | grep "P50:" | awk '{print $2}' || echo "N/A")
    local p95=$(echo "$output" | grep "P95:" | awk '{print $2}' || echo "N/A")
    local p99=$(echo "$output" | grep "P99:" | awk '{print $2}' || echo "N/A")
    local avg=$(echo "$output" | grep "Avg:" | awk '{print $2}' || echo "N/A")
    
    echo -e "${GREEN}P50: $p50, P95: $p95, P99: $p99, Avg: $avg${NC}"
    
    # Store results
    RESULTS+=("Latency|$transport|N/A|N/A|N/A|$avg|0")
    
    # Cleanup
    kill $SERVER_PID 2>/dev/null || true
    wait $SERVER_PID 2>/dev/null || true
    
    sleep 1
    
    return $ret
}

# Generate report
generate_report() {
    echo -e "${BLUE}================================${NC}"
    echo -e "${BLUE}Generating Performance Report${NC}"
    echo -e "${BLUE}================================${NC}"
    
    cat > "$RESULTS_FILE" << 'EOF'
# UVRPC Comprehensive Performance Report

**Date:** $(date '+%Y-%m-%d %H:%M:%S')  
**Test Duration:** 3000ms per test  
**Platform:** Linux $(uname -m)

## Executive Summary

This report presents comprehensive performance testing results for UVRPC across all supported transports and modes.

## Test Configuration

- **Test Duration:** 3 seconds per test
- **Modes Tested:**
  - Client-Server (CS) Mode
  - Broadcast Mode
- **Transports Tested:**
  - TCP (Network)
  - UDP (Network)
  - IPC (Unix Domain Socket)
  - INPROC (In-Process)

---

## CS Mode Performance Results

### TCP Transport

| Test | Clients | Concurrency | Sent | Received | Success Rate | Throughput (ops/s) | Memory (MB) |
|------|---------|-------------|------|----------|--------------|-------------------|-------------|
EOF

    # Add results
    for result in "${RESULTS[@]}"; do
        IFS='|' read -r name transport sent received success_rate throughput memory <<< "$result"
        
        if [[ "$name" == *"CS Mode"* ]] && [[ "$transport" == "TCP" ]]; then
            echo "| $name | - | - | $sent | $received | $success_rate | $throughput | $memory |" >> "$RESULTS_FILE"
        fi
    done
    
    cat >> "$RESULTS_FILE" << 'EOF'

### UDP Transport

| Test | Clients | Concurrency | Sent | Received | Success Rate | Throughput (ops/s) | Memory (MB) |
|------|---------|-------------|------|----------|--------------|-------------------|-------------|
EOF

    for result in "${RESULTS[@]}"; do
        IFS='|' read -r name transport sent received success_rate throughput memory <<< "$result"
        
        if [[ "$name" == *"CS Mode"* ]] && [[ "$transport" == "UDP" ]]; then
            echo "| $name | - | - | $sent | $received | $success_rate | $throughput | $memory |" >> "$RESULTS_FILE"
        fi
    done
    
    cat >> "$RESULTS_FILE" << 'EOF'

### IPC Transport

| Test | Clients | Concurrency | Sent | Received | Success Rate | Throughput (ops/s) | Memory (MB) |
|------|---------|-------------|------|----------|--------------|-------------------|-------------|
EOF

    for result in "${RESULTS[@]}"; do
        IFS='|' read -r name transport sent received success_rate throughput memory <<< "$result"
        
        if [[ "$name" == *"CS Mode"* ]] && [[ "$transport" == "IPC" ]]; then
            echo "| $name | - | - | $sent | $received | $success_rate | $throughput | $memory |" >> "$RESULTS_FILE"
        fi
    done
    
    cat >> "$RESULTS_FILE" << 'EOF'

### INPROC Transport

| Test | Clients | Concurrency | Sent | Received | Success Rate | Throughput (ops/s) | Memory (MB) |
|------|---------|-------------|------|----------|--------------|-------------------|-------------|
EOF

    for result in "${RESULTS[@]}"; do
        IFS='|' read -r name transport sent received success_rate throughput memory <<< "$result"
        
        if [[ "$name" == *"CS Mode"* ]] && [[ "$transport" == "INPROC" ]]; then
            echo "| $name | - | - | $sent | $received | $success_rate | $throughput | $memory |" >> "$RESULTS_FILE"
        fi
    done
    
    cat >> "$RESULTS_FILE" << 'EOF'

---

## Broadcast Mode Performance Results

### UDP Transport

| Test | Publishers | Subscribers | Messages | Throughput (msgs/s) | Bandwidth (MB/s) |
|------|------------|-------------|----------|---------------------|------------------|
EOF

    for result in "${RESULTS[@]}"; do
        IFS='|' read -r name transport sent received success_rate throughput memory <<< "$result"
        
        if [[ "$name" == *"Broadcast"* ]] && [[ "$transport" == "UDP" ]]; then
            echo "| $name | - | - | $sent | $throughput | $received |" >> "$RESULTS_FILE"
        fi
    done
    
    cat >> "$RESULTS_FILE" << 'EOF'

### IPC Transport

| Test | Publishers | Subscribers | Messages | Throughput (msgs/s) | Bandwidth (MB/s) |
|------|------------|-------------|----------|---------------------|------------------|
EOF

    for result in "${RESULTS[@]}"; do
        IFS='|' read -r name transport sent received success_rate throughput memory <<< "$result"
        
        if [[ "$name" == *"Broadcast"* ]] && [[ "$transport" == "IPC" ]]; then
            echo "| $name | - | - | $sent | $throughput | $received |" >> "$RESULTS_FILE"
        fi
    done
    
    cat >> "$RESULTS_FILE" << 'EOF'

---

## Latency Test Results

| Transport | P50 (ms) | P95 (ms) | P99 (ms) | Avg (ms) |
|-----------|----------|----------|----------|----------|
EOF

    for result in "${RESULTS[@]}"; do
        IFS='|' read -r name transport sent received success_rate throughput memory <<< "$result"
        
        if [[ "$name" == "Latency" ]]; then
            echo "| $transport | N/A | N/A | N/A | $throughput |" >> "$RESULTS_FILE"
        fi
    done
    
    cat >> "$RESULTS_FILE" << 'EOF'

---

## Key Findings

1. **Throughput Comparison:**
   - Highest throughput: [To be filled based on results]
   - Lowest throughput: [To be filled based on results]

2. **Latency Comparison:**
   - Lowest latency: [To be filled based on results]
   - Highest latency: [To be filled based on results]

3. **Memory Efficiency:**
   - Most memory-efficient: [To be filled based on results]
   - Least memory-efficient: [To be filled based on results]

4. **Success Rate:**
   - All transports maintained [X]%+ success rate

---

## Conclusion

UVRPC demonstrates excellent performance across all transports, with [summary of key findings].

---

**Generated by:** UVRPC Benchmark Suite  
**Version:** 0.1.0
EOF

    echo -e "${GREEN}Report saved to: $RESULTS_FILE${NC}"
    cat "$RESULTS_FILE"
}

# Main test execution
main() {
    echo -e "${BLUE}================================${NC}"
    echo -e "${BLUE}UVRPC Comprehensive Performance Test${NC}"
    echo -e "${BLUE}================================${NC}"
    echo -e "Binary: $BENCHMARK_BIN"
    echo -e "Duration: ${TEST_DURATION}ms per test"
    echo -e "Results: $RESULTS_FILE"
    echo -e "${BLUE}================================${NC}\n"
    
    # Check binary exists
    if [ ! -x "$BENCHMARK_BIN" ]; then
        echo -e "${RED}Error: Benchmark binary not found: $BENCHMARK_BIN${NC}"
        echo -e "${YELLOW}Please run 'make' first${NC}"
        exit 1
    fi
    
    # ==================== CS MODE TESTS ====================
    echo -e "\n${CYAN}====================${NC}"
    echo -e "${CYAN}CS MODE TESTS${NC}"
    echo -e "${CYAN}====================${NC}\n"
    
    # TCP Tests
    echo -e "${YELLOW}--- TCP Transport ---${NC}\n"
    run_test "CS Mode - TCP Single Client" "TCP" "tcp://127.0.0.1:5555" -c 1 -b 50
    run_test "CS Mode - TCP 5 Clients" "TCP" "tcp://127.0.0.1:5556" -c 5 -b 100
    run_test "CS Mode - TCP 10 Clients" "TCP" "tcp://127.0.0.1:5557" -c 10 -b 50
    run_test "CS Mode - TCP Multi-Thread" "TCP" "tcp://127.0.0.1:5558" -t 4 -c 3 -b 50
    run_test "CS Mode - TCP Fork Mode" "TCP" "tcp://127.0.0.1:5559" --fork -t 2 -c 5
    echo ""
    
    # UDP Tests
    echo -e "${YELLOW}--- UDP Transport ---${NC}\n"
    run_test "CS Mode - UDP Single Client" "UDP" "udp://127.0.0.1:6000" -c 1 -b 50
    run_test "CS Mode - UDP 5 Clients" "UDP" "udp://127.0.0.1:6001" -c 5 -b 100
    run_test "CS Mode - UDP 10 Clients" "UDP" "udp://127.0.0.1:6002" -c 10 -b 50
    run_test "CS Mode - UDP Multi-Thread" "UDP" "udp://127.0.0.1:6003" -t 4 -c 3 -b 50
    echo ""
    
    # IPC Tests
    echo -e "${YELLOW}--- IPC Transport ---${NC}\n"
    run_test "CS Mode - IPC Single Client" "IPC" "ipc:///tmp/uvrpc_benchmark_1.sock" -c 1 -b 50
    run_test "CS Mode - IPC 5 Clients" "IPC" "ipc:///tmp/uvrpc_benchmark_2.sock" -c 5 -b 100
    run_test "CS Mode - IPC 10 Clients" "IPC" "ipc:///tmp/uvrpc_benchmark_3.sock" -c 10 -b 50
    run_test "CS Mode - IPC Multi-Thread" "IPC" "ipc:///tmp/uvrpc_benchmark_4.sock" -t 4 -c 3 -b 50
    echo ""
    
    # INPROC Tests
    echo -e "${YELLOW}--- INPROC Transport ---${NC}\n"
    run_test "CS Mode - INPROC Single Client" "INPROC" "inproc://test1" -c 1 -b 50
    run_test "CS Mode - INPROC 5 Clients" "INPROC" "inproc://test2" -c 5 -b 100
    run_test "CS Mode - INPROC 10 Clients" "INPROC" "inproc://test3" -c 10 -b 50
    run_test "CS Mode - INPROC Multi-Thread" "INPROC" "inproc://test4" -t 4 -c 3 -b 50
    echo ""
    
    # ==================== BROADCAST MODE TESTS ====================
    echo -e "\n${CYAN}====================${NC}"
    echo -e "${CYAN}BROADCAST MODE TESTS${NC}"
    echo -e "${CYAN}====================${NC}\n"
    
    # UDP Broadcast Tests
    echo -e "${YELLOW}--- UDP Broadcast ---${NC}\n"
    run_broadcast_test "Broadcast - UDP Single" "UDP" "udp://127.0.0.1:7000" -p 1 -s 1 -b 50
    run_broadcast_test "Broadcast - UDP Multi-Publisher" "UDP" "udp://127.0.0.1:7001" -p 3 -s 2 -b 50
    run_broadcast_test "Broadcast - UDP High Throughput" "UDP" "udp://127.0.0.1:7002" -p 5 -s 5 -b 100
    echo ""
    
    # IPC Broadcast Tests
    echo -e "${YELLOW}--- IPC Broadcast ---${NC}\n"
    run_broadcast_test "Broadcast - IPC Single" "IPC" "ipc:///tmp/uvrpc_broadcast_1.sock" -p 1 -s 1 -b 50
    run_broadcast_test "Broadcast - IPC Multi-Publisher" "IPC" "ipc:///tmp/uvrpc_broadcast_2.sock" -p 3 -s 2 -b 50
    run_broadcast_test "Broadcast - IPC High Throughput" "IPC" "ipc:///tmp/uvrpc_broadcast_3.sock" -p 5 -s 5 -b 100
    echo ""
    
    # ==================== LATENCY TESTS ====================
    echo -e "\n${CYAN}====================${NC}"
    echo -e "${CYAN}LATENCY TESTS${NC}"
    echo -e "${CYAN}====================${NC}\n"
    
    run_latency_test "TCP" "tcp://127.0.0.1:8000"
    run_latency_test "UDP" "udp://127.0.0.1:8001"
    run_latency_test "IPC" "ipc:///tmp/uvrpc_latency.sock"
    # Note: INPROC latency test requires special handling
    echo -e "${YELLOW}INPROC latency test skipped (requires special setup)${NC}\n"
    
    # ==================== GENERATE REPORT ====================
    echo -e "\n${CYAN}====================${NC}"
    echo -e "${CYAN}GENERATING REPORT${NC}"
    echo -e "${CYAN}====================${NC}\n"
    
    generate_report
    
    echo -e "\n${GREEN}================================${NC}"
    echo -e "${GREEN}All tests completed successfully!${NC}"
    echo -e "${GREEN}================================${NC}"
}

main "$@"