/**
 * Simple Latency Test - Direct connection without performance mode
 */

#include "../include/uvrpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static volatile int g_done = 0;
static volatile int g_connected = 0;
static struct timespec g_start_time, g_end_time;
static int64_t g_latency_ns = 0;

void on_connect(int status, void* ctx) {
    (void)ctx;
    if (status == 0) {
        g_connected = 1;
        printf("Connected!\n");
    } else {
        fprintf(stderr, "Connection failed: %d\n", status);
    }
}

void on_response(uvrpc_response_t* resp, void* ctx) {
    (void)ctx;
    if (resp->status == UVRPC_OK) {
        clock_gettime(CLOCK_MONOTONIC, &g_end_time);
        g_latency_ns = (g_end_time.tv_sec - g_start_time.tv_sec) * 1000000000LL +
                      (g_end_time.tv_nsec - g_start_time.tv_nsec);
        printf("Response received!\n");
        g_done = 1;
    }
}

int main(void) {
    /* Create loop */
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* Create config */
    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, &loop);
    uvrpc_config_set_address(config, "127.0.0.1:5555");
    uvrpc_config_set_transport(config, UVRPC_TRANSPORT_TCP);
    uvrpc_config_set_comm_type(config, UVRPC_COMM_SERVER_CLIENT);
    
    /* Create client */
    uvrpc_client_t* client = uvrpc_client_create(config);
    if (!client) {
        fprintf(stderr, "Failed to create client\n");
        return 1;
    }
    
    /* Connect */
    printf("Connecting...\n");
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
        fprintf(stderr, "Connection timeout\n");
        return 1;
    }
    
    printf("Testing latency...\n");
    int32_t params[2] = {10, 20};
    
    /* Warm up */
    uv_run(&loop, UV_RUN_ONCE);
    
    /* Measure latency */
    printf("Sending request...\n");
    clock_gettime(CLOCK_MONOTONIC, &g_start_time);
    ret = uvrpc_client_call(client, "add", (uint8_t*)params, sizeof(params), on_response, NULL);
    if (ret != UVRPC_OK) {
        fprintf(stderr, "Call failed: %d\n", ret);
        return 1;
    }
    printf("Request sent, waiting...\n");
    
    /* Wait for response */
    int resp_wait = 0;
    while (!g_done && resp_wait < 10000) {
        uv_run(&loop, UV_RUN_ONCE);
        resp_wait++;
        if (resp_wait % 1000 == 0) {
            printf("Still waiting... (%d/10000)\n", resp_wait);
        }
    }
    
    if (!g_done) {
        fprintf(stderr, "Timeout!\n");
        return 1;
    }

    double latency_us = g_latency_ns / 1000.0;
    printf("\n=== Result ===\n");
    printf("Latency: %.3f us\n", latency_us);
    printf("Latency (ns): %lld\n", (long long)g_latency_ns);

    /* Cleanup */
    uvrpc_client_free(client);
    uvrpc_config_free(config);
    uv_loop_close(&loop);

    return 0;
}