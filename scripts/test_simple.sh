#!/bin/bash
# 简化测试 - 只测试服务器处理能力

set -e

echo "=== Simple Server Capacity Test ==="

# Start server
./dist/bin/simple_server tcp://127.0.0.1:7777 > /tmp/srv.log 2>&1 &
SERVER_PID=$!
sleep 2

# Send single request
echo "Sending single request..."
./dist/bin/simple_client tcp://127.0.0.1:7777 echo 2>&1 | tail -5

# Check server logs
echo ""
echo "Server logs:"
tail -10 /tmp/srv.log

# Cleanup
kill $SERVER_PID 2>/dev/null
sleep 1

echo ""
echo "Total requests handled:"
grep -c "Looking for handler" /tmp/srv.log || echo "0"