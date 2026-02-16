/**
 * UVBus Working Example - Demonstrates how to use UVBus independently
 * This example shows a simple echo server and client using UVBus directly
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>
#include "../include/uvbus.h"

/* Server-side callbacks */
void server_recv(const uint8_t* data, size_t size, void* client_ctx, void* server_ctx) {
    printf("[SERVER] Received %zu bytes\n", size);
    if (size > 0 && data) {
        printf("[SERVER] Data: %.*s\n", (int)size, (char*)data);
        
        /* Echo back to client */
        uvbus_t* server = (uvbus_t*)server_ctx;
        if (server && client_ctx) {
            uvbus_send_to(server, data, size, client_ctx);
            printf("[SERVER] Echoed back\n");
        }
    }
}

void server_error(uvbus_error_t error_code, const char* error_msg, void* ctx) {
    printf("[SERVER ERROR] %d - %s\n", error_code, error_msg ? error_msg : "Unknown");
}

/* Client-side callbacks */
void client_recv(const uint8_t* data, size_t size, void* client_ctx, void* server_ctx) {
    printf("[CLIENT] Received %zu bytes\n", size);
    if (size > 0 && data) {
        printf("[CLIENT] Data: %.*s\n", (int)size, (char*)data);
    }
}

void client_connect(uvbus_error_t status, void* ctx) {
    if (status == UVBUS_OK) {
        printf("[CLIENT] Connected successfully\n");
    } else {
        printf("[CLIENT] Connection failed: %d\n", status);
    }
}

void client_error(uvbus_error_t error_code, const char* error_msg, void* ctx) {
    printf("[CLIENT ERROR] %d - %s\n", error_code, error_msg ? error_msg : "Unknown");
}

int main() {
    printf("UVBus Working Example\n");
    printf("=====================\n\n");
    
    /* Create event loop */
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* ========== SERVER SETUP ========== */
    printf("Creating server...\n");
    uvbus_config_t* server_config = uvbus_config_new();
    uvbus_config_set_loop(server_config, &loop);
    uvbus_config_set_transport(server_config, UVBUS_TRANSPORT_TCP);
    uvbus_config_set_address(server_config, "tcp://127.0.0.1:7878");
    uvbus_config_set_recv_callback(server_config, server_recv, NULL);
    uvbus_config_set_error_callback(server_config, server_error, NULL);
    
    uvbus_t* server = uvbus_server_new(server_config);
    if (!server) {
        printf("Failed to create server\n");
        uvbus_config_free(server_config);
        uv_loop_close(&loop);
        return 1;
    }
    
    printf("Server created, starting to listen...\n");
    uvbus_error_t listen_result = uvbus_listen(server);
    if (listen_result != UVBUS_OK) {
        printf("Failed to listen: %d\n", listen_result);
        uvbus_free(server);
        uvbus_config_free(server_config);
        uv_loop_close(&loop);
        return 1;
    }
    printf("Server is listening on tcp://127.0.0.1:7878\n\n");
    
    /* Run loop briefly to let server start */
    for (int i = 0; i < 10; i++) {
        uv_run(&loop, UV_RUN_ONCE);
        uv_sleep(10);
    }
    
    /* ========== CLIENT SETUP ========== */
    printf("Creating client...\n");
    uvbus_config_t* client_config = uvbus_config_new();
    uvbus_config_set_loop(client_config, &loop);
    uvbus_config_set_transport(client_config, UVBUS_TRANSPORT_TCP);
    uvbus_config_set_address(client_config, "tcp://127.0.0.1:7878");
    uvbus_config_set_recv_callback(client_config, client_recv, NULL);
    uvbus_config_set_connect_callback(client_config, client_connect, NULL);
    uvbus_config_set_error_callback(client_config, client_error, NULL);
    
    uvbus_t* client = uvbus_client_new(client_config);
    if (!client) {
        printf("Failed to create client\n");
        uvbus_free(server);
        uvbus_config_free(server_config);
        uvbus_config_free(client_config);
        uv_loop_close(&loop);
        return 1;
    }
    
    printf("Client created, connecting...\n");
    uvbus_connect(client);
    
    /* Wait for connection */
    int iterations = 0;
    while (!uvbus_is_connected(client) && iterations < 100) {
        uv_run(&loop, UV_RUN_ONCE);
        iterations++;
        uv_sleep(10);
    }
    
    if (!uvbus_is_connected(client)) {
        printf("Connection timeout\n");
        uvbus_free(client);
        uvbus_free(server);
        uvbus_config_free(client_config);
        uvbus_config_free(server_config);
        uv_loop_close(&loop);
        return 1;
    }
    printf("Client connected!\n\n");
    
    /* ========== SEND TEST MESSAGE ========== */
    const char* test_msg = "Hello UVBus!";
    printf("Sending message: %s\n", test_msg);
    uvbus_send(client, (const uint8_t*)test_msg, strlen(test_msg));
    
    /* Run loop to process events */
    for (int i = 0; i < 20; i++) {
        uv_run(&loop, UV_RUN_ONCE);
        uv_sleep(10);
    }
    
    /* ========== CLEANUP ========== */
    printf("\nCleaning up...\n");
    uvbus_free(client);
    uvbus_free(server);
    uvbus_config_free(client_config);
    uvbus_config_free(server_config);
    
    uv_loop_close(&loop);
    
    printf("Example completed successfully!\n");
    return 0;
}