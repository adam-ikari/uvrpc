/**
 * Improved UVRPC Test Server
 */

#include "../include/uvrpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

static volatile int g_running = 1;

void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
    printf("\nShutting down...\n");
}

/* Echo handler */
void echo_handler(uvrpc_server_t* server, const char* service, const char* method,
                  const uint8_t* data, size_t size, void* ctx) {
    (void)server;
    (void)service;
    (void)method;
    (void)ctx;
    
    printf("Echo: %zu bytes\n", size);
}

int main(int argc, char** argv) {
    const char* addr = (argc > 1) ? argv[1] : "tcp://127.0.0.1:5555";
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("UVRPC Test Server\n");
    printf("Address: %s\n\n", addr);
    
    /* Create config */
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, &loop);
    uvrpc_config_set_address(config, addr);
    
    /* Create server */
    uvrpc_server_t* server = uvrpc_server_create(config);
    if (!server) {
        fprintf(stderr, "Failed to create server\n");
        return 1;
    }
    
    /* Start server */
    if (uvrpc_server_start(server) != 0) {
        fprintf(stderr, "Failed to start server\n");
        uvrpc_server_free(server);
        uvrpc_config_free(config);
        return 1;
    }
    
    /* Register service */
    uvrpc_server_register(server, "echo", echo_handler, NULL);
    
    printf("Press Ctrl+C to stop\n\n");
    
    /* Run server */
    while (g_running) {
        uv_run(&loop, UV_RUN_ONCE);
        usleep(1000);
    }
    
    /* Cleanup */
    uvrpc_server_free(server);
    uvrpc_config_free(config);
    uv_loop_close(&loop);
    
    printf("Server stopped\n");
    return 0;
}