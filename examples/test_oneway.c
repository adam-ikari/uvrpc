/**
 * UVRPC Oneway RPC Test
 * Tests Oneway RPC mode (fire-and-forget, no response)
 */

#include "../include/uvrpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

volatile int g_connected = 0;
volatile int g_request_count = 0;

/* Connection callback */
void on_connect(int status, void* ctx) {
    (void)ctx;
    if (status == 0) {
        g_connected = 1;
        printf("[CLIENT] Connected successfully\n");
    } else {
        fprintf(stderr, "[CLIENT] Connection failed: %d\n", status);
    }
}

/* Simple handler for oneway calls */
void log_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    
    if (req->params_size >= 8) {
        int32_t a = *(int32_t*)req->params;
        int32_t b = *(int32_t*)(req->params + 4);
        printf("[HANDLER] Received oneway request: %d + %d (no response sent)\n", a, b);
        g_request_count++;
    }
    
    /* Do NOT send response for oneway requests */
}

int main(int argc, char** argv) {
    const char* address = (argc > 1) ? argv[1] : "tcp://127.0.0.1:14141";
    
    printf("[MAIN] UVRPC Oneway RPC Test\n");
    printf("[MAIN] Address: %s\n\n", address);
    
    /* Create loop */
    uv_loop_t loop;
    if (uv_loop_init(&loop) != 0) {
        fprintf(stderr, "[MAIN] Failed to init loop\n");
        return 1;
    }
    
    /* Create server configuration */
    uvrpc_config_t* server_config = uvrpc_config_new();
    uvrpc_config_set_loop(server_config, &loop);
    uvrpc_config_set_address(server_config, address);
    
    /* Create server */
    uvrpc_server_t* server = uvrpc_server_create(server_config);
    if (!server) {
        fprintf(stderr, "[MAIN] Failed to create server\n");
        uvrpc_config_free(server_config);
        return 1;
    }
    
    /* Register handler */
    uvrpc_server_register(server, "log", log_handler, NULL);
    
    /* Start server */
    if (uvrpc_server_start(server) != UVRPC_OK) {
        fprintf(stderr, "[MAIN] Failed to start server\n");
        uvrpc_server_free(server);
        uvrpc_config_free(server_config);
        return 1;
    }
    
    printf("[MAIN] Server started\n");
    
    /* Create client configuration */
    uvrpc_config_t* client_config = uvrpc_config_new();
    uvrpc_config_set_loop(client_config, &loop);
    uvrpc_config_set_address(client_config, address);
    
    /* Create client */
    uvrpc_client_t* client = uvrpc_client_create(client_config);
    if (!client) {
        fprintf(stderr, "[MAIN] Failed to create client\n");
        uvrpc_config_free(client_config);
        uvrpc_server_free(server);
        return 1;
    }
    
    /* Connect with callback */
    if (uvrpc_client_connect_with_callback(client, on_connect, NULL) != UVRPC_OK) {
        fprintf(stderr, "[MAIN] Failed to connect\n");
        uvrpc_client_free(client);
        uvrpc_config_free(client_config);
        uvrpc_server_free(server);
        return 1;
    }
    
    /* Wait for connection */
    int iterations = 0;
    while (!g_connected && iterations < 50) {
        uv_run(&loop, UV_RUN_ONCE);
        iterations++;
    }
    
    if (!g_connected) {
        fprintf(stderr, "[MAIN] Connection timeout\n");
        uvrpc_client_disconnect(client);
        uvrpc_client_free(client);
        uvrpc_config_free(client_config);
        uvrpc_server_free(server);
        return 1;
    }
    
    printf("[MAIN] Client connected\n\n");
    
    /* Send 10 oneway requests */
    printf("[MAIN] Sending 10 oneway requests...\n");
    for (int i = 0; i < 10; i++) {
        int32_t params[2] = {i, i * 2};
        
        int ret = uvrpc_client_call_oneway(client, "log", 
                                            (uint8_t*)params, sizeof(params));
        
        if (ret == UVRPC_OK) {
            printf("[MAIN] Oneway request #%d sent (no response expected)\n", i + 1);
        } else {
            fprintf(stderr, "[MAIN] Failed to send oneway request #%d: %d\n", i + 1, ret);
        }
        
        /* Small delay between requests */
        usleep(10000);  // 10ms
    }
    
    printf("\n[MAIN] All oneway requests sent\n");
    
    /* Run event loop for a while to process all requests */
    printf("[MAIN] Running event loop for 1 second...\n");
    for (int i = 0; i < 100; i++) {
        uv_run(&loop, UV_RUN_ONCE);
        usleep(10000);  // 10ms
    }
    
    /* Cleanup */
    printf("\n[MAIN] Shutting down...\n");
    printf("[MAIN] Server handled %d oneway requests\n", g_request_count);
    uvrpc_client_disconnect(client);
    uvrpc_client_free(client);
    uvrpc_server_free(server);
    uvrpc_config_free(client_config);
    uvrpc_config_free(server_config);
    uv_loop_close(&loop);
    
    printf("[MAIN] Done\n");
    return 0;
}