#!/bin/bash
# UVRPC Transport Performance Test
# Tests all transport layers: INPROC, IPC, TCP, UDP

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[0;33m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
SERVER_BIN="${PROJECT_ROOT}/dist/bin/server"
CLIENT_BIN="${PROJECT_ROOT}/dist/bin/client"
TEST_INPROC_BIN="${PROJECT_ROOT}/dist/bin/test_inproc_simple"

echo -e "${BLUE}================================${NC}"
echo -e "${BLUE}UVRPC Transport Performance Test${NC}"
echo -e "${BLUE}================================${NC}\n"

# Cleanup function
cleanup() {
    pkill -9 -f "perf_server|perf_benchmark|test_inproc" 2>/dev/null || true
    sleep 1
}

trap cleanup EXIT

# Force cleanup on start
cleanup

echo -e "${BLUE}1. Testing TCP Transport${NC}"
echo -e "${YELLOW}================================${NC}"
"$SCRIPT_DIR/run_benchmark.sh" tcp://127.0.0.1:5555 single
echo ""

echo -e "${BLUE}2. Testing UDP Transport${NC}"
echo -e "${YELLOW}================================${NC}"
"$SCRIPT_DIR/run_benchmark.sh" udp://127.0.0.1:5556 single
echo ""

echo -e "${BLUE}3. Testing IPC Transport${NC}"
echo -e "${YELLOW}================================${NC}"
"$SCRIPT_DIR/run_benchmark.sh" ipc://uvrpc_ipc_test single
echo ""

echo -e "${BLUE}4. Testing INPROC Transport${NC}"
echo -e "${YELLOW}================================${NC}"
if [ -x "$TEST_INPROC_BIN" ]; then
    "$TEST_INPROC_BIN" 100000 > /tmp/inproc_test.log 2>&1 &
    INPROC_PID=$!
    sleep 3
    if kill -0 $INPROC_PID 2>/dev/null; then
        kill $INPROC_PID 2>/dev/null || true
    fi
    cat /tmp/inproc_test.log | grep -A 10 "=== Benchmark Results ===" || echo "INPROC test failed"
else
    echo -e "${RED}INPROC binary not found${NC}"
fi
echo ""

echo -e "${GREEN}================================${NC}"
echo -e "${GREEN}All transport tests completed${NC}"
echo -e "${GREEN}================================${NC}"
