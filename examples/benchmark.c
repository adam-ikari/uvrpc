/**
 * UVRPC Benchmark Program
 * 
 * Usage:
 *   ./benchmark [options]
 * 
 * Options:
 *   -t, --transport <tcp|udp|ipc|inproc>  Transport type (default: inproc)
 *   -p, --port <port>                     Port for TCP/UDP (default: 5555)
 *   -a, --address <addr>                  Address for TCP/UDP (default: 127.0.0.1)
 *   -s, --size <bytes>                    Payload size in bytes (default: 1024)
 *   -c, --clients <num>                   Number of clients (default: 1)
 *   -n, --concurrency <num>               Concurrent requests per client (default: 10)
 *   -r, --requests <num>                  Total requests per client (default: 10000)
 *   -T, --timeout <ms>                    Request timeout in ms (default: 5000)
 *   -w, --warmup <num>                    Warmup requests (default: 100)
 *   -v, --verbose                         Verbose output
 *   -h, --help                            Show this help
 */

#include "../include/uvrpc.h"
#include "../include/uvrpc_async.h"
#include "../include/uvrpc_allocator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <inttypes.h>
#include <time.h>

/* Statistics */
typedef struct {
    uint64_t total_requests;
    uint64_t successful_requests;
    uint64_t failed_requests;
    uint64_t timeout_requests;
    uint64_t total_latency_ns;
    uint64_t min_latency_ns;
    uint64_t max_latency_ns;
    uint64_t p50_latency_ns;
    uint64_t p95_latency_ns;
    uint64_t p99_latency_ns;
    uint64_t start_time_ns;
    uint64_t end_time_ns;
} benchmark_stats_t;

/* Benchmark context */
typedef struct {
    int client_id;
    uv_loop_t* loop;
    uvrpc_client_t* client;
    benchmark_stats_t* stats;
    int total_requests;
    int completed_requests;
    int concurrency;
    int timeout_ms;
    char* payload;
    size_t payload_size;
    int verbose;
    uint64_t start_time_ns;
} benchmark_ctx_t;

/* Command line options */
typedef struct {
    uvrpc_transport_type transport;
    char* address;
    int port;
    size_t payload_size;
    int num_clients;
    int concurrency;
    int requests_per_client;
    int timeout_ms;
    int warmup;
    int verbose;
} benchmark_options_t;

/* Global signal flag */
static volatile int g_running = 1;

/* Signal handler */
static void signal_handler(int signum) {
    (void)signum;
    g_running = 0;
}

/* Get current time in nanoseconds */
static uint64_t get_time_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* Echo handler */
static void echo_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    uvrpc_request_send_response(req, UVRPC_OK, 
        req->params, req->params_size);
}

/* Calculate percentiles */
static void calculate_percentiles(uint64_t* latencies, size_t count, 
                                  uint64_t* p50, uint64_t* p95, uint64_t* p99) {
    if (count == 0) {
        *p50 = *p95 = *p99 = 0;
        return;
    }
    
    /* Simple bubble sort for percentiles (efficient enough for benchmarks) */
    for (size_t i = 0; i < count - 1; i++) {
        for (size_t j = 0; j < count - i - 1; j++) {
            if (latencies[j] > latencies[j + 1]) {
                uint64_t tmp = latencies[j];
                latencies[j] = latencies[j + 1];
                latencies[j + 1] = tmp;
            }
        }
    }
    
    *p50 = latencies[(size_t)(count * 0.50)];
    *p95 = latencies[(size_t)(count * 0.95)];
    *p99 = latencies[(size_t)(count * 0.99)];
}

/* Client callback */
static void benchmark_callback(uvrpc_response_t* resp, void* ctx) {
    benchmark_ctx_t* bctx = (benchmark_ctx_t*)ctx;
    
    uint64_t now = get_time_ns();
    
    /* Calculate latency (approximate, use request start time in real implementation) */
    uint64_t latency = now - bctx->start_time_ns; /* Placeholder */
    
    /* Update statistics */
    __sync_fetch_and_add(&bctx->stats->total_requests, 1);
    
    if (resp && resp->status == UVRPC_OK && resp->error_code == 0) {
        __sync_fetch_and_add(&bctx->stats->successful_requests, 1);
        __sync_fetch_and_add(&bctx->stats->total_latency_ns, latency);
        
        /* Update min/max latency */
        uint64_t current_min = bctx->stats->min_latency_ns;
        while (latency < current_min && 
               !__sync_bool_compare_and_swap(&bctx->stats->min_latency_ns, current_min, latency)) {
            current_min = bctx->stats->min_latency_ns;
        }
        
        uint64_t current_max = bctx->stats->max_latency_ns;
        while (latency > current_max && 
               !__sync_bool_compare_and_swap(&bctx->stats->max_latency_ns, current_max, latency)) {
            current_max = bctx->stats->max_latency_ns;
        }
    } else if (resp && resp->error_code == (int32_t)UVRPC_ERROR_TIMEOUT) {
        __sync_fetch_and_add(&bctx->stats->timeout_requests, 1);
    } else {
        __sync_fetch_and_add(&bctx->stats->failed_requests, 1);
    }
    
    bctx->completed_requests++;
    
    if (bctx->verbose) {
        printf("[Client %d] Request %d/%d completed\n", 
               bctx->client_id, bctx->completed_requests, bctx->total_requests);
    }
    
    uvrpc_response_free(resp);
}

/* Run benchmark for a single client */
static int run_benchmark_client(benchmark_ctx_t* bctx, benchmark_options_t* opts) {
    int requests_sent = 0;
    
    /* Using simple sequential calls for simplicity */
    while (requests_sent < bctx->total_requests && g_running) {
        for (int i = 0; i < bctx->concurrency && requests_sent < bctx->total_requests; i++) {
            uvrpc_client_call(bctx->client, "echo", 
                             (uint8_t*)bctx->payload, bctx->payload_size,
                             benchmark_callback, bctx);
            requests_sent++;
        }
        
        /* Process events */
        uv_run(bctx->loop, UV_RUN_NOWAIT);
    }
    
    /* Wait for all responses */
    while (bctx->completed_requests < requests_sent && g_running) {
        uv_run(bctx->loop, UV_RUN_ONCE);
    }
    
    return 0;
}

/* Print usage */
static void print_usage(const char* prog) {
    printf("Usage: %s [options]\n\n", prog);
    printf("Options:\n");
    printf("  -t, --transport <tcp|udp|ipc|inproc>  Transport type (default: inproc)\n");
    printf("  -p, --port <port>                     Port for TCP/UDP (default: 5555)\n");
    printf("  -a, --address <addr>                  Address for TCP/UDP (default: 127.0.0.1)\n");
    printf("  -s, --size <bytes>                    Payload size in bytes (default: 1024)\n");
    printf("  -c, --clients <num>                   Number of clients (default: 1)\n");
    printf("  -n, --concurrency <num>               Concurrent requests per client (default: 10)\n");
    printf("  -r, --requests <num>                  Total requests per client (default: 10000)\n");
    printf("  -T, --timeout <ms>                    Request timeout in ms (default: 5000)\n");
    printf("  -w, --warmup <num>                    Warmup requests (default: 100)\n");
    printf("  -v, --verbose                         Verbose output\n");
    printf("  -h, --help                            Show this help\n");
}

/* Parse arguments */
static int parse_arguments(int argc, char** argv, benchmark_options_t* opts) {
    memset(opts, 0, sizeof(benchmark_options_t));
    
    /* Defaults */
    opts->transport = UVRPC_TRANSPORT_INPROC;
    opts->address = strdup("127.0.0.1");
    opts->port = 5555;
    opts->payload_size = 1024;
    opts->num_clients = 1;
    opts->concurrency = 10;
    opts->requests_per_client = 10000;
    opts->timeout_ms = 5000;
    opts->warmup = 100;
    opts->verbose = 0;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--transport") == 0) {
            if (i + 1 >= argc) return -1;
            i++;
            if (strcmp(argv[i], "tcp") == 0) opts->transport = UVRPC_TRANSPORT_TCP;
            else if (strcmp(argv[i], "udp") == 0) opts->transport = UVRPC_TRANSPORT_UDP;
            else if (strcmp(argv[i], "ipc") == 0) opts->transport = UVRPC_TRANSPORT_IPC;
            else if (strcmp(argv[i], "inproc") == 0) opts->transport = UVRPC_TRANSPORT_INPROC;
            else return -1;
        } else if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--port") == 0) {
            if (i + 1 >= argc) return -1;
            opts->port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--address") == 0) {
            if (i + 1 >= argc) return -1;
            free(opts->address);
            opts->address = strdup(argv[++i]);
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--size") == 0) {
            if (i + 1 >= argc) return -1;
            opts->payload_size = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--clients") == 0) {
            if (i + 1 >= argc) return -1;
            opts->num_clients = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--concurrency") == 0) {
            if (i + 1 >= argc) return -1;
            opts->concurrency = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--requests") == 0) {
            if (i + 1 >= argc) return -1;
            opts->requests_per_client = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-T") == 0 || strcmp(argv[i], "--timeout") == 0) {
            if (i + 1 >= argc) return -1;
            opts->timeout_ms = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-w") == 0 || strcmp(argv[i], "--warmup") == 0) {
            if (i + 1 >= argc) return -1;
            opts->warmup = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            opts->verbose = 1;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            exit(0);
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return -1;
        }
    }
    
    return 0;
}

/* Build address string */
static char* build_address(benchmark_options_t* opts) {
    char* addr = (char*)malloc(256);
    switch (opts->transport) {
        case UVRPC_TRANSPORT_TCP:
            snprintf(addr, 256, "tcp://%s:%d", opts->address, opts->port);
            break;
        case UVRPC_TRANSPORT_UDP:
            snprintf(addr, 256, "udp://%s:%d", opts->address, opts->port);
            break;
        case UVRPC_TRANSPORT_IPC:
            snprintf(addr, 256, "ipc://uvrpc_benchmark");
            break;
        case UVRPC_TRANSPORT_INPROC:
            snprintf(addr, 256, "inproc://uvrpc_benchmark");
            break;
        default:
            snprintf(addr, 256, "inproc://uvrpc_benchmark");
            break;
    }
    return addr;
}

/* Print statistics */
static void print_statistics(benchmark_stats_t* stats, benchmark_options_t* opts) {
    double duration_sec = (double)(stats->end_time_ns - stats->start_time_ns) / 1e9;
    double throughput = stats->successful_requests / duration_sec;
    double avg_latency_ms = (stats->total_latency_ns / 1e6) / stats->successful_requests;
    
    printf("\n=== Benchmark Results ===\n");
    printf("Transport: %s\n", 
           opts->transport == UVRPC_TRANSPORT_TCP ? "TCP" :
           opts->transport == UVRPC_TRANSPORT_UDP ? "UDP" :
           opts->transport == UVRPC_TRANSPORT_IPC ? "IPC" : "INPROC");
    printf("Payload size: %zu bytes\n", opts->payload_size);
    printf("Clients: %d\n", opts->num_clients);
    printf("Concurrency per client: %d\n", opts->concurrency);
    printf("Total requests: %" PRIu64 "\n", stats->total_requests);
    printf("Successful: %" PRIu64 " (%.2f%%)\n", stats->successful_requests, 
           (double)stats->successful_requests / stats->total_requests * 100);
    printf("Failed: %" PRIu64 "\n", stats->failed_requests);
    printf("Timeout: %" PRIu64 "\n", stats->timeout_requests);
    printf("\n");
    printf("Duration: %.3f seconds\n", duration_sec);
    printf("Throughput: %.2f req/sec\n", throughput);
    printf("Average latency: %.3f ms\n", avg_latency_ms);
    printf("Min latency: %.3f ms\n", stats->min_latency_ns / 1e6);
    printf("Max latency: %.3f ms\n", stats->max_latency_ns / 1e6);
    printf("P50 latency: %.3f ms\n", stats->p50_latency_ns / 1e6);
    printf("P95 latency: %.3f ms\n", stats->p95_latency_ns / 1e6);
    printf("P99 latency: %.3f ms\n", stats->p99_latency_ns / 1e6);
    printf("========================\n\n");
}

int main(int argc, char** argv) {
    benchmark_options_t opts;
    
    printf("UVRPC Benchmark\n");
    printf("================\n\n");
    
    /* Parse arguments */
    if (parse_arguments(argc, argv, &opts) != 0) {
        print_usage(argv[0]);
        return 1;
    }
    
    /* Setup signal handlers */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    /* Build address */
    char* address = build_address(&opts);
    printf("Address: %s\n", address);
    printf("Payload size: %zu bytes\n", opts.payload_size);
    printf("Clients: %d\n", opts.num_clients);
    printf("Concurrency: %d\n", opts.concurrency);
    printf("Requests per client: %d\n", opts.requests_per_client);
    printf("Total requests: %d\n", opts.num_clients * opts.requests_per_client);
    printf("Warmup: %d\n", opts.warmup);
    printf("\n");
    
    /* Create payload */
    char* payload = (char*)malloc(opts.payload_size + 1);
    for (size_t i = 0; i < opts.payload_size; i++) {
        payload[i] = 'A' + (i % 26);
    }
    payload[opts.payload_size] = '\0';
    
    /* Initialize statistics */
    benchmark_stats_t stats;
    memset(&stats, 0, sizeof(stats));
    stats.min_latency_ns = UINT64_MAX;
    
    /* Create server */
    printf("Creating server...\n");
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    uvrpc_config_t* server_config = uvrpc_config_new();
    uvrpc_config_set_loop(server_config, &loop);
    uvrpc_config_set_address(server_config, address);
    uvrpc_config_set_transport(server_config, opts.transport);
    
    uvrpc_server_t* server = uvrpc_server_create(server_config);
    
    /* Register echo handler */
    uvrpc_server_register(server, "echo", echo_handler, NULL);
    
    /* Start server */
    int ret = uvrpc_server_start(server);
    if (ret != UVRPC_OK) {
        fprintf(stderr, "Failed to start server: %d\n", ret);
        return 1;
    }
    
    printf("Starting benchmark...\n");
    stats.start_time_ns = get_time_ns();
    
    /* Create clients and run benchmark */
    for (int client_id = 0; client_id < opts.num_clients && g_running; client_id++) {
        uv_loop_t* client_loop = &loop; /* Use same loop for simplicity */
        
        uvrpc_config_t* client_config = uvrpc_config_new();
        uvrpc_config_set_loop(client_config, client_loop);
        uvrpc_config_set_address(client_config, address);
        uvrpc_config_set_transport(client_config, opts.transport);
        
        uvrpc_client_t* client = uvrpc_client_create(client_config);
        int ret = uvrpc_client_connect(client);
        if (ret != UVRPC_OK) {
            fprintf(stderr, "Failed to connect client %d: %d\n", client_id, ret);
            continue;
        }
        
        benchmark_ctx_t bctx;
        memset(&bctx, 0, sizeof(bctx));
        bctx.client_id = client_id;
        bctx.loop = client_loop;
        bctx.client = client;
        bctx.stats = &stats;
        bctx.total_requests = opts.requests_per_client;
        bctx.concurrency = opts.concurrency;
        bctx.timeout_ms = opts.timeout_ms;
        bctx.payload = payload;
        bctx.payload_size = opts.payload_size;
        bctx.verbose = opts.verbose;
        bctx.start_time_ns = get_time_ns();
        
        /* Run benchmark for this client */
        run_benchmark_client(&bctx, &opts);
        
        /* Cleanup client */
        uvrpc_client_free(client);
        uvrpc_config_free(client_config);
    }
    
    stats.end_time_ns = get_time_ns();
    
    /* Wait for remaining events */
    for (int i = 0; i < 100 && g_running; i++) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }
    
    /* Print results */
    print_statistics(&stats, &opts);
    
    /* Cleanup */
    uvrpc_server_free(server);
    uvrpc_config_free(server_config);
    uv_loop_close(&loop);
    free(address);
    free(payload);
    free(opts.address);
    
    return 0;
}
