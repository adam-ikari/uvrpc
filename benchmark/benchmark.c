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
 * 
 * Architecture:
 *   - Each client runs in its own thread with its own libuv loop
 *   - No locks, no critical sections, zero synchronization
 *   - Each client has independent statistics
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
#include <pthread.h>
#include <unistd.h>

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
} benchmark_stats_t;

/* Benchmark options */
typedef struct {
    char* address;
    int port;
    int transport;
    size_t payload_size;
    int num_clients;
    int concurrency;
    int requests_per_client;
    int timeout_ms;
    int warmup;
    int verbose;
} benchmark_options_t;

/* Client context */
typedef struct {
    int client_id;
    benchmark_options_t* opts;
    uvrpc_client_t* client;
    uv_loop_t* loop;
    uvrpc_config_t* config;
    uvrpc_async_ctx_t* async_ctx;
    benchmark_stats_t* stats;
    uint64_t total_requests;
    int concurrency;
    int timeout_ms;
    uint8_t* payload;
    size_t payload_size;
    int verbose;
    uint64_t start_time_ns;
    volatile int connected;
} benchmark_ctx_t;

/* Connection context */
typedef struct {
    volatile int done;
    volatile int result;
} connect_ctx_t;

/* Global running flag */
static volatile int g_running = 1;

/* Signal handler */
void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
}

/* Get current time in nanoseconds */
static uint64_t get_time_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* Parse address string */
static void parse_address(const char* input, char** out_addr, int* out_transport) {
    if (strncmp(input, "tcp://", 6) == 0) {
        *out_addr = strdup(input + 6);
        *out_transport = UVRPC_TRANSPORT_TCP;
    } else if (strncmp(input, "udp://", 6) == 0) {
        *out_addr = strdup(input + 6);
        *out_transport = UVRPC_TRANSPORT_UDP;
    } else if (strncmp(input, "ipc://", 6) == 0) {
        *out_addr = strdup(input + 6);
        *out_transport = UVRPC_TRANSPORT_IPC;
    } else if (strncmp(input, "inproc://", 9) == 0) {
        *out_addr = strdup(input + 9);
        *out_transport = UVRPC_TRANSPORT_INPROC;
    } else {
        *out_addr = strdup(input);
        *out_transport = UVRPC_TRANSPORT_TCP;
    }
}

/* Parse options */
static int parse_options(int argc, char** argv, benchmark_options_t* opts) {
    memset(opts, 0, sizeof(benchmark_options_t));
    
    /* Defaults */
    opts->address = strdup("127.0.0.1");
    opts->port = 5555;
    opts->transport = UVRPC_TRANSPORT_INPROC;
    opts->payload_size = 1024;
    opts->num_clients = 1;
    opts->concurrency = 10;
    opts->requests_per_client = 10000;
    opts->timeout_ms = 5000;
    opts->warmup = 100;
    opts->verbose = 0;
    
    /* Parse command line */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            return -1;
        } else if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--address") == 0) {
            if (i + 1 < argc) {
                free(opts->address);
                opts->address = strdup(argv[++i]);
            }
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--size") == 0) {
            if (i + 1 < argc) {
                opts->payload_size = atoi(argv[++i]);
            }
        } else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--clients") == 0) {
            if (i + 1 < argc) {
                opts->num_clients = atoi(argv[++i]);
            }
        } else if (strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--concurrency") == 0) {
            if (i + 1 < argc) {
                opts->concurrency = atoi(argv[++i]);
            }
        } else if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--requests") == 0) {
            if (i + 1 < argc) {
                opts->requests_per_client = atoi(argv[++i]);
            }
        } else if (strcmp(argv[i], "-T") == 0 || strcmp(argv[i], "--timeout") == 0) {
            if (i + 1 < argc) {
                opts->timeout_ms = atoi(argv[++i]);
            }
        } else if (strcmp(argv[i], "-w") == 0 || strcmp(argv[i], "--warmup") == 0) {
            if (i + 1 < argc) {
                opts->warmup = atoi(argv[++i]);
            }
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            opts->verbose = 1;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return -1;
        }
    }
    
    /* Parse address string to get transport type */
    parse_address(opts->address, &opts->address, &opts->transport);
    
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
            snprintf(addr, 256, "ipc://%s", opts->address);
            break;
        case UVRPC_TRANSPORT_INPROC:
            snprintf(addr, 256, "inproc://%s", opts->address);
            break;
        default:
            snprintf(addr, 256, "tcp://%s:%d", opts->address, opts->port);
            break;
    }
    return addr;
}

/* Echo handler for server */
void echo_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    uvrpc_request_send_response(req, UVRPC_OK, req->params, req->params_size);
}

/* Response callback */
static void benchmark_response_callback(uvrpc_response_t* resp, void* ctx) {
    benchmark_ctx_t* bctx = (benchmark_ctx_t*)ctx;
    benchmark_stats_t* stats = bctx->stats;
    
    uint64_t end_time_ns = get_time_ns();
    uint64_t latency_ns = end_time_ns - (uint64_t)resp->user_data;
    
    stats->total_requests++;
    stats->total_latency_ns += latency_ns;
    
    if (latency_ns < stats->min_latency_ns) {
        stats->min_latency_ns = latency_ns;
    }
    if (latency_ns > stats->max_latency_ns) {
        stats->max_latency_ns = latency_ns;
    }
    
    if (resp->status == UVRPC_OK) {
        stats->successful_requests++;
    } else {
        stats->failed_requests++;
    }
    
    if (bctx->verbose) {
        printf("[Client %d] Response: status=%d, latency=%" PRIu64 " ns\n", 
               bctx->client_id, resp->status, latency_ns);
    }
}

/* Connect callback */
static void benchmark_connect_callback(int status, void* ctx) {
    connect_ctx_t* conn_ctx = (connect_ctx_t*)ctx;
    conn_ctx->result = status;
    conn_ctx->done = 1;
}

/* Client thread function */
static void* client_thread_func(void* arg) {
    benchmark_ctx_t* bctx = (benchmark_ctx_t*)arg;
    benchmark_stats_t* stats = bctx->stats;
    
    /* Initialize statistics */
    memset(stats, 0, sizeof(benchmark_stats_t));
    stats->min_latency_ns = UINT64_MAX;
    
    printf("[Client %d] Starting benchmark...\n", bctx->client_id);
    
    /* Run requests */
    uint64_t requests_sent = 0;
    while (g_running && requests_sent < bctx->total_requests) {
        /* Run event loop */
        uv_run(bctx->loop, UV_RUN_NOWAIT);
        
        /* Send requests */
        int batch_size = bctx->concurrency;
        for (int i = 0; i < batch_size && requests_sent < bctx->total_requests; i++) {
            uint64_t request_time = get_time_ns();
            
            uvrpc_async_result_t* result = NULL;
            int ret = uvrpc_client_call_async(bctx->async_ctx, bctx->client, "echo", 
                                              bctx->payload, bctx->payload_size,
                                              &result);
            if (ret == UVRPC_OK && result) {
                uvrpc_async_result_free(result);
                requests_sent++;
            } else if (bctx->verbose) {
                printf("[Client %d] Failed to send request: %d\n", bctx->client_id, ret);
            }
            
            /* Run event loop after each batch */
            if (i % 5 == 0) {
                uv_run(bctx->loop, UV_RUN_NOWAIT);
            }
        }
        
        /* Run event loop to process responses */
        uv_run(bctx->loop, UV_RUN_NOWAIT);
        
        /* Small delay to avoid CPU spinning */
        usleep(1000);
    }
    
    /* Wait for all pending responses */
    uint64_t wait_start = get_time_ns();
    while (g_running && stats->successful_requests + stats->failed_requests < requests_sent) {
        uv_run(bctx->loop, UV_RUN_NOWAIT);
        
        /* Timeout after 10 seconds */
        if ((get_time_ns() - wait_start) > 10000000000ULL) {
            stats->timeout_requests = requests_sent - (stats->successful_requests + stats->failed_requests);
            break;
        }
        
        usleep(10000);
    }
    
    printf("[Client %d] Completed: %lu/%lu requests\n", 
           bctx->client_id, stats->successful_requests + stats->failed_requests, bctx->total_requests);
    
    return NULL;
}

/* Print statistics */
static void print_statistics(benchmark_stats_t* stats, double duration_sec, int num_clients) {
    uint64_t total_requests = stats->total_requests;
    uint64_t successful = stats->successful_requests;
    uint64_t failed = stats->failed_requests;
    uint64_t timeout = stats->timeout_requests;
    
    double throughput = total_requests / duration_sec;
    double success_rate = total_requests > 0 ? (double)successful / total_requests * 100.0 : 0.0;
    
    printf("\n=== Benchmark Results ===\n");
    printf("Total clients: %d\n", num_clients);
    printf("Total requests: %lu\n", total_requests);
    printf("Successful: %lu (%.2f%%)\n", successful, success_rate);
    printf("Failed: %lu\n", failed);
    printf("Timeout: %lu\n", timeout);
    printf("Duration: %.3f seconds\n", duration_sec);
    printf("Throughput: %.2f requests/second\n", throughput);
    
    if (total_requests > 0) {
        double avg_latency = (double)stats->total_latency_ns / total_requests / 1000000.0;
        double min_latency = (double)stats->min_latency_ns / 1000000.0;
        double max_latency = (double)stats->max_latency_ns / 1000000.0;
        
        printf("\nLatency (ms):\n");
        printf("  Average: %.3f ms\n", avg_latency);
        printf("  Min: %.3f ms\n", min_latency);
        printf("  Max: %.3f ms\n", max_latency);
    }
    printf("========================\n");
}

int main(int argc, char** argv) {
    benchmark_options_t opts;
    
    /* Parse options */
    if (parse_options(argc, argv, &opts) != 0) {
        printf("UVRPC Benchmark\n");
        printf("================\n\n");
        printf("Usage: %s [options]\n\n", argv[0]);
        printf("Options:\n");
        printf("  -a, --address <addr>      Address string (default: inproc://uvrpc_benchmark)\n");
        printf("  -s, --size <bytes>        Payload size in bytes (default: 1024)\n");
        printf("  -c, --clients <num>       Number of clients (default: 1)\n");
        printf("  -n, --concurrency <num>   Concurrent requests per client (default: 10)\n");
        printf("  -r, --requests <num>      Total requests per client (default: 10000)\n");
        printf("  -T, --timeout <ms>        Request timeout in ms (default: 5000)\n");
        printf("  -w, --warmup <num>        Warmup requests (default: 100)\n");
        printf("  -v, --verbose             Verbose output\n");
        printf("  -h, --help                Show this help\n");
        return 0;
    }
    
    /* Setup signal handler */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    /* Create payload */
    uint8_t* payload = (uint8_t*)malloc(opts.payload_size);
    memset(payload, 'A', opts.payload_size);
    
    /* Create server */
    printf("Creating server...\n");
    uv_loop_t server_loop;
    uv_loop_init(&server_loop);
    
    char* server_addr = build_address(&opts);
    uvrpc_config_t* server_config = uvrpc_config_new();
    uvrpc_config_set_loop(server_config, &server_loop);
    uvrpc_config_set_address(server_config, server_addr);
    uvrpc_config_set_transport(server_config, opts.transport);
    
    uvrpc_server_t* server = uvrpc_server_create(server_config);
    
    /* Register echo handler */
    uvrpc_server_register(server, "echo", echo_handler, NULL);
    
    /* Start server */
    int ret = uvrpc_server_start(server);
    if (ret != UVRPC_OK) {
        fprintf(stderr, "Failed to start server: %d\n", ret);
        free(server_addr);
        return 1;
    }
    
    free(server_addr);
    printf("Server started\n\n");
    
    /* Allocate statistics */
    benchmark_stats_t* client_stats = (benchmark_stats_t*)calloc(opts.num_clients, sizeof(benchmark_stats_t));
    
    /* Start benchmark */
    printf("Starting benchmark with %d clients in separate threads...\n", opts.num_clients);
    
    /* Create arrays for client contexts and threads */
    benchmark_ctx_t* client_contexts = (benchmark_ctx_t*)calloc(opts.num_clients, sizeof(benchmark_ctx_t));
    pthread_t* client_threads = (pthread_t*)calloc(opts.num_clients, sizeof(pthread_t));
    
    /* Create and start client threads */
    for (int client_id = 0; client_id < opts.num_clients && g_running; client_id++) {
        benchmark_ctx_t* bctx = &client_contexts[client_id];
        
        /* Create separate loop for this client */
        bctx->loop = (uv_loop_t*)malloc(sizeof(uv_loop_t));
        uv_loop_init(bctx->loop);
        
        /* Build client address */
        char* client_addr = build_address(&opts);
        
        /* Create config */
        bctx->config = uvrpc_config_new();
        uvrpc_config_set_loop(bctx->config, bctx->loop);
        uvrpc_config_set_address(bctx->config, client_addr);
        uvrpc_config_set_transport(bctx->config, opts.transport);
        
        /* Create and connect client */
        bctx->client = uvrpc_client_create(bctx->config);
        
        /* Connect with callback to wait for connection */
        connect_ctx_t conn_ctx;
        conn_ctx.done = 0;
        conn_ctx.result = 0;
        
        ret = uvrpc_client_connect_with_callback(bctx->client,
            benchmark_connect_callback,
            &conn_ctx);
        
        if (ret != UVRPC_OK) {
            fprintf(stderr, "Failed to initiate connection for client %d: %d\n", client_id, ret);
            free(client_addr);
            continue;
        }
        
        /* Wait for connection to complete */
        int wait_count = 0;
        while (!conn_ctx.done && wait_count < 100 && g_running) {
            uv_run(bctx->loop, UV_RUN_NOWAIT);
            usleep(10000);  // 10ms
            wait_count++;
        }
        
        free(client_addr);
        
        if (!conn_ctx.done) {
            fprintf(stderr, "Client %d connection timeout\n", client_id);
            continue;
        }
        
        if (conn_ctx.result != UVRPC_OK) {
            fprintf(stderr, "Client %d connection failed: %d\n", client_id, conn_ctx.result);
            continue;
        }
        
        /* Create async context */
        bctx->async_ctx = uvrpc_async_ctx_new(bctx->loop);
        if (!bctx->async_ctx) {
            fprintf(stderr, "Failed to create async context for client %d\n", client_id);
            continue;
        }
        
        /* Initialize context */
        bctx->client_id = client_id;
        bctx->stats = &client_stats[client_id];
        bctx->opts = &opts;
        bctx->total_requests = opts.requests_per_client;
        bctx->concurrency = opts.concurrency;
        bctx->timeout_ms = opts.timeout_ms;
        bctx->payload = payload;
        bctx->payload_size = opts.payload_size;
        bctx->verbose = opts.verbose;
        bctx->start_time_ns = get_time_ns();
        bctx->connected = 1;
        
        /* Create thread for this client */
        pthread_create(&client_threads[client_id], NULL, client_thread_func, bctx);
    }
    
    /* Start global timer */
    uint64_t global_start_ns = get_time_ns();
    
    /* Run server event loop in main thread */
    printf("Running server event loop...\n");
    int running_clients = opts.num_clients;
    while (g_running && running_clients > 0) {
        uv_run(&server_loop, UV_RUN_ONCE);
        
        /* Check if all clients are done */
        running_clients = 0;
        for (int i = 0; i < opts.num_clients; i++) {
            if (client_contexts[i].connected && 
                (client_stats[i].successful_requests + client_stats[i].failed_requests + client_stats[i].timeout_requests) < opts.requests_per_client) {
                running_clients++;
            }
        }
        
        if (running_clients > 0) {
            usleep(10000);  // 10ms
        }
    }
    
    uint64_t global_end_ns = get_time_ns();
    
    /* Wait for all threads to complete */
    for (int client_id = 0; client_id < opts.num_clients; client_id++) {
        pthread_join(client_threads[client_id], NULL);
    }
    
    /* Aggregate statistics from all clients */
    benchmark_stats_t aggregated_stats;
    memset(&aggregated_stats, 0, sizeof(aggregated_stats));
    aggregated_stats.min_latency_ns = UINT64_MAX;
    
    for (int i = 0; i < opts.num_clients; i++) {
        aggregated_stats.total_requests += client_stats[i].total_requests;
        aggregated_stats.successful_requests += client_stats[i].successful_requests;
        aggregated_stats.failed_requests += client_stats[i].failed_requests;
        aggregated_stats.timeout_requests += client_stats[i].timeout_requests;
        aggregated_stats.total_latency_ns += client_stats[i].total_latency_ns;
        
        if (client_stats[i].min_latency_ns < aggregated_stats.min_latency_ns) {
            aggregated_stats.min_latency_ns = client_stats[i].min_latency_ns;
        }
        if (client_stats[i].max_latency_ns > aggregated_stats.max_latency_ns) {
            aggregated_stats.max_latency_ns = client_stats[i].max_latency_ns;
        }
    }
    
    /* Print results */
    double duration_sec = (double)(global_end_ns - global_start_ns) / 1000000000.0;
    print_statistics(&aggregated_stats, duration_sec, opts.num_clients);
    
    /* Cleanup */
    for (int i = 0; i < opts.num_clients; i++) {
        if (client_contexts[i].client) {
            uvrpc_client_disconnect(client_contexts[i].client);
            uvrpc_client_free(client_contexts[i].client);
        }
        if (client_contexts[i].async_ctx) {
            uvrpc_async_ctx_free(client_contexts[i].async_ctx);
        }
        if (client_contexts[i].loop) {
            uv_loop_close(client_contexts[i].loop);
            free(client_contexts[i].loop);
        }
        if (client_contexts[i].config) {
            uvrpc_config_free(client_contexts[i].config);
        }
    }
    
    free(client_contexts);
    free(client_threads);
    free(client_stats);
    
    uvrpc_server_free(server);
    uvrpc_config_free(server_config);
    uv_loop_close(&server_loop);
    
    free(payload);
    free(opts.address);
    
    return 0;
}
