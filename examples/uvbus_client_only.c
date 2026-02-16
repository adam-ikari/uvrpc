/**
 * UVBus Client Only - Standalone UVBus client
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>
#include "../include/uvbus.h"

int g_connected = 0;
int g_received = 0;

void client_recv(const uint8_t* data, size_t size, void* client_ctx, void* server_ctx) {
    printf("[CLIENT] Received %zu bytes: %.*s\n", size, (int)size, (char*)data);
    g_received = 1;
}

void client_connect(uvbus_error_t status, void* ctx) {
    if (status == UVBUS_OK) {
        printf("[CLIENT] Connected!\n");
        g_connected = 1;
    } else {
        printf("[CLIENT] Connection failed: %d\n", status);
    }
}

void client_error(uvbus_error_t error_code, const char* error_msg, void* ctx) {
    printf("[CLIENT ERROR] %d - %s\n", error_code, error_msg ? error_msg : "Unknown");
}

int main() {
    printf("UVBus Client\n");
    printf("============\n\n");
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    uvbus_config_t* config = uvbus_config_new();
    uvbus_config_set_loop(config, &loop);
    uvbus_config_set_transport(config, UVBUS_TRANSPORT_TCP);
    uvbus_config_set_address(config, "tcp://127.0.0.1:8989");
    
    /* IMPORTANT: Set callbacks BEFORE creating client */
    uvbus_config_set_recv_callback(config, client_recv, config);
    uvbus_config_set_connect_callback(config, client_connect, config);
    
    uvbus_t* client = uvbus_client_new(config);
    if (!client) {
        printf("Failed to create client\n");
        return 1;
    }
    
    printf("Connecting to server...\n");
    uvbus_connect(client);
    
    /* Wait for connection */
    int iterations = 0;
    while (!g_connected && iterations < 100) {
        uv_run(&loop, UV_RUN_ONCE);
        iterations++;
        uv_sleep(10);
    }
    
    if (!g_connected) {
        printf("Connection timeout\n");
        return 1;
    }
    
    /* Send test message */
    const char* msg = "Hello UVBus!";
    printf("Sending: %s\n", msg);
    uvbus_send(client, (const uint8_t*)msg, strlen(msg));
    
    /* Wait for response */
    iterations = 0;
    while (!g_received && iterations < 100) {
        uv_run(&loop, UV_RUN_ONCE);
        iterations++;
        uv_sleep(10);
    }
    
    if (!g_received) {
        printf("No response received\n");
    } else {
        printf("Test successful!\n");
    }
    
    uvbus_free(client);
    uvbus_config_free(config);
    uv_loop_close(&loop);
    
    return g_received ? 0 : 1;
}