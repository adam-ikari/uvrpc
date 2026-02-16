#!/bin/bash
# Test TCP and IPC transport protocols

echo "=== UVRPC Transport Performance Comparison ==="

# Test TCP
echo ""
echo "=== TCP Transport ==="
./dist/bin/server tcp://127.0.0.1:5555 > /tmp/tcp_server.log 2>&1 &
TCP_PID=$!
sleep 2
TCP_RESULT=$(timeout 5 ./dist/bin/client tcp://127.0.0.1:5555 -d 1000 2>&1 | grep "Throughput:" | head -1 | awk '{print $2}')
kill $TCP_PID 2>/dev/null || true
wait $TCP_PID 2>/dev/null || true
echo "TCP Throughput: $TCP_RESULT ops/s"

# Test IPC
echo ""
echo "=== IPC Transport ==="
rm -f /tmp/uvrpc_test.sock
./dist/bin/server ipc:///tmp/uvrpc_test.sock > /tmp/ipc_server.log 2>&1 &
IPC_PID=$!
sleep 2
IPC_RESULT=$(timeout 5 ./dist/bin/client ipc:///tmp/uvrpc_test.sock -d 1000 2>&1 | grep "Throughput:" | head -1 | awk '{print $2}')
kill $IPC_PID 2>/dev/null || true
wait $IPC_PID 2>/dev/null || true
echo "IPC Throughput: $IPC_RESULT ops/s"

echo ""
echo "=== Summary ==="
echo "TCP: $TCP_RESULT ops/s"
echo "IPC: $IPC_RESULT ops/s"

if [ -n "$TCP_RESULT" ] && [ -n "$IPC_RESULT" ]; then
    IMPROVEMENT=$((IPC_RESULT * 100 / TCP_RESULT))
    echo ""
    echo "IPC is $((IMPROVEMENT - 100))% faster than TCP"
fi

echo "Test completed"