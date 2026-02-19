#!/bin/bash
# UVRPC All Modes and Transports Performance Test
# Tests CS and Broadcast modes across all transports (TCP, UDP, IPC, INPROC) with various concurrency levels

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
MAGENTA='\033[0;35m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BENCHMARK_BIN="${PROJECT_ROOT}/dist/bin/benchmark"
RESULTS_DIR="${PROJECT_ROOT}/benchmark/results"
RESULTS_FILE="${RESULTS_DIR}/all_modes_transports_report_$(date +%Y%m%d_%H%M%S).md"
SUMMARY_FILE="${RESULTS_DIR}/summary_$(date +%Y%m%d_%H%M%S).txt"

# Create results directory
mkdir -p "$RESULTS_DIR"

# Test configuration
TEST_DURATION=3000  # 3 seconds per test
CONCURRENCY_LEVELS=(1 5 10 20 50)
declare -A RESULTS
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0

# Cleanup function
cleanup() {
    echo -e "\n${YELLOW}Cleaning up...${NC}"
    pkill -9 perf_benchmark 2>/dev/null || true
    # Clean up IPC sockets
    rm -f /tmp/uvrpc_benchmark*.sock 2>/dev/null || true
    sleep 1
}

trap cleanup EXIT INT TERM

# Force cleanup on startup
cleanup

# Run CS mode test
run_cs_test() {
    local transport="$1"
    local address="$2"
    local concurrency="$3"
    local threads="${4:-1}"
    local test_name="CS_${transport}_c${concurrency}_t${threads}"
    
    echo -e "${CYAN}=== $test_name ===${NC}"
    echo -e "Address: $address, Concurrency: $concurrency, Threads: $threads"
    
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    
    # Start server
    "$BENCHMARK_BIN" --server -a "$address" --server-timeout $((TEST_DURATION + 5000)) > /tmp/server_${test_name}.log 2>&1 &
    SERVER_PID=$!
    sleep 2
    
    if ! kill -0 $SERVER_PID 2>/dev/null; then
        echo -e "${RED}Server failed to start${NC}"
        cat /tmp/server_${test_name}.log
        FAILED_TESTS=$((FAILED_TESTS + 1))
        return 1
    fi
    
    # Run client test
    local output=$("$BENCHMARK_BIN" -a "$address" -c "$concurrency" -t "$threads" -d "$TEST_DURATION" 2>&1)
    local ret=$?
    
    # Parse results
    local sent=$(echo "$output" | grep "^Sent:" | awk '{print $2}' || echo "0")
    local received=$(echo "$output" | grep "^Received:" | awk '{print $2}' || echo "0")
    local success_rate=$(echo "$output" | grep "Success rate:" | awk '{print $3}' || echo "0%")
    local throughput=$(echo "$output" | grep "Client throughput:" | awk '{print $3}' || echo "0")
    local memory=$(echo "$output" | grep "Memory:" | awk '{print $3}' || echo "0")
    local latency=$(echo "$output" | grep "Avg latency:" | awk '{print $3}' || echo "N/A")
    
    # Kill server
    kill $SERVER_PID 2>/dev/null || true
    wait $SERVER_PID 2>/dev/null || true
    
    if [ $ret -eq 0 ] && [ "$received" -gt 0 ]; then
        echo -e "${GREEN}✓ Sent: $sent, Received: $received, Success: $success_rate, Throughput: $throughput ops/s${NC}"
        PASSED_TESTS=$((PASSED_TESTS + 1))
    else
        echo -e "${RED}✗ Test failed${NC}"
        echo "$output"
        FAILED_TESTS=$((FAILED_TESTS + 1))
    fi
    
    # Store results
    RESULTS["$test_name"]="$transport|$concurrency|$threads|$sent|$received|$success_rate|$throughput|$memory|$latency"
    
    sleep 1
    return $ret
}

# Run Broadcast mode test
run_broadcast_test() {
    local transport="$1"
    local address="$2"
    local publishers="$3"
    local subscribers="$4"
    local test_name="BROADCAST_${transport}_p${publishers}_s${subscribers}"
    
    echo -e "${CYAN}=== $test_name ===${NC}"
    echo -e "Address: $address, Publishers: $publishers, Subscribers: $subscribers"
    
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    
    # Start publisher
    "$BENCHMARK_BIN" --publisher -a "$address" -p "$publishers" -d "$TEST_DURATION" > /tmp/publisher_${test_name}.log 2>&1 &
    PUB_PID=$!
    sleep 1
    
    if ! kill -0 $PUB_PID 2>/dev/null; then
        echo -e "${RED}Publisher failed to start${NC}"
        cat /tmp/publisher_${test_name}.log
        FAILED_TESTS=$((FAILED_TESTS + 1))
        return 1
    fi
    
    # Start subscriber
    local sub_output=$("$BENCHMARK_BIN" --subscriber -a "$address" -s "$subscribers" -d "$TEST_DURATION" 2>&1)
    local sub_ret=$?
    
    # Parse results
    local messages=$(echo "$sub_output" | grep "Messages received:" | awk '{print $3}' || echo "0")
    local throughput=$(echo "$sub_output" | grep "Throughput:" | awk '{print $2}' || echo "0")
    local bandwidth=$(echo "$sub_output" | grep "Bandwidth:" | awk '{print $2}' || echo "0")
    local success_rate=$(echo "$sub_output" | grep "Success rate:" | awk '{print $3}' || echo "0%")
    
    # Cleanup
    kill $PUB_PID 2>/dev/null || true
    wait $PUB_PID 2>/dev/null || true
    
    if [ $sub_ret -eq 0 ] && [ "$messages" -gt 0 ]; then
        echo -e "${GREEN}✓ Messages: $messages, Throughput: $throughput msgs/s, Bandwidth: $bandwidth MB/s, Success: $success_rate${NC}"
        PASSED_TESTS=$((PASSED_TESTS + 1))
    else
        echo -e "${RED}✗ Test failed${NC}"
        echo "$sub_output"
        FAILED_TESTS=$((FAILED_TESTS + 1))
    fi
    
    # Store results
    RESULTS["$test_name"]="$transport|$publishers|$subscribers|$messages|$throughput|$bandwidth|$success_rate|0|N/A"
    
    sleep 1
    return $sub_ret
}

# Generate markdown report
generate_report() {
    echo -e "${BLUE}================================${NC}"
    echo -e "${BLUE}Generating Performance Report${NC}"
    echo -e "${BLUE}================================${NC}"
    
    cat > "$RESULTS_FILE" << EOF
# UVRPC All Modes and Transports Performance Report

**Date:** $(date '+%Y-%m-%d %H:%M:%S')  
**Test Duration:** ${TEST_DURATION}ms per test  
**Platform:** Linux $(uname -m)  
**Total Tests:** $TOTAL_TESTS  
**Passed:** $PASSED_TESTS  
**Failed:** $FAILED_TESTS  
**Pass Rate:** $(echo "scale=2; $PASSED_TESTS * 100 / $TOTAL_TESTS" | bc)%

## Test Configuration

- **Test Duration:** ${TEST_DURATION}ms per test
- **Concurrency Levels:** ${CONCURRENCY_LEVELS[*]}
- **Modes Tested:**
  - Client-Server (CS) Mode
  - Broadcast Mode
- **Transports Tested:**
  - TCP (Network)
  - UDP (Network)
  - IPC (Unix Domain Socket)
  - INPROC (In-Process)

---

## 1. CS Mode Performance Results

### TCP Transport

| Concurrency | Threads | Sent | Received | Success Rate | Throughput (ops/s) | Memory (MB) | Avg Latency (ms) |
|-------------|---------|------|----------|--------------|-------------------|-------------|------------------|
EOF

    for concurrency in "${CONCURRENCY_LEVELS[@]}"; do
        for threads in 1 2 4; do
            local test_name="CS_TCP_c${concurrency}_t${threads}"
            if [ -n "${RESULTS[$test_name]}" ]; then
                IFS='|' read -r trans con thr sent recv succ thrp mem lat <<< "${RESULTS[$test_name]}"
                echo "| $concurrency | $threads | $sent | $recv | $succ | $thrp | $mem | $lat |" >> "$RESULTS_FILE"
            fi
        done
    done
    
    cat >> "$RESULTS_FILE" << 'EOF'

### UDP Transport

| Concurrency | Threads | Sent | Received | Success Rate | Throughput (ops/s) | Memory (MB) | Avg Latency (ms) |
|-------------|---------|------|----------|--------------|-------------------|-------------|------------------|
EOF

    for concurrency in "${CONCURRENCY_LEVELS[@]}"; do
        for threads in 1 2 4; do
            local test_name="CS_UDP_c${concurrency}_t${threads}"
            if [ -n "${RESULTS[$test_name]}" ]; then
                IFS='|' read -r trans con thr sent recv succ thrp mem lat <<< "${RESULTS[$test_name]}"
                echo "| $concurrency | $threads | $sent | $recv | $succ | $thrp | $mem | $lat |" >> "$RESULTS_FILE"
            fi
        done
    done
    
    cat >> "$RESULTS_FILE" << 'EOF'

### IPC Transport

| Concurrency | Threads | Sent | Received | Success Rate | Throughput (ops/s) | Memory (MB) | Avg Latency (ms) |
|-------------|---------|------|----------|--------------|-------------------|-------------|------------------|
EOF

    for concurrency in "${CONCURRENCY_LEVELS[@]}"; do
        for threads in 1 2 4; do
            local test_name="CS_IPC_c${concurrency}_t${threads}"
            if [ -n "${RESULTS[$test_name]}" ]; then
                IFS='|' read -r trans con thr sent recv succ thrp mem lat <<< "${RESULTS[$test_name]}"
                echo "| $concurrency | $threads | $sent | $recv | $succ | $thrp | $mem | $lat |" >> "$RESULTS_FILE"
            fi
        done
    done
    
    cat >> "$RESULTS_FILE" << 'EOF'

### INPROC Transport

| Concurrency | Threads | Sent | Received | Success Rate | Throughput (ops/s) | Memory (MB) | Avg Latency (ms) |
|-------------|---------|------|----------|--------------|-------------------|-------------|------------------|
EOF

    for concurrency in "${CONCURRENCY_LEVELS[@]}"; do
        for threads in 1 2 4; do
            local test_name="CS_INPROC_c${concurrency}_t${threads}"
            if [ -n "${RESULTS[$test_name]}" ]; then
                IFS='|' read -r trans con thr sent recv succ thrp mem lat <<< "${RESULTS[$test_name]}"
                echo "| $concurrency | $threads | $sent | $recv | $succ | $thrp | $mem | $lat |" >> "$RESULTS_FILE"
            fi
        done
    done
    
    cat >> "$RESULTS_FILE" << 'EOF'

---

## 2. Broadcast Mode Performance Results

### UDP Transport

| Publishers | Subscribers | Messages | Throughput (msgs/s) | Bandwidth (MB/s) | Success Rate |
|------------|-------------|----------|---------------------|------------------|--------------|
EOF

    for publishers in 1 3 5; do
        for subscribers in 1 3 5; do
            local test_name="BROADCAST_UDP_p${publishers}_s${subscribers}"
            if [ -n "${RESULTS[$test_name]}" ]; then
                IFS='|' read -r trans pub sub msgs thrp bw succ mem lat <<< "${RESULTS[$test_name]}"
                echo "| $publishers | $subscribers | $msgs | $thrp | $bw | $succ |" >> "$RESULTS_FILE"
            fi
        done
    done
    
    cat >> "$RESULTS_FILE" << 'EOF'

### IPC Transport

| Publishers | Subscribers | Messages | Throughput (msgs/s) | Bandwidth (MB/s) | Success Rate |
|------------|-------------|----------|---------------------|------------------|--------------|
EOF

    for publishers in 1 3 5; do
        for subscribers in 1 3 5; do
            local test_name="BROADCAST_IPC_p${publishers}_s${subscribers}"
            if [ -n "${RESULTS[$test_name]}" ]; then
                IFS='|' read -r trans pub sub msgs thrp bw succ mem lat <<< "${RESULTS[$test_name]}"
                echo "| $publishers | $subscribers | $msgs | $thrp | $bw | $succ |" >> "$RESULTS_FILE"
            fi
        done
    done
    
    cat >> "$RESULTS_FILE" << 'EOF'

### INPROC Transport

| Publishers | Subscribers | Messages | Throughput (msgs/s) | Bandwidth (MB/s) | Success Rate |
|------------|-------------|----------|---------------------|------------------|--------------|
EOF

    for publishers in 1 3 5; do
        for subscribers in 1 3 5; do
            local test_name="BROADCAST_INPROC_p${publishers}_s${subscribers}"
            if [ -n "${RESULTS[$test_name]}" ]; then
                IFS='|' read -r trans pub sub msgs thrp bw succ mem lat <<< "${RESULTS[$test_name]}"
                echo "| $publishers | $subscribers | $msgs | $thrp | $bw | $succ |" >> "$RESULTS_FILE"
            fi
        done
    done
    
    cat >> "$RESULTS_FILE" << 'EOF'

---

## 3. Performance Analysis

### 3.1 Throughput Comparison (CS Mode)

| Transport | Max Throughput | Avg Throughput | Best Concurrency |
|-----------|---------------|----------------|------------------|
EOF

    # Calculate best throughput for each transport
    for transport in TCP UDP IPC INPROC; do
        local max_thr=0
        local avg_thr=0
        local count=0
        local best_conc=""
        
        for concurrency in "${CONCURRENCY_LEVELS[@]}"; do
            for threads in 1 2 4; do
                local test_name="CS_${transport}_c${concurrency}_t${threads}"
                if [ -n "${RESULTS[$test_name]}" ]; then
                    IFS='|' read -r trans con thr sent recv succ thrp mem lat <<< "${RESULTS[$test_name]}"
                    thrp_int=$(echo "$thrp" | cut -d'.' -f1)
                    if [ "$thrp_int" -gt "$max_thr" ]; then
                        max_thr=$thrp_int
                        best_conc="${conc}_t${threads}"
                    fi
                    avg_thr=$((avg_thr + thrp_int))
                    count=$((count + 1))
                fi
            done
        done
        
        if [ $count -gt 0 ]; then
            avg_thr=$((avg_thr / count))
            echo "| $transport | $max_thr | $avg_thr | $best_conc |" >> "$RESULTS_FILE"
        fi
    done
    
    cat >> "$RESULTS_FILE" << 'EOF'

### 3.2 Success Rate Comparison (CS Mode)

| Transport | Min Success Rate | Avg Success Rate |
|-----------|-----------------|------------------|
EOF

    for transport in TCP UDP IPC INPROC; do
        local min_succ=100
        local avg_succ=0
        local count=0
        
        for concurrency in "${CONCURRENCY_LEVELS[@]}"; do
            for threads in 1 2 4; do
                local test_name="CS_${transport}_c${concurrency}_t${threads}"
                if [ -n "${RESULTS[$test_name]}" ]; then
                    IFS='|' read -r trans con thr sent recv succ thrp mem lat <<< "${RESULTS[$test_name]}"
                    succ_num=$(echo "$succ" | sed 's/%//')
                    if [ "$succ_num" -lt "$min_succ" ]; then
                        min_succ=$succ_num
                    fi
                    avg_succ=$((avg_succ + succ_num))
                    count=$((count + 1))
                fi
            done
        done
        
        if [ $count -gt 0 ]; then
            avg_succ=$((avg_succ / count))
            echo "| $transport | ${min_succ}% | ${avg_succ}% |" >> "$RESULTS_FILE"
        fi
    done
    
    cat >> "$RESULTS_FILE" << 'EOF'

### 3.3 Memory Usage Comparison (CS Mode)

| Transport | Min Memory (MB) | Avg Memory (MB) | Max Memory (MB) |
|-----------|-----------------|-----------------|-----------------|
EOF

    for transport in TCP UDP IPC INPROC; do
        local min_mem=999999
        local avg_mem=0
        local max_mem=0
        local count=0
        
        for concurrency in "${CONCURRENCY_LEVELS[@]}"; do
            for threads in 1 2 4; do
                local test_name="CS_${transport}_c${concurrency}_t${threads}"
                if [ -n "${RESULTS[$test_name]}" ]; then
                    IFS='|' read -r trans con thr sent recv succ thrp mem lat <<< "${RESULTS[$test_name]}"
                    mem_int=$(echo "$mem" | cut -d'.' -f1)
                    if [ "$mem_int" -lt "$min_mem" ]; then
                        min_mem=$mem_int
                    fi
                    if [ "$mem_int" -gt "$max_mem" ]; then
                        max_mem=$mem_int
                    fi
                    avg_mem=$((avg_mem + mem_int))
                    count=$((count + 1))
                fi
            done
        done
        
        if [ $count -gt 0 ]; then
            avg_mem=$((avg_mem / count))
            echo "| $transport | $min_mem | $avg_mem | $max_mem |" >> "$RESULTS_FILE"
        fi
    done
    
    cat >> "$RESULTS_FILE" << 'EOF'

### 3.4 Broadcast Mode Performance

| Transport | Max Throughput | Avg Throughput | Max Bandwidth |
|-----------|---------------|----------------|---------------|
EOF

    for transport in UDP IPC INPROC; do
        local max_thr=0
        local avg_thr=0
        local max_bw=0
        local count=0
        
        for publishers in 1 3 5; do
            for subscribers in 1 3 5; do
                local test_name="BROADCAST_${transport}_p${publishers}_s${subscribers}"
                if [ -n "${RESULTS[$test_name]}" ]; then
                    IFS='|' read -r trans pub sub msgs thrp bw succ mem lat <<< "${RESULTS[$test_name]}"
                    thrp_int=$(echo "$thrp" | cut -d'.' -f1)
                    bw_float=$(echo "$bw" | cut -d' ' -f1)
                    if [ "$thrp_int" -gt "$max_thr" ]; then
                        max_thr=$thrp_int
                    fi
                    if [ "$bw_float" != "N/A" ]; then
                        bw_int=$(echo "$bw_float" | cut -d'.' -f1)
                        if [ "$bw_int" -gt "$max_bw" ]; then
                            max_bw=$bw_int
                        fi
                    fi
                    avg_thr=$((avg_thr + thrp_int))
                    count=$((count + 1))
                fi
            done
        done
        
        if [ $count -gt 0 ]; then
            avg_thr=$((avg_thr / count))
            echo "| $transport | $max_thr | $avg_thr | $max_bw |" >> "$RESULTS_FILE"
        fi
    done
    
    cat >> "$RESULTS_FILE" << 'EOF'

---

## 4. Key Findings

### 4.1 Throughput Performance
- **Highest Throughput:** [To be analyzed from results]
- **Transport Ranking:** [To be determined]
- **Concurrency Impact:** [To be analyzed]

### 4.2 Success Rate
- **Best Success Rate:** [To be analyzed from results]
- **Transport Reliability:** [To be determined]
- **Concurrency Impact:** [To be analyzed]

### 4.3 Memory Efficiency
- **Most Memory-Efficient:** [To be analyzed from results]
- **Memory Scalability:** [To be analyzed]

### 4.4 Broadcast Performance
- **Best Broadcast Performance:** [To be analyzed from results]
- **Publisher/Subscriber Scalability:** [To be analyzed]

---

## 5. Conclusion

[Summary of key findings and recommendations]

---

**Generated by:** UVRPC Benchmark Suite  
**Report Location:** $RESULTS_FILE
EOF

    echo -e "${GREEN}Report saved to: $RESULTS_FILE${NC}"
}

# Generate text summary
generate_summary() {
    echo -e "${BLUE}================================${NC}"
    echo -e "${BLUE}Generating Summary${NC}"
    echo -e "${BLUE}================================${NC}"
    
    cat > "$SUMMARY_FILE" << EOF
========================================
UVRPC Performance Test Summary
========================================

Date: $(date '+%Y-%m-%d %H:%M:%S')
Test Duration: ${TEST_DURATION}ms per test

Test Results:
- Total Tests: $TOTAL_TESTS
- Passed: $PASSED_TESTS
- Failed: $FAILED_TESTS
- Pass Rate: $(echo "scale=2; $PASSED_TESTS * 100 / $TOTAL_TESTS" | bc)%

Transport Coverage:
- CS Mode: TCP, UDP, IPC, INPROC
- Broadcast Mode: UDP, IPC, INPROC

Concurrency Levels Tested: ${CONCURRENCY_LEVELS[*]}

Detailed Report: $RESULTS_FILE

========================================
EOF

    echo -e "${GREEN}Summary saved to: $SUMMARY_FILE${NC}"
    cat "$SUMMARY_FILE"
}

# Main test execution
main() {
    echo -e "${MAGENTA}========================================${NC}"
    echo -e "${MAGENTA}UVRPC All Modes and Transports Test${NC}"
    echo -e "${MAGENTA}========================================${NC}"
    echo -e "Binary: $BENCHMARK_BIN"
    echo -e "Duration: ${TEST_DURATION}ms per test"
    echo -e "Results: $RESULTS_FILE"
    echo -e "${MAGENTA}========================================${NC}\n"
    
    # Check binary exists
    if [ ! -x "$BENCHMARK_BIN" ]; then
        echo -e "${RED}Error: Benchmark binary not found: $BENCHMARK_BIN${NC}"
        echo -e "${YELLOW}Please run 'make' first${NC}"
        exit 1
    fi
    
    # ==================== CS MODE TESTS ====================
    echo -e "\n${MAGENTA}========================================${NC}"
    echo -e "${MAGENTA}CLIENT-SERVER MODE TESTS${NC}"
    echo -e "${MAGENTA}========================================${NC}\n"
    
    # TCP Tests
    echo -e "${YELLOW}--- TCP Transport ---${NC}\n"
    PORT=5000
    for concurrency in "${CONCURRENCY_LEVELS[@]}"; do
        for threads in 1 2 4; do
            run_cs_test "TCP" "tcp://127.0.0.1:$PORT" "$concurrency" "$threads"
            PORT=$((PORT + 1))
        done
    done
    echo ""
    
    # UDP Tests
    echo -e "${YELLOW}--- UDP Transport ---${NC}\n"
    PORT=6000
    for concurrency in "${CONCURRENCY_LEVELS[@]}"; do
        for threads in 1 2 4; do
            run_cs_test "UDP" "udp://127.0.0.1:$PORT" "$concurrency" "$threads"
            PORT=$((PORT + 1))
        done
    done
    echo ""
    
    # IPC Tests
    echo -e "${YELLOW}--- IPC Transport ---${NC}\n"
    for concurrency in "${CONCURRENCY_LEVELS[@]}"; do
        for threads in 1 2 4; do
            run_cs_test "IPC" "ipc:///tmp/uvrpc_ipc_${concurrency}_${threads}.sock" "$concurrency" "$threads"
        done
    done
    echo ""
    
    # INPROC Tests
    echo -e "${YELLOW}--- INPROC Transport ---${NC}\n"
    for concurrency in "${CONCURRENCY_LEVELS[@]}"; do
        for threads in 1 2 4; do
            run_cs_test "INPROC" "inproc://test_${concurrency}_${threads}" "$concurrency" "$threads"
        done
    done
    echo ""
    
    # ==================== BROADCAST MODE TESTS ====================
    echo -e "\n${MAGENTA}========================================${NC}"
    echo -e "${MAGENTA}BROADCAST MODE TESTS${NC}"
    echo -e "${MAGENTA}========================================${NC}\n"
    
    # UDP Broadcast Tests
    echo -e "${YELLOW}--- UDP Broadcast ---${NC}\n"
    PORT=7000
    for publishers in 1 3 5; do
        for subscribers in 1 3 5; do
            run_broadcast_test "UDP" "udp://127.0.0.1:$PORT" "$publishers" "$subscribers"
            PORT=$((PORT + 1))
        done
    done
    echo ""
    
    # IPC Broadcast Tests
    echo -e "${YELLOW}--- IPC Broadcast ---${NC}\n"
    for publishers in 1 3 5; do
        for subscribers in 1 3 5; do
            run_broadcast_test "IPC" "ipc:///tmp/uvrpc_bcast_${publishers}_${subscribers}.sock" "$publishers" "$subscribers"
        done
    done
    echo ""
    
    # INPROC Broadcast Tests
    echo -e "${YELLOW}--- INPROC Broadcast ---${NC}\n"
    for publishers in 1 3 5; do
        for subscribers in 1 3 5; do
            run_broadcast_test "INPROC" "inproc://bcast_${publishers}_${subscribers}" "$publishers" "$subscribers"
        done
    done
    echo ""
    
    # ==================== GENERATE REPORTS ====================
    echo -e "\n${MAGENTA}========================================${NC}"
    echo -e "${MAGENTA}GENERATING REPORTS${NC}"
    echo -e "${MAGENTA}========================================${NC}\n"
    
    generate_report
    generate_summary
    
    echo -e "\n${GREEN}========================================${NC}"
    echo -e "${GREEN}All tests completed!${NC}"
    echo -e "${GREEN}Total: $TOTAL_TESTS, Passed: $PASSED_TESTS, Failed: $FAILED_TESTS${NC}"
    echo -e "${GREEN}Report: $RESULTS_FILE${NC}"
    echo -e "${GREEN}========================================${NC}"
}

main "$@"