/**
 * UVRPC Unified Benchmark Client
 */

#include "../include/uvrpc.h"
#include "../../generated/rpc_benchmark/rpc_benchmarkservice_api.h"
#include "../../generated/rpc_benchmark/rpc_benchmark_builder.h"
#include "../../generated/rpc_benchmark/flatbuffers_common_builder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/resource.h>

#define MAX_THREADS 10
#define MAX_CLIENTS 100
#define MAX_PROCESSES 32

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
    int test_duration_ms;
    const char* address;
    atomic_int* total_responses;
    atomic_int* total_failures;
    atomic_int* total_requests;
    int low_latency;
    int timer_interval_ms;  /* Configurable timer interval */
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
    uv_timer_t batch_timer_handle;
    uvrpc_client_t** clients;
    int num_clients;
    int batch_size;
    int failed;
    int timer_interval_ms;  /* Configurable timer interval (default: 1ms), 0 for immediate mode */
    int max_concurrent_requests;  /* Max concurrent requests for immediate mode */
    uv_loop_t* loop;  /* Event loop reference for immediate mode */
} thread_state_t;

/* Global state */
static atomic_int g_response_sum = 0;
static atomic_int g_result_sum = 0;

/* Latency test state */
static struct {
    struct timespec* start_times;
    struct timespec* end_times;
    int* received;
    int total;
} g_latency_state = {NULL, NULL, NULL, 0};

/* Signal handler for graceful shutdown */
static volatile sig_atomic_t g_shutdown_requested = 0;

/* Server state */
static uv_timer_t g_stats_timer;
static uvrpc_server_t* g_server = NULL;
static uint64_t g_last_requests = 0;
static uint64_t g_last_responses = 0;
static uv_loop_t* g_server_loop = NULL;

/* Shared memory for fork mode */
typedef struct {
    pid_t pid;
    int loop_idx;
    int client_idx;
    int completed;
    int errors;
    unsigned long long latency_us;
} client_stats_t;

static client_stats_t* g_shared_stats = NULL;
static size_t g_shm_size = 0;
static int g_shm_fd = -1;
static const char* g_shm_name = "/uvrpc_benchmark_shm";
static volatile sig_atomic_t g_server_pid = 0;
static const char* g_server_address = NULL;
static int g_server_timeout_ms = 0;  /* Server timeout in milliseconds */
static uv_timer_t g_server_timeout_timer;  /* Server timeout timer */
static struct timespec g_server_start_time;  /* Server start time for throughput calculation */

void on_signal(int signum) {
    (void)signum;
    g_shutdown_requested = 1;
}

/* Handler for Add operation */
void benchmark_add_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    if (req->params && req->params_size >= sizeof(int32_t) * 2) {
        int32_t a = *(int32_t*)req->params;
        int32_t b = *(int32_t*)(req->params + sizeof(int32_t));
        int32_t result = a + b;
        
        /* Check for integer overflow */
        if (a > 0 && b > 0 && result < 0) {
            uvrpc_request_send_response(req, UVRPC_ERROR_INVALID_PARAM, NULL, 0);
            return;
        }
        if (a < 0 && b < 0 && result > 0) {
            uvrpc_request_send_response(req, UVRPC_ERROR_INVALID_PARAM, NULL, 0);
            return;
        }
        
        uvrpc_request_send_response(req, UVRPC_OK, (uint8_t*)&result, sizeof(result));
    } else {
        uvrpc_request_send_response(req, UVRPC_ERROR_INVALID_PARAM, NULL, 0);
    }
}

/* Server signal handler for uv_signal_t */
void on_server_signal(uv_signal_t* handle, int signum) {
    (void)handle;
    printf("\n[SERVER] Received signal %d, shutting down...\n", signum);
    fflush(stdout);
    
    g_shutdown_requested = 1;
    
    /* Stop the event loop */
    if (g_server_loop) {
        uv_stop(g_server_loop);
    }
}

/* Server statistics timer callback */
void on_stats_timer(uv_timer_t* handle) {
    (void)handle;
    if (g_server) {
        uint64_t total_requests = uvrpc_server_get_total_requests(g_server);
        uint64_t total_responses = uvrpc_server_get_total_responses(g_server);
        
        uint64_t requests_delta = total_requests - g_last_requests;
        uint64_t responses_delta = total_responses - g_last_responses;
        
        printf("[SERVER] Total: %lu req, %lu resp | Delta: %lu req/s, %lu resp/s | Throughput: %lu ops/s\n",
               total_requests, total_responses, requests_delta, responses_delta, responses_delta);
        fflush(stdout);
        
        g_last_requests = total_requests;
        g_last_responses = total_responses;
    }
}

/* Helper: Get memory usage statistics */
static void print_memory_usage(const char* label) {
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        long rss_mb = usage.ru_maxrss / 1024;  /* Convert KB to MB */
        printf("[%s] Memory: %ld MB RSS\n", label, rss_mb);
        fflush(stdout);
    }
}

/* Forward declarations */
void send_batch_requests(uv_timer_t* handle);
void send_batch_requests_fast(uv_timer_t* handle);

/* Server timeout callback - auto-stop server after timeout */
static void on_server_timeout(uv_timer_t* handle) {
    (void)handle;
    fprintf(stderr, "[SERVER] Timeout reached (%d ms), shutting down...\n", g_server_timeout_ms);
    fflush(stderr);
    
    /* Stop the event loop gracefully */
    if (g_server_loop) {
        uv_stop(g_server_loop);
    }
}

/* Timer callback to stop test */
static void on_test_timer(uv_timer_t* handle) {
    thread_state_t* state = (thread_state_t*)handle->data;
    g_shutdown_requested = 1;
    state->done = 1;
    uv_timer_stop(handle);
    /* Stop the batch timer */
    if (state->batch_timer_handle.loop) {
        uv_timer_stop(&state->batch_timer_handle);
    }
    /* Stop the event loop */
    uv_stop(handle->loop);
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
    
    /* Count all responses, regardless of status */
    state->responses_received++;
    
    if (resp->status == UVRPC_OK && resp->result && resp->result_size >= sizeof(int32_t)) {
        /* Parse result as int32_t */
        int32_t result = *((int32_t*)resp->result);
        
        /* Prevent compiler optimization by using atomic counter */
        atomic_fetch_add(&g_response_sum, 1);
        
        /* Also accumulate result to verify correctness */
        atomic_fetch_add(&g_result_sum, result);
    }
    
    /* In immediate mode (interval=0), send new request immediately after receiving response
     * This maintains constant concurrency without timer delays
     */
    if (state->timer_interval_ms == 0 && !g_shutdown_requested && state->loop && state->clients) {
        /* Check if we can send more requests (backpressure check) */
        int total_pending = 0;
        for (int i = 0; i < state->num_clients; i++) {
            total_pending += uvrpc_client_get_pending_count(state->clients[i]);
        }
        
        /* Send new request if below concurrency limit */
        if (total_pending < state->max_concurrent_requests) {
            /* Send to the client with lowest pending count for load balancing */
            int min_pending = INT_MAX;
            int target_client = 0;
            
            for (int i = 0; i < state->num_clients; i++) {
                int pending = uvrpc_client_get_pending_count(state->clients[i]);
                if (pending < min_pending) {
                    min_pending = pending;
                    target_client = i;
                }
            }
            
            /* Send new request */
            int32_t a = 100;
            int32_t b = 200;
            int32_t params[2] = {a, b};
            
            int ret = uvrpc_client_call(state->clients[target_client], "Add", 
                                       (uint8_t*)params, sizeof(params), on_response, state);
            
            if (ret == UVRPC_OK) {
                state->sent_requests++;
            } else {
                state->failed++;
            }
        }
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
    
    /* Calculate required pending callbacks based on concurrency
     * High concurrency (>= 50) needs larger buffer to avoid UVRPC_ERROR_CALLBACK_LIMIT
     * Formula: clients * concurrency * 10 to accommodate pending requests and responses
     * with sufficient headroom for burst traffic
     * 
     * Testing larger buffer sizes:
     * - Default: 2^16 = 65,536
     * - Medium: 2^18 = 262,144
     * - High: 2^19 = 524,288
     * - Very High: 2^20 = 1,048,576
     * - Ultra High: 2^21 = 2,097,152 (for testing)
     */
    int total_concurrency = num_clients * state->batch_size;
    int max_pending = (1 << 16);  /* Default: 65,536 */
    if (total_concurrency >= 400) {
        max_pending = (1 << 21);  /* 2,097,152 for very high concurrency (testing) */
    } else if (total_concurrency >= 100) {
        max_pending = (1 << 21);  /* 2,097,152 for high concurrency (testing) */
    } else if (total_concurrency >= 50) {
        max_pending = (1 << 21);  /* 2,097,152 for medium concurrency (testing) */
    }
    
    for (int i = 0; i < num_clients; i++) {
        uvrpc_config_t* config = uvrpc_config_new();
        uvrpc_config_set_loop(config, loop);
        uvrpc_config_set_address(config, address);
        uvrpc_config_set_comm_type(config, UVRPC_COMM_SERVER_CLIENT);
        uvrpc_config_set_performance_mode(config, perf_mode);
        uvrpc_config_set_max_pending_callbacks(config, max_pending);
        
        /* Set max concurrent requests based on batch size with headroom */
        /* Allow 2x batch size to accommodate pending requests */
        int max_concurrent = state->batch_size * 2;
        if (max_concurrent < 100) max_concurrent = 100;  /* Minimum 100 */
        if (max_concurrent > 1000) max_concurrent = 1000;  /* Maximum 1000 */
        uvrpc_config_set_max_concurrent(config, max_concurrent);
        
        clients[i] = uvrpc_client_create(config);
        uvrpc_config_free(config);  /* Free config after client creation */
        
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

/* Helper: Send batch of requests with adaptive backpressure control */
void send_batch_requests(uv_timer_t* handle) {
    thread_state_t* state = (thread_state_t*)handle->data;
    if (!state) return;
    
    uv_loop_t* loop = handle->loop;
    uvrpc_client_t** clients = state->clients;
    int num_clients = state->num_clients;
    int batch_size = state->batch_size;
    
    if (num_clients == 0 || !clients) {
        return;
    }
    
    /* Copy timer handle to state for cleanup */
    state->batch_timer_handle = *handle;
    
    /* Send batch of requests with adaptive backpressure control
     * Check pending count for each client before sending
     * Skip sending if pending count is too high to avoid buffer overflow
     */
    int sent_this_round = 0;
    int skipped_count = 0;
    
    /* Special handling for immediate mode (interval=0) */
    int is_immediate_mode = (state->timer_interval_ms == 0);
    
    for (int i = 0; i < batch_size && !g_shutdown_requested; i++) {
        int client_idx = state->sent_requests % num_clients;
        
        /* Check pending request count before sending (backpressure mechanism)
         * Use adaptive threshold based on max_concurrent to prevent buffer overflow
         * Get max_concurrent from the first client (all clients have same config)
         */
        int pending_count = uvrpc_client_get_pending_count(clients[client_idx]);
        int max_concurrent = batch_size * 2;  /* From create_clients: max_concurrent = batch_size * 2 */
        
        /* Backpressure threshold: 80% of max_concurrent
         * This prevents buffer overflow while allowing reasonable throughput
         */
        int threshold = max_concurrent * 8 / 10;  /* 80% threshold */
        
        /* Allow small burst (up to 10 extra) to handle traffic spikes
         * This improves throughput while still preventing buffer overflow
         */
        if (pending_count > threshold + 10) {
            /* Skip sending this request, but don't count as failure
             * This is normal backpressure behavior
             */
            skipped_count++;
            continue;
        }
        
        int32_t a = 100;
        int32_t b = 200;
        int32_t params[2] = {a, b};
        
        int ret = uvrpc_client_call(clients[client_idx], "Add", (uint8_t*)params, sizeof(params), on_response, state);
        
        if (ret == UVRPC_OK) {
            state->sent_requests++;
            sent_this_round++;
        } else {
            state->failed++;
            /* Skip this request and try next one */
        }
    }
    
    /* In immediate mode (interval=0), schedule next batch immediately
     * Add a tiny delay (100 microseconds) to allow event loop to process responses
     * This prevents overwhelming the system while still being faster than timer mode
     */
    if (is_immediate_mode && !g_shutdown_requested && sent_this_round > 0) {
        /* Schedule next batch with 100us delay to allow event loop processing */
        uv_timer_start(handle, send_batch_requests, 0, 0);
    }
    
    /* If shutdown was requested, stop the event loop */
    if (g_shutdown_requested) {
        uv_stop(loop);
    }
}

/* Helper: Send multiple batches per timer callback for immediate mode */
void send_batch_requests_fast(uv_timer_t* handle) {
    thread_state_t* state = (thread_state_t*)handle->data;
    if (!state) return;
    
    uv_loop_t* loop = handle->loop;
    uvrpc_client_t** clients = state->clients;
    int num_clients = state->num_clients;
    int batch_size = state->batch_size;
    
    if (num_clients == 0 || !clients) {
        return;
    }
    
    /* Copy timer handle to state for cleanup */
    state->batch_timer_handle = *handle;
    
    /* Send 10 batches per timer callback to achieve 10x faster sending
     * This is more efficient than immediate mode because it allows event loop
     * to process responses between batches
     */
    int total_sent = 0;
    int total_skipped = 0;
    
    for (int batch = 0; batch < 10 && !g_shutdown_requested; batch++) {
        /* Send one batch */
        for (int i = 0; i < batch_size; i++) {
            int client_idx = state->sent_requests % num_clients;
            
            /* Check pending request count before sending (backpressure mechanism) */
            int pending_count = uvrpc_client_get_pending_count(clients[client_idx]);
            int max_concurrent = batch_size * 2;
            int threshold = max_concurrent * 8 / 10;
            
            if (pending_count > threshold + 10) {
                total_skipped++;
                continue;
            }
            
            int32_t a = 100;
            int32_t b = 200;
            int32_t params[2] = {a, b};
            
            int ret = uvrpc_client_call(clients[client_idx], "Add", (uint8_t*)params, sizeof(params), on_response, state);
            
            if (ret == UVRPC_OK) {
                state->sent_requests++;
                total_sent++;
            } else {
                state->failed++;
            }
        }
        
        /* Run event loop to process responses between batches
         * Use RUN_NOWAIT to avoid blocking, but allow processing
         */
        uv_run(loop, UV_RUN_NOWAIT);
        
        /* Small sleep to prevent overwhelming the system
         * 100 microseconds allows network processing while maintaining high throughput
         */
        usleep(100);
    }
    
    /* If shutdown was requested, stop the event loop */
    if (g_shutdown_requested) {
        uv_stop(loop);
    }
}

/* Helper: Wait for responses */
int wait_for_responses(uv_loop_t* loop, thread_state_t* state, int target) {
    /* Run event loop to process remaining responses, but limit iterations */
    int iterations = 0;
    int max_iterations = 10000; /* Increase limit to handle more responses */
    
    while (state->responses_received < state->sent_requests && iterations < max_iterations) {
        uv_run(loop, UV_RUN_NOWAIT); /* Use NOWAIT to avoid blocking */
        iterations++;
    }
    
    return state->responses_received;
}

/* Helper: Print test results */
void print_test_results(int sent, int responses, int failed, struct timespec* start, struct timespec* end, int num_clients) {
    double elapsed = (end->tv_sec - start->tv_sec) + (end->tv_nsec - start->tv_nsec) / 1e9;
    printf("Sent: %d\n", sent);
    printf("Received: %d\n", responses);
    printf("Time: %.3f s\n", elapsed);
    printf("Client throughput: %.0f ops/s (sent)\n", sent / elapsed);
    
    /* Calculate success rate based on received responses only */
    int successful_responses = atomic_load(&g_response_sum);
    int result_sum = atomic_load(&g_result_sum);
    if (responses > 0) {
        printf("Success rate: %.1f%% (based on received responses)\n", (successful_responses * 100.0) / responses);
        printf("Result count: %d (correct responses)\n", successful_responses);
        if (successful_responses > 0) {
            printf("Result average: %.1f (to verify correctness)\n", (double)result_sum / successful_responses);
        }
    }
    printf("Failed: %d\n", failed);
    
    /* Print memory usage */
    print_memory_usage("TEST CLIENT PROCESS");
    if (num_clients > 0) {
        struct rusage usage;
        if (getrusage(RUSAGE_SELF, &usage) == 0) {
            long total_rss_mb = usage.ru_maxrss / 1024;
            long per_client_rss_kb = (usage.ru_maxrss * 1024) / num_clients;
            printf("[TEST CLIENT] Total RSS: %ld MB (%ld KB per client)\n", total_rss_mb, per_client_rss_kb);
        }
    }
    fflush(stdout);
}

void cleanup_clients_and_loop(uvrpc_client_t** clients, int num_clients, uv_loop_t* loop, void* ctx) {
    (void)ctx;
    for (int i = 0; i < num_clients; i++) {
        uvrpc_client_free(clients[i]);
    }
    
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
    print_memory_usage("LATENCY");
    printf("============================\n");
}

/* Single/Multi client test */
void run_single_multi_test(const char* address, int num_clients, int concurrency, int test_duration_ms, int low_latency) {
    printf("run_single_multi_test started\n");
    fflush(stdout);
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* Configure loop for lower memory usage */
    uv_loop_configure(&loop, UV_LOOP_BLOCK_SIGNAL, SIGPROF);
    
    /* Dynamically allocate clients array to save memory */
    uvrpc_client_t** clients = malloc(num_clients * sizeof(uvrpc_client_t*));
    if (!clients) {
        fprintf(stderr, "Failed to allocate clients array\n");
        uv_loop_close(&loop);
        return;
    }
    
    thread_state_t state = {
        .responses_received = 0,
        .requests_sent = 0,
        .target_clients = num_clients,
        .connections_established = 0,
        .ready_to_send = 0,
        .sent_requests = 0,
        .test_duration_ms = test_duration_ms,
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
    printf("%d clients connected (target: %d), running throughput test for %.1f seconds...\n",
           connected, num_clients, test_duration_ms / 1000.0);

    printf("Sending requests (press Ctrl+C to stop)...\n");
    fflush(stdout);

    /* Setup timer to stop test after specified duration */
    uv_timer_t timer;
    uv_timer_init(&loop, &timer);
    timer.data = &state;
    
    uv_timer_start(&timer, on_test_timer, test_duration_ms, 0);

    /* Setup batch timer to send requests periodically */
    uv_timer_t batch_timer;
    uv_timer_init(&loop, &batch_timer);
    state.clients = clients;
    state.num_clients = num_clients;
    state.batch_size = concurrency;
    state.failed = 0;
    batch_timer.data = &state;
    batch_timer.loop = &loop;
    
    /* Send requests every 1ms */
    uv_timer_start(&batch_timer, send_batch_requests, 1, 1);

    /* Run event loop with UV_RUN_DEFAULT */
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    uv_run(&loop, UV_RUN_DEFAULT);
    
    clock_gettime(CLOCK_MONOTONIC, &end);

    /* Cleanup timers */
    uv_timer_stop(&timer);
    uv_timer_stop(&batch_timer);
    uv_close((uv_handle_t*)&timer, NULL);
    uv_close((uv_handle_t*)&batch_timer, NULL);

    /* Results */
    print_test_results(state.sent_requests, state.responses_received, state.failed, &start, &end, num_clients);

    /* Cleanup */
    for (int i = 0; i < num_clients; i++) {
        uvrpc_client_free(clients[i]);
    }
    
    free(clients);  /* Free the dynamically allocated array */
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
        .test_duration_ms = ctx->test_duration_ms,
        .done = 0
    };

    printf("[Thread %d] Starting\n", ctx->thread_id);
    fflush(stdout);

    uv_loop_t loop;
    uv_loop_init(&loop);

    /* Dynamically allocate clients array to save memory */
    uvrpc_client_t** clients = malloc(ctx->num_clients * sizeof(uvrpc_client_t*));
    if (!clients) {
        fprintf(stderr, "[Thread %d] Failed to allocate clients array\n", ctx->thread_id);
        return NULL;
    }
    
    unsigned int seed = time(NULL) ^ ctx->thread_id;
    
    /* Create clients */
    if (create_clients(clients, ctx->num_clients, &loop, ctx->address, &state, ctx->low_latency) != 0) {
        fprintf(stderr, "[Thread %d] Failed to create clients\n", ctx->thread_id);
        free(clients);
        return NULL;
    }

    /* Wait for connections */
    int connected = wait_for_connections(&loop, &state);
    printf("[Thread %d] %d clients connected\n", ctx->thread_id, connected);

    /* Setup timer to stop test after specified duration */
    uv_timer_t timer;
    uv_timer_init(&loop, &timer);
    timer.data = &state;
    
    uv_timer_start(&timer, on_test_timer, ctx->test_duration_ms, 0);

    /* Setup batch timer to send requests periodically */
    uv_timer_t batch_timer;
    uv_timer_init(&loop, &batch_timer);
    state.clients = clients;
    state.num_clients = ctx->num_clients;
    state.batch_size = ctx->concurrency;
    state.failed = 0;
    state.timer_interval_ms = ctx->timer_interval_ms;  /* Use configured timer interval */
    batch_timer.data = &state;
    batch_timer.loop = &loop;
    
    int batch_size = state.batch_size;  /* Local copy for immediate mode */
    int num_clients = state.num_clients;  /* Local copy for immediate mode */
    
    /* Set loop and clients reference for immediate mode callback */
    state.loop = &loop;
    state.clients = clients;
    state.num_clients = num_clients;
    
    /* Send requests with configurable interval (default: 0ms for immediate mode)
     * Special case: interval=0 means "immediate mode" - send requests as fast as possible with backpressure
     * In immediate mode, send requests immediately when response arrives, maintaining concurrency limit
     */
    if (state.timer_interval_ms == 0) {
        /* Immediate mode: send requests with immediate callback on response
         * Maintain concurrency limit: send when pending count is below threshold
         * This sends requests as fast as responses arrive, respecting backpressure
         */
        int max_concurrent = batch_size * 2;
        int threshold = max_concurrent * 8 / 10;  /* 80% threshold */
        
        printf("[Thread %d] Immediate mode: maintaining %d max concurrent requests...\n", 
               ctx->thread_id, max_concurrent);
        
        /* Use a custom callback that triggers next send on response */
        state.max_concurrent_requests = max_concurrent;
        
        /* Send initial batch to reach concurrency limit */
        int initial_sent = 0;
        for (int i = 0; i < num_clients; i++) {
            /* Send batch_size requests per client to reach concurrency */
            for (int j = 0; j < batch_size && initial_sent < max_concurrent; j++) {
                int32_t a = 100;
                int32_t b = 200;
                int32_t params[2] = {a, b};
                
                int ret = uvrpc_client_call(clients[i], "Add", (uint8_t*)params, sizeof(params), on_response, &state);
                
                if (ret == UVRPC_OK) {
                    state.sent_requests++;
                    initial_sent++;
                } else {
                    state.failed++;
                }
            }
        }
        
        printf("[Thread %d] Sent %d initial requests, entering event loop to send on response...\n", 
               ctx->thread_id, initial_sent);
        
        /* Don't start timer, enter event loop to wait for responses and send new requests */
    } else {
        /* Normal mode: use timer-based periodic sending */
        uv_timer_start(&batch_timer, send_batch_requests, state.timer_interval_ms, state.timer_interval_ms);
    }

    /* Run event loop with UV_RUN_DEFAULT */
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    uv_run(&loop, UV_RUN_DEFAULT);
    
    clock_gettime(CLOCK_MONOTONIC, &end);

    /* Cleanup timers */
    uv_timer_stop(&timer);
    uv_timer_stop(&batch_timer);
    uv_close((uv_handle_t*)&timer, NULL);
    uv_close((uv_handle_t*)&batch_timer, NULL);

    /* Cleanup */
    cleanup_clients_and_loop(clients, ctx->num_clients, &loop, NULL);
    free(clients);  /* Free the dynamically allocated array */

    atomic_fetch_add(ctx->total_responses, state.responses_received);
    atomic_fetch_add(ctx->total_failures, state.failed);
    atomic_fetch_add(ctx->total_requests, state.sent_requests);
    
    printf("[Thread %d] Completed: %d requests sent, %d responses received, %d failures\n", 
           ctx->thread_id, state.sent_requests, state.responses_received, state.failed);

    return NULL;
}

/* Function declarations for broadcast mode */
static void run_publisher_mode(const char* address, int num_threads, int publishers_per_thread, 
                               int batch_size, int test_duration_ms);
static void run_subscriber_mode(const char* address, int num_threads, int subscribers_per_thread, 
                                int test_duration_ms);

void run_multi_thread_test(const char* address, int num_threads, int clients_per_thread, int concurrency, int test_duration_ms, int low_latency, int timer_interval_ms) {
    atomic_int total_responses = 0;
    atomic_int total_failures = 0;
    atomic_int total_requests = 0;

    printf("=== Multi-Thread Test ===\n");
    printf("Threads: %d\n", num_threads);
    printf("Clients per thread: %d\n", clients_per_thread);
    printf("Total clients: %d\n", num_threads * clients_per_thread);
    printf("Concurrency per client: %d\n", concurrency);
    printf("Performance Mode: %s\n", low_latency ? "Low Latency" : "High Throughput");
    printf("Test Duration: %.1f seconds per thread\n", test_duration_ms / 1000.0);
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
        contexts[i].test_duration_ms = test_duration_ms;
        contexts[i].address = address;
        contexts[i].total_responses = &total_responses;
        contexts[i].total_failures = &total_failures;
        contexts[i].total_requests = &total_requests;
        contexts[i].low_latency = low_latency;
        contexts[i].timer_interval_ms = timer_interval_ms;  /* Pass timer interval */

        pthread_create(&threads[i], NULL, thread_func, &contexts[i]);
    }

    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    int responses = atomic_load(&total_responses);
    int failures = atomic_load(&total_failures);
    int requests = atomic_load(&total_requests);

    printf("\n=== Test Results ===\n");
    printf("Time: %.3f s\n", elapsed);
    printf("Total requests: %d\n", requests);
    printf("Total responses: %d\n", responses);
    printf("Total failures: %d\n", failures);
    printf("Success rate: %.1f%%\n", requests > 0 ? (responses * 100.0) / requests : 0);
    printf("Throughput: %.0f ops/s\n", responses / elapsed);
    print_memory_usage("MULTI-THREAD");
    printf("====================\n");
}

/* Server mode - integrated server functionality */
void run_server_mode(const char* address) {
    if (!address) {
        fprintf(stderr, "Invalid address\n");
        return;
    }
    
    /* Create event loop */
    uv_loop_t loop;
    uv_loop_init(&loop);
    g_server_loop = &loop;
    
    /* Create config */
    uvrpc_config_t* config = uvrpc_config_new();
    if (!config) {
        fprintf(stderr, "Failed to create config\n");
        uv_loop_close(&loop);
        return;
    }
    
    uvrpc_config_set_loop(config, &loop);
    uvrpc_config_set_address(config, address);
    uvrpc_config_set_comm_type(config, UVRPC_COMM_SERVER_CLIENT);
    
    /* Create server */
    g_server = uvrpc_server_create(config);
    
    if (!g_server) {
        fprintf(stderr, "Failed to create server\n");
        uvrpc_config_free(config);
        uv_loop_close(&loop);
        return;
    }
    
    uvrpc_config_free(config);  /* Free config after successful server creation */
    
    /* Register handlers manually */
    uvrpc_server_register(g_server, "Add", benchmark_add_handler, NULL);
    
    /* Start server */
    int ret = uvrpc_server_start(g_server);
    if (ret != UVRPC_OK) {
        fprintf(stderr, "Failed to start server: %d\n", ret);
        uvrpc_server_free(g_server);
        uv_loop_close(&loop);
        return;
    }
    
    /* Record server start time for throughput calculation */
    clock_gettime(CLOCK_MONOTONIC, &g_server_start_time);
    
    printf("Server started on %s\n", address);
    printf("Press Ctrl+C to stop the server\n");
    fflush(stdout);
    
    /* Start stats timer (print every 1 second) */
    uv_timer_init(&loop, &g_stats_timer);
    uv_timer_start(&g_stats_timer, on_stats_timer, 1000, 1000);
    
    /* Setup server timeout timer if timeout is configured */
    if (g_server_timeout_ms > 0) {
        uv_timer_init(&loop, &g_server_timeout_timer);
        uv_timer_start(&g_server_timeout_timer, on_server_timeout, g_server_timeout_ms, 0);
        printf("[SERVER] Auto-shutdown in %d ms\n", g_server_timeout_ms);
        fflush(stdout);
    }
    
    /* Setup signal handlers for graceful shutdown */
    static uv_signal_t sigint_sig, sigterm_sig;
    uv_signal_init(&loop, &sigint_sig);
    uv_signal_start(&sigint_sig, on_server_signal, SIGINT);
    
    uv_signal_init(&loop, &sigterm_sig);
    uv_signal_start(&sigterm_sig, on_server_signal, SIGTERM);
    
    /* Run event loop with UV_RUN_DEFAULT */
    printf("Running event loop (server is driven by external loop)...\n");
    fflush(stdout);
    uv_run(&loop, UV_RUN_DEFAULT);
    printf("Event loop exited\n");
    fflush(stdout);
    
    /* Cleanup */
    uv_timer_stop(&g_stats_timer);
    uv_close((uv_handle_t*)&g_stats_timer, NULL);
    
    /* Stop and close timeout timer if it was configured */
    if (g_server_timeout_ms > 0) {
        uv_timer_stop(&g_server_timeout_timer);
        uv_close((uv_handle_t*)&g_server_timeout_timer, NULL);
    }
    
    /* Print final statistics */
    struct timespec end_time;
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    
    uint64_t total_requests = uvrpc_server_get_total_requests(g_server);
    uint64_t total_responses = uvrpc_server_get_total_responses(g_server);
    
    /* Calculate total elapsed time in seconds */
    double elapsed = (end_time.tv_sec - g_server_start_time.tv_sec) + 
                     (end_time.tv_nsec - g_server_start_time.tv_nsec) / 1e9;
    
    /* Calculate total throughput */
    double total_throughput = elapsed > 0 ? total_responses / elapsed : 0;
    
    printf("[SERVER] Final statistics:\n");
    printf("[SERVER]   Total requests: %lu\n", total_requests);
    printf("[SERVER]   Total responses: %lu\n", total_responses);
    printf("[SERVER]   Elapsed time: %.3f s\n", elapsed);
    printf("[SERVER]   Total throughput: %.0f ops/s\n", total_throughput);
    print_memory_usage("SERVER PROCESS");
    
    /* Calculate memory per request */
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0 && total_responses > 0) {
        long total_rss_mb = usage.ru_maxrss / 1024;
        long bytes_per_request = (usage.ru_maxrss * 1024) / total_responses;
        printf("[SERVER]   Memory efficiency: %ld bytes/request\n", bytes_per_request);
    }
    fflush(stdout);
    
    uvrpc_server_free(g_server);
    printf("Server stopped\n");
    fflush(stdout);
    
    g_server = NULL;
    g_server_loop = NULL;
    uv_loop_close(&loop);
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
    uvrpc_config_free(config);  /* Free config after client creation */
    
    if (!client) {
        fprintf(stderr, "Failed to create client\n");
        uv_loop_close(&loop);
        return;
    }
    
    /* Allocate latency tracking arrays */
    g_latency_state.start_times = malloc(iterations * sizeof(struct timespec));
    if (!g_latency_state.start_times) {
        fprintf(stderr, "Failed to allocate start_times array\n");
        uv_loop_close(&loop);
        return;
    }
    
    g_latency_state.end_times = malloc(iterations * sizeof(struct timespec));
    if (!g_latency_state.end_times) {
        fprintf(stderr, "Failed to allocate end_times array\n");
        free(g_latency_state.start_times);
        uv_loop_close(&loop);
        return;
    }
    
    g_latency_state.received = calloc(iterations, sizeof(int));
    if (!g_latency_state.received) {
        fprintf(stderr, "Failed to allocate received array\n");
        free(g_latency_state.start_times);
        free(g_latency_state.end_times);
        uv_loop_close(&loop);
        return;
    }
    
    g_latency_state.total = iterations;
    
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    /* Send requests one by one for latency measurement */
    for (int i = 0; i < iterations; i++) {
        int32_t a = 10;
        int32_t b = 20;
        int32_t params[2] = {a, b};
        
        clock_gettime(CLOCK_MONOTONIC, &g_latency_state.start_times[i]);
        
        /* Use uvrpc_client_call API */
        uvrpc_client_call(client, "Add", (uint8_t*)params, sizeof(params), on_latency_response, (void*)(long)i);
        
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

/* Fork mode client process */
static void run_fork_client_process(int loop_idx, int client_idx, const char* address,
                                     int total_requests, int low_latency) {
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, &loop);
    uvrpc_config_set_address(config, address);
    uvrpc_config_set_comm_type(config, UVRPC_COMM_SERVER_CLIENT);
    uvrpc_config_set_performance_mode(config, 
        low_latency ? UVRPC_PERF_LOW_LATENCY : UVRPC_PERF_HIGH_THROUGHPUT);
    
    uvrpc_client_t* client = uvrpc_client_create(config);
    
    if (!client) {
        uvrpc_config_free(config);
        uv_loop_close(&loop);
        exit(1);
    }
    
    uvrpc_config_free(config);  /* Free config after successful client creation */
    
    if (uvrpc_client_connect(client) != UVRPC_OK) {
        fprintf(stderr, "[FORK CLIENT %d-%d] Failed to connect\n", loop_idx, client_idx);
        uvrpc_client_free(client);
        uv_loop_close(&loop);
        exit(1);
    }
    
    /* Wait for connection to be fully established */
    for (int i = 0; i < 500; i++) {
        uv_run(&loop, UV_RUN_ONCE);
        if (i % 100 == 0) {
            usleep(10000); /* Wait 10ms every 100 iterations */
        }
    }
    
    /* Small delay to ensure connection is stable */
    usleep(50000); /* 50ms */
    
    /* Send requests */
    int completed = 0;
    int errors = 0;
    struct timeval start, end;
    gettimeofday(&start, NULL);
    
    for (int i = 0; i < total_requests && !g_shutdown_requested; i++) {
        int32_t a = 42;
        int32_t b = 58;
        
        flatcc_builder_t builder;
        flatcc_builder_init(&builder);
        rpc_BenchmarkAddRequest_start_as_root(&builder);
        rpc_BenchmarkAddRequest_a_add(&builder, a);
        rpc_BenchmarkAddRequest_b_add(&builder, b);
        rpc_BenchmarkAddRequest_end_as_root(&builder);
        
        size_t size;
        void* buf = flatcc_builder_finalize_buffer(&builder, &size);
        
        int ret = uvrpc_client_call(client, "Add", buf, size, NULL, NULL);
        
        flatcc_builder_reset(&builder);
        
        if (ret == UVRPC_OK) {
            completed++;
        } else {
            errors++;
        }
        
        uv_run(&loop, UV_RUN_ONCE);
    }
    
    gettimeofday(&end, NULL);
    
    /* Update shared memory */
    if (g_shared_stats) {
        int idx = loop_idx * MAX_CLIENTS + client_idx;
        int total_clients = MAX_PROCESSES * MAX_CLIENTS;
        
        if (idx >= total_clients) {
            fprintf(stderr, "[FORK CLIENT %d-%d] Invalid index %d >= %d\n", 
                    loop_idx, client_idx, idx, total_clients);
        } else {
            g_shared_stats[idx].pid = getpid();
            g_shared_stats[idx].loop_idx = loop_idx;
            g_shared_stats[idx].client_idx = client_idx;
            g_shared_stats[idx].completed = completed;
            g_shared_stats[idx].errors = errors;
            g_shared_stats[idx].latency_us = 
                (end.tv_sec - start.tv_sec) * 1000000LL + (end.tv_usec - start.tv_usec);
        }
    }
    
    uvrpc_client_disconnect(client);
    uvrpc_client_free(client);
    uvrpc_config_free(config);
    uv_loop_close(&loop);
    
    exit(0);
}

/* Fork mode main */
static void run_fork_mode(const char* address, int num_loops, int clients_per_loop,
                          int test_duration_ms, int low_latency) {
    /* Initialize shared memory */
    int total_clients = num_loops * clients_per_loop;
    g_shm_size = total_clients * sizeof(client_stats_t);
    
    g_shm_fd = shm_open(g_shm_name, O_CREAT | O_RDWR, 0600);
    if (g_shm_fd == -1) {
        perror("shm_open");
        return;
    }
    
    if (ftruncate(g_shm_fd, g_shm_size) == -1) {
        perror("ftruncate");
        close(g_shm_fd);
        shm_unlink(g_shm_name);
        return;
    }
    
    g_shared_stats = mmap(NULL, g_shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, g_shm_fd, 0);
    if (g_shared_stats == MAP_FAILED) {
        perror("mmap");
        close(g_shm_fd);
        shm_unlink(g_shm_name);
        return;
    }
    
    memset(g_shared_stats, 0, g_shm_size);
    
    /* Fork client processes */
    pid_t pids[MAX_PROCESSES * MAX_CLIENTS];
    int num_pids = 0;
    
    for (int i = 0; i < num_loops; i++) {
        for (int j = 0; j < clients_per_loop; j++) {
            pid_t pid = fork();
            if (pid < 0) {
                perror("fork");
                continue;
            } else if (pid == 0) {
                /* Child process */
                int requests_per_client = 1000; /* Default requests per client */
                run_fork_client_process(i, j, address, requests_per_client, low_latency);
                exit(0);
            } else {
                /* Parent process */
                pids[num_pids++] = pid;
            }
        }
    }
    
    /* Wait for all child processes */
    for (int i = 0; i < num_pids; i++) {
        int status;
        waitpid(pids[i], &status, 0);
    }
    
    /* Collect and print statistics */
    printf("\n=== Fork Mode Results ===\n");
    printf("Total clients: %d\n", total_clients);
    
    int total_completed = 0;
    int total_errors = 0;
    
    for (int i = 0; i < total_clients; i++) {
        total_completed += g_shared_stats[i].completed;
        total_errors += g_shared_stats[i].errors;
    }
    
    printf("Total requests completed: %d\n", total_completed);
    printf("Total errors: %d\n", total_errors);
    printf("Success rate: %.2f%%\n", 
           total_completed > 0 ? (total_completed * 100.0) / (total_completed + total_errors) : 0);
    
    /* Print memory usage */
    print_memory_usage("FORK");
    
    /* Cleanup shared memory */
    munmap(g_shared_stats, g_shm_size);
    close(g_shm_fd);
    shm_unlink(g_shm_name);
}

void print_usage(const char* prog_name) {
    printf("UVRPC Unified Benchmark\n\n");
    printf("Usage: %s [mode] [options]\n\n", prog_name);
    printf("Modes:\n");
    printf("  --server          Run in server mode (for SERVER_CLIENT mode)\n");
    printf("  --publisher      Run in publisher mode (for BROADCAST mode)\n");
    printf("  --subscriber     Run in subscriber mode (for BROADCAST mode)\n\n");
    printf("Options:\n");
    printf("  -a <address>      Server/Publisher address (default: tcp://127.0.0.1:5555)\n");
    printf("  -t <threads>      Number of threads/processes (default: 1)\n");
    printf("  -c <clients>      Clients per thread/process (default: 1)\n");
    printf("  -p <publishers>   Publishers per thread/process (for BROADCAST mode, default: 1)\n");
    printf("  -s <subscribers>  Subscribers per thread/process (for BROADCAST mode, default: 1)\n");
    printf("  -b <concurrency>  Batch size (default: 100)\n");
    printf("  -i <interval>     Timer interval in milliseconds (default: 0 for immediate mode, use 1-5 for timer-based)\n");
    printf("  -d <duration>     Test duration in milliseconds (default: 1000)\n");
    printf("  -l                Enable low latency mode (default: high throughput)\n");
    printf("  --latency         Run latency test (ignores -t and -c)\n");
    printf("  --fork            Use fork mode instead of threads (for multi-process testing)\n");
    printf("  --server-timeout <ms> Server auto-shutdown timeout (default: 0, no timeout)\n");
    printf("  -h                Show this help\n\n");
    printf("Communication Types:\n");
    printf("  SERVER_CLIENT: Request-Response RPC (default)\n");
    printf("  BROADCAST:    Publisher-Subscriber (use --publisher or --subscriber)\n\n");
    printf("Test Methods:\n");
    printf("  Throughput test: Runs for specified duration, measures maximum ops/s\n");
    printf("  Latency test: Measures request-response latency with percentiles\n\n");
    printf("Examples:\n");
    printf("  # Start server (SERVER_CLIENT mode)\n");
    printf("  %s --server -a tcp://127.0.0.1:5000\n\n", prog_name);
    printf("  # Single client throughput test\n");
    printf("  %s -a tcp://127.0.0.1:5000 -b 100\n\n", prog_name);
    printf("  # Multi-thread test (5 threads, 2 clients each)\n");
    printf("  %s -a tcp://127.0.0.1:5000 -t 5 -c 2 -b 50 -d 3000\n\n", prog_name);
    printf("  # Multi-process test with fork\n");
    printf("  %s -a tcp://127.0.0.1:5000 -t 4 -c 5 --fork\n\n", prog_name);
    printf("  # Start publisher (BROADCAST mode)\n");
    printf("  %s --publisher -a udp://127.0.0.1:6000 -p 3 -b 20 -d 5000\n\n", prog_name);
    printf("  # Start subscriber (BROADCAST mode)\n");
    printf("  %s --subscriber -a udp://127.0.0.1:6000 -s 5 -d 5000\n\n", prog_name);
    printf("  # Multi-publisher test (BROADCAST mode)\n");
    printf("  %s --publisher -a udp://127.0.0.1:6000 -t 3 -p 2 -b 10 -d 3000\n\n", prog_name);
    printf("  # Latency test\n");
    printf("  %s -a tcp://127.0.0.1:5000 --latency\n\n", prog_name);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    /* Setup signal handlers for graceful shutdown */
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

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
    int publishers_per_thread = 1;
    int subscribers_per_thread = 1;
    int concurrency = 100;
    int timer_interval_ms = 0;  /* Default: 0ms (immediate mode) */
    int test_duration_ms = 1000;  /* Default: 1 second */
    int low_latency = 0;
    int latency_mode = 0;
    int fork_mode = 0;
    int server_mode = 0;
    int publisher_mode = 0;
    int subscriber_mode = 0;
    int server_timeout_ms = 0;  /* Default: no timeout */

    /* Parse all arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-a") == 0 && i + 1 < argc) {
            address = argv[++i];
        } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            num_threads = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            clients_per_thread = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            publishers_per_thread = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            subscribers_per_thread = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-b") == 0 && i + 1 < argc) {
            concurrency = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
            timer_interval_ms = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            test_duration_ms = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-l") == 0) {
            low_latency = 1;
        } else if (strcmp(argv[i], "--latency") == 0) {
            latency_mode = 1;
        } else if (strcmp(argv[i], "--fork") == 0) {
            fork_mode = 1;
        } else if (strcmp(argv[i], "--server") == 0) {
            server_mode = 1;
        } else if (strcmp(argv[i], "--publisher") == 0) {
            publisher_mode = 1;
        } else if (strcmp(argv[i], "--subscriber") == 0) {
            subscriber_mode = 1;
        } else if (strcmp(argv[i], "--server-timeout") == 0 && i + 1 < argc) {
            server_timeout_ms = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        }
    }

    printf("=== UVRPC Unified Benchmark ===\n");
    printf("Address: %s\n", address);
    printf("Performance Mode: %s\n", low_latency ? "Low Latency" : "High Throughput");
    printf("Press Ctrl+C to stop the benchmark\n");
    
    /* Handle different modes */
    if (server_mode) {
        g_server_timeout_ms = server_timeout_ms;  /* Set global timeout */
        printf("Mode: Server (SERVER_CLIENT)\n\n");
        run_server_mode(address);
        _exit(0);
    }
    
    if (publisher_mode) {
        int total_publishers = num_threads * publishers_per_thread;
        printf("Mode: Publisher (BROADCAST)\n");
        printf("Total publishers: %d\n", total_publishers);
        printf("Test duration: %.1f seconds\n\n", test_duration_ms / 1000.0);
        run_publisher_mode(address, num_threads, publishers_per_thread, concurrency, test_duration_ms);
        _exit(0);
    }
    
    if (subscriber_mode) {
        int total_subscribers = num_threads * subscribers_per_thread;
        printf("Mode: Subscriber (BROADCAST)\n");
        printf("Total subscribers: %d\n", total_subscribers);
        printf("Test duration: %.1f seconds\n\n", test_duration_ms / 1000.0);
        run_subscriber_mode(address, num_threads, subscribers_per_thread, test_duration_ms);
        _exit(0);
    }
    
    /* Default: SERVER_CLIENT mode */
    if (latency_mode) {
        printf("Mode: Client (SERVER_CLIENT)\n");
        printf("Test Mode: Latency\n\n");
        run_latency_test(address, 1000, low_latency);
    } else {
        int total_clients = num_threads * clients_per_thread;
        
        if (fork_mode) {
            printf("Mode: Client (SERVER_CLIENT) - Fork (Multi-Process)\n");
            printf("Loops/Processes: %d\n", num_threads);
            printf("Clients per loop: %d\n", clients_per_thread);
            printf("Total clients: %d\n", total_clients);
            printf("Test Mode: Throughput\n\n");
            
            run_fork_mode(address, num_threads, clients_per_thread, test_duration_ms, low_latency);
        } else {
            printf("Mode: Client (SERVER_CLIENT) - Thread (Shared Loop)\n");
            printf("Threads: %d\n", num_threads);
            printf("Clients per thread: %d\n", clients_per_thread);
            printf("Total clients: %d\n", total_clients);
            printf("Concurrency: %d\n", concurrency);
            printf("Test Mode: Throughput (%.1f seconds)\n\n", test_duration_ms / 1000.0);
            
            if (num_threads == 1) {
                run_single_multi_test(address, clients_per_thread, concurrency, test_duration_ms, low_latency);
            } else {
                run_multi_thread_test(address, num_threads, clients_per_thread, concurrency, test_duration_ms, low_latency, timer_interval_ms);
            }
        }
    }

    /* Force exit to avoid hanging on libuv thread pool cleanup */
    _exit(0);
    
    return 0;
}

/* ==================== BROADCAST MODE FUNCTIONS ==================== */

/* Publisher callback */
static void publish_callback(int status, void* ctx) {
    (void)ctx;
    if (status != UVRPC_OK) {
        atomic_fetch_add(&g_response_sum, 1);  /* Count as error */
    }
}

/* Publisher thread context */
typedef struct {
    int thread_id;
    int num_publishers;
    int batch_size;
    int test_duration_ms;
    const char* address;
    atomic_int* total_messages;
    atomic_int* total_bytes;
} publisher_thread_context_t;

/* Timer callback for publisher test duration */
static void publisher_timer_callback(uv_timer_t* handle) {
    int* done = (int*)handle->data;
    *done = 1;
}

/* Publisher thread function */
static void* publisher_thread_func(void* arg) {
    publisher_thread_context_t* ctx = (publisher_thread_context_t*)arg;
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    uvrpc_publisher_t** publishers = (uvrpc_publisher_t**)malloc(sizeof(uvrpc_publisher_t*) * ctx->num_publishers);
    if (!publishers) {
        return NULL;
    }
    
    /* Create publishers */
    for (int i = 0; i < ctx->num_publishers; i++) {
        uvrpc_config_t* config = uvrpc_config_new();
        uvrpc_config_set_loop(config, &loop);
        uvrpc_config_set_address(config, ctx->address);
        uvrpc_config_set_comm_type(config, UVRPC_COMM_BROADCAST);
        
        publishers[i] = uvrpc_publisher_create(config);
        uvrpc_config_free(config);
        
        if (!publishers[i]) {
            fprintf(stderr, "Failed to create publisher %d in thread %d\n", i, ctx->thread_id);
            continue;
        }
        
        if (uvrpc_publisher_start(publishers[i]) != UVRPC_OK) {
            fprintf(stderr, "Failed to start publisher %d in thread %d\n", i, ctx->thread_id);
            uvrpc_publisher_free(publishers[i]);
            publishers[i] = NULL;
            continue;
        }
    }
    
    /* Prepare message */
    const char* message = "UVRPC Broadcast Benchmark Message";
    size_t msg_size = strlen(message);
    
    /* Timer for test duration */
    int done = 0;
    uv_timer_t timer;
    uv_timer_init(&loop, &timer);
    timer.data = &done;
    uv_timer_start(&timer, publisher_timer_callback, ctx->test_duration_ms, 0);
    
    /* Publish messages */
    while (!done) {
        for (int i = 0; i < ctx->num_publishers; i++) {
            if (!publishers[i]) continue;
            
            for (int j = 0; j < ctx->batch_size; j++) {
                uvrpc_publisher_publish(publishers[i], "benchmark_topic", 
                                       (const uint8_t*)message, msg_size, 
                                       publish_callback, NULL);
                atomic_fetch_add(ctx->total_messages, 1);
                atomic_fetch_add(ctx->total_bytes, (int)msg_size);
            }
        }
        
        uv_run(&loop, UV_RUN_NOWAIT);
    }
    
    /* Cleanup */
    uv_close((uv_handle_t*)&timer, NULL);
    for (int i = 0; i < ctx->num_publishers; i++) {
        if (publishers[i]) {
            uvrpc_publisher_stop(publishers[i]);
            uvrpc_publisher_free(publishers[i]);
        }
    }
    free(publishers);
    
    /* Drain event loop */
    for (int i = 0; i < 10; i++) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }
    uv_loop_close(&loop);
    
    return NULL;
}

/* Run publisher mode */
static void run_publisher_mode(const char* address, int num_threads, int publishers_per_thread, 
                               int batch_size, int test_duration_ms) {
    pthread_t* threads = (pthread_t*)malloc(sizeof(pthread_t) * num_threads);
    publisher_thread_context_t* contexts = (publisher_thread_context_t*)malloc(sizeof(publisher_thread_context_t) * num_threads);
    
    atomic_int total_messages = 0;
    atomic_int total_bytes = 0;
    
    /* Start publisher threads */
    for (int i = 0; i < num_threads; i++) {
        contexts[i].thread_id = i;
        contexts[i].num_publishers = publishers_per_thread;
        contexts[i].batch_size = batch_size;
        contexts[i].test_duration_ms = test_duration_ms;
        contexts[i].address = address;
        contexts[i].total_messages = &total_messages;
        contexts[i].total_bytes = &total_bytes;
        
        pthread_create(&threads[i], NULL, publisher_thread_func, &contexts[i]);
    }
    
    /* Wait for all threads */
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    
    /* Print results */
    double duration_sec = test_duration_ms / 1000.0;
    int messages_sent = atomic_load(&total_messages);
    int bytes_sent = atomic_load(&total_bytes);
    
    printf("\n=== Publisher Results ===\n");
    printf("Messages sent: %d\n", messages_sent);
    printf("Bytes sent: %d\n", bytes_sent);
    printf("Throughput: %.0f msgs/s\n", messages_sent / duration_sec);
    printf("Bandwidth: %.2f MB/s\n", (bytes_sent / 1024.0 / 1024.0) / duration_sec);
    
    free(threads);
    free(contexts);
}

/* Subscriber callback */
static void subscribe_callback(const char* topic, const uint8_t* data, size_t size, void* ctx) {
    (void)topic;
    (void)data;
    
    publisher_thread_context_t* pub_ctx = (publisher_thread_context_t*)ctx;
    atomic_fetch_add(pub_ctx->total_messages, 1);
    atomic_fetch_add(pub_ctx->total_bytes, (int)size);
}

/* Subscriber thread function */
static void* subscriber_thread_func(void* arg) {
    publisher_thread_context_t* ctx = (publisher_thread_context_t*)arg;
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    int num_subscribers = ctx->num_publishers;  /* Reuse this field for subscribers */
    uvrpc_subscriber_t** subscribers = (uvrpc_subscriber_t**)malloc(sizeof(uvrpc_subscriber_t*) * num_subscribers);
    if (!subscribers) {
        return NULL;
    }
    
    /* Create subscribers */
    for (int i = 0; i < num_subscribers; i++) {
        uvrpc_config_t* config = uvrpc_config_new();
        uvrpc_config_set_loop(config, &loop);
        uvrpc_config_set_address(config, ctx->address);
        uvrpc_config_set_comm_type(config, UVRPC_COMM_BROADCAST);
        
        subscribers[i] = uvrpc_subscriber_create(config);
        uvrpc_config_free(config);
        
        if (!subscribers[i]) {
            fprintf(stderr, "Failed to create subscriber %d in thread %d\n", i, ctx->thread_id);
            continue;
        }
        
        if (uvrpc_subscriber_connect(subscribers[i]) != UVRPC_OK) {
            fprintf(stderr, "Failed to connect subscriber %d in thread %d\n", i, ctx->thread_id);
            uvrpc_subscriber_free(subscribers[i]);
            subscribers[i] = NULL;
            continue;
        }
        
        if (uvrpc_subscriber_subscribe(subscribers[i], "benchmark_topic", 
                                       subscribe_callback, ctx) != UVRPC_OK) {
            fprintf(stderr, "Failed to subscribe %d in thread %d\n", i, ctx->thread_id);
        }
    }
    
    /* Timer for test duration */
    int done = 0;
    uv_timer_t timer;
    uv_timer_init(&loop, &timer);
    timer.data = &done;
    uv_timer_start(&timer, publisher_timer_callback, ctx->test_duration_ms, 0);
    
    /* Run event loop */
    while (!done) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }
    
    /* Cleanup */
    uv_close((uv_handle_t*)&timer, NULL);
    for (int i = 0; i < num_subscribers; i++) {
        if (subscribers[i]) {
            uvrpc_subscriber_unsubscribe(subscribers[i], "benchmark_topic");
            uvrpc_subscriber_disconnect(subscribers[i]);
            uvrpc_subscriber_free(subscribers[i]);
        }
    }
    free(subscribers);
    
    /* Drain event loop */
    for (int i = 0; i < 10; i++) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }
    uv_loop_close(&loop);
    
    return NULL;
}

/* Run subscriber mode */
static void run_subscriber_mode(const char* address, int num_threads, int subscribers_per_thread, 
                                int test_duration_ms) {
    pthread_t* threads = (pthread_t*)malloc(sizeof(pthread_t) * num_threads);
    publisher_thread_context_t* contexts = (publisher_thread_context_t*)malloc(sizeof(publisher_thread_context_t) * num_threads);
    
    atomic_int total_messages = 0;
    atomic_int total_bytes = 0;
    
    /* Start subscriber threads */
    for (int i = 0; i < num_threads; i++) {
        contexts[i].thread_id = i;
        contexts[i].num_publishers = subscribers_per_thread;  /* Reuse for subscribers */
        contexts[i].test_duration_ms = test_duration_ms;
        contexts[i].address = address;
        contexts[i].total_messages = &total_messages;
        contexts[i].total_bytes = &total_bytes;
        
        pthread_create(&threads[i], NULL, subscriber_thread_func, &contexts[i]);
    }
    
    /* Wait for all threads */
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    
    /* Print results */
    double duration_sec = test_duration_ms / 1000.0;
    int messages_received = atomic_load(&total_messages);
    int bytes_received = atomic_load(&total_bytes);
    
    printf("\n=== Subscriber Results ===\n");
    printf("Messages received: %d\n", messages_received);
    printf("Bytes received: %d\n", bytes_received);
    printf("Throughput: %.0f msgs/s\n", messages_received / duration_sec);
    printf("Bandwidth: %.2f MB/s\n", (bytes_received / 1024.0 / 1024.0) / duration_sec);
    
    free(threads);
    free(contexts);
}
