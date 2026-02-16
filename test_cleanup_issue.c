/**
 * Test to demonstrate cleanup blocking issue
 */

#include "include/uvrpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_connected = 0;

void on_connect(int status, void* ctx) {
    (void)ctx;
    if (status == 0) {
        g_connected = 1;
        printf("Connected\n");
    } else {
        fprintf(stderr, "Connection failed: %d\n", status);
    }
}

int main(int argc, char** argv) {
    const char* address = (argc > 1) ? argv[1] : "tcp://127.0.0.1:5555";
    int num_clients = (argc > 2) ? atoi(argv[2]) : 5;

    printf("Testing cleanup with %d clients\n", num_clients);

    /* Create loop */
    uv_loop_t loop;
    uv_loop_init(&loop);

    /* Create clients */
    uvrpc_client_t* clients[100];
    for (int i = 0; i < num_clients; i++) {
        uvrpc_config_t* config = uvrpc_config_new();
        uvrpc_config_set_loop(config, &loop);
        uvrpc_config_set_address(config, address);

        clients[i] = uvrpc_client_create(config);
        if (!clients[i]) {
            fprintf(stderr, "Failed to create client %d\n", i);
            return 1;
        }

        uvrpc_client_connect_with_callback(clients[i], on_connect, NULL);
    }

    /* Wait for connection */
    int conn_wait = 0;
    while (!g_connected && conn_wait < 100) {
        uv_run(&loop, UV_RUN_ONCE);
        conn_wait++;
    }

    if (!g_connected) {
        fprintf(stderr, "Connection timeout\n");
        return 1;
    }

    printf("All clients connected\n");

    /* Start cleanup timer */
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    printf("Starting cleanup...\n");

    /* Free clients */
    for (int i = 0; i < num_clients; i++) {
        printf("Freeing client %d...\n", i);
        uvrpc_client_free(clients[i]);
    }

    printf("Closing loop...\n");
    int close_result = uv_loop_close(&loop);

    clock_gettime(CLOCK_MONOTONIC, &end);

    double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;

    printf("\n=== Cleanup Results ===\n");
    printf("Number of clients: %d\n", num_clients);
    printf("Cleanup time: %.3f s\n", elapsed);
    printf("Loop close result: %d (%s)\n", close_result,
           close_result == 0 ? "OK" : uv_strerror(close_result));

    if (elapsed > 1.0) {
        printf("WARNING: Cleanup took more than 1 second!\n");
        printf("This indicates uv_loop_close is blocking on closing handles.\n");
    }

    return 0;
}