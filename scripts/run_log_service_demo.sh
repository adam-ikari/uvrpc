#!/bin/bash
# Quick script to build and run the log service demo

set -e

echo "=== UVRPC Log Service Demo ==="
echo ""

# Check if generated code exists
if [ ! -d "generated/log_service" ]; then
    echo "Generating log service code..."
    python3 tools/uvrpcc.py --flatcc deps/flatcc/bin/flatcc \
        schema/log_service.fbs -o generated/log_service
fi

# Build the demo
echo "Building log_service_demo..."
gcc -I. -Igenerated -Iinclude -Ideps/libuv/include \
    -Ideps/mimalloc/include -Lbuild -Ldeps/mimalloc/out/release \
    examples/log_simple_demo.c \
    generated/log_service/log_logservice_client.c \
    generated/log_service/log_logservice_server_stub.c \
    generated/log_service/log_logservice_rpc_common.c \
    -o log_simple_demo -luvrpc -luv -lmimalloc -lpthread

echo ""
echo "Running log_simple_demo..."
echo "=========================="
echo ""

./log_simple_demo

echo ""
echo "=========================="
echo "Demo completed!"