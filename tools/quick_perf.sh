#!/bin/bash
# UVRPC Quick Performance Test
# Quick performance test with default optimal configuration

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BENCHMARK_BIN="${PROJECT_ROOT}/dist/bin/benchmark"

# Default configuration
DEFAULT_TRANSPORT="tcp://127.0.0.1:5000"
DEFAULT_THREADS=2
DEFAULT_CLIENTS=2
DEFAULT_CONCURRENCY=100
DEFAULT_INTERVAL=2  # Recommended: 2ms
DEFAULT_DURATION=3000  # 3 seconds

# Parse arguments
TRANSPORT="${1:-$DEFAULT_TRANSPORT}"
THREADS="${2:-$DEFAULT_THREADS}"
CLIENTS="${3:-$DEFAULT_CLIENTS}"
CONCURRENCY="${4:-$DEFAULT_CONCURRENCY}"
INTERVAL="${5:-$DEFAULT_INTERVAL}"
DURATION="${6:-$DEFAULT_DURATION}"

echo -e "${BLUE}================================${NC}"
echo -e "${BLUE}UVRPC Quick Performance Test${NC}"
echo -e "${BLUE}================================${NC}"
echo -e "Transport: $TRANSPORT"
echo -e "Threads: $THREADS"
echo -e "Clients: $CLIENTS"
echo -e "Concurrency: $CONCURRENCY"
echo -e "Interval: ${INTERVAL}ms"
echo -e "Duration: ${DURATION}ms"
echo -e "${BLUE}================================${NC}\n"

# Cleanup
cleanup() {
    pkill -9 benchmark 2>/dev/null || true
    rm -f /tmp/uvrpc_*.sock 2>/dev/null || true
}

trap cleanup EXIT

# Check binary
if [ ! -x "$BENCHMARK_BIN" ]; then
    echo -e "${RED}Error: Benchmark binary not found${NC}"
    echo -e "${YELLOW}Please run 'make' first${NC}"
    exit 1
fi

# Start server
echo -e "${BLUE}Starting server...${NC}"
"$BENCHMARK_BIN" --server -a "$TRANSPORT" --server-timeout $((DURATION + 5000)) > /tmp/server.log 2>&1 &
SERVER_PID=$!
sleep 2

if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo -e "${RED}Server failed to start${NC}"
    cat /tmp/server.log
    exit 1
fi

echo -e "${GREEN}Server started (PID: $SERVER_PID)${NC}\n"

# Run client test
echo -e "${BLUE}Running client test...${NC}"
echo "$BENCHMARK_BIN" -a "$TRANSPORT" -t "$THREADS" -c "$CLIENTS" -b "$CONCURRENCY" -i "$INTERVAL" -d "$DURATION"
EXIT_CODE=$?

# Wait for server to finish
wait $SERVER_PID 2>/dev/null

if [ $EXIT_CODE -eq 0 ]; then
    echo -e "\n${GREEN}================================${NC}"
    echo -e "${GREEN}Test completed successfully!${NC}"
    echo -e "${GREEN}================================${NC}"
else
    echo -e "\n${RED}================================${NC}"
    echo -e "${RED}Test failed with exit code: $EXIT_CODE${NC}"
    echo -e "${RED}================================${NC}"
fi

exit $EXIT_CODE