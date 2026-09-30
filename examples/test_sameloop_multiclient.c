/**
 * @file test_sameloop_multiclient.c
 * @brief Test SAMELOOP with multiple clients to verify proper multi-client support
 */

#include "../include/uvbus.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define NUM_CLIENTS 5
#define MESSAGES_PER_CLIENT 10
static int server_messages[NUM_CLIENTS] = {0};
static int client_messages[NUM_CLIENTS] = {0};

/* The recv callback's context is whatever the config registered, and the config
 * is built before the bus exists -- so it cannot be the bus handle. This example
 * used to read the handle back out of a context it had passed as NULL, which
 * silently disabled the server's echo and every client's counting, and the
 * example reported zero messages received while exiting 0-worthy "OK" lines.
 *
 * The server keeps its handle in a file-scope pointer (assigned once the bus
 * exists, read when a message arrives); each client registers its own index as
 * its context, which is exactly what the field is for. */
void server_recv(const uint8_t* data, size_t size, void* client_ctx, void* server_ctx) {
    (void)client_ctx;

    /* Parse client ID from message */
    int client_id = -1;
    if (size >= 4) {
        client_id = data[0] - '0';
    }

    if (client_id >= 0 && client_id < NUM_CLIENTS) {
        server_messages[client_id]++;
        printf("[SERVER] Received from client %d: %.*s (count: %d)\n",
               client_id, (int)size, (char*)data, server_messages[client_id]);
    }

    /* Echo back to all clients. The recv context is a pointer to a slot in main
     * that holds the bus handle; the transport copies that slot's address and
     * delivers it here, so the handle comes through the context. */
    uvbus_t* server = *(uvbus_t**)server_ctx;
    if (server) {
        const char* response = "Server broadcast";
        uvbus_send(server, (const uint8_t*)response, strlen(response));
    }
}

void client_recv(const uint8_t* data, size_t size, void* client_ctx, void* server_ctx) {
    (void)server_ctx;
    /* Each client registered its own index as the recv context. */
    int client_id = (int)(intptr_t)client_ctx;
    if (client_id >= 0 && client_id < NUM_CLIENTS) {
        client_messages[client_id]++;
        printf("[CLIENT-%d] Received: %.*s (count: %d)\n",
               client_id, (int)size, (char*)data, client_messages[client_id]);
    }
}

int main() {
    uv_loop_t loop = {0};
    uv_loop_init(&loop);
    
    const char* address = "sameloop://multiclient_test";

    printf("=== SAMELOOP Multi-Client Test ===\n");
    printf("Address: %s\n", address);
    printf("Number of clients: %d\n", NUM_CLIENTS);
    printf("Messages per client: %d\n\n", MESSAGES_PER_CLIENT);

    /* recv-ctx slot, filled once the bus exists (see server_recv). */
    uvbus_t* server_slot = NULL;

    /* Create server */
    printf("[MAIN] Creating server...\n");
    uvbus_config_t* server_config = uvbus_config_new();
    uvbus_config_set_loop(server_config, &loop);
    /* INPROC and SAMELOOP peers meet through a registry the caller owns. */
    uvbus_loop_registry_t* registry = uvbus_loop_registry_new();

    uvbus_config_set_loop_registry(server_config, registry);
    uvbus_config_set_address(server_config, address);
    uvbus_config_set_transport(server_config, UVBUS_TRANSPORT_SAMELOOP);
    uvbus_config_set_recv_callback(server_config, server_recv, &server_slot);

    uvbus_t* server = uvbus_server_new(server_config);
    uvbus_config_free(server_config);

    if (!server) {
        printf("[MAIN] Failed to create server\n");
        uvbus_loop_registry_free(registry);
        uv_loop_close(&loop);
        return 1;
    }

    printf("[MAIN] Server created successfully\n");
    server->is_active = 1;
    
    /* Fill the slot the recv callback reads (see the ctx note in server_recv). */
    server_slot = server;

    /* Listen on server */
    uvbus_error_t err = uvbus_listen(server);
    if (err != UVBUS_OK) {
        printf("[MAIN] Failed to listen: %d\n", err);
        uvbus_free(server);
        uv_loop_close(&loop);
        return 1;
    }
    printf("[MAIN] Server listening\n");

    /* Create multiple clients */
    uvbus_t* clients[NUM_CLIENTS];
    for (int i = 0; i < NUM_CLIENTS; i++) {
        printf("[MAIN] Creating client %d...\n", i);
        
        uvbus_config_t* client_config = uvbus_config_new();
        uvbus_config_set_loop(client_config, &loop);
        uvbus_config_set_loop_registry(client_config, registry);
        uvbus_config_set_address(client_config, address);
        uvbus_config_set_transport(client_config, UVBUS_TRANSPORT_SAMELOOP);
        uvbus_config_set_recv_callback(client_config, client_recv, (void*)(intptr_t)i);

        clients[i] = uvbus_client_new(client_config);
        uvbus_config_free(client_config);

        if (!clients[i]) {
            printf("[MAIN] Failed to create client %d\n", i);
            for (int j = 0; j < i; j++) {
                uvbus_free(clients[j]);
            }
            uvbus_free(server);
            uv_loop_close(&loop);
            return 1;
        }

        clients[i]->is_active = 1;
        
        
        /* Connect client */
        uvbus_error_t err = uvbus_connect(clients[i]);
        if (err != UVBUS_OK) {
            printf("[MAIN] Failed to connect client %d: %d\n", i, err);
        }
    }

    printf("[MAIN] All %d clients created successfully\n\n", NUM_CLIENTS);

    /* Send messages from each client */
    printf("[MAIN] Sending messages from all clients...\n\n");
    
    for (int i = 0; i < NUM_CLIENTS; i++) {
        char message[128];
        for (int j = 0; j < MESSAGES_PER_CLIENT; j++) {
            /* The server reads the client id from the first byte, so it has to
             * lead the payload. */
            snprintf(message, sizeof(message), "%d: message %d", i, j);
            uvbus_send(clients[i], (const uint8_t*)message, strlen(message));
        }
    }

    /* Run the loop to process messages */
    printf("[MAIN] Running event loop...\n");
    uv_run(&loop, UV_RUN_DEFAULT);

    /* Cleanup */
    printf("\n[MAIN] Cleanup...\n");
    for (int i = 0; i < NUM_CLIENTS; i++) {
        uvbus_free(clients[i]);
    }
    uvbus_free(server);
    uv_loop_close(&loop);

    /* Report results */
    printf("\n=== Test Results ===\n");
    printf("Server received messages per client:\n");
    int total_server = 0;
    for (int i = 0; i < NUM_CLIENTS; i++) {
        printf("  Client %d: %d messages\n", i, server_messages[i]);
        total_server += server_messages[i];
    }
    
    printf("\nClient received messages:\n");
    int total_client = 0;
    for (int i = 0; i < NUM_CLIENTS; i++) {
        printf("  Client %d: %d messages\n", i, client_messages[i]);
        total_client += client_messages[i];
    }
    
    printf("\nTotal server received: %d\n", total_server);
    printf("Total client received: %d\n", total_client);
    printf("Expected: %d messages per client\n", MESSAGES_PER_CLIENT);
    
    /* Verify results */
    int success = 1;
    for (int i = 0; i < NUM_CLIENTS; i++) {
        if (server_messages[i] != MESSAGES_PER_CLIENT) {
            printf("✗ Client %d: Expected %d, got %d\n", i, MESSAGES_PER_CLIENT, server_messages[i]);
            success = 0;
        }
    }
    
    if (success && total_server == NUM_CLIENTS * MESSAGES_PER_CLIENT) {
        printf("\n✓ SUCCESS: Multi-client test passed!\n");
        printf("  All clients successfully sent and received messages\n");
        return 0;
    } else {
        printf("\n✗ FAILURE: Some messages were lost\n");
        return 1;
    }
}
