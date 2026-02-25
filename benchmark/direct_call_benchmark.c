/**
 * Direct Call Benchmark - Pure Function Call Performance
 * 
 * This test measures the absolute minimum overhead of calling a function
 * directly, without any transport layer, serialization, or networking.
 * 
 * Purpose: Establish a baseline to understand the overhead of SAMELOOP vs INPROC
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdint.h>

/* Simple callback type */
typedef void (*callback_t)(void* data, void* ctx);

/* Performance statistics */
typedef struct {
    uint64_t total_calls;
    struct timespec start_time;
    struct timespec end_time;
} perf_stats_t;

/* Simple handler - simulates add operation */
static volatile int32_t g_result = 0;
static void add_handler(void* data, void* ctx) {
    (void)ctx;
    /* Simulate simple operation */
    int32_t* a = (int32_t*)data;
    int32_t* b = (int32_t*)(data + 4);
    g_result = *a + *b;
}

/* Direct call benchmark */
static double benchmark_direct_call(int num_iterations) {
    perf_stats_t stats = {0};
    stats.total_calls = num_iterations;
    
    /* Prepare data */
    int32_t data[2] = {1, 1};
    void* ctx = NULL;
    
    /* Warmup */
    for (int i = 0; i < 1000; i++) {
        add_handler(data, ctx);
    }
    
    /* Start timing */
    clock_gettime(CLOCK_MONOTONIC, &stats.start_time);
    
    /* Direct calls */
    for (int i = 0; i < num_iterations; i++) {
        add_handler(data, ctx);
    }
    
    /* End timing */
    clock_gettime(CLOCK_MONOTONIC, &stats.end_time);
    
    /* Calculate duration */
    double duration_ns = (stats.end_time.tv_sec - stats.start_time.tv_sec) * 1e9 +
                        (stats.end_time.tv_nsec - stats.start_time.tv_nsec);
    double duration_s = duration_ns / 1e9;
    
    return stats.total_calls / duration_s;
}

/* Function pointer call benchmark */
static double benchmark_function_pointer(int num_iterations) {
    perf_stats_t stats = {0};
    stats.total_calls = num_iterations;
    
    /* Prepare data */
    int32_t data[2] = {1, 1};
    void* ctx = NULL;
    callback_t callback = add_handler;
    
    /* Warmup */
    for (int i = 0; i < 1000; i++) {
        callback(data, ctx);
    }
    
    /* Start timing */
    clock_gettime(CLOCK_MONOTONIC, &stats.start_time);
    
    /* Function pointer calls */
    for (int i = 0; i < num_iterations; i++) {
        callback(data, ctx);
    }
    
    /* End timing */
    clock_gettime(CLOCK_MONOTONIC, &stats.end_time);
    
    /* Calculate duration */
    double duration_ns = (stats.end_time.tv_sec - stats.start_time.tv_sec) * 1e9 +
                        (stats.end_time.tv_nsec - stats.start_time.tv_nsec);
    double duration_s = duration_ns / 1e9;
    
    return stats.total_calls / duration_s;
}

/* Indirect call benchmark (simulate transport layer overhead) */
static double benchmark_indirect_call(int num_iterations) {
    perf_stats_t stats = {0};
    stats.total_calls = num_iterations;
    
    /* Prepare data */
    int32_t data[2] = {1, 1};
    void* ctx = NULL;
    callback_t callback = add_handler;
    
    /* Simulate endpoint structure */
    struct endpoint {
        callback_t recv_cb;
        void* callback_ctx;
        int is_active;
    } endpoint = {
        .recv_cb = callback,
        .callback_ctx = ctx,
        .is_active = 1
    };
    
    /* Warmup */
    for (int i = 0; i < 1000; i++) {
        if (endpoint.is_active && endpoint.recv_cb) {
            endpoint.recv_cb(data, endpoint.callback_ctx);
        }
    }
    
    /* Start timing */
    clock_gettime(CLOCK_MONOTONIC, &stats.start_time);
    
    /* Indirect calls with checks */
    for (int i = 0; i < num_iterations; i++) {
        if (endpoint.is_active && endpoint.recv_cb) {
            endpoint.recv_cb(data, endpoint.callback_ctx);
        }
    }
    
    /* End timing */
    clock_gettime(CLOCK_MONOTONIC, &stats.end_time);
    
    /* Calculate duration */
    double duration_ns = (stats.end_time.tv_sec - stats.start_time.tv_sec) * 1e9 +
                        (stats.end_time.tv_nsec - stats.start_time.tv_nsec);
    double duration_s = duration_ns / 1e9;
    
    return stats.total_calls / duration_s;
}

int main(int argc, char** argv) {
    int num_iterations = 10000000;  // 10 million calls
    
    if (argc > 1) {
        num_iterations = atoi(argv[1]);
    }
    
    printf("╔════════════════════════════════════════════════════════════════╗\n");
    printf("║           Direct Call Benchmark - Performance Baseline          ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n\n");
    
    printf("Iterations: %d\n\n", num_iterations);
    
    /* Run benchmarks */
    double direct_ops = benchmark_direct_call(num_iterations);
    double funcptr_ops = benchmark_function_pointer(num_iterations);
    double indirect_ops = benchmark_indirect_call(num_iterations);
    
    /* Print results */
    printf("┌────────────────────────────────────────────────────────────────┐\n");
    printf("│ Benchmark Type               │ Throughput      │ Latency        │\n");
    printf("├────────────────────────────────────────────────────────────────┤\n");
    printf("│ Direct Call                  │ %14.0f ops/s │ %10.3f ns  │\n", direct_ops, 1e9 / direct_ops);
    printf("│ Function Pointer             │ %14.0f ops/s │ %10.3f ns  │\n", funcptr_ops, 1e9 / funcptr_ops);
    printf("│ Indirect Call (with checks)  │ %14.0f ops/s │ %10.3f ns  │\n", indirect_ops, 1e9 / indirect_ops);
    printf("└────────────────────────────────────────────────────────────────┘\n\n");
    
    /* Calculate overhead */
    printf("Overhead Analysis:\n");
    printf("  Function Pointer vs Direct: %.1fx slower (%.3f ns overhead)\n", 
           direct_ops / funcptr_ops, (1e9 / funcptr_ops) - (1e9 / direct_ops));
    printf("  Indirect vs Direct:         %.1fx slower (%.3f ns overhead)\n", 
           direct_ops / indirect_ops, (1e9 / indirect_ops) - (1e9 / direct_ops));
    printf("  Indirect vs Function Ptr:   %.1fx slower (%.3f ns overhead)\n", 
           funcptr_ops / indirect_ops, (1e9 / indirect_ops) - (1e9 / funcptr_ops));
    
    return 0;
}