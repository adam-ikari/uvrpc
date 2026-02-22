/**
 * Simple IPC transport test
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <uv.h>
#include "include/uvbus.h"

static int server_received = 0;
static int client_received = 0;
static int server_connected = 0;

void server_recv(const uint8_t* data, size_t size, void* client_ctx, void* server_ctx) {
    printf("[SERVER] Received %zu bytes: %.*s\n", size, (int)size, (char*)data);
    server_received++;

    /* Echo back to client */
    uvbus_transport_t* transport = (uvbus_transport_t*)server_ctx;
    if (transport && client_ctx) {
        uvbus_send_to(transport, data, size, client_ctx);
    }
}

void client_recv(const uint8_t* data, size_t size, void* client_ctx, void* server_ctx) {
    printf("[CLIENT] Received %zu bytes: %.*s\n", size, (int)size, (char*)data);
    client_received++;
}

void on_connect(uvbus_error_t status, void* ctx) {
    if (status == UVBUS_OK) {
        printf("[CLIENT] Connected!\n");
    } else {
        printf("[CLIENT] Connect failed: %d\n", status);
    }
}

int main() {
    uv_loop_t loop;
    uv_loop_init(&loop);

    const char* address = "ipc:///tmp/uvrpc_test_simple";

    /* Fork to create server and client */
    pid_t pid = fork();
    if (pid == 0) {
        /* Child - Server */
        printf("[SERVER] Starting on %s\n", address);

        uvbus_config_t* config = uvbus_config_new();
        uvbus_config_set_loop(config, &loop);
        uvbus_config_set_transport(config, UVBUS_TRANSPORT_IPC);
        uvbus_config_set_address(config, address);
        uvbus_config_set_recv_callback(config, server_recv, NULL);

        uvbus_t* server = uvbus_server_new(config);
        uvbus_config_free(config);

        int ret = uvbus_start(server);
        if (ret != UVBUS_OK) {
            printf("[SERVER] Failed to start: %d\n", ret);
            return 1;
        }

        printf("[SERVER] Listening...\n");
        uv_run(&loop, UV_RUN_DEFAULT);

        uvbus_free(server);
        printf("[SERVER] Exiting, received %d messages\n", server_received);
    } else {
        /* Parent - Client */
        sleep(1);  /* Wait for server to start */

        printf("[CLIENT] Connecting to %s\n", address);

        uvbus_config_t* config = uvbus_config_new();
        uvbus_config_set_loop(config, &loop);
        uvbus_config_set_transport(config, UVBUS_TRANSPORT_IPC);
        uvbus_config_set_address(config, address);
        uvbus_config_set_recv_callback(config, client_recv, NULL);
        uvbus_config_set_connect_callback(config, on_connect, NULL);

        uvbus_t* client = uvbus_client_new(config);
        uvbus_config_free(config);

        int ret = uvbus_start(client);
        if (ret != UVBUS_OK) {
            printf("[CLIENT] Failed to start: %d\n", ret);
            kill(pid, SIGTERM);
            return 1;
        }

        /* Wait for connection */
        sleep(1);

        /* Send some data */
        const char* msg = "Hello from client!";
        printf("[CLIENT] Sending: %s\n", msg);
        uvbus_send(client, (const uint8_t*)msg, strlen(msg));

        /* Run for a bit */
        sleep(2);

        printf("[CLIENT] Exiting, received %d messages\n", client_received);

        uvbus_free(client);
        kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);
    }

    uv_loop_close(&loop);
    return 0;
}
