/**
 * Experimental UVRPC Server
 * Testing NNG + msgpack performance
 */

#include "../include/uvrpc.h"
#include <nng/nng.h>
#include <mpack.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* NNG initialization */
static int g_nng_initialized = 0;

static int init_nng(void) {
    if (g_nng_initialized) return 0;
    
    nng_init_params params;
    memset(&params, 0, sizeof(params));
    params.num_task_threads = 0;
    params.max_task_threads = 0;
    
    if (nng_init(&params) != 0) {
        return -1;
    }
    
    g_nng_initialized = 1;
    return 0;
}

/* Simple echo server */
int main(int argc, char** argv) {
    const char* addr = (argc > 1) ? argv[1] : "tcp://127.0.0.1:5555";
    
    printf("UVRPC Experimental Server\n");
    printf("Address: %s\n\n", addr);
    
    if (init_nng() != 0) {
        fprintf(stderr, "Failed to initialize NNG\n");
        return 1;
    }
    
    /* Open REP socket */
    nng_socket sock;
    if (nng_rep0_open(&sock) != 0) {
        fprintf(stderr, "Failed to open socket\n");
        return 1;
    }
    
    /* Create listener */
    nng_listener listener;
    if (nng_listener_create(&listener, sock, addr) != 0) {
        fprintf(stderr, "Failed to create listener\n");
        nng_socket_close(sock);
        return 1;
    }
    
    /* Start listener */
    if (nng_listener_start(listener, 0) != 0) {
        fprintf(stderr, "Failed to start listener\n");
        nng_listener_close(listener);
        nng_socket_close(sock);
        return 1;
    }
    
    printf("Server started, waiting for requests...\n");
    
    /* Receive and echo loop */
    int count = 0;
    while (1) {
        nng_msg* msg = NULL;
        if (nng_recvmsg(sock, &msg, 0) != 0) {
            continue;
        }
        
        count++;
        
        /* Echo back */
        if (nng_sendmsg(sock, msg, 0) != 0) {
            nng_msg_free(msg);
        }
        
        if (count % 10000 == 0) {
            printf("Processed %d requests\n", count);
        }
    }
    
    nng_listener_close(listener);
    nng_socket_close(sock);
    
    return 0;
}
