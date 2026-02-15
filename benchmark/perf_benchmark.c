/**
 * UVRPC Unified Benchmark Client
 */

#include "../include/uvrpc.h"
#include "../../generated/rpc_benchmark/rpc_api.h"
#include "../../generated/rpc_benchmark/rpc_benchmark_builder.h"
#include "../../generated/rpc_benchmark/flatbuffers_common_builder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>

#define MAX_THREADS 10
#define MAX_CLIENTS 100

/* Constants */
#define MAX_CONN_WAIT 100
#define MAX_RESP_WAIT 10000
#define MAX_LATENCY_WAIT 1000
#define MAX_LOOP_DRAIN 10

/* Comparison function for qsort */
static int compare_double(const void* a, const void* b) {
    double da = *(const double*)a;
    double db = *(const double*)b;
    return (da > db) - (da < db);
}

/* Thread context for multi-thread test */
typedef struct {
    int thread_id;
    int num_clients;
    int concurrency;
    const char* address;
    atomic_int* total_responses;
    atomic_int* total_failures;
    int low_latency;
} thread_context_t;

/* Thread-specific state */
typedef struct {
    int responses_received;
    int requests_sent;
    int target_clients;
    int connections_established;
    int ready_to_send;
    int sent_requests;
    int test_duration_ms;
    volatile int done;
    uv_timer_t timer_handle;
} thread_state_t;

/* Global state */
static atomic_int g_response_sum = 0;

/* Latency test state */
static struct {
    struct timespec* start_times;
    struct timespec* end_times;
    int* received;
    int total;
} g_latency_state = {NULL, NULL, NULL, 0};

/* Timer callback to stop the test after fixed duration */
void on_test_timeout(uv_timer_t* handle) {
    thread_state_t* state = (thread_state_t*)handle->data;
    state->done = 1;
}

void on_connect(int status, void* ctx) {
    thread_state_t* state = (thread_state_t*)ctx;
    if (status == 0) {
        state->connections_established++;
        /* Set ready_to_send when all expected clients are connected */
        if (state->connections_established >= state->target_clients) {
            state->ready_to_send = 1;
        }
    }
}

void on_response(uvrpc_response_t* resp, void* ctx) {
    thread_state_t* state = (thread_state_t*)ctx;
    
    if (resp->status == UVRPC_OK) {
        state->responses_received++;
        
        /* Prevent compiler optimization by using atomic counter */
        atomic_fetch_add(&g_response_sum, 1);
    }
}

/* Latency test callback */
void on_latency_response(uvrpc_response_t* resp, void* ctx) {
    (void)resp;
    long req_id = (long)ctx;
    
    if (req_id >= 0 && req_id < g_latency_state.total) {
        g_latency_state.received[req_id] = 1;
        clock_gettime(CLOCK_MONOTONIC, &g_latency_state.end_times[req_id]);
    }
}

int create_clients(uvrpc_client_t** clients, int num_clients, uv_loop_t* loop,
                   const char* address, thread_state_t* state, int low_latency) {
    uvrpc_perf_mode_t perf_mode = low_latency ? UVRPC_PERF_LOW_LATENCY : UVRPC_PERF_HIGH_THROUGHPUT;
    
    /* Store target number of clients */
    state->target_clients = num_clients;
    
    for (int i = 0; i < num_clients; i++) {
        uvrpc_config_t* config = uvrpc_config_new();
        uvrpc_config_set_loop(config, loop);
        uvrpc_config_set_address(config, address);
        uvrpc_config_set_comm_type(config, UVRPC_COMM_SERVER_CLIENT);
        uvrpc_config_set_performance_mode(config, perf_mode);
        
        clients[i] = uvrpc_client_create(config);
        if (!clients[i]) {
            return -1;
        }
        
        int ret = uvrpc_client_connect_with_callback(clients[i], on_connect, state);
        if (ret != UVRPC_OK) {
            return -1;
        }
    }
    
    return 0;
}

/* Helper: Wait for clients to connect */
int wait_for_connections(uv_loop_t* loop, thread_state_t* state) {
    int wait = 0;
    while (!state->ready_to_send && wait < MAX_CONN_WAIT) {
        uv_run(loop, UV_RUN_ONCE);
        wait++;
    }
    return state->connections_established;
}

/* Helper: Send batch of requests continuously until test is done */
int send_requests(uvrpc_client_t** clients, int num_clients, int batch_size, flatcc_builder_t* builder, 
                  thread_state_t* state, unsigned int* seed, uv_loop_t* loop) {
    int failed = 0;
    
    /* Start timer to stop test after fixed duration */
    uv_timer_init(loop, &state->timer_handle);
    state->timer_handle.data = state;
    uv_timer_start(&state->timer_handle, on_test_timeout, state->test_duration_ms, 0);
    
    /* Send requests continuously until timer triggers */
    while (!state->done) {
        /* Send batch of requests */
        for (int i = 0; i < batch_size && !state->done; i++) {
            int client_idx = state->sent_requests % num_clients;
            
            /* Use random parameters to prevent compiler optimization */
            int32_t a = (int32_t)(rand_r(seed) % 1000);
            int32_t b = (int32_t)(rand_r(seed) % 1000);
            
            flatcc_builder_reset(builder);
            rpc_BenchmarkAddRequest_start_as_root(builder);
            rpc_BenchmarkAddRequest_a_add(builder, a);
            rpc_BenchmarkAddRequest_b_add(builder, b);
            rpc_BenchmarkAddRequest_end_as_root(builder);
            
            size_t size;
            void* buf = flatcc_builder_finalize_buffer(builder, &size);
            
            int ret = uvrpc_client_call(clients[client_idx], "Add", buf, size, on_response, state);
            
            if (ret == UVRPC_OK) {
                state->sent_requests++;
            } else {
                failed++;
            }
            
            /* Run event loop to process responses while sending */
            if (i % 10 == 0) {
                uv_run(loop, UV_RUN_NOWAIT);
            }
        }
        
        /* Run event loop to process responses */
        uv_run(loop, UV_RUN_ONCE);
    }
    
    /* Stop timer */
    uv_timer_stop(&state->timer_handle);
    uv_close((uv_handle_t*)&state->timer_handle, NULL);
    
    return failed;
}

/* Helper: Wait for responses */
int wait_for_responses(uv_loop_t* loop, thread_state_t* state, int target) {
    /* Wait a short time for any pending responses to complete */
    for (int i = 0; i < 100; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }
    return state->responses_received;
}

/* Helper: Print test results */
void print_test_results(int sent, int responses, int failed, struct timespec* start, struct timespec* end) {
    double elapsed = (end->tv_sec - start->tv_sec) + (end->tv_nsec - start->tv_nsec) / 1e9;
    printf("Sent: %d\n", sent);
    printf("Received: %d\n", responses);
    printf("Time: %.3f s\n", elapsed);
    printf("Throughput: %.0f ops/s (sent)\n", sent / elapsed);
    printf("Success rate: %.1f%%\n", (responses * 100.0) / sent);
    printf("Response sum: %d (to prevent compiler optimization)\n", atomic_load(&g_response_sum));
    printf("Failed: %d\n", failed);
    fflush(stdout);
}

/* Helper: Cleanup clients and drain loop */
void cleanup_clients_and_loop(uvrpc_client_t** clients, int num_clients, uv_loop_t* loop, flatcc_builder_t* builder) {
    for (int i = 0; i < num_clients; i++) {
        uvrpc_client_free(clients[i]);
    }
    
    flatcc_builder_clear(builder);
    
    for (int i = 0; i < MAX_LOOP_DRAIN; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }
}

/* Helper: Calculate and print latency statistics */
void print_latency_results(int iterations) {
    /* Calculate latency statistics */
    double latencies[iterations];
    int received_count = 0;
    for (int i = 0; i < iterations; i++) {
        if (g_latency_state.received[i]) {
            double latency = (g_latency_state.end_times[i].tv_sec - g_latency_state.start_times[i].tv_sec) +
                           (g_latency_state.end_times[i].tv_nsec - g_latency_state.start_times[i].tv_nsec) / 1e9;
            latencies[received_count++] = latency;
        }
    }

    /* Sort latencies using qsort (O(n log n) instead of O(n²)) */
    qsort(latencies, received_count, sizeof(double), compare_double);

    printf("=== Latency Test Results ===\n");
    printf("Iterations: %d\n", iterations);
    printf("Received: %d (%.1f%%)\n", received_count, (received_count * 100.0) / iterations);
    if (received_count > 0) {
        printf("Min: %.3f ms\n", latencies[0] * 1000);
        printf("P50: %.3f ms\n", latencies[received_count / 2] * 1000);
        printf("P95: %.3f ms\n", latencies[(int)(received_count * 0.95)] * 1000);
        printf("P99: %.3f ms\n", latencies[(int)(received_count * 0.99)] * 1000);
        printf("Max: %.3f ms\n", latencies[received_count - 1] * 1000);
        
        double avg = 0;
        for (int i = 0; i < received_count; i++) {
            avg += latencies[i];
        }
        printf("Avg: %.3f ms\n", (avg / received_count) * 1000);
    }
    printf("============================\n");
}

/* Single/Multi client test */
void run_single_multi_test(const char* address, int num_clients, int concurrency, int low_latency) {
    printf("run_single_multi_test started\n");
    fflush(stdout);
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    uvrpc_client_t* clients[MAX_CLIENTS];
    thread_state_t state = {
        .responses_received = 0,
        .requests_sent = 0,
        .target_clients = num_clients,
        .connections_established = 0,
        .ready_to_send = 0,
        .sent_requests = 0,
        .test_duration_ms = 1000,  /* 1 second test duration */
        .done = 0
    };
    
    printf("About to create clients...\n");
    fflush(stdout);
    
    /* Create clients */
    if (create_clients(clients, num_clients, &loop, address, &state, low_latency) != 0) {
        fprintf(stderr, "Failed to create clients\n");
        return;
    }

    /* Wait for connections */
    int connected = wait_for_connections(&loop, &state);
    printf("%d clients connected (target: %d), running throughput test for 1 second...\n",
           connected, num_clients);

    /* Pre-create FlatBuffers builder for reuse */
    flatcc_builder_t builder;
    flatcc_builder_init(&builder);
    unsigned int seed = time(NULL);

    /* Run throughput test */
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    int failed = send_requests(clients, num_clients, concurrency, &builder, &state, &seed, &loop);
    
    /* Wait for remaining responses */
    wait_for_responses(&loop, &state, 0);
    
    clock_gettime(CLOCK_MONOTONIC, &end);

    /* Results */
    print_test_results(state.sent_requests, state.responses_received, failed, &start, &end);

    /* Cleanup */
    for (int i = 0; i < num_clients; i++) {
        uvrpc_client_free(clients[i]);
    }
    
    flatcc_builder_clear(&builder);
    
    /* Drain event loop briefly */
    for (int i = 0; i < 2; i++) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }
    
    uv_loop_close(&loop);
}

/* Multi-thread test */
void* thread_func(void* arg) {
    thread_context_t* ctx = (thread_context_t*)arg;
    thread_state_t state = {
        .responses_received = 0,
        .requests_sent = 0,
        .target_clients = ctx->num_clients,
        .connections_established = 0,
        .ready_to_send = 0,
        .sent_requests = 0,
        .test_duration_ms = 1000,
        .done = 0
    };

    printf("[Thread %d] Starting\n", ctx->thread_id);
    fflush(stdout);

    uv_loop_t loop;
    uv_loop_init(&loop);

    uvrpc_client_t* clients[100];
    unsigned int seed = time(NULL) ^ ctx->thread_id;
    
    /* Create clients */
    if (create_clients(clients, ctx->num_clients, &loop, ctx->address, &state, ctx->low_latency) != 0) {
        fprintf(stderr, "[Thread %d] Failed to create clients\n", ctx->thread_id);
        return NULL;
    }

    /* Wait for connections */
    int connected = wait_for_connections(&loop, &state);
    printf("[Thread %d] %d clients connected\n", ctx->thread_id, connected);

    /* Pre-create FlatBuffers builder for reuse */
    flatcc_builder_t builder;
    flatcc_builder_init(&builder);

    /* Send requests continuously for 1 second */
    int failed = send_requests(clients, ctx->num_clients, ctx->concurrency, &builder, &state, &seed, &loop);
    
    /* Wait for remaining responses */
    wait_for_responses(&loop, &state, 0);

    /* Cleanup */
    cleanup_clients_and_loop(clients, ctx->num_clients, &loop, &builder);
    uv_loop_close(&loop);

    atomic_fetch_add(ctx->total_responses, state.responses_received);
    atomic_fetch_add(ctx->total_failures, failed);
    
    printf("[Thread %d] Completed: %d requests sent, %d responses received, %d failures\n", 
           ctx->thread_id, state.requests_sent, state.responses_received, failed);

    return NULL;
}

void run_multi_thread_test(const char* address, int num_threads, int clients_per_thread, int concurrency, int low_latency) {
    atomic_int total_responses = 0;
    atomic_int total_failures = 0;

    printf("=== Multi-Thread Test ===\n");
    printf("Threads: %d\n", num_threads);
    printf("Clients per thread: %d\n", clients_per_thread);
    printf("Total clients: %d\n", num_threads * clients_per_thread);
    printf("Concurrency per client: %d\n", concurrency);
    printf("Performance Mode: %s\n", low_latency ? "Low Latency" : "High Throughput");
    printf("Test Duration: 1 second per thread\n");
    printf("=======================\n\n");

    pthread_t threads[MAX_THREADS];
    thread_context_t contexts[MAX_THREADS];

    printf("Creating %d threads...\n\n", num_threads);

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int i = 0; i < num_threads; i++) {
        contexts[i].thread_id = i;
        contexts[i].num_clients = clients_per_thread;
        contexts[i].concurrency = concurrency;
        contexts[i].address = address;
        contexts[i].total_responses = &total_responses;
        contexts[i].total_failures = &total_failures;
        contexts[i].low_latency = low_latency;

        pthread_create(&threads[i], NULL, thread_func, &contexts[i]);
    }

    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    int responses = atomic_load(&total_responses);
    int failures = atomic_load(&total_failures);

    printf("\n=== Test Results ===\n");
    printf("Time: %.3f s\n", elapsed);
    printf("Total responses: %d\n", responses);
    printf("Total failures: %d\n", failures);
    printf("Success rate: %.1f%%\n", responses > 0 ? (responses * 100.0) / (responses + failures) : 0);
    printf("Throughput: %.0f ops/s\n", responses / elapsed);
    printf("====================\n");
}

/* Latency test */
void run_latency_test(const char* address, int iterations, int low_latency) {
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    uvrpc_client_t* client;
    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, &loop);
    uvrpc_config_set_address(config, address);
    uvrpc_config_set_comm_type(config, UVRPC_COMM_SERVER_CLIENT);
    
    /* Set performance mode */
    uvrpc_perf_mode_t perf_mode = low_latency ? UVRPC_PERF_LOW_LATENCY : UVRPC_PERF_HIGH_THROUGHPUT;
    uvrpc_config_set_performance_mode(config, perf_mode);
    
    client = uvrpc_client_create(config);
    
    /* Allocate latency tracking arrays */
    g_latency_state.start_times = malloc(iterations * sizeof(struct timespec));
    g_latency_state.end_times = malloc(iterations * sizeof(struct timespec));
    g_latency_state.received = calloc(iterations, sizeof(int));
    g_latency_state.total = iterations;
    
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    /* Send requests one by one for latency measurement */
    for (int i = 0; i < iterations; i++) {
        flatcc_builder_t builder;
        flatcc_builder_init(&builder);
        
        rpc_BenchmarkAddRequest_start_as_root(&builder);
        rpc_BenchmarkAddRequest_a_add(&builder, 10);
        rpc_BenchmarkAddRequest_b_add(&builder, 20);
        rpc_BenchmarkAddRequest_end_as_root(&builder);
        
        size_t size;
        void* buf = flatcc_builder_finalize_buffer(&builder, &size);
        
        clock_gettime(CLOCK_MONOTONIC, &g_latency_state.start_times[i]);
        
        uvrpc_client_call(client, "Add", buf, size, on_latency_response, (void*)(long)i);
        
        flatcc_builder_reset(&builder);
        
        /* Wait for response */
        int wait = 0;
        while (g_latency_state.received[i] == 0 && wait < MAX_LATENCY_WAIT) {
            uv_run(&loop, UV_RUN_ONCE);
            wait++;
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    double total_time = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;

    /* Print latency statistics */
    print_latency_results(iterations);
    
    printf("Total time: %.3f s\n", total_time);
    printf("Average QPS: %.0f\n", iterations / total_time);

    /* Cleanup */
    free(g_latency_state.start_times);
    free(g_latency_state.end_times);
    free(g_latency_state.received);

    uvrpc_client_free(client);
    uv_loop_close(&loop);
}

void print_usage(const char* prog_name) {
    printf("UVRPC Unified Benchmark Client\n\n");
    printf("Usage: %s [options]\n\n", prog_name);
    printf("Options:\n");
    printf("  -a <address>      Server address (default: tcp://127.0.0.1:5555)\n");
    printf("  -t <threads>      Number of threads (default: 1)\n");
    printf("  -c <clients>      Clients per thread (default: 1)\n");
    printf("  -b <concurrency>  Batch size (default: 100)\n");
    printf("  -l                Enable low latency mode (default: high throughput)\n");
    printf("  --latency         Run latency test (ignores -t and -c)\n");
    printf("  -h                Show this help\n\n");
    printf("Test Methods:\n");
    printf("  Throughput test: Runs for 1 second, measures maximum ops/s\n");
    printf("  Latency test: Measures request-response latency with percentiles\n\n");
    printf("Examples:\n");
    printf("  # Single client throughput test\n");
    printf("  %s -a tcp://127.0.0.1:5555 -b 100\n\n", prog_name);
    printf("  # Multi-client throughput test (10 clients)\n");
    printf("  %s -a tcp://127.0.0.1:5555 -c 10 -b 100\n\n", prog_name);
    printf("  # Multi-thread test (5 threads, 2 clients each)\n");
    printf("  %s -a tcp://127.0.0.1:5555 -t 5 -c 2 -b 50\n\n", prog_name);
    printf("  # Latency test\n");
    printf("  %s -a tcp://127.0.0.1:5555 --latency\n\n", prog_name);
    printf("  # Low latency mode\n");
    printf("  %s -a tcp://127.0.0.1:5555 -l\n\n", prog_name);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char* first_arg = argv[1];
    
    /* Check for help first */
    if (strcmp(first_arg, "-h") == 0 || strcmp(first_arg, "--help") == 0) {
        print_usage(argv[0]);
        return 0;
    }

    /* Parse options */
    const char* address = "tcp://127.0.0.1:5555";
    int num_threads = 1;
    int clients_per_thread = 1;
    int concurrency = 100;
    int low_latency = 0;
    int latency_mode = 0;

    /* Parse all arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-a") == 0 && i + 1 < argc) {
            address = argv[++i];
        } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            num_threads = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            clients_per_thread = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-b") == 0 && i + 1 < argc) {
            concurrency = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-l") == 0) {
            low_latency = 1;
        } else if (strcmp(argv[i], "--latency") == 0) {
            latency_mode = 1;
        } else if (strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        }
    }

    printf("=== UVRPC Unified Benchmark ===\n");
    printf("Address: %s\n", address);
    printf("Performance Mode: %s\n", low_latency ? "Low Latency" : "High Throughput");
    
    if (latency_mode) {
        printf("Test Mode: Latency\n\n");
        run_latency_test(address, 1000, low_latency);
    } else {
        int total_clients = num_threads * clients_per_thread;
        printf("Threads: %d\n", num_threads);
        printf("Clients per thread: %d\n", clients_per_thread);
        printf("Total clients: %d\n", total_clients);
        printf("Concurrency: %d\n", concurrency);
        printf("Test Mode: Throughput (1 second)\n\n");
        
        if (num_threads == 1) {
            run_single_multi_test(address, clients_per_thread, concurrency, low_latency);
        } else {
            run_multi_thread_test(address, num_threads, clients_per_thread, concurrency, low_latency);
        }
    }

    /* Force exit to avoid hanging on libuv thread pool cleanup */
    _exit(0);
    
    return 0;
}