/**
 * @file test_sameloop.c
 * @brief Test Same-Loop Transport (Zero-Lock, Zero-Copy)
 */

#include "../include/uvbus.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int server_received = 0;
static int client_received = 0;

void server_recv(const uint8_t* data, size_t size, void* client_ctx, void* server_ctx) {
    printf("[SERVER] Received %zu bytes: %.*s\n", size, (int)size, (char*)data);
    server_received++;

    /* Echo back to all clients */
    uvbus_t* server = (uvbus_t*)server_ctx;
    if (server) {
        printf("[SERVER] Echoing back to clients...\n");
        uvbus_send(server, data, size);
        printf("[SERVER] Echo sent\n");
    }
}

void client_recv(const uint8_t* data, size_t size, void* client_ctx, void* server_ctx) {
    printf("[CLIENT] Received %zu bytes: %.*s\n", size, (int)size, (char*)data);
    client_received++;
}

int main() {
    uvbus_t* server = NULL;
    uvbus_t* client = NULL;
    uvbus_error_t err;
    
    /* SAMELOOP requires same event loop instance */
    uv_loop_t loop = {0};
    uv_loop_init(&loop);
    
    const char* address = "sameloop://test_service";

    printf("=== Same-Loop Transport Test ===\n");
    printf("Address: %s\n\n", address);

    /* Create server */
    printf("[MAIN] Creating server...\n");
    uvbus_config_t* server_config = uvbus_config_new();
    uvbus_config_set_loop(server_config, &loop);
    uvbus_config_set_address(server_config, address);
    uvbus_config_set_transport(server_config, UVBUS_TRANSPORT_SAMELOOP);
    uvbus_config_set_recv_callback(server_config, server_recv, NULL);

    server = uvbus_server_new(server_config);
    uvbus_config_free(server_config);

    if (!server) {
        printf("[MAIN] Failed to create server\n");
        uv_loop_close(&loop);
        return 1;
    }

    printf("[MAIN] Server created successfully\n");
    
    /* Set callback context after server is created */
    server->transport->callback_ctx = server;
    
    /* Listen on the server */
    err = uvbus_listen(server);
    if (err != UVBUS_OK) {
        printf("[MAIN] Failed to listen server: %d\n", err);
        uvbus_free(server);
        uv_loop_close(&loop);
        return 1;
    }
    
    server->is_active = 1;
    
    printf("\n[MAIN] Creating client...\n");
    uvbus_config_t* client_config = uvbus_config_new();
    uvbus_config_set_loop(client_config, &loop);
    uvbus_config_set_address(client_config, address);
    uvbus_config_set_transport(client_config, UVBUS_TRANSPORT_SAMELOOP);
    uvbus_config_set_recv_callback(client_config, client_recv, NULL);

    client = uvbus_client_new(client_config);
    uvbus_config_free(client_config);

    if (!client) {
        printf("[MAIN] Failed to create client\n");
        uvbus_free(server);
        uv_loop_close(&loop);
        return 1;
    }

    printf("[MAIN] Client created successfully\n");
    
    /* Set callback context after client is created */
    client->transport->callback_ctx = client;
    
    /* Connect the client */
    err = uvbus_connect(client);
    if (err != UVBUS_OK) {
        printf("[MAIN] Failed to connect client: %d\n", err);
        uvbus_free(client);
        uvbus_free(server);
        uv_loop_close(&loop);
        return 1;
    }
    
    client->is_active = 1;

    /* Send data */
    const char* msg = "Hello from Same-Loop Transport!";
    printf("[MAIN] Sending: %s\n", msg);
    
    uvbus_send(client, (const uint8_t*)msg, strlen(msg));
    
    printf("[MAIN] Checking results...\n");
    
    /* Cleanup */
    printf("\n[MAIN] Cleanup...\n");
    uvbus_free(client);
    uvbus_free(server);
    uv_loop_close(&loop);

    printf("\n=== Test Results ===\n");
    printf("Server received: %d\n", server_received);
    printf("Client received: %d\n", client_received);

    if (server_received == 1 && client_received == 1) {
        printf("\n=== SAMELOOP Test PASSED ===\n");
        return 0;
    } else {
        printf("\n=== SAMELOOP Test FAILED ===\n");
        return 1;
    }
}