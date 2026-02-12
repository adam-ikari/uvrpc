/**
 * UVRPC Performance Test
 * 测试吞吐量和延迟
 */

#include "../include/uvrpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TOTAL_REQUESTS 100000

static int g_responses_received = 0;
static struct timespec g_start_time, g_end_time;

void echo_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    uvrpc_request_send_response(req, 0, (uint8_t*)"OK", 2);
}

void response_callback(uvrpc_response_t* resp, void* ctx) {
    (void)ctx;
    g_responses_received++;
}

double elapsed_ms(struct timespec start, struct timespec end) {
    return (end.tv_sec - start.tv_sec) * 1000.0 + 
           (end.tv_nsec - start.tv_nsec) / 1000000.0;
}

int main() {
    printf("=== UVRPC Performance Test ===\n\n");
    printf("Test: RPC throughput (%d requests)\n", TOTAL_REQUESTS);
    printf("--------------------------------\n");
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* Server */
    uvrpc_config_t* server_config = uvrpc_config_new();
    uvrpc_config_set_loop(server_config, &loop);
    uvrpc_config_set_address(server_config, "inproc://perf_test");
    
    uvrpc_server_t* server = uvrpc_server_create(server_config);
    uvrpc_server_register(server, "echo", echo_handler, NULL);
    uvrpc_server_start(server);
    
    /* Client */
    uvrpc_config_t* client_config = uvrpc_config_new();
    uvrpc_config_set_loop(client_config, &loop);
    uvrpc_config_set_address(client_config, "inproc://perf_test");
    
    uvrpc_client_t* client = uvrpc_client_create(client_config);
    uvrpc_client_connect(client);
    
    /* Run loop briefly to establish connection */
    for (int i = 0; i < 10; i++) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }
    
    /* Send requests */
    clock_gettime(CLOCK_MONOTONIC, &g_start_time);
    
    for (int i = 0; i < TOTAL_REQUESTS; i++) {
        uvrpc_client_call(client, "echo", (uint8_t*)"test", 4, response_callback, NULL);
    }
    
    /* Process all responses */
    while (g_responses_received < TOTAL_REQUESTS) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }
    
    clock_gettime(CLOCK_MONOTONIC, &g_end_time);
    
    double total_time = elapsed_ms(g_start_time, g_end_time);
    double throughput = (double)TOTAL_REQUESTS / (total_time / 1000.0);
    double avg_latency = total_time / TOTAL_REQUESTS;
    
    printf("  Total time: %.2f ms\n", total_time);
    printf("  Throughput: %.2f req/sec\n", throughput);
    printf("  Avg latency: %.4f ms\n", avg_latency);
    printf("  Responses: %d/%d\n", g_responses_received, TOTAL_REQUESTS);
    printf("\n");
    
    /* Cleanup */
    uvrpc_client_disconnect(client);
    uvrpc_client_free(client);
    uvrpc_config_free(client_config);
    uvrpc_server_stop(server);
    uvrpc_server_free(server);
    uvrpc_config_free(server_config);
    uv_loop_close(&loop);
    
    printf("=== Test Complete ===\n");
    return 0;
}
