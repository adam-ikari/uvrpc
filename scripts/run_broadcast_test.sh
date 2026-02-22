#!/bin/bash
# Broadcast mode performance test script

cd "$(dirname "$0")/.."

# Start subscriber
./dist/bin/broadcast_subscriber udp://127.0.0.1:6000 1000 > /tmp/broadcast_sub.log 2>&1 &
SUB_PID=$!

# Wait for subscriber to be ready
sleep 2

# Start publisher
echo "=== Running Broadcast Performance Test ==="
echo "Subscriber PID: $SUB_PID"
echo ""

./dist/bin/broadcast_publisher udp://127.0.0.1:6000 1000 > /tmp/broadcast_pub.log 2>&1 &
PUB_PID=$!

# Wait for publisher to finish
wait $PUB_PID 2>/dev/null

# Give subscriber time to finish
sleep 1

# Cleanup
kill $SUB_PID 2>/dev/null
wait $SUB_PID 2>/dev/null

echo ""
echo "=== Publisher Results ==="
cat /tmp/broadcast_pub.log | grep -E "Published|messages"

echo ""
echo "=== Subscriber Results ==="
cat /tmp/broadcast_sub.log | grep -E "Received|messages"

echo ""
echo "=== Test Complete ==="