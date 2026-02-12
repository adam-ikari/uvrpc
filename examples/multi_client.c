/**
 * UVRPC Multi-Client Concurrent Test
 * Tests single-threaded server with multiple concurrent clients
 */

#include "../include/uvrpc.h"
#include <nng/nng.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include "../src/uvrpc_msgpack.h"

typedef struct {
    int client_id;
    const char* address;
    int num_requests;
    int payload_size;
    int received;
    double elapsed_ms;
} client_thread_data_t;

void* client_thread(void* arg) {
    client_thread_data_t* data = (client_thread_data_t*)arg;
    
    /* Initialize NNG */
    nng_init_params params = {0};
    nng_init(&params);
    
    /* Open REQ socket */
    nng_socket sock;
    if (nng_req0_open(&sock) != 0) {
        data->received = 0;
        data->elapsed_ms = 0;
        return NULL;
    }
    
    /* Connect */
    nng_dialer dialer;
    if (nng_dialer_create(&dialer, sock, data->address) != 0) {
        nng_socket_close(sock);
        data->received = 0;
        data->elapsed_ms = 0;
        return NULL;
    }
    
    if (nng_dialer_start(dialer, 0) != 0) {
        nng_dialer_close(dialer);
        nng_socket_close(sock);
        data->received = 0;
        data->elapsed_ms = 0;
        return NULL;
    }
    
    /* Prepare test data */
    uint8_t* test_data = malloc(data->payload_size);
    memset(test_data, 'A', data->payload_size);
    
    /* Warmup */
    for (int i = 0; i < 5; i++) {
        size_t packed_size = 0;
        char* packed = uvrpc_pack_request("echo", "echo", test_data, data->payload_size, &packed_size);
        if (packed) {
            nng_msg* msg;
            nng_msg_alloc(&msg, packed_size);
            memcpy(nng_msg_body(msg), packed, packed_size);
            nng_sendmsg(sock, msg, 0);
            free(packed);
            
            nng_msg* reply;
            nng_recvmsg(sock, &reply, 0);
            nng_msg_free(reply);
        }
    }
    
    /* Benchmark */
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    data->received = 0;
    for (int i = 0; i < data->num_requests; i++) {
        size_t packed_size = 0;
        char* packed = uvrpc_pack_request("echo", "echo", test_data, data->payload_size, &packed_size);
        if (!packed) continue;
        
        nng_msg* msg;
        nng_msg_alloc(&msg, packed_size);
        memcpy(nng_msg_body(msg), packed, packed_size);
        free(packed);
        
        if (nng_sendmsg(sock, msg, 0) == 0) {
            nng_msg* reply;
            if (nng_recvmsg(sock, &reply, 0) == 0) {
                nng_msg_free(reply);
                data->received++;
            }
        } else {
            nng_msg_free(msg);
        }
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    data->elapsed_ms = (end.tv_sec - start.tv_sec) * 1000.0 + 
                      (end.tv_nsec - start.tv_nsec) / 1000000.0;
    
    /* Cleanup */
    free(test_data);
    nng_dialer_close(dialer);
    nng_socket_close(sock);
    
    return NULL;
}

int main(int argc, char** argv) {
    int num_clients = (argc > 1) ? atoi(argv[1]) : 5;
    int requests_per_client = (argc > 2) ? atoi(argv[2]) : 2000;
    int payload_size = (argc > 3) ? atoi(argv[3]) : 64;
    const char* addr = (argc > 4) ? argv[4] : "tcp://127.0.0.1:12348";
    
    printf("UVRPC Multi-Client Concurrent Test\n");
    printf("Clients: %d\n", num_clients);
    printf("Requests per client: %d\n", requests_per_client);
    printf("Total requests: %d\n", num_clients * requests_per_client);
    printf("Payload: %d bytes\n", payload_size);
    printf("Server: %s\n\n", addr);
    
    /* Allocate thread data */
    client_thread_data_t* thread_data = calloc(num_clients, sizeof(client_thread_data_t));
    pthread_t* threads = calloc(num_clients, sizeof(pthread_t));
    
    for (int i = 0; i < num_clients; i++) {
        thread_data[i].client_id = i;
        thread_data[i].address = addr;
        thread_data[i].num_requests = requests_per_client;
        thread_data[i].payload_size = payload_size;
        thread_data[i].received = 0;
        thread_data[i].elapsed_ms = 0;
    }
    
    printf("Starting clients...\n");
    
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    /* Create all threads */
    for (int i = 0; i < num_clients; i++) {
        pthread_create(&threads[i], NULL, client_thread, &thread_data[i]);
    }
    
    /* Wait for all threads */
    for (int i = 0; i < num_clients; i++) {
        pthread_join(threads[i], NULL);
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    /* Calculate results */
    double total_elapsed_ms = (end.tv_sec - start.tv_sec) * 1000.0 + 
                              (end.tv_nsec - start.tv_nsec) / 1000000.0;
    
    int total_received = 0;
    double total_client_time = 0;
    
    printf("\n========== Results ==========\n");
    printf("Client | Received | Time (ms) | Throughput (ops/s)\n");
    printf("-------|----------|-----------|-------------------\n");
    
    for (int i = 0; i < num_clients; i++) {
        total_received += thread_data[i].received;
        total_client_time += thread_data[i].elapsed_ms;
        double throughput = thread_data[i].received / (thread_data[i].elapsed_ms / 1000.0);
        printf("%6d | %8d | %9.2f | %18.0f\n", 
               thread_data[i].client_id, 
               thread_data[i].received, 
               thread_data[i].elapsed_ms, 
               throughput);
    }
    
    printf("\nTotal:\n");
    printf("  Elapsed time: %.2f ms\n", total_elapsed_ms);
    printf("  Total received: %d / %d\n", total_received, num_clients * requests_per_client);
    printf("  Total throughput: %.0f ops/s\n", total_received / (total_elapsed_ms / 1000.0));
    printf("  Avg client throughput: %.0f ops/s\n", 
           (total_received / num_clients) / (total_client_time / (num_clients * 1000.0)));
    printf("=============================\n");
    
    /* Cleanup */
    free(thread_data);
    free(threads);
    
    return 0;
}