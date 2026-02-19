#!/bin/bash
# UVRPC Performance Monitor
# Monitor performance metrics in real-time

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
NC='\033[0m'

# Default configuration
DEFAULT_TRANSPORT="tcp://127.0.0.1:5000"
DEFAULT_INTERVAL=2
DEFAULT_DURATION=10000  # 10 seconds

# Parse arguments
TRANSPORT="${1:-$DEFAULT_TRANSPORT}"
INTERVAL="${2:-$DEFAULT_INTERVAL}"
DURATION="${3:-$DEFAULT_DURATION}"

echo -e "${BLUE}================================${NC}"
echo -e "${BLUE}UVRPC Performance Monitor${NC}"
echo -e "${BLUE}================================${NC}"
echo -e "Transport: $TRANSPORT"
echo -e "Interval: ${INTERVAL}ms"
echo -e "Duration: ${DURATION}ms"
echo -e "${BLUE}================================${NC}\n"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BENCHMARK_BIN="${PROJECT_ROOT}/dist/bin/benchmark"

# Cleanup
cleanup() {
    pkill -9 benchmark 2>/dev/null || true
    rm -f /tmp/uvrpc_*.sock 2>/dev/null || true
}

trap cleanup EXIT

# Check binary
if [ ! -x "$BENCHMARK_BIN" ]; then
    echo -e "${RED}Error: Benchmark binary not found${NC}"
    exit 1
fi

# Start server
echo -e "${CYAN}Starting server...${NC}"
"$BENCHMARK_BIN" --server -a "$TRANSPORT" --server-timeout $((DURATION + 5000)) > /tmp/server.log 2>&1 &
SERVER_PID=$!
sleep 2

if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo -e "${RED}Server failed to start${NC}"
    exit 1
fi

echo -e "${GREEN}Server started (PID: $SERVER_PID)${NC}\n"

# Monitor server stats
echo -e "${CYAN}Monitoring server stats (Ctrl+C to stop)...${NC}\n"

# Extract and display server stats
tail -f /tmp/server.log 2>/dev/null | grep --line-buffered "SERVER.*Total:" | while IFS= read -r line; do
    # Extract metrics
    req=$(echo "$line" | grep -oP 'Total: \K\d+' || echo "0")
    resp=$(echo "$line" | grep -oP 'responses: \K\d+' || echo "0")
    delta_req=$(echo "$line" | grep -oP 'Delta: \K[\d,]+ req/s' | sed 's/,//g' || echo "0")
    delta_resp=$(echo "$line" | grep -oP 'responses: \K[\d,]+ resp/s' | sed 's/,//g' || echo "0")
    throughput=$(echo "$line" | grep -oP 'Throughput: \K[\d,]+ ops/s' | sed 's/,//g' || echo "0")
    
    # Calculate success rate
    if [ "$req" -gt 0 ]; then
        success_rate=$((resp * 100 / req))
    else
        success_rate=0
    fi
    
    # Display stats
    echo -e "\r${CYAN}[$(date +%H:%M:%S)]${NC} Req: ${YELLOW}$req${NC} Resp: ${GREEN}$resp${NC} Success: ${success_rate}% ${CYAN}Throughput: ${BLUE}$throughput${NC} ops/s   " | tr -d '\n'
    
    # Store for summary
    LAST_REQ=$req
    LAST_RESP=$resp
    LAST_THROUGHPUT=$throughput
    LAST_SUCCESS=$success_rate
done &
TAIL_PID=$!

# Run client test in background
echo -e "${CYAN}Running client test in background...${NC}\n"
"$BENCHMARK_BIN" -a "$TRANSPORT" -t 2 -c 2 -b 100 -i "$INTERVAL" -d "$DURATION" > /tmp/client.log 2>&1 &
CLIENT_PID=$!

# Wait for client
wait $CLIENT_PID 2>/dev/null
CLIENT_EXIT=$?

# Stop tail
kill $TAIL_PID 2>/dev/null || true
wait $TAIL_PID 2>/dev/null 2>/dev/null

echo -e "\n${BLUE}================================${NC}"
echo -e "${BLUE}Summary${NC}"
echo -e "${BLUE}================================${NC}"

# Extract client results
CLIENT_SENT=$(grep "^Total requests:" /tmp/client.log | awk '{print $3}' || echo "0")
CLIENT_RECEIVED=$(grep "^Total responses:" /tmp/client.log | awk '{print $3}' || echo "0")
CLIENT_FAILURES=$(grep "^Total failures:" /tmp/client.log | awk '{print $3}' || echo "0")
CLIENT_SUCCESS=$(grep "Success rate:" /tmp/client.log | awk '{print $3}' || echo "0%")
CLIENT_THROUGHPUT=$(grep "Throughput:" /tmp/client.log | awk '{print $3}' || echo "0")
CLIENT_MEMORY=$(grep "Memory:" /tmp/client.log | awk '{print $3}' || echo "0")

# Extract server results
SERVER_REQ=$(grep "Total requests:" /tmp/server.log | tail -1 | awk '{print $3}' || echo "0")
SERVER_RESP=$(grep "Total responses:" /tmp/server.log | tail -1 | awk '{print $3}' || echo "0")

echo -e "Client Results:"
echo -e "  Sent: $CLIENT_SENT"
echo -e "  Received: $CLIENT_RECEIVED"
echo -e "  Failures: $CLIENT_FAILURES"
echo -e "  Success Rate: $CLIENT_SUCCESS"
echo -e "  Throughput: $CLIENT_THROUGHPUT ops/s"
echo -e "  Memory: $CLIENT_MEMORY MB"

echo -e "\nServer Results:"
echo -e "  Total Requests: $SERVER_REQ"
echo -e "  Total Responses: $SERVER_RESP"

echo -e "\n${BLUE}================================${NC}"
echo -e "${GREEN}Monitoring completed${NC}"
echo -e "${BLUE}================================${NC}"

exit $CLIENT_EXIT
