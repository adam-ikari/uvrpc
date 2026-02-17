#!/bin/bash
# Simple Performance Test for UVRPC Transports

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

cleanup() {
    pkill -9 -f "perf_server|perf_benchmark" 2>/dev/null || true
    sleep 1
}

trap cleanup EXIT

cleanup

# Test TCP
echo -e "${BLUE}=== TCP Transport ===${NC}"
"$SERVER_BIN" tcp://127.0.0.1:5555 > /tmp/server_tcp.log 2>&1 &
SERVER_PID=$!
sleep 2

if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo -e "${RED}Server failed to start${NC}"
    cat /tmp/server_tcp.log
    exit 1
fi

timeout 5 "$CLIENT_BIN" -a tcp://127.0.0.1:5555 -b 100 -d 3000 2>&1 | grep -E "Throughput:|Success rate:|Failed:" || echo "TCP test failed"
kill $SERVER_PID 2>/dev/null || true
wait $SERVER_PID 2>/dev/null || true
sleep 1
echo ""

# Test UDP
echo -e "${BLUE}=== UDP Transport ===${NC}"
"$SERVER_BIN" udp://127.0.0.1:5556 > /tmp/server_udp.log 2>&1 &
SERVER_PID=$!
sleep 2

if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo -e "${RED}Server failed to start${NC}"
    cat /tmp/server_udp.log
    exit 1
fi

timeout 5 "$CLIENT_BIN" -a udp://127.0.0.1:5556 -b 100 -d 3000 2>&1 | grep -E "Throughput:|Success rate:|Failed:" || echo "UDP test failed"
kill $SERVER_PID 2>/dev/null || true
wait $SERVER_PID 2>/dev/null || true
sleep 1
echo ""

# Test IPC
echo -e "${BLUE}=== IPC Transport ===${NC}"
"$SERVER_BIN" ipc://uvrpc_ipc_test > /tmp/server_ipc.log 2>&1 &
SERVER_PID=$!
sleep 2

if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo -e "${RED}Server failed to start${NC}"
    cat /tmp/server_ipc.log
    exit 1
fi

timeout 5 "$CLIENT_BIN" -a ipc://uvrpc_ipc_test -b 100 -d 3000 2>&1 | grep -E "Throughput:|Success rate:|Failed:" || echo "IPC test failed"
kill $SERVER_PID 2>/dev/null || true
wait $SERVER_PID 2>/dev/null || true
sleep 1
echo ""

# Test INPROC
echo -e "${BLUE}=== INPROC Transport ===${NC}"
if [ -x "$PROJECT_ROOT/dist/bin/test_inproc_simple" ]; then
    "$PROJECT_ROOT/dist/bin/test_inproc_simple" 50000 > /tmp/inproc_test.log 2>&1 &
    INPROC_PID=$!
    sleep 2
    if kill -0 $INPROC_PID 2>/dev/null; then
        kill $INPROC_PID 2>/dev/null || true
    fi
    wait $INPROC_PID 2>/dev/null || true
    cat /tmp/inproc_test.log | grep -A 10 "=== Benchmark Results ===" || echo "INPROC test failed"
else
    echo -e "${YELLOW}INPROC binary not found${NC}"
fi
echo ""

echo -e "${GREEN}=== All tests completed ===${NC}"