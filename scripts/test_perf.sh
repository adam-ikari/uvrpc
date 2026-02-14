#!/bin/bash
# UVRPC Performance Test Script

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Configuration
ADDRESS="127.0.0.1:54321"
SERVER_BIN="./dist/bin/simple_server"
CLIENT_BIN="./dist/bin/simple_client"
LOG_DIR="/tmp/uvrpc_perf_test"

# Clean up old logs
rm -rf $LOG_DIR 2>/dev/null || true
mkdir -p $LOG_DIR

# Start server
echo -e "${GREEN}Starting server...${NC}"
$SERVER_BIN tcp://$ADDRESS > $LOG_DIR/server.log 2>&1 &
SERVER_PID=$!
sleep 2

echo -e "${GREEN}Server started (PID: $SERVER_PID)${NC}"

# Test 1: Single request latency
echo ""
echo "=== Test 1: Single Request Latency ==="
latencies=()
for i in {1..5}; do
    start=$(date +%s%N)
    timeout 10 $CLIENT_BIN tcp://$ADDRESS > $LOG_DIR/client_$i.log 2>&1
    result=$?
    end=$(date +%s%N)
    
    if [ $result -eq 0 ]; then
        latency=$(( (end - start) / 1000000 ))
        latencies+=($latency)
        echo "  Request $i: ${latency}ms"
    else
        echo -e "  Request $i: ${RED}Failed (exit code: $result)${NC}"
    fi
done

if [ ${#latencies[@]} -gt 0 ]; then
    total=0
    for l in "${latencies[@]}"; do total=$((total + l)); done
    avg=$((total / ${#latencies[@]}))
    echo -e "  ${GREEN}Average latency: ${avg}ms${NC}"
fi

# Test 2: Throughput (sequential)
echo ""
echo "=== Test 2: Throughput (Sequential) ==="
TEST_COUNT=10
start_time=$(date +%s%N)
success=0

for i in $(seq 1 $TEST_COUNT); do
    if timeout 10 $CLIENT_BIN tcp://$ADDRESS > $LOG_DIR/client_seq_$i.log 2>&1; then
        if grep -q "Result: 30" $LOG_DIR/client_seq_$i.log; then
            success=$((success + 1))
        fi
    fi
done

end_time=$(date +%s%N)
elapsed=$(( (end_time - start_time) / 1000000 ))

if [ $elapsed -gt 0 ]; then
    rps=$(( 10000 * success / elapsed ))
    echo -e "  ${GREEN}Success rate: $success/$TEST_COUNT${NC}"
    echo -e "  ${GREEN}Total time: ${elapsed}ms${NC}"
    echo -e "  ${GREEN}Throughput: ~${rps} RPS${NC}"
else
    echo -e "  ${RED}Test failed (elapsed time: ${elapsed}ms)${NC}"
fi

# Server stats
echo ""
echo "=== Server Statistics ==="
recv_count=$(grep SERVER_RECV $LOG_DIR/server.log 2>/dev/null | wc -l || echo 0)
handler_count=$(grep "About to call handler" $LOG_DIR/server.log 2>/dev/null | wc -l || echo 0)
echo "  Requests received: $recv_count"
echo "  Handlers called: $handler_count"

# Cleanup
echo ""
echo -e "${YELLOW}Cleaning up...${NC}"
kill $SERVER_PID 2>/dev/null || true
pkill -9 -f simple_client 2>/dev/null || true

echo ""
echo -e "${GREEN}Performance test completed!${NC}"