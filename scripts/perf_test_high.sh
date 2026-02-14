#!/bin/bash
# UVRPC 高性能测试 - 使用单个连接

set -e

ADDRESS="tcp://127.0.0.1:8888"
REQUESTS=10000

echo "=== UVRPC High Performance Test ==="
echo "Using single persistent connection"
echo "Requests: $REQUESTS"
echo ""

# Create a test program that keeps connection alive
cat > /tmp/perf_client.c << 'EOF'
#include "../include/uvrpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>

static volatile int g_running = 1;
static volatile int g_received = 0;
static int g_requests_sent = 0;
static uint64_t g_total_latency_ns = 0;

void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
}

uint64_t get_time_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

void response_callback(uvrpc_response_t* resp, void* ctx) {
    uint64_t start_time = (uint64_t)ctx;
    uint64_t latency = get_time_ns() - start_time;
    
    g_received++;
    g_total_latency_ns += latency;
    
    if (g_requests_sent % 100 == 0) {
        printf("\rProgress: %d/%d", g_received, g_requests_sent);
        fflush(stdout);
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <address> <requests>\n", argv[0]);
        return 1;
    }
    
    const char* address = argv[1];
    int requests = atoi(argv[2]);
    
    signal(SIGINT, signal_handler);
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, &loop);
    uvrpc_config_set_address(config, address);
    uvrpc_config_set_transport(config, UVRPC_TRANSPORT_TCP);
    uvrpc_config_set_comm_type(config, UVRPC_COMM_SERVER_CLIENT);
    
    uvrpc_client_t* client = uvrpc_client_create(config);
    if (!client) {
        fprintf(stderr, "Failed to create client\n");
        return 1;
    }
    
    /* Connect */
    int ret = uvrpc_client_connect(client);
    if (ret != UVRPC_OK) {
        fprintf(stderr, "Failed to connect: %d\n", ret);
        return 1;
    }
    
    printf("Connected. Sending %d requests...\n", requests);
    fflush(stdout);
    
    /* Send all requests */
    const char* msg = "Hello";
    uint64_t start_time = get_time_ns();
    
    for (int i = 0; i < requests && g_running; i++) {
        g_requests_sent++;
        uint64_t req_time = get_time_ns();
        
        ret = uvrpc_client_call(client, "echo", 
                                  (uint8_t*)msg, strlen(msg),
                                  response_callback, (void*)req_time);
        
        if (ret != UVRPC_OK) {
            fprintf(stderr, "Failed to send request %d: %d\n", i, ret);
        }
        
        /* Run event loop */
        uv_run(&loop, UV_RUN_NOWAIT);
        
        /* Small delay every 100 requests */
        if (i % 100 == 99) {
            usleep(1000);
        }
    }
    
    /* Wait for all responses */
    int wait_iter = 0;
    while (g_received < g_requests_sent && g_running && wait_iter < 5000) {
        uv_run(&loop, UV_RUN_NOWAIT);
        usleep(1000);
        wait_iter++;
    }
    
    uint64_t end_time = get_time_ns();
    double duration_sec = (double)(end_time - start_time) / 1000000000.0;
    
    printf("\n\n=== Results ===\n");
    printf("Sent: %d\n", g_requests_sent);
    printf("Received: %d\n", g_received);
    printf("Duration: %.3f seconds\n", duration_sec);
    
    if (g_received > 0) {
        double rps = g_received / duration_sec;
        double avg_latency_us = (double)g_total_latency_ns / g_received / 1000.0;
        printf("Throughput: %.2f RPS\n", rps);
        printf("Avg latency: %.3f us\n", avg_latency_us);
    }
    
    uvrpc_client_disconnect(client);
    uvrpc_client_free(client);
    uvrpc_config_free(config);
    uv_loop_close(&loop);
    
    return 0;
}
EOF

# Compile
gcc -I./include -I./generated -I./deps/libuv/include -I./deps/flatcc/include -I./deps/uthash/include \
    /tmp/perf_client.c \
    dist/lib/libuvrpc.a \
    deps/libuv/build_shared/libuv.a \
    deps/flatcc/lib/libflatcc.a \
    deps/mimalloc/build_shared/libmimalloc.a \
    -pthread -lrt -ldl -o /tmp/perf_client 2>&1 | head -10

# Start server
echo "Starting server..."
./dist/bin/simple_server $ADDRESS > /tmp/srv.log 2>&1 &
SERVER_PID=$!
sleep 2

# Run test
echo ""
timeout 15 /tmp/perf_client $ADDRESS $REQUESTS 2>&1
TEST_RESULT=$?

# Cleanup
kill $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null

rm -f /tmp/perf_client.c /tmp/perf_client

if [ $TEST_RESULT -eq 124 ]; then
    echo ""
    echo "Warning: Test timed out after 15 seconds"
fi

echo ""
echo "Server handled $(tail -100 /tmp/srv.log | grep -c "Looking for handler") requests"