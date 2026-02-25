/**
 * @file test_sameloop_recursion.c
 * @brief Test SAMELOOP stack overflow prevention with recursive callbacks
 */

#include "../include/uvbus.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int message_count = 0;
static int max_recursion_depth = 0;
static int current_recursion_depth = 0;
static const int MAX_MESSAGES = 100;

void server_recv(const uint8_t* data, size_t size, void* client_ctx, void* server_ctx) {
    current_recursion_depth++;
    if (current_recursion_depth > max_recursion_depth) {
        max_recursion_depth = current_recursion_depth;
    }
    
    message_count++;
    printf("[SERVER] Message #%d, Recursion depth: %d, Data: %.*s\n", 
           message_count, current_recursion_depth, (int)size, (char*)data);
    
    /* Echo back to client - this creates potential for stack overflow */
    if (message_count < MAX_MESSAGES) {
        uvbus_t* server = (uvbus_t*)server_ctx;
        if (server) {
            const char* response = "Echo response";
            uvbus_send(server, (const uint8_t*)response, strlen(response));
        }
    }
    
    current_recursion_depth--;
}

void client_recv(const uint8_t* data, size_t size, void* client_ctx, void* server_ctx) {
    current_recursion_depth++;
    if (current_recursion_depth > max_recursion_depth) {
        max_recursion_depth = current_recursion_depth;
    }
    
    printf("[CLIENT] Recursion depth: %d, Data: %.*s\n", 
           current_recursion_depth, (int)size, (char*)data);
    
    /* Echo back to server - creates bidirectional communication */
    uvbus_t* client = (uvbus_t*)client_ctx;
    if (client && message_count < MAX_MESSAGES) {
        const char* response = "Client response";
        uvbus_send(client, (const uint8_t*)response, strlen(response));
    }
    
    current_recursion_depth--;
}

int main() {
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    const char* address = "sameloop://test_recursion";

    printf("=== SAMELOOP Recursion Test ===\n");
    printf("Address: %s\n", address);
    printf("Max messages: %d\n\n", MAX_MESSAGES);

    /* Create server */
    printf("[MAIN] Creating server...\n");
    uvbus_config_t* server_config = uvbus_config_new();
    uvbus_config_set_loop(server_config, &loop);
    uvbus_config_set_address(server_config, address);
    uvbus_config_set_transport(server_config, UVBUS_TRANSPORT_SAMELOOP);
    uvbus_config_set_recv_callback(server_config, server_recv, NULL);

    uvbus_t* server = uvbus_server_new(server_config);
    uvbus_config_free(server_config);

    if (!server) {
        printf("[MAIN] Failed to create server\n");
        uv_loop_close(&loop);
        return 1;
    }

    printf("[MAIN] Server created successfully\n");
    server->is_active = 1;
    
    /* Create client */
    printf("[MAIN] Creating client...\n");
    uvbus_config_t* client_config = uvbus_config_new();
    uvbus_config_set_loop(client_config, &loop);
    uvbus_config_set_address(client_config, address);
    uvbus_config_set_transport(client_config, UVBUS_TRANSPORT_SAMELOOP);
    uvbus_config_set_recv_callback(client_config, client_recv, NULL);

    uvbus_t* client = uvbus_client_new(client_config);
    uvbus_config_free(client_config);

    if (!client) {
        printf("[MAIN] Failed to create client\n");
        uvbus_free(server);
        uv_loop_close(&loop);
        return 1;
    }

    printf("[MAIN] Client created successfully\n");
    client->is_active = 1;

    /* Send initial message */
    const char* msg = "Initial message";
    printf("[MAIN] Sending initial message...\n\n");
    
    uvbus_send(client, (const uint8_t*)msg, strlen(msg));
    
    /* Cleanup */
    printf("\n[MAIN] Cleanup...\n");
    uvbus_free(client);
    uvbus_free(server);
    uv_loop_close(&loop);

    printf("\n=== Test Results ===\n");
    printf("Total messages: %d\n", message_count);
    printf("Max recursion depth: %d\n", max_recursion_depth);
    
    if (max_recursion_depth < 10) {
        printf("\n✓ SUCCESS: Stack overflow prevention working!\n");
        printf("  Recursion depth kept low through message queuing\n");
        return 0;
    } else {
        printf("\n✗ WARNING: High recursion depth detected\n");
        printf("  Stack overflow risk may still exist\n");
        return 1;
    }
}