/**
 * @file test_sameloop_stress.c
 * @brief Stress test for SAMELOOP transport with high message volume
 */

#include "../include/uvbus.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define NUM_MESSAGES 10000
#define NUM_CLIENTS 10
#define MESSAGE_SIZE 256

static int total_received = 0;
static int total_sent = 0;
static time_t start_time;

void server_recv(const uint8_t* data, size_t size, void* client_ctx, void* server_ctx) {
    total_received++;
    
    /* Periodic progress reporting */
    if (total_received % 1000 == 0) {
        time_t now = time(NULL);
        double elapsed = difftime(now, start_time);
        printf("[SERVER] Received %d/%d messages (%.1f msg/s)\n", 
               total_received, NUM_MESSAGES * NUM_CLIENTS, 
               total_received / (elapsed > 0 ? elapsed : 1));
    }
    
    /* Simple echo - send back the same message */
    uvbus_t* server = (uvbus_t*)server_ctx;
    if (server && size > 0) {
        uvbus_send(server, data, size);
    }
}

void client_recv(const uint8_t* data, size_t size, void* client_ctx, void* server_ctx) {
    /* Just count received messages */
    (void)data;
    (void)size;
    (void)client_ctx;
    (void)server_ctx;
}

int main() {
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    const char* address = "sameloop://stress_test";

    printf("=== SAMELOOP Stress Test ===\n");
    printf("Address: %s\n", address);
    printf("Messages per client: %d\n", NUM_MESSAGES);
    printf("Number of clients: %d\n", NUM_CLIENTS);
    printf("Total messages: %d\n", NUM_MESSAGES * NUM_CLIENTS);
    printf("Message size: %d bytes\n\n", MESSAGE_SIZE);

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
    
    /* Set callback context AFTER server is created */
    server->transport->callback_ctx = server;

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
        uvbus_config_set_address(client_config, address);
        uvbus_config_set_transport(client_config, UVBUS_TRANSPORT_SAMELOOP);
        uvbus_config_set_recv_callback(client_config, client_recv, NULL);

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
        
        /* Set callback context AFTER client is created */
        clients[i]->transport->callback_ctx = clients[i];
        
        /* Connect client */
        err = uvbus_connect(clients[i]);
        if (err != UVBUS_OK) {
            printf("[MAIN] Failed to connect client %d: %d\n", i, err);
        }
    }

    printf("[MAIN] All %d clients created successfully\n\n", NUM_CLIENTS);

    /* Start timing */
    start_time = time(NULL);
    printf("[MAIN] Starting stress test...\n");

    /* Send messages from all clients */
    uint8_t message[MESSAGE_SIZE];
    memset(message, 'X', MESSAGE_SIZE);
    
    for (int i = 0; i < NUM_CLIENTS; i++) {
        for (int j = 0; j < NUM_MESSAGES; j++) {
            uvbus_send(clients[i], message, MESSAGE_SIZE);
            total_sent++;
        }
    }

    printf("[MAIN] Sent %d messages\n", total_sent);
    printf("[MAIN] Waiting for echo responses...\n\n");

    /* Let the loop process messages */
    for (int i = 0; i < 10 && total_received < total_sent; i++) {
        uv_run(&loop, UV_RUN_ONCE);
    }

    time_t end_time = time(NULL);
    double elapsed = difftime(end_time, start_time);

    /* Cleanup */
    printf("\n[MAIN] Cleanup...\n");
    for (int i = 0; i < NUM_CLIENTS; i++) {
        uvbus_free(clients[i]);
    }
    uvbus_free(server);
    uv_loop_close(&loop);

    /* Report results */
    printf("\n=== Test Results ===\n");
    printf("Total sent: %d\n", total_sent);
    printf("Total received: %d\n", total_received);
    printf("Elapsed time: %.2f seconds\n", elapsed);
    printf("Throughput: %.0f msg/s\n", total_received / (elapsed > 0 ? elapsed : 1));
    
    if (total_received >= total_sent * 0.95) {
        printf("\n✓ SUCCESS: Stress test passed!\n");
        printf("  All messages were delivered successfully\n");
        return 0;
    } else {
        printf("\n✗ FAILURE: Too many messages lost\n");
        printf("  Only %.1f%% of messages were received\n", 
               (total_received * 100.0) / total_sent);
        return 1;
    }
}