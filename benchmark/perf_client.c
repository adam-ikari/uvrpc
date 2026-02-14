/**
 * UVRPC Performance Client
 * High-throughput performance test
 */

#include "../include/uvrpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int g_responses_received = 0;
static int g_target_responses = 0;
static volatile int g_done = 0;
static volatile int g_connected = 0;

void on_connect(int status, void* ctx) {
    (void)ctx;
    if (status == 0) {
        g_connected = 1;
    } else {
        fprintf(stderr, "Connection failed: %d\n", status);
    }
}

void on_response(uvrpc_response_t* resp, void* ctx) {
    (void)ctx;
    if (resp->status == UVRPC_OK) {
        g_responses_received++;
        if (g_responses_received >= g_target_responses) {
            g_done = 1;
        }
    }
}

int main(int argc, char** argv) {
    const char* address = (argc > 1) ? argv[1] : "127.0.0.1:5555";
    int iterations = (argc > 2) ? atoi(argv[2]) : 10000;
    
    g_target_responses = iterations;
    
    /* Create loop */
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* Create config */
    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, &loop);
    uvrpc_config_set_address(config, address);
    uvrpc_config_set_transport(config, UVRPC_TRANSPORT_TCP);
    uvrpc_config_set_comm_type(config, UVRPC_COMM_SERVER_CLIENT);
    
    /* Create client */
    uvrpc_client_t* client = uvrpc_client_create(config);
    if (!client) {
        fprintf(stderr, "Failed to create client\n");
        return 1;
    }
    
    /* Connect */
    int ret = uvrpc_client_connect_with_callback(client, on_connect, NULL);
    if (ret != UVRPC_OK) {
        fprintf(stderr, "Connect failed: %d\n", ret);
        return 1;
    }

    /* Wait for connection */
    int conn_wait = 0;
    while (!g_connected && conn_wait < 100) {
        uv_run(&loop, UV_RUN_ONCE);
        conn_wait++;
        if (conn_wait % 10 == 0) {
            fprintf(stderr, "Waiting for connection... (%d/100)\n", conn_wait);
        }
    }

    if (!g_connected) {
        fprintf(stderr, "Connection timeout after %d iterations\n", conn_wait);
        return 1;
    }
    
    /* Performance test */
    int32_t params[2] = {10, 20};
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    /* Send requests in batches */
    int sent = 0;
    while (sent < iterations) {
        int batch_end = (sent + 100 < iterations) ? sent + 100 : iterations;
        
        /* Send a batch of requests */
        for (int i = sent; i < batch_end; i++) {
            int ret = uvrpc_client_call(client, "add", (uint8_t*)params, sizeof(params), on_response, NULL);
            if (ret != UVRPC_OK) {
                fprintf(stderr, "Call failed: %d\n", ret);
            }
        }
        sent = batch_end;
        
        /* Run event loop once to allow data to be sent */
        uv_run(&loop, UV_RUN_ONCE);
    }
    
    /* Wait for all remaining responses */
    int resp_wait = 0;
    while (!g_done && g_responses_received < iterations && resp_wait < 10000) {
        uv_run(&loop, UV_RUN_ONCE);
        resp_wait++;
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);

    double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    double ops = iterations / elapsed;

    printf("Iterations: %d\n", iterations);
    printf("Time: %.3f s\n", elapsed);
    printf("Throughput: %.0f ops/s\n", ops);
    printf("Success rate: %.1f%%\n", (g_responses_received * 100.0) / iterations);
    fflush(stdout);

    /* Cleanup */
    uvrpc_client_free(client);
    uvrpc_config_free(config);
    uv_loop_close(&loop);

    return 0;
}
