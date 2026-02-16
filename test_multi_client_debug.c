/**
 * Debug test for multi-client connection issue
 */

#include "include/uvrpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_connected_count = 0;
static int g_target_clients = 0;

void on_connect(int status, void* ctx) {
    int client_id = *(int*)ctx;
    if (status == 0) {
        g_connected_count++;
        printf("Client %d connected successfully (total: %d/%d)\n",
               client_id, g_connected_count, g_target_clients);
    } else {
        fprintf(stderr, "Client %d connection failed: %d\n", client_id, status);
    }
}

int main(int argc, char** argv) {
    const char* address = (argc > 1) ? argv[1] : "127.0.0.1:5555";
    int num_clients = (argc > 2) ? atoi(argv[2]) : 5;

    g_target_clients = num_clients;

    printf("Testing %d client connections to %s\n", num_clients, address);

    /* Create loop */
    uv_loop_t loop;
    uv_loop_init(&loop);

    /* Create multiple clients sharing the same loop */
    uvrpc_client_t* clients[100];
    int client_ids[100];

    for (int i = 0; i < num_clients; i++) {
        /* Create config */
        uvrpc_config_t* config = uvrpc_config_new();
        uvrpc_config_set_loop(config, &loop);
        uvrpc_config_set_address(config, address);

        /* Create client */
        clients[i] = uvrpc_client_create(config);
        if (!clients[i]) {
            fprintf(stderr, "Failed to create client %d\n", i);
            return 1;
        }

        client_ids[i] = i;

        /* Connect */
        printf("Connecting client %d...\n", i);
        int ret = uvrpc_client_connect_with_callback(clients[i], on_connect, &client_ids[i]);
        if (ret != UVRPC_OK) {
            fprintf(stderr, "Connect failed for client %d: %d\n", i, ret);
            return 1;
        }
    }

    /* Wait for all connections */
    int conn_wait = 0;
    printf("Waiting for all connections...\n");
    while (g_connected_count < g_target_clients && conn_wait < 1000) {
        uv_run(&loop, UV_RUN_ONCE);
        conn_wait++;
        if (conn_wait % 10 == 0) {
            printf("Waiting... %d/%d connected (%d iterations)\n",
                   g_connected_count, g_target_clients, conn_wait);
        }
    }

    printf("\n=== Connection Results ===\n");
    printf("Target clients: %d\n", g_target_clients);
    printf("Connected clients: %d\n", g_connected_count);
    printf("Wait iterations: %d\n", conn_wait);

    if (g_connected_count == g_target_clients) {
        printf("SUCCESS: All clients connected!\n");
    } else {
        printf("FAILURE: Only %d/%d clients connected\n", g_connected_count, g_target_clients);
    }

    /* Check each client's is_connected status */
    printf("\n=== Client Status ===\n");
    for (int i = 0; i < num_clients; i++) {
        int is_connected = 0;
        /* This would require access to client->is_connected, but it's private */
        /* For now, we'll just report based on g_connected_count */
        printf("Client %d: (status check not accessible)\n", i);
    }

    /* Cleanup */
    for (int i = 0; i < num_clients; i++) {
        uvrpc_client_free(clients[i]);
    }
    uv_loop_close(&loop);

    return (g_connected_count == g_target_clients) ? 0 : 1;
}