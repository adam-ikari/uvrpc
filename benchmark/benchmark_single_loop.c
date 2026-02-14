/**
 * UVRPC Benchmark - Single Event Loop with Context Injection
 */

#include "../include/uvrpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>

typedef struct {
    uint64_t total_requests;
    uint64_t successful_requests;
    uint64_t failed_requests;
    uint64_t total_latency_ns;
    uint64_t min_latency_ns;
    uint64_t max_latency_ns;
} benchmark_stats_t;

typedef struct benchmark_ctx {
    int client_id;
    uvrpc_client_t* client;
    benchmark_stats_t* stats;
    int total_requests;
    int completed_requests;
    char* payload;
    size_t payload_size;
} benchmark_ctx_t;

typedef struct request_context {
    int request_id;
    uint64_t start_time_ns;
    benchmark_ctx_t* client_ctx;
} request_context_t;

static uv_loop_t* g_loop;
static uvrpc_server_t* g_server;
static benchmark_ctx_t* g_client_contexts;
static int g_num_clients;
static int g_all_clients_done;

static uint64_t get_time_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void echo_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    uvrpc_request_send_response(req, UVRPC_OK, req->params, req->params_size);
}

static void benchmark_callback(uvrpc_response_t* resp, void* ctx) {
    request_context_t* req_ctx = (request_context_t*)ctx;
    benchmark_ctx_t* bctx = req_ctx->client_ctx;

    uint64_t now = get_time_ns();
    uint64_t latency = now - req_ctx->start_time_ns;

    bctx->stats->total_requests++;

    if (resp && resp->status == UVRPC_OK && resp->error_code == 0) {
        bctx->stats->successful_requests++;
        bctx->stats->total_latency_ns += latency;

        if (latency < bctx->stats->min_latency_ns) {
            bctx->stats->min_latency_ns = latency;
        }
        if (latency > bctx->stats->max_latency_ns) {
            bctx->stats->max_latency_ns = latency;
        }
    } else {
        bctx->stats->failed_requests++;
    }

    bctx->completed_requests++;

    uvrpc_response_free(resp);
    free(req_ctx);

    if (bctx->completed_requests >= bctx->total_requests) {
        int all_done = 1;
        for (int i = 0; i < g_num_clients; i++) {
            if (g_client_contexts[i].completed_requests < g_client_contexts[i].total_requests) {
                all_done = 0;
                break;
            }
        }
        if (all_done) {
            g_all_clients_done = 1;
        }
    }
}

static char* create_payload(size_t size) {
    char* payload = (char*)malloc(size + 1);
    for (size_t i = 0; i < size; i++) {
        payload[i] = 'A' + (i % 26);
    }
    payload[size] = '\0';
    return payload;
}

int main(int argc, char** argv) {
    char* address = strdup("tcp://127.0.0.1:6666");
    size_t payload_size = 1024;
    int num_clients = 1;
    int requests_per_client = 100;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--address") == 0) {
            if (i + 1 >= argc) { printf("Usage: %s [-a address] [-s size] [-c clients] [-r requests]\n", argv[0]); return 1; }
            free(address);
            address = strdup(argv[++i]);
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--size") == 0) {
            if (i + 1 >= argc) { printf("Usage: %s [-a address] [-s size] [-c clients] [-r requests]\n", argv[0]); return 1; }
            payload_size = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--clients") == 0) {
            if (i + 1 >= argc) { printf("Usage: %s [-a address] [-s size] [-c clients] [-r requests]\n", argv[0]); return 1; }
            num_clients = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--requests") == 0) {
            if (i + 1 >= argc) { printf("Usage: %s [-a address] [-s size] [-c clients] [-r requests]\n", argv[0]); return 1; }
            requests_per_client = atoi(argv[++i]);
        }
    }

    printf("UVRPC Benchmark (Single Loop with Context Injection)\n");
    printf("==================================================\n\n");
    printf("Address: %s\n", address);
    printf("Payload size: %zu bytes\n", payload_size);
    printf("Clients: %d\n", num_clients);
    printf("Requests per client: %d\n", requests_per_client);
    printf("Total requests: %d\n\n", num_clients * requests_per_client);
    fflush(stdout);

    g_num_clients = num_clients;
    g_all_clients_done = 0;

    g_loop = uv_loop_new();
    printf("Event loop created\n");
    fflush(stdout);

    uvrpc_config_t* server_config = uvrpc_config_new();
    uvrpc_config_set_loop(server_config, g_loop);
    uvrpc_config_set_address(server_config, address);
    uvrpc_config_set_transport(server_config, UVRPC_TRANSPORT_TCP);

    g_server = uvrpc_server_create(server_config);
    uvrpc_server_register(g_server, "echo", echo_handler, NULL);
    uvrpc_server_start(g_server);
    printf("Server started\n");
    fflush(stdout);

    g_client_contexts = (benchmark_ctx_t*)calloc(num_clients, sizeof(benchmark_ctx_t));
    benchmark_stats_t* client_stats = (benchmark_stats_t*)calloc(num_clients, sizeof(benchmark_stats_t));

    for (int i = 0; i < num_clients; i++) {
        benchmark_ctx_t* bctx = &g_client_contexts[i];
        bctx->client_id = i;
        bctx->stats = &client_stats[i];
        bctx->total_requests = requests_per_client;
        bctx->completed_requests = 0;
        bctx->payload_size = payload_size;
        bctx->payload = create_payload(payload_size);
        bctx->stats->min_latency_ns = UINT64_MAX;

        uvrpc_config_t* client_config = uvrpc_config_new();
        uvrpc_config_set_loop(client_config, g_loop);
        uvrpc_config_set_address(client_config, address);
        uvrpc_config_set_transport(client_config, UVRPC_TRANSPORT_TCP);

        bctx->client = uvrpc_client_create(client_config);
        uvrpc_client_connect(bctx->client);

        printf("Client %d created and connected\n", i);
        fflush(stdout);
    }

    printf("Waiting for connections to establish...\n");
    fflush(stdout);
    for (int i = 0; i < 100; i++) {
        uv_run(g_loop, UV_RUN_ONCE);
    }

    printf("Sending requests...\n");
    fflush(stdout);
    uint64_t global_start_ns = get_time_ns();

    for (int i = 0; i < num_clients; i++) {
        benchmark_ctx_t* bctx = &g_client_contexts[i];
        for (int j = 0; j < requests_per_client; j++) {
            request_context_t* req_ctx = (request_context_t*)malloc(sizeof(request_context_t));
            req_ctx->client_ctx = bctx;
            req_ctx->request_id = j;
            req_ctx->start_time_ns = get_time_ns();

            uvrpc_client_call(bctx->client, "echo",
                            (uint8_t*)bctx->payload, bctx->payload_size,
                            benchmark_callback, req_ctx);
        }
    }

    printf("All requests sent, waiting for responses...\n");
    fflush(stdout);

    while (!g_all_clients_done) {
        uv_run(g_loop, UV_RUN_ONCE);
    }

    uint64_t global_end_ns = get_time_ns();

    benchmark_stats_t aggregated_stats;
    memset(&aggregated_stats, 0, sizeof(aggregated_stats));
    aggregated_stats.min_latency_ns = UINT64_MAX;

    for (int i = 0; i < num_clients; i++) {
        aggregated_stats.total_requests += client_stats[i].total_requests;
        aggregated_stats.successful_requests += client_stats[i].successful_requests;
        aggregated_stats.failed_requests += client_stats[i].failed_requests;
        aggregated_stats.total_latency_ns += client_stats[i].total_latency_ns;

        if (client_stats[i].min_latency_ns < aggregated_stats.min_latency_ns) {
            aggregated_stats.min_latency_ns = client_stats[i].min_latency_ns;
        }
        if (client_stats[i].max_latency_ns > aggregated_stats.max_latency_ns) {
            aggregated_stats.max_latency_ns = client_stats[i].max_latency_ns;
        }
    }

    printf("\n=== Benchmark Results ===\n");
    printf("Address: %s\n", address);
    printf("Payload size: %zu bytes\n", payload_size);
    printf("Clients: %d\n", num_clients);
    printf("Total requests: %" PRIu64 "\n", aggregated_stats.total_requests);
    printf("Successful: %" PRIu64 " (%.2f%%)\n", aggregated_stats.successful_requests,
           (double)aggregated_stats.successful_requests / aggregated_stats.total_requests * 100);
    printf("Failed: %" PRIu64 "\n", aggregated_stats.failed_requests);
    printf("\n");
    printf("Duration: %.3f seconds\n", (global_end_ns - global_start_ns) / 1e9);
    printf("Throughput: %.2f req/sec\n",
           aggregated_stats.successful_requests / ((global_end_ns - global_start_ns) / 1e9));
    printf("Average latency: %.3f ms\n",
           (aggregated_stats.total_latency_ns / 1e6) / aggregated_stats.successful_requests);
    printf("Min latency: %.3f ms\n", aggregated_stats.min_latency_ns / 1e6);
    printf("Max latency: %.3f ms\n", aggregated_stats.max_latency_ns / 1e6);
    printf("========================\n\n");
    fflush(stdout);

    for (int i = 0; i < num_clients; i++) {
        uvrpc_client_free(g_client_contexts[i].client);
        free(g_client_contexts[i].payload);
    }

    uvrpc_server_free(g_server);
    uvrpc_config_free(server_config);

    uv_loop_close(g_loop);
    free(g_loop);

    free(g_client_contexts);
    free(client_stats);
    free(address);

    return 0;
}
