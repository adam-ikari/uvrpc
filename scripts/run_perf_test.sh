#!/bin/bash
# Simple performance test script

cd "$(dirname "$0")/.."

# Start server
./dist/bin/simple_server tcp://127.0.0.1:5555 > /tmp/perf_server.log 2>&1 &
SERVER_PID=$!

# Wait for server to start
sleep 2

# Run performance test
echo "=== Running Performance Test ==="
echo "Server PID: $SERVER_PID"
echo ""

# Compile perf_test.c if not already compiled
if [ ! -f /tmp/perf_test ]; then
    echo "Compiling perf_test.c..."
    gcc -I./include -I./src -I./deps/libuv/include -I./deps/uthash/include \
        -I./deps/mimalloc/include -I./generated -o /tmp/perf_test \
        examples/perf_test.c dist/lib/libuvrpc_full.a \
        -lpthread -lrt -ldl -lm -static 2>&1
    if [ $? -ne 0 ]; then
        echo "Failed to compile perf_test.c"
        kill $SERVER_PID 2>/dev/null
        exit 1
    fi
fi

# Run test
/tmp/perf_test 1000

# Cleanup
kill $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null

echo ""
echo "=== Test Complete ==="