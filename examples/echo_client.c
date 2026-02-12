/**
 * Improved UVRPC Test Client
 */

#include "../include/uvrpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Response callback */
void response_callback(int status, const uint8_t* data, size_t size, void* ctx) {
    (void)status;
    (void)data;
    (void)size;
    (void)ctx;
    /* Process response */
}

int main(int argc, char** argv) {
    const char* addr = (argc > 1) ? argv[1] : "tcp://127.0.0.1:5555";
    int num_requests = (argc > 2) ? atoi(argv[2]) : 10000;
    int payload_size = (argc > 3) ? atoi(argv[3]) : 64;
    
    printf("UVRPC Test Client\n");
    printf("Address: %s\n", addr);
    printf("Requests: %d\n", num_requests);
    printf("Payload: %d bytes\n\n", payload_size);
    
    /* Create config */
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, &loop);
    uvrpc_config_set_address(config, addr);
    
    /* Create client */
    uvrpc_client_t* client = uvrpc_client_create(config);
    if (!client) {
        fprintf(stderr, "Failed to create client\n");
        return 1;
    }
    
    /* Connect */
    if (uvrpc_client_connect(client) != 0) {
        fprintf(stderr, "Failed to connect\n");
        uvrpc_client_free(client);
        uvrpc_config_free(config);
        return 1;
    }
    
    printf("Connected to server\n\n");
    
    /* Prepare test data */
    uint8_t* test_data = malloc(payload_size);
    memset(test_data, 'A', payload_size);
    
    /* Warmup */
    printf("Warming up...\n");
    for (int i = 0; i < 100; i++) {
        uvrpc_client_call(client, "echo", "echo", test_data, payload_size, response_callback, NULL);
        uv_run(&loop, UV_RUN_ONCE);
    }
    
    printf("Starting benchmark...\n");
    
    /* Benchmark */
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    for (int i = 0; i < num_requests; i++) {
        uvrpc_client_call(client, "echo", "echo", test_data, payload_size, response_callback, NULL);
        uv_run(&loop, UV_RUN_ONCE);
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    double elapsed = (end.tv_sec - start.tv_sec) * 1000.0 + 
                     (end.tv_nsec - start.tv_nsec) / 1000000.0;
    double throughput = num_requests / (elapsed / 1000.0);
    
    printf("\n========== Results ==========\n");
    printf("Total time: %.2f ms\n", elapsed);
    printf("Throughput: %.0f ops/s\n", throughput);
    printf("Avg latency: %.3f ms\n", elapsed / num_requests);
    printf("=============================\n");
    
    /* Cleanup */
    free(test_data);
    uvrpc_client_disconnect(client);
    uvrpc_client_free(client);
    uvrpc_config_free(config);
    uv_loop_close(&loop);
    
    return 0;
}
