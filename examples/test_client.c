/**
 * Experimental UVRPC Client
 * Testing NNG + msgpack performance
 */

#include <nng/nng.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

int main(int argc, char** argv) {
    const char* addr = (argc > 1) ? argv[1] : "tcp://127.0.0.1:5555";
    int num_requests = (argc > 2) ? atoi(argv[2]) : 10000;
    int payload_size = (argc > 3) ? atoi(argv[3]) : 64;
    
    printf("UVRPC Experimental Client\n");
    printf("Address: %s\n", addr);
    printf("Requests: %d\n", num_requests);
    printf("Payload: %d bytes\n\n", payload_size);
    
    if (init_nng() != 0) {
        fprintf(stderr, "Failed to initialize NNG\n");
        return 1;
    }
    
    /* Open REQ socket */
    nng_socket sock;
    if (nng_req0_open(&sock) != 0) {
        fprintf(stderr, "Failed to open socket\n");
        return 1;
    }
    
    /* Create dialer */
    nng_dialer dialer;
    if (nng_dialer_create(&dialer, sock, addr) != 0) {
        fprintf(stderr, "Failed to create dialer\n");
        nng_socket_close(sock);
        return 1;
    }
    
    /* Start dialer */
    if (nng_dialer_start(dialer, 0) != 0) {
        fprintf(stderr, "Failed to start dialer\n");
        nng_dialer_close(dialer);
        nng_socket_close(sock);
        return 1;
    }
    
    /* Prepare test data */
    uint8_t* test_data = malloc(payload_size);
    memset(test_data, 'A', payload_size);
    
    printf("Warming up...\n");
    for (int i = 0; i < 100; i++) {
        nng_msg* msg = NULL;
        nng_msg_alloc(&msg, payload_size);
        memcpy(nng_msg_body(msg), test_data, payload_size);
        nng_sendmsg(sock, msg, 0);
        nng_recvmsg(sock, &msg, 0);
        nng_msg_free(msg);
    }
    
    printf("Starting benchmark...\n");
    
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    for (int i = 0; i < num_requests; i++) {
        nng_msg* msg = NULL;
        nng_msg_alloc(&msg, payload_size);
        memcpy(nng_msg_body(msg), test_data, payload_size);
        
        if (nng_sendmsg(sock, msg, 0) != 0) {
            nng_msg_free(msg);
            continue;
        }
        
        nng_msg* reply = NULL;
        if (nng_recvmsg(sock, &reply, 0) != 0) {
            continue;
        }
        
        nng_msg_free(reply);
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    double elapsed = (end.tv_sec - start.tv_sec) * 1000.0 + 
                     (end.tv_nsec - start.tv_nsec) / 1000000.0;
    double throughput = num_requests / (elapsed / 1000.0);
    
    printf("\n========== Results ==========\n");
    printf("Total time: %.2f ms\n", elapsed);
    printf("Throughput: %.0f ops/s\n", throughput);
    printf("Avg latency: %.3f ms\n", elapsed / num_requests);
    printf("=============================\n");
    
    free(test_data);
    nng_dialer_close(dialer);
    nng_socket_close(sock);
    
    return 0;
}