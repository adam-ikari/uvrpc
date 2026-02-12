/**
 * Simple NNG REP server for testing multi-client
 * No UVRPC layer, just raw NNG
 */

#include <nng/nng.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

static volatile int g_running = 1;

void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
}

int main(int argc, char** argv) {
    const char* addr = (argc > 1) ? argv[1] : "tcp://127.0.0.1:5555";
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("Simple NNG REP Server\n");
    printf("Address: %s\n\n", addr);
    
    nng_init_params params = {0};
    nng_init(&params);
    
    nng_socket sock;
    if (nng_rep0_open(&sock) != 0) {
        fprintf(stderr, "Failed to open socket\n");
        return 1;
    }
    
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
    
    printf("Server started. Press Ctrl+C to stop\n\n");
    
    /* Echo server */
    while (g_running) {
        nng_msg* msg = NULL;
        if (nng_recvmsg(sock, &msg, 0) == 0) {
            nng_sendmsg(sock, msg, 0);
        }
    }
    
    printf("\nStopping server...\n");
    nng_listener_close(listener);
    nng_socket_close(sock);
    
    return 0;
}