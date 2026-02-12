/**
 * UVRPC Pipeline Server - High Throughput Test
 * Uses PAIR1 mode for bidirectional pipeline
 */

#include "../include/uvrpc.h"
#include <nng/nng.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

static volatile int g_running = 1;

void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
}

int main(int argc, char** argv) {
    const char* addr = (argc > 1) ? argv[1] : "tcp://127.0.0.1:12346";
    
    printf("UVRPC Pipeline Server\n");
    printf("Address: %s\n\n", addr);
    
    /* Setup signal handling */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    /* Initialize NNG */
    nng_init_params params;
    memset(&params, 0, sizeof(params));
    nng_init(&params);
    
    /* Open PAIR1 socket */
    nng_socket sock;
    if (nng_pair1_open(&sock) != 0) {
        fprintf(stderr, "Failed to open socket\n");
        return 1;
    }
    
    /* Listen */
    printf("Starting server...\n");
    nng_listener listener;
    if (nng_listener_create(&listener, sock, addr) != 0) {
        fprintf(stderr, "Failed to create listener\n");
        nng_socket_close(sock);
        return 1;
    }
    
    if (nng_listener_start(listener, 0) != 0) {
        fprintf(stderr, "Failed to start listener\n");
        nng_listener_close(listener);
        nng_socket_close(sock);
        return 1;
    }
    
    printf("Server listening on %s\n", addr);
    printf("Press Ctrl+C to stop\n\n");
    
    /* Main loop - process messages */
    while (g_running) {
        nng_msg* msg = NULL;
        
        /* Blocking receive - more efficient than non-blocking with sleep */
        if (nng_recvmsg(sock, &msg, 0) == 0) {
            /* Echo back the message */
            nng_sendmsg(sock, msg, 0);
        }
    }
    
    printf("\nShutting down server...\n");
    
    /* Cleanup */
    nng_listener_close(listener);
    nng_socket_close(sock);
    
    printf("Server stopped\n");
    return 0;
}