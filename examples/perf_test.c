#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include "../include/uvrpc.h"

static double get_time_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: %s <iterations>\n", argv[0]);
        return 1;
    }
    
    int iterations = atoi(argv[1]);
    if (iterations <= 0) iterations = 100;
    
    printf("=== UVRPC Performance Test ===\n");
    printf("Iterations: %d\n", iterations);
    printf("Address: 127.0.0.1:5555\n\n");
    
    /* Create event loop */
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* Create client */
    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, &loop);
    uvrpc_config_set_address(config, "127.0.0.1:5555");
    
    uvrpc_client_t* client = uvrpc_client_create(config);
    uvrpc_config_free(config);
    
    if (!client) {
        printf("Failed to create client\n");
        return 1;
    }
    
    /* Connect */
    if (uvrpc_client_connect(client) != UVRPC_OK) {
        printf("Failed to connect\n");
        uvrpc_client_free(client);
        return 1;
    }
    
    printf("Connected!\n");
    fflush(stdout);
    
    /* Warm up */
    for (int i = 0; i < 10; i++) {
        int32_t params[2] = {i, i+1};
        uvrpc_client_call(client, "Add", (uint8_t*)params, sizeof(params), NULL, NULL);
        uv_run(&loop, UV_RUN_ONCE);
    }
    
    printf("Warming up complete\n");
    fflush(stdout);
    
    /* Performance test */
    double latencies[iterations];
    int successful = 0;
    int failed = 0;
    
    double start_time = get_time_ms();
    
    for (int i = 0; i < iterations; i++) {
        double req_start = get_time_ms();
        
        int32_t params[2] = {i, i+1};
        uvrpc_client_call(client, "Add", (uint8_t*)params, sizeof(params), NULL, NULL);
        
        uv_run(&loop, UV_RUN_ONCE);
        
        double req_end = get_time_ms();
        latencies[i] = req_end - req_start;
        
        if (latencies[i] > 0) {
            successful++;
        } else {
            failed++;
        }
    }
    
    double end_time = get_time_ms();
    double total_time = end_time - start_time;
    
    /* Calculate statistics */
    double avg_latency = 0;
    for (int i = 0; i < iterations; i++) {
        avg_latency += latencies[i];
    }
    avg_latency /= iterations;
    
    /* Find P50, P95, P99 */
    /* Simple sorting for percentiles */
    for (int i = 0; i < iterations - 1; i++) {
        for (int j = 0; j < iterations - i - 1; j++) {
            if (latencies[j] > latencies[j + 1]) {
                double temp = latencies[j];
                latencies[j] = latencies[j + 1];
                latencies[j + 1] = temp;
            }
        }
    }
    
    double p50 = latencies[iterations / 2];
    double p95 = latencies[(int)(iterations * 0.95)];
    double p99 = latencies[(int)(iterations * 0.99)];
    
    /* Results */
    printf("\n=== Performance Results ===\n");
    printf("Total time: %.2f ms\n", total_time);
    printf("Throughput: %.2f ops/s\n", (iterations / total_time) * 1000.0);
    printf("Success rate: %d/%d (%.1f%%)\n", successful, iterations, (successful * 100.0) / iterations);
    printf("Failed: %d\n", failed);
    printf("\nLatency:\n");
    printf("  Average: %.3f ms\n", avg_latency);
    printf("  P50: %.3f ms\n", p50);
    printf("  P95: %.3f ms\n", p95);
    printf("  P99: %.3f ms\n", p99);
    printf("  Min: %.3f ms\n", latencies[0]);
    printf("  Max: %.3f ms\n", latencies[iterations - 1]);
    
    /* Cleanup */
    uvrpc_client_free(client);
    uv_loop_close(&loop);
    
    printf("\n=== Test Complete ===\n");
    return 0;
}
