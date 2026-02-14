/**
 * UVRPC Benchmark - Multi-Process (Simulating independent clients)
 *
 * Architecture:
 *   - Server runs in parent process
 *   - Each client runs in separate forked process
 *   - Uses TCP transport (avoids global INPROC registry)
 *   - Zero shared state - each process has independent statistics
 */

#include "../include/uvrpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <inttypes.h>
#include <time.h>

/* Statistics */
typedef struct {
    uint64_t total_requests;
    uint64_t successful_requests;
    uint64_t failed_requests;
    uint64_t total_latency_ns;
    uint64_t min_latency_ns;
    uint64_t max_latency_ns;
} benchmark_stats_t;

/* Benchmark context */
typedef struct benchmark_ctx {
    int client_id;
    uvrpc_client_t* client;
    benchmark_stats_t* stats;
    int total_requests;
    int completed_requests;
    char* payload;
    size_t payload_size;
} benchmark_ctx_t;

/* Request context */
typedef struct request_context {
    int request_id;
    uint64_t start_time_ns;
    benchmark_ctx_t* client_ctx;
} request_context_t;

/* Get current time in nanoseconds */
static uint64_t get_time_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* Echo handler (server only) */
static void echo_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    uvrpc_request_send_response(req, UVRPC_OK,
        req->params, req->params_size);
}

/* Request callback */
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
}

/* Create payload */
static char* create_payload(size_t size) {
    char* payload = (char*)malloc(size + 1);
    for (size_t i = 0; i < size; i++) {
        payload[i] = 'A' + (i % 26);
    }
    payload[size] = '\0';
    return payload;
}

/* Run server in parent process */
static int run_server(const char* address) {
    printf("Server: Starting...\n");
    fflush(stdout);

    uv_loop_t loop;
    uv_loop_init(&loop);

    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, &loop);
    uvrpc_config_set_address(config, address);
    uvrpc_config_set_transport(config, UVRPC_TRANSPORT_TCP);

    uvrpc_server_t* server = uvrpc_server_create(config);
    uvrpc_server_register(server, "echo", echo_handler, NULL);
    uvrpc_server_start(server);

    printf("Server: Ready and listening on %s\n", address);
    fflush(stdout);

    /* Run event loop forever */
    uv_run(&loop, UV_RUN_DEFAULT);

    /* Cleanup (never reached in this design) */
    uvrpc_server_free(server);
    uvrpc_config_free(config);
    uv_loop_close(&loop);

    return 0;
}

/* Run client in child process */
static int run_client(const char* address, int client_id, int num_requests, size_t payload_size) {
    printf("[Client %d] Starting...\n", client_id);
    fflush(stdout);

    uv_loop_t loop;
    uv_loop_init(&loop);

    /* Create client */
    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, &loop);
    uvrpc_config_set_address(config, address);
    uvrpc_config_set_transport(config, UVRPC_TRANSPORT_TCP);

    benchmark_ctx_t bctx;
    memset(&bctx, 0, sizeof(bctx));
    bctx.client_id = client_id;
    bctx.stats = (benchmark_stats_t*)calloc(1, sizeof(benchmark_stats_t));
    bctx.total_requests = num_requests;
    bctx.completed_requests = 0;
    bctx.payload_size = payload_size;
    bctx.payload = create_payload(payload_size);
    bctx.stats->min_latency_ns = UINT64_MAX;

    bctx.client = uvrpc_client_create(config);
    int connect_ret = uvrpc_client_connect(bctx.client);
    printf("[Client %d] Connect returned: %d\n", client_id, connect_ret);
    fflush(stdout);

    /* Wait for connection */
    printf("[Client %d] Waiting for connection...\n", client_id);
    fflush(stdout);
    for (int i = 0; i < 100; i++) {
        uv_run(&loop, UV_RUN_ONCE);
        if (i % 10 == 0) {
            printf("[Client %d] Loop iteration %d\n", client_id, i);
            fflush(stdout);
        }
    }
    printf("[Client %d] Connection wait done\n", client_id);
    fflush(stdout);

    /* Send all requests */
    printf("[Client %d] Sending %d requests...\n", client_id, num_requests);
    fflush(stdout);

    for (int i = 0; i < num_requests; i++) {
        request_context_t* req_ctx = (request_context_t*)malloc(sizeof(request_context_t));
        req_ctx->client_ctx = &bctx;
        req_ctx->request_id = i;
        req_ctx->start_time_ns = get_time_ns();

        uvrpc_client_call(bctx.client, "echo",
                        (uint8_t*)bctx.payload, bctx.payload_size,
                        benchmark_callback, req_ctx);
    }

    /* Wait for all responses */
    printf("[Client %d] Waiting for responses...\n", client_id);
    fflush(stdout);

    int iterations = 0;
    while (bctx.completed_requests < num_requests && iterations < 10000) {
        uv_run(&loop, UV_RUN_ONCE);
        iterations++;
    }

    /* Print client results */
    printf("[Client %d] Completed: %d/%d requests\n",
           client_id, bctx.completed_requests, num_requests);
    printf("[Client %d] Success: %" PRIu64 ", Failed: %" PRIu64 "\n",
           client_id, bctx.stats->successful_requests, bctx.stats->failed_requests);
    if (bctx.stats->successful_requests > 0) {
        printf("[Client %d] Avg latency: %.3f ms\n",
               client_id,
               (bctx.stats->total_latency_ns / 1e6) / bctx.stats->successful_requests);
    }
    fflush(stdout);

    /* Cleanup */
    uvrpc_client_free(bctx.client);
    uvrpc_config_free(config);
    uv_loop_close(&loop);
    free(bctx.payload);
    free(bctx.stats);

    return 0;
}

int main(int argc, char** argv) {
    char* address = strdup("tcp://127.0.0.1:6666");
    size_t payload_size = 1024;
    int num_clients = 2;
    int requests_per_client = 100;

    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--address") == 0) {
            if (i + 1 >= argc) {
                printf("Usage: %s [-a address] [-s size] [-c clients] [-r requests]\n", argv[0]);
                return 1;
            }
            free(address);
            address = strdup(argv[++i]);
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--size") == 0) {
            if (i + 1 >= argc) {
                printf("Usage: %s [-a address] [-s size] [-c clients] [-r requests]\n", argv[0]);
                return 1;
            }
            payload_size = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--clients") == 0) {
            if (i + 1 >= argc) {
                printf("Usage: %s [-a address] [-s size] [-c clients] [-r requests]\n", argv[0]);
                return 1;
            }
            num_clients = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--requests") == 0) {
            if (i + 1 >= argc) {
                printf("Usage: %s [-a address] [-s size] [-c clients] [-r requests]\n", argv[0]);
                return 1;
            }
            requests_per_client = atoi(argv[++i]);
        }
    }

    printf("UVRPC Benchmark - Multi-Process\n");
    printf("===============================\n\n");
    printf("Address: %s\n", address);
    printf("Payload size: %zu bytes\n", payload_size);
    printf("Clients: %d (separate processes)\n", num_clients);
    printf("Requests per client: %d\n", requests_per_client);
    printf("Total requests: %d\n\n", num_clients * requests_per_client);
    fflush(stdout);

    /* Fork server process */
    pid_t server_pid = fork();
    if (server_pid < 0) {
        perror("fork server");
        return 1;
    }

    if (server_pid == 0) {
        /* Child process: run server */
        return run_server(address);
    }

    /* Parent process: wait a bit for server to start */
    sleep(1);

    /* Fork client processes */
    pid_t* client_pids = (pid_t*)malloc(num_clients * sizeof(pid_t));
    uint64_t global_start_ns = get_time_ns();

    for (int i = 0; i < num_clients; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork client");
            return 1;
        }

        if (pid == 0) {
            /* Child process: run client */
            free(client_pids);
            return run_client(address, i, requests_per_client, payload_size);
        }

        client_pids[i] = pid;
    }

    /* Wait for all client processes to complete */
    for (int i = 0; i < num_clients; i++) {
        int status;
        waitpid(client_pids[i], &status, 0);
        printf("Client %d process exited (status=%d)\n", i, WEXITSTATUS(status));
        fflush(stdout);
    }

    uint64_t global_end_ns = get_time_ns();

    /* Kill server process */
    printf("\nStopping server...\n");
    fflush(stdout);
    kill(server_pid, SIGTERM);
    waitpid(server_pid, NULL, 0);

    /* Note: We cannot aggregate statistics across processes in this design
       because each process has independent memory. For aggregated stats,
       we would need shared memory or IPC. */

    printf("\n=== Benchmark Summary ===\n");
    printf("Total duration: %.3f seconds\n", (global_end_ns - global_start_ns) / 1e9);
    printf("Note: Each client process reported its own statistics above.\n");
    printf("========================\n\n");
    fflush(stdout);

    free(client_pids);
    free(address);

    return 0;
}