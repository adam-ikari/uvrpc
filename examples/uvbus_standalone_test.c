/**
 * UVBus Standalone Test - Complete test of UVBus without UVRPC
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>
#include "../include/uvbus.h"

static int g_server_running = 1;
static int g_client_connected = 0;
static int g_client_received = 0;

/* Server receive callback */
void server_recv(const uint8_t* data, size_t size, void* client_ctx, void* server_ctx) {
    printf("[SERVER] Received %zu bytes from client\n", size);
    if (size > 0 && data) {
        printf("[SERVER] Data: %.*s\n", (int)size, (char*)data);
        
        /* Echo back to client */
        uvbus_t* server = (uvbus_t*)server_ctx;
        if (server && client_ctx) {
            uvbus_send_to(server, data, size, client_ctx);
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

/* Server thread function */
void server_thread(void* arg) {
    uv_loop_t* loop = (uv_loop_t*)arg;
    
    /* Create server configuration */
    uvbus_config_t* config = uvbus_config_new();
    uvbus_config_set_loop(config, loop);
    uvbus_config_set_transport(config, UVBUS_TRANSPORT_TCP);
    uvbus_config_set_address(config, "tcp://127.0.0.1:7777");
    uvbus_config_set_recv_callback(config, server_recv, config);
    uvbus_config_set_error_callback(config, server_error, config);
    
    /* Create server */
    uvbus_t* server = uvbus_server_new(config);
    if (!server) {
        printf("[SERVER] Failed to create server\n");
        uvbus_config_free(config);
        return;
    }
    
    printf("[SERVER] Created and listening on tcp://127.0.0.1:7777\n");
    
    /* Run event loop */
    while (g_server_running) {
        uv_run(loop, UV_RUN_ONCE);
    }
    
    /* Cleanup */
    printf("[SERVER] Shutting down...\n");
    uvbus_free(server);
    uvbus_config_free(config);
}

int main() {
    printf("UVBus Standalone Test\n");
    printf("======================\n\n");
    
    /* Create server loop */
    uv_loop_t server_loop;
    uv_loop_init(&server_loop);
    
    /* Start server in background */
    printf("Starting server...\n");
    uv_thread_t server_tid;
    uv_thread_create(&server_tid, server_thread, &server_loop);
    uv_sleep(500);  /* Wait for server to start */
    
    /* Create client loop */
    uv_loop_t client_loop;
    uv_loop_init(&client_loop);
    
    /* Create client configuration */
    uvbus_config_t* client_config = uvbus_config_new();
    uvbus_config_set_loop(client_config, &client_loop);
    uvbus_config_set_transport(client_config, UVBUS_TRANSPORT_TCP);
    uvbus_config_set_address(client_config, "tcp://127.0.0.1:7777");
    uvbus_config_set_recv_callback(client_config, client_recv, NULL);
    uvbus_config_set_connect_callback(client_config, client_connect, NULL);
    uvbus_config_set_error_callback(client_config, client_error, NULL);
    
    /* Create client */
    uvbus_t* client = uvbus_client_new(client_config);
    if (!client) {
        printf("[CLIENT] Failed to create client\n");
        g_server_running = 0;
        uv_thread_join(&server_tid);
        uv_loop_close(&server_loop);
        uvbus_config_free(client_config);
        uv_loop_close(&client_loop);
        return 1;
    }
    
    printf("[CLIENT] Created, connecting to server...\n");
    
    /* Connect to server */
    printf("[CLIENT] Calling uvbus_connect...\n");
    uvbus_error_t conn_result = uvbus_connect(client);
    printf("[CLIENT] uvbus_connect returned: %d\n", conn_result);
    
    /* Wait for connection */
    int iterations = 0;
    while (!g_client_connected && iterations < 50) {
        uv_run(&client_loop, UV_RUN_ONCE);
        iterations++;
        uv_sleep(10);
    }
    
    if (!g_client_connected) {
        printf("[TEST] Connection timeout\n");
        g_server_running = 0;
        uv_thread_join(&server_tid);
        uv_loop_close(&server_loop);
        uvbus_free(client);
        uvbus_config_free(client_config);
        uv_loop_close(&client_loop);
        return 1;
    }
    
    /* Send test message */
    const char* test_msg = "Hello from UVBus client!";
    printf("[CLIENT] Sending test message: %s\n", test_msg);
    uvbus_send(client, (const uint8_t*)test_msg, strlen(test_msg));
    
    /* Wait for echo response */
    iterations = 0;
    while (!g_client_received && iterations < 50) {
        uv_run(&client_loop, UV_RUN_ONCE);
        iterations++;
        uv_sleep(10);
    }
    
    if (!g_client_received) {
        printf("[TEST] Response timeout\n");
    } else {
        printf("[TEST] Successfully received echo response!\n");
    }
    
    /* Cleanup */
    printf("\n[TEST] Cleaning up...\n");
    g_server_running = 0;
    uv_thread_join(&server_tid);
    
    uvbus_free(client);
    uvbus_config_free(client_config);
    uv_loop_close(&client_loop);
    uv_loop_close(&server_loop);
    
    printf("[TEST] Test completed\n");
    return g_client_received ? 0 : 1;
}
