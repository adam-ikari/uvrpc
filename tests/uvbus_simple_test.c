/**
 * @file uvbus_simple_test.c
 * @brief Simple UVBus test to verify basic functionality
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <uv.h>
#include "../include/uvbus.h"

static int server_recv_count = 0;
static int client_recv_count = 0;

static void server_recv(const uint8_t* data, size_t size, void* client_ctx, void* server_ctx) {
    server_recv_count++;
    printf("Server received: %zu bytes\n", size);
}

static void client_recv(const uint8_t* data, size_t size, void* client_ctx, void* server_ctx) {
    client_recv_count++;
    printf("Client received: %zu bytes\n", size);
}

int main(void) {
    uv_loop_t loop;
    uv_loop_init(&loop);

    /* Create server */
    uvbus_config_t* server_config = uvbus_config_new();
    uvbus_config_set_loop(server_config, &loop);
    uvbus_config_set_transport(server_config, UVBUS_TRANSPORT_TCP);
    uvbus_config_set_address(server_config, "tcp://127.0.0.1:15557");
    uvbus_config_set_recv_callback(server_config, server_recv, NULL);

    uvbus_t* server = uvbus_server_new(server_config);
    if (!server) {
        printf("ERROR: Failed to create server\n");
        return 1;
    }
    printf("Server created\n");

    uvbus_error_t result = uvbus_listen(server);
    if (result != UVBUS_OK) {
        printf("ERROR: Failed to listen: %d\n", result);
        return 1;
    }
    printf("Server listening\n");

    /* Create client */
    uvbus_config_t* client_config = uvbus_config_new();
    uvbus_config_set_loop(client_config, &loop);
    uvbus_config_set_transport(client_config, UVBUS_TRANSPORT_TCP);
    uvbus_config_set_address(client_config, "tcp://127.0.0.1:15557");
    uvbus_config_set_recv_callback(client_config, client_recv, NULL);

    uvbus_t* client = uvbus_client_new(client_config);
    if (!client) {
        printf("ERROR: Failed to create client\n");
        return 1;
    }
    printf("Client created\n");

    result = uvbus_connect(client);
    if (result != UVBUS_OK) {
        printf("ERROR: Failed to connect: %d\n", result);
    }
    printf("Client connecting...\n");

    /* Wait for connection (UV_RUN_NOWAIT: non-blocking pump + sleep, so a live
     * TCP connection does not make uv_run block forever). */
    for (int i = 0; i < 100; i++) {
        uv_run(&loop, UV_RUN_NOWAIT);
        usleep(10000);
    }

    if (!uvbus_is_connected(client)) {
        printf("WARNING: Client not connected after 1 second\n");
    } else {
        printf("Client connected\n");
    }

    /* Send data */
    const char* msg = "Hello UVBus!";
    result = uvbus_client_send(client, (const uint8_t*)msg, strlen(msg));
    if (result != UVBUS_OK) {
        printf("ERROR: Failed to send: %d\n", result);
    } else {
        printf("Data sent\n");
    }

    /* Wait for response */
    for (int i = 0; i < 100; i++) {
        uv_run(&loop, UV_RUN_NOWAIT);
        usleep(10000);
    }

    printf("\nResults:\n");
    printf("Server received: %d\n", server_recv_count);
    printf("Client received: %d\n", client_recv_count);

    /* Cleanup */
    uvbus_free(client);
    uvbus_free(server);
    uvbus_config_free(client_config);
    uvbus_config_free(server_config);

    uv_loop_close(&loop);

    printf("Test complete\n");
    return 0;
}