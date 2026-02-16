/**
 * UVRPC IPC Performance Test - Fixed
 */

#include "../include/uvrpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Echo handler */
void echo_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    uvrpc_request_send_response(req, UVRPC_OK, req->params, req->params_size);
}

/* Global state for client test */
static int requests_completed = 0;
static int target_requests = 10000;

void callback(uvrpc_response_t* resp, void* ctx) {
    (void)ctx;
    requests_completed++;
    if (requests_completed % 1000 == 0) {
        printf(".");
        fflush(stdout);
    }
}

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("Usage: %s <server|client> <ipc_address>\n", argv[0]);
        printf("Example: %s server ipc:///tmp/uvrpc_ipc.sock\n", argv[0]);
        return 1;
    }
    
    const char* mode = argv[1];
    const char* address = argv[2];
    
    printf("=== UVRPC IPC Performance Test ===\n");
    printf("Mode: %s\n", mode);
    printf("Address: %s\n\n", address);
    
    if (strcmp(mode, "server") == 0) {
        /* Server mode */
        uv_loop_t loop;
        uv_loop_init(&loop);
        
        uvrpc_config_t* config = uvrpc_config_new();
        uvrpc_config_set_loop(config, &loop);
        uvrpc_config_set_address(config, address);  /* Auto-detect transport */
        
        printf("Server transport type: %d\n", config->transport);
        
        uvrpc_server_t* server = uvrpc_server_create(config);
        if (!server) {
            fprintf(stderr, "Failed to create server\n");
            return 1;
        }
        
        uvrpc_server_register(server, "echo", echo_handler, NULL);
        
        printf("Starting IPC server...\n");
        int result = uvrpc_server_start(server);
        if (result != UVRPC_OK) {
            fprintf(stderr, "Failed to start server: %d\n", result);
            return 1;
        }
        
        printf("Server running! Press Ctrl+C to stop.\n");
        uv_run(&loop, UV_RUN_DEFAULT);
        
        uvrpc_server_free(server);
        uvrpc_config_free(config);
        uv_loop_close(&loop);
        
    } else if (strcmp(mode, "client") == 0) {
        /* Client mode */
        uv_loop_t loop;
        uv_loop_init(&loop);
        
        uvrpc_config_t* config = uvrpc_config_new();
        uvrpc_config_set_loop(config, &loop);
        uvrpc_config_set_address(config, address);  /* Auto-detect transport */
        
        printf("Client transport type: %d\n", config->transport);
        
        uvrpc_client_t* client = uvrpc_client_create(config);
        if (!client) {
            fprintf(stderr, "Failed to create client\n");
            return 1;
        }
        
        printf("Connecting to server...\n");
        int result = uvrpc_client_connect(client);
        if (result != UVRPC_OK) {
            fprintf(stderr, "Failed to connect: %d\n", result);
            return 1;
        }
        
        printf("Connected! Starting performance test...\n\n");
        
        struct timeval start, end;
        gettimeofday(&start, NULL);
        
        /* Send requests */
        int32_t value = 42;
        for (int i = 0; i < target_requests; i++) {
            uvrpc_client_call(client, "echo", (uint8_t*)&value, sizeof(value), callback, NULL);
            
            /* Run event loop periodically */
            if (i % 100 == 0) {
                uv_run(&loop, UV_RUN_NOWAIT);
            }
        }
        
        /* Wait for all responses */
        while (requests_completed < target_requests) {
            uv_run(&loop, UV_RUN_ONCE);
            usleep(1000);  /* 1ms sleep */
        }
        
        gettimeofday(&end, NULL);
        
        double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;
        double throughput = target_requests / elapsed;
        
        printf("\n\n=== IPC Performance Results ===\n");
        printf("Total requests: %d\n", target_requests);
        printf("Completed: %d\n", requests_completed);
        printf("Time: %.3f seconds\n", elapsed);
        printf("Throughput: %.0f ops/s\n", throughput);
        printf("Success rate: %.1f%%\n", (requests_completed * 100.0) / target_requests);
        printf("Average latency: %.3f ms\n", (elapsed * 1000.0) / target_requests);
        
        uvrpc_client_free(client);
        uvrpc_config_free(config);
        uv_loop_close(&loop);
        
    } else {
        fprintf(stderr, "Invalid mode: %s\n", mode);
        return 1;
    }
    
    return 0;
}
