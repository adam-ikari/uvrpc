/**
 * UVBus Simple Test - Test UVBus with single event loop
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>
#include "../include/uvbus.h"

static int g_server_running = 1;
static int g_client_connected = 0;
static int g_client_received = 0;
static uvbus_t* g_server = NULL;
static uvbus_t* g_client = NULL;

/* Server receive callback */
void server_recv(const uint8_t* data, size_t size, void* client_ctx, void* server_ctx) {
    printf("[SERVER] Received %zu bytes from client\n", size);
    if (size > 0 && data) {
        printf("[SERVER] Data: %.*s\n", (int)size, (char*)data);
        
        /* Echo back to client */
        if (g_server && client_ctx) {
            uvbus_send_to(g_server, data, size, client_ctx);
            printf("[SERVER] Echoed back to client\n");
        }
    }
}

/* Server error callback */
void server_error(uvbus_error_t error_code, const char* error_msg, void* ctx) {
    printf("[SERVER ERROR] %d - %s\n", error_code, error_msg ? error_msg : "Unknown error");
}

/* Client receive callback */
void client_recv(const uint8_t* data, size_t size, void* client_ctx, void* server_ctx) {
    (void)client_ctx;
    (void)server_ctx;
    printf("[CLIENT] Received %zu bytes from server\n", size);
    if (size > 0 && data) {
        printf("[CLIENT] Data: %.*s\n", (int)size, (char*)data);
        g_client_received = 1;
    }
}

/* Client connect callback */
void client_connect(uvbus_error_t status, void* ctx) {
    if (status == UVBUS_OK) {
        printf("[CLIENT] Connected successfully\n");
        g_client_connected = 1;
    } else {
        printf("[CLIENT] Connection failed with error: %d\n", status);
    }
}

/* Client error callback */
void client_error(uvbus_error_t error_code, const char* error_msg, void* ctx) {
    printf("[CLIENT ERROR] %d - %s\n", error_code, error_msg ? error_msg : "Unknown error");
}

int main() {
    printf("UVBus Simple Test\n");
    printf("==================\n\n");
    
    /* Create single event loop */
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* Create server configuration */
    uvbus_config_t* server_config = uvbus_config_new();
    uvbus_config_set_loop(server_config, &loop);
    uvbus_config_set_transport(server_config, UVBUS_TRANSPORT_TCP);
    uvbus_config_set_address(server_config, "tcp://127.0.0.1:8888");
    uvbus_config_set_recv_callback(server_config, server_recv, server_config);
    uvbus_config_set_error_callback(server_config, server_error, server_config);
    
    /* Create server */
    g_server = uvbus_server_new(server_config);
    if (!g_server) {
        printf("[SERVER] Failed to create server\n");
        uvbus_config_free(server_config);
        uv_loop_close(&loop);
        return 1;
    }
    
    /* Start listening */
    uvbus_error_t listen_result = uvbus_listen(g_server);
    if (listen_result != UVBUS_OK) {
        printf("[SERVER] Failed to start listening: %d\n", listen_result);
        uvbus_free(g_server);
        uvbus_config_free(server_config);
        uv_loop_close(&loop);
        return 1;
    }
    
    printf("[SERVER] Created and listening on tcp://127.0.0.1:8888\n");
    printf("[SERVER] is_active=%d, is_connected=%d\n", g_server->is_active, uvbus_is_connected(g_server));
    
    /* Run loop longer to let server start and be ready to accept connections */
    for (int i = 0; i < 50; i++) {
        uv_run(&loop, UV_RUN_ONCE);
        uv_sleep(10);
    }
    printf("[SERVER] Server should be ready now, is_active=%d\n", g_server->is_active);
    
    /* Create client configuration */
    uvbus_config_t* client_config = uvbus_config_new();
    uvbus_config_set_loop(client_config, &loop);
    uvbus_config_set_transport(client_config, UVBUS_TRANSPORT_TCP);
    uvbus_config_set_address(client_config, "tcp://127.0.0.1:8888");
    uvbus_config_set_recv_callback(client_config, client_recv, NULL);
    uvbus_config_set_connect_callback(client_config, client_connect, NULL);
    uvbus_config_set_error_callback(client_config, client_error, NULL);
    
    /* Create client */
    g_client = uvbus_client_new(client_config);
    if (!g_client) {
        printf("[CLIENT] Failed to create client\n");
        g_server_running = 0;
        uvbus_free(g_server);
        uvbus_config_free(server_config);
        uvbus_config_free(client_config);
        uv_loop_close(&loop);
        return 1;
    }
    
    printf("[CLIENT] Created, connecting to server...\n");
    
    /* Connect to server */
    uvbus_connect(g_client);
    
    /* Wait for connection */
    int iterations = 0;
    while (!g_client_connected && iterations < 100) {
        uv_run(&loop, UV_RUN_ONCE);
        iterations++;
        uv_sleep(10);
        if (iterations % 10 == 0) {
            printf("[CLIENT] Waiting for connection... (%d/100)\n", iterations);
            fflush(stdout);
        }
    }
    
    if (!g_client_connected) {
        printf("[TEST] Connection timeout\n");
        g_server_running = 0;
        uvbus_free(g_server);
        uvbus_free(g_client);
        uvbus_config_free(server_config);
        uvbus_config_free(client_config);
        uv_loop_close(&loop);
        return 1;
    }
    
    /* Send test message */
    const char* test_msg = "Hello from UVBus client!";
    printf("[CLIENT] Sending test message: %s\n", test_msg);
    uvbus_send(g_client, (const uint8_t*)test_msg, strlen(test_msg));
    
    /* Wait for echo response */
    iterations = 0;
    while (!g_client_received && iterations < 100) {
        uv_run(&loop, UV_RUN_ONCE);
        iterations++;
        uv_sleep(10);
        if (iterations % 10 == 0) {
            printf("[CLIENT] Waiting for response... (%d/100)\n", iterations);
            fflush(stdout);
        }
    }
    
    if (!g_client_received) {
        printf("[TEST] Response timeout\n");
    } else {
        printf("[TEST] Successfully received echo response!\n");
    }
    
    /* Cleanup */
    printf("\n[TEST] Cleaning up...\n");
    g_server_running = 0;
    
    uvbus_free(g_client);
    uvbus_free(g_server);
    uvbus_config_free(client_config);
    uvbus_config_free(server_config);
    
    /* Run loop to cleanup */
    for (int i = 0; i < 10; i++) {
        uv_run(&loop, UV_RUN_ONCE);
        uv_sleep(10);
    }
    
    uv_loop_close(&loop);
    
    printf("[TEST] Test completed\n");
    return g_client_received ? 0 : 1;
}