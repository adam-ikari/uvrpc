#!/bin/bash
# Test UVRPC performance with different concurrency levels

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BENCHMARK_BIN="${PROJECT_ROOT}/dist/bin/benchmark"
RESULTS_DIR="${PROJECT_ROOT}/benchmark/results"
RESULTS_FILE="${RESULTS_DIR}/concurrent_performance_report_$(date +%Y%m%d_%H%M%S).md"

mkdir -p "$RESULTS_DIR"

# Cleanup function
cleanup() {
    echo -e "\n${YELLOW}Cleaning up...${NC}"
    pkill -9 benchmark 2>/dev/null || true
    rm -f /tmp/uvrpc_test*.sock 2>/dev/null || true
    sleep 1
}

trap cleanup EXIT INT TERM
cleanup

# Test duration
TEST_DURATION=3000

# Start server
start_server() {
    local address="$1"
    echo -e "${BLUE}Starting server on $address...${NC}"
    "$BENCHMARK_BIN" --server -a "$address" --server-timeout 120000 > /tmp/server.log 2>&1 &
    SERVER_PID=$!
    sleep 2
    
    if ! kill -0 $SERVER_PID 2>/dev/null; then
        echo -e "${RED}Server failed to start${NC}"
        cat /tmp/server.log
        return 1
    fi
    echo -e "${GREEN}Server started (PID: $SERVER_PID)${NC}"
    return 0
}

# Run test
run_test() {
    local transport="$1"
    local address="$2"
    local threads="$3"
    local clients="$4"
    local concurrency="$5"
    local test_name="${transport}_t${threads}_c${clients}_con${concurrency}"
    
    echo -e "${CYAN}=== $test_name ===${NC}"
    
    # Run client
    local output=$("$BENCHMARK_BIN" -a "$address" -t "$threads" -c "$clients" -d "$TEST_DURATION" 2>&1)
    local ret=$?
    
    # Parse results
    local sent=$(echo "$output" | grep "^Sent:" | awk '{print $2}' || echo "0")
    local received=$(echo "$output" | grep "^Received:" | awk '{print $2}' || echo "0")
    local success_rate=$(echo "$output" | grep "Success rate:" | awk '{print $3}' || echo "0%")
    local throughput=$(echo "$output" | grep "Client throughput:" | awk '{print $3}' || echo "0")
    
    echo -e "Sent: $sent, Received: $received, Success: $success_rate, Throughput: $throughput ops/s"
    
    echo "$test_name|$transport|$threads|$clients|$concurrency|$sent|$received|$success_rate|$throughput" >> "$TEMP_RESULTS"
}

# Create temp file for results
TEMP_RESULTS=$(mktemp)

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}UVRPC Concurrent Performance Test${NC}"
echo -e "${BLUE}========================================${NC}"

# Test configuration
declare -a CONCURRENCY_LEVELS=(10 20 50 100)

# ==================== TCP Tests ====================
echo -e "\n${CYAN}=== TCP Transport Tests ===${NC}"

PORT=5000
for concurrency in "${CONCURRENCY_LEVELS[@]}"; do
    # Single thread, single client
    start_server "tcp://127.0.0.1:$PORT"
    run_test "TCP" "tcp://127.0.0.1:$PORT" 1 1 "$concurrency"
    kill $SERVER_PID 2>/dev/null || true
    wait $SERVER_PID 2>/dev/null || true
    PORT=$((PORT + 1))
    
    # Single thread, 5 clients
    start_server "tcp://127.0.0.1:$PORT"
    run_test "TCP" "tcp://127.0.0.1:$PORT" 1 5 "$concurrency"
    kill $SERVER_PID 2>/dev/null || true
    wait $SERVER_PID 2>/dev/null || true
    PORT=$((PORT + 1))
    
    # 2 threads, 2 clients total
    start_server "tcp://127.0.0.1:$PORT"
    run_test "TCP" "tcp://127.0.0.1:$PORT" 2 2 "$concurrency"
    kill $SERVER_PID 2>/dev/null || true
    wait $SERVER_PID 2>/dev/null || true
    PORT=$((PORT + 1))
    
    sleep 1
done

# ==================== UDP Tests ====================
echo -e "\n${CYAN}=== UDP Transport Tests ===${NC}"

PORT=6000
for concurrency in "${CONCURRENCY_LEVELS[@]}"; do
    # Single thread, single client
    start_server "udp://127.0.0.1:$PORT"
    run_test "UDP" "udp://127.0.0.1:$PORT" 1 1 "$concurrency"
    kill $SERVER_PID 2>/dev/null || true
    wait $SERVER_PID 2>/dev/null || true
    PORT=$((PORT + 1))
    
    # Single thread, 5 clients
    start_server "udp://127.0.0.1:$PORT"
    run_test "UDP" "udp://127.0.0.1:$PORT" 1 5 "$concurrency"
    kill $SERVER_PID 2>/dev/null || true
    wait $SERVER_PID 2>/dev/null || true
    PORT=$((PORT + 1))
    
    # 2 threads, 2 clients total
    start_server "udp://127.0.0.1:$PORT"
    run_test "UDP" "udp://127.0.0.1:$PORT" 2 2 "$concurrency"
    kill $SERVER_PID 2>/dev/null || true
    wait $SERVER_PID 2>/dev/null || true
    PORT=$((PORT + 1))
    
    sleep 1
done

# ==================== IPC Tests ====================
echo -e "\n${CYAN}=== IPC Transport Tests ===${NC}"

for concurrency in "${CONCURRENCY_LEVELS[@]}"; do
    # Single thread, single client
    start_server "ipc:///tmp/uvrpc_test_c${concurrency}_1.sock"
    run_test "IPC" "ipc:///tmp/uvrpc_test_c${concurrency}_1.sock" 1 1 "$concurrency"
    kill $SERVER_PID 2>/dev/null || true
    wait $SERVER_PID 2>/dev/null || true
    
    # Single thread, 5 clients
    start_server "ipc:///tmp/uvrpc_test_c${concurrency}_5.sock"
    run_test "IPC" "ipc:///tmp/uvrpc_test_c${concurrency}_5.sock" 1 5 "$concurrency"
    kill $SERVER_PID 2>/dev/null || true
    wait $SERVER_PID 2>/dev/null || true
    
    # 2 threads, 2 clients total
    start_server "ipc:///tmp/uvrpc_test_c${concurrency}_2t.sock"
    run_test "IPC" "ipc:///tmp/uvrpc_test_c${concurrency}_2t.sock" 2 2 "$concurrency"
    kill $SERVER_PID 2>/dev/null || true
    wait $SERVER_PID 2>/dev/null || true
    
    sleep 1
done

# ==================== INPROC Tests ====================
echo -e "\n${CYAN}=== INPROC Transport Tests ===${NC}"

for concurrency in "${CONCURRENCY_LEVELS[@]}"; do
    # Single thread, single client
    start_server "inproc://test_c${concurrency}_1"
    run_test "INPROC" "inproc://test_c${concurrency}_1" 1 1 "$concurrency"
    kill $SERVER_PID 2>/dev/null || true
    wait $SERVER_PID 2>/dev/null || true
    
    # Single thread, 5 clients
    start_server "inproc://test_c${concurrency}_5"
    run_test "INPROC" "inproc://test_c${concurrency}_5" 1 5 "$concurrency"
    kill $SERVER_PID 2>/dev/null || true
    wait $SERVER_PID 2>/dev/null || true
    
    # 2 threads, 2 clients total
    start_server "inproc://test_c${concurrency}_2t"
    run_test "INPROC" "inproc://test_c${concurrency}_2t" 2 2 "$concurrency"
    kill $SERVER_PID 2>/dev/null || true
    wait $SERVER_PID 2>/dev/null || true
    
    sleep 1
done

# ==================== Generate Report ====================
echo -e "\n${CYAN}=== Generating Report ===${NC}"

cat > "$RESULTS_FILE" << EOF
# UVRPC Concurrent Performance Report

**Date:** $(date '+%Y-%m-%d %H:%M:%S')  
**Test Duration:** ${TEST_DURATION}ms per test  
**Concurrency Levels:** ${CONCURRENCY_LEVELS[*]}

## Test Results

### TCP Transport

| Threads | Clients | Concurrency | Sent | Received | Success Rate | Throughput (ops/s) |
|---------|---------|-------------|------|----------|--------------|-------------------|
EOF

while IFS='|' read -r name transport threads clients concurrency sent received success_rate throughput; do
    if [ "$transport" == "TCP" ]; then
        echo "| $threads | $clients | $concurrency | $sent | $received | $success_rate | $throughput |" >> "$RESULTS_FILE"
    fi
done < "$TEMP_RESULTS"

cat >> "$RESULTS_FILE" << EOF

### UDP Transport

| Threads | Clients | Concurrency | Sent | Received | Success Rate | Throughput (ops/s) |
|---------|---------|-------------|------|----------|--------------|-------------------|
EOF

while IFS='|' read -r name transport threads clients concurrency sent received success_rate throughput; do
    if [ "$transport" == "UDP" ]; then
        echo "| $threads | $clients | $concurrency | $sent | $received | $success_rate | $throughput |" >> "$RESULTS_FILE"
    fi
done < "$TEMP_RESULTS"

cat >> "$RESULTS_FILE" << EOF

### IPC Transport

| Threads | Clients | Concurrency | Sent | Received | Success Rate | Throughput (ops/s) |
|---------|---------|-------------|------|----------|--------------|-------------------|
EOF

while IFS='|' read -r name transport threads clients concurrency sent received success_rate throughput; do
    if [ "$transport" == "IPC" ]; then
        echo "| $threads | $clients | $concurrency | $sent | $received | $success_rate | $throughput |" >> "$RESULTS_FILE"
    fi
done < "$TEMP_RESULTS"

cat >> "$RESULTS_FILE" << EOF

### INPROC Transport

| Threads | Clients | Concurrency | Sent | Received | Success Rate | Throughput (ops/s) |
|---------|---------|-------------|------|----------|--------------|-------------------|
EOF

while IFS='|' read -r name transport threads clients concurrency sent received success_rate throughput; do
    if [ "$transport" == "INPROC" ]; then
        echo "| $threads | $clients | $concurrency | $sent | $received | $success_rate | $throughput |" >> "$RESULTS_FILE"
    fi
done < "$TEMP_RESULTS"

cat >> "$RESULTS_FILE" << EOF

## Analysis

### Success Rate by Concurrency Level

| Concurrency | TCP | UDP | IPC | INPROC |
|-------------|-----|-----|-----|--------|
EOF

for concurrency in "${CONCURRENCY_LEVELS[@]}"; do
    tcp_avg=$(grep "TCP.*con${concurrency}" "$TEMP_RESULTS" | awk -F'|' '{sum+=$8; count++} END {if(count>0) printf "%.1f%%", sum/count; else print "N/A"}')
    udp_avg=$(grep "UDP.*con${concurrency}" "$TEMP_RESULTS" | awk -F'|' '{sum+=$8; count++} END {if(count>0) printf "%.1f%%", sum/count; else print "N/A"}')
    ipc_avg=$(grep "IPC.*con${concurrency}" "$TEMP_RESULTS" | awk -F'|' '{sum+=$8; count++} END {if(count>0) printf "%.1f%%", sum/count; else print "N/A"}')
    inproc_avg=$(grep "INPROC.*con${concurrency}" "$TEMP_RESULTS" | awk -F'|' '{sum+=$8; count++} END {if(count>0) printf "%.1f%%", sum/count; else print "N/A"}')
    echo "| $concurrency | $tcp_avg | $udp_avg | $ipc_avg | $inproc_avg |" >> "$RESULTS_FILE"
done

cat >> "$RESULTS_FILE" << EOF

## Key Findings

1. **Optimal Concurrency Level**: [To be analyzed]
2. **Thread Scaling**: [To be analyzed]
3. **Transport Comparison**: [To be analyzed]

---

**Generated by:** UVRPC Benchmark Suite  
**Report Location:** $RESULTS_FILE
EOF

rm "$TEMP_RESULTS"

echo -e "${GREEN}Report saved to: $RESULTS_FILE${NC}"
cat "$RESULTS_FILE"

echo -e "\n${GREEN}========================================${NC}"
echo -e "${GREEN}All tests completed!${NC}"
echo -e "${GREEN}========================================${NC}"
