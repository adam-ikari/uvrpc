/**
 * UVRPC Pipeline Client - High Throughput Test
 * Uses PAIR1 mode for bidirectional pipeline (no strict request-response correlation)
 */

#include "../include/uvrpc.h"
#include <nng/nng.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int main(int argc, char** argv) {
    const char* addr = (argc > 1) ? argv[1] : "tcp://127.0.0.1:12346";
    int num_requests = (argc > 2) ? atoi(argv[2]) : 100000;
    int payload_size = (argc > 3) ? atoi(argv[3]) : 64;
    
    printf("UVRPC Pipeline Client\n");
    printf("Address: %s\n", addr);
    printf("Requests: %d\n", num_requests);
    printf("Payload: %d bytes\n\n", payload_size);
    
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
    
    /* Dial */
    printf("Connecting to server...\n");
    nng_dialer dialer;
    if (nng_dialer_create(&dialer, sock, addr) != 0) {
        fprintf(stderr, "Failed to create dialer\n");
        nng_socket_close(sock);
        return 1;
    }
    
    if (nng_dialer_start(dialer, 0) != 0) {
        fprintf(stderr, "Failed to connect\n");
        nng_dialer_close(dialer);
        nng_socket_close(sock);
        return 1;
    }
    
    printf("Connected to server\n\n");
    
    /* Prepare test data */
    uint8_t* test_data = malloc(payload_size);
    memset(test_data, 'A', payload_size);
    
    /* Warmup */
    printf("Warming up...\n");
    for (int i = 0; i < 10; i++) {
        nng_msg* msg;
        nng_msg_alloc(&msg, payload_size);
        memcpy(nng_msg_body(msg), test_data, payload_size);
        nng_sendmsg(sock, msg, 0);
        
        nng_msg* reply;
        nng_recvmsg(sock, &reply, 0);
        nng_msg_free(reply);
    }
    
    printf("Starting benchmark...\n");
    
    /* Benchmark - Pipeline mode: send all, then receive all */
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    /* Send all requests first (pipeline) */
    for (int i = 0; i < num_requests; i++) {
        nng_msg* msg;
        nng_msg_alloc(&msg, payload_size);
        memcpy(nng_msg_body(msg), test_data, payload_size);
        nng_sendmsg(sock, msg, 0);
    }
    
    /* Receive all responses */
    int received = 0;
    while (received < num_requests) {
        nng_msg* reply;
        if (nng_recvmsg(sock, &reply, 0) == 0) {
            nng_msg_free(reply);
            received++;
        }
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    double elapsed = (end.tv_sec - start.tv_sec) * 1000.0 + 
                     (end.tv_nsec - start.tv_nsec) / 1000000.0;
    double throughput = num_requests / (elapsed / 1000.0);
    
    printf("\n========== Results ==========\n");
    printf("Total time: %.2f ms\n", elapsed);
    printf("Received: %d / %d\n", received, num_requests);
    printf("Throughput: %.0f ops/s\n", throughput);
    printf("Avg latency: %.3f ms\n", elapsed / num_requests);
    printf("=============================\n");
    
    /* Cleanup */
    free(test_data);
    nng_dialer_close(dialer);
    nng_socket_close(sock);
    
    return 0;
}
