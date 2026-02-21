/* Unified Benchmark - Fully event-driven with UV_RUN_DEFAULT + signals
 * 
 * Usage: 
 *   First start server: ./benchmark --server -a tcp://127.0.0.1:5555
 *   Then run tests:    ./benchmark [options]
 */

#include "../generated/benchmark_server_reader.h"
#include "../generated/benchmark_server_builder.h"
#include "../include/uvrpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <getopt.h>
#include <uv.h>
#include <errno.h>
#include <signal.h>
#include <stdatomic.h>

#define DEFAULT_ADDRESS "tcp://127.0.0.1:5555"
#define DEFAULT_REQUESTS 10000
#define DEFAULT_CLIENTS 4

/* ============================================================
 * Server (UV_RUN_DEFAULT + signals)
 * ============================================================ */

static void log_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    flatcc_builder_t builder;
    flatcc_builder_init(&builder);
    benchmark_EmptyResponse_start_as_root(&builder);
    benchmark_EmptyResponse_end_as_root(&builder);
    size_t size;
    void* buf = flatcc_builder_finalize_buffer(&builder, &size);
    uvrpc_request_send_response(req, UVRPC_OK, buf, size);
    free(buf);
    flatcc_builder_clear(&builder);
}

static void add_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    benchmark_AddRequest_table_t req_data = benchmark_AddRequest_as_root(req->params);
    int32_t a = benchmark_AddRequest_a(req_data);
    int32_t b = benchmark_AddRequest_b(req_data);
    int32_t result = a + b;
    
    flatcc_builder_t builder;
    flatcc_builder_init(&builder);
    benchmark_AddResponse_start_as_root(&builder);
    benchmark_AddResponse_result_add(&builder, result);
    benchmark_AddResponse_end_as_root(&builder);
    size_t size;
    void* buf = flatcc_builder_finalize_buffer(&builder, &size);
    uvrpc_request_send_response(req, UVRPC_OK, buf, size);
    free(buf);
    flatcc_builder_clear(&builder);
}

/* Server signal handler */
static void on_server_signal(uv_signal_t* handle, int signum) {
    (void)signum;
    printf("\n[SERVER] Received signal, shutting down...\n");
    fflush(stdout);
    uv_stop(handle->loop);
}

static int run_server(const char* address) {
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* Signal handlers - UV_RUN_DEFAULT style */
    static uv_signal_t sigint_sig, sigterm_sig;
    uv_signal_init(&loop, &sigint_sig);
    uv_signal_start(&sigint_sig, on_server_signal, SIGINT);
    uv_signal_init(&loop, &sigterm_sig);
    uv_signal_start(&sigterm_sig, on_server_signal, SIGTERM);
    
    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, &loop);
    uvrpc_config_set_address(config, address);
    uvrpc_config_set_comm_type(config, UVRPC_COMM_SERVER_CLIENT);
    /* Large buffer for logging service: handle high throughput */
    uvrpc_config_set_max_pending_callbacks(config, 262144);  /* 256K */
    
    uvrpc_server_t* server = uvrpc_server_create(config);
    uvrpc_config_free(config);
    
    uvrpc_server_register(server, "Log", log_handler, NULL);
    uvrpc_server_register(server, "Add", add_handler, NULL);
    
    int ret = uvrpc_server_start(server);
    if (ret != UVRPC_OK) {
        fprintf(stderr, "Failed to start server: %d\n", ret);
        uv_signal_stop(&sigint_sig);
        uv_signal_stop(&sigterm_sig);
        uvrpc_server_free(server);
        uv_loop_close(&loop);
        return -1;
    }
    
    printf("Server started on %s\n", address);
    printf("Press Ctrl+C to stop\n");
    fflush(stdout);
    
    /* UV_RUN_DEFAULT - driven by signals */
    uv_run(&loop, UV_RUN_DEFAULT);
    
    uvrpc_server_free(server);
    uv_loop_close(&loop);
    return 0;
}

/* ============================================================
 * Client Test
 * ============================================================ */

typedef struct {
    int connected;
    atomic_int sent;
    atomic_int completed;
    atomic_int failed;
    uvrpc_client_t* client;
} client_test_t;

/* Response callback */
static void on_resp(uvrpc_response_t* resp, void* ctx) {
    client_test_t* c = (client_test_t*)ctx;
    if (resp->status == UVRPC_OK) {
        atomic_fetch_add(&c->completed, 1);
    }
}

/* Connect callback */
static void on_conn(int status, void* ctx) {
    client_test_t* c = (client_test_t*)ctx;
    if (status == 0) {
        c->connected = 1;
    } else {
        /* Connection failed - will be retried by timer */
        printf("[CLIENT] Connection failed: %d, will retry...\n", status);
    }
}

/* Timer callback to check if all clients are connected */
static void check_connections(uv_timer_t* handle) {
    client_test_t* clients = (client_test_t*)handle->data;
    int num_clients = (int)(intptr_t)handle->loop->data;
    int all_connected = 1;
    for (int i = 0; i < num_clients; i++) {
        if (!clients[i].connected) {
            all_connected = 0;
            break;
        }
    }
    if (all_connected) {
        uv_stop(handle->loop);
    }
}

/* Test regular RPC */
static int test_regular(const char* address, int num_requests, int num_clients) {
    printf("\n=== Regular RPC Test ===\n");
    printf("Requests: %d, Clients: %d\n", num_requests, num_clients);
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* Create clients */
    client_test_t* clients = calloc(num_clients, sizeof(client_test_t));
    
    for (int i = 0; i < num_clients; i++) {
        uvrpc_config_t* cfg = uvrpc_config_new();
        uvrpc_config_set_loop(cfg, &loop);
        uvrpc_config_set_address(cfg, address);
        uvrpc_config_set_comm_type(cfg, UVRPC_COMM_SERVER_CLIENT);
        uvrpc_config_set_max_pending_callbacks(cfg, 64);
        uvrpc_config_set_max_concurrent(cfg, 128);
        
        clients[i].client = uvrpc_client_create(cfg);
        uvrpc_config_free(cfg);

        if (!clients[i].client) {
            fprintf(stderr, "Failed to create client %d\n", i);
            continue;
        }

        if (clients[i].client) {
            uvrpc_client_connect_with_callback(clients[i].client, on_conn, &clients[i]);
        }
    }
    
    /* Wait for connections - run loop until all clients call back with connected status */
    uv_timer_t check_timer;
    uv_timer_init(&loop, &check_timer);
    check_timer.data = clients;
    loop.data = (void*)(intptr_t)num_clients;
    
    uv_timer_start(&check_timer, check_connections, 10, 10);
    
    /* Run until all connected */
    uv_run(&loop, UV_RUN_DEFAULT);
    uv_timer_stop(&check_timer);
    uv_close((uv_handle_t*)&check_timer, NULL);
    uv_run(&loop, UV_RUN_NOWAIT); /* Cleanup handles */
    
    /* Verify all clients connected */
    int all_connected = 1;
    
    /* Send requests - let client handle backpressure naturally
     * Ring buffer in client will signal when full */
    struct timeval start, end;
    gettimeofday(&start, NULL);

    int sent = 0;
    int completed = 0;

    while (sent < num_requests || completed < num_requests) {

        /* Send batch - pump loop when batch is full */
        while (sent < num_requests) {
            int client_idx = sent % num_clients;

            flatcc_builder_t builder;
            flatcc_builder_init(&builder);
            benchmark_AddRequest_start_as_root(&builder);
            benchmark_AddRequest_a_add(&builder, 100);
            benchmark_AddRequest_b_add(&builder, 200);
            benchmark_AddRequest_end_as_root(&builder);
            size_t size;
            void* buf = flatcc_builder_finalize_buffer(&builder, &size);

            int ret = uvrpc_client_call(clients[client_idx].client, "Add", buf, size, on_resp, &clients[client_idx]);
            free(buf);
            flatcc_builder_clear(&builder);

            if (ret == UVRPC_OK) {
                sent++;
                atomic_fetch_add(&clients[client_idx].sent, 1);
            } else {
                atomic_fetch_add(&clients[client_idx].failed, 1);
                /* Ring buffer full - pump loop to process and make space */
                uv_run(&loop, UV_RUN_ONCE);
                /* If still failing, continue pumping */
                if (sent < num_requests) continue;
            }

            /* Pump loop every 100 sends to process responses */
            if (sent % 100 == 0) break;
        }
        
        /* Pump loop to process responses */
        uv_run(&loop, UV_RUN_ONCE);
        
        /* Check completed */
        completed = 0;
        for (int i = 0; i < num_clients; i++) {
            completed += atomic_load(&clients[i].completed);
        }
        
        if (sent >= num_requests && completed < num_requests) {
            /* All sent, wait for remaining responses */
        } else if (completed >= num_requests) {
            break;
        }
    }
    
    gettimeofday(&end, NULL);
    double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;
    printf("Completed: %d, Elapsed: %.2fs, Throughput: %.2f ops/sec\n", 
           completed, elapsed, completed / elapsed);
    
    /* Cleanup */
    for (int i = 0; i < num_clients; i++) {
        if (clients[i].client) uvrpc_client_free(clients[i].client);
    }
    free(clients);
    uv_loop_close(&loop);
    
    return 0;
}

/* Timer callback declarations */
static void check_connections(uv_timer_t* handle);
static void pump_keepalive(uv_timer_t* handle);
static void stop_loop_timer(uv_timer_t* handle);

/* Test oneway RPC */
static int test_oneway(const char* address, int num_requests, int num_clients) {
    printf("\n=== Oneway RPC Test ===\n");
    printf("Requests: %d, Clients: %d\n", num_requests, num_clients);
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    client_test_t* clients = calloc(num_clients, sizeof(client_test_t));
    
    /* Create and connect */
    for (int i = 0; i < num_clients; i++) {
        uvrpc_config_t* cfg = uvrpc_config_new();
        uvrpc_config_set_loop(cfg, &loop);
        uvrpc_config_set_address(cfg, address);
        uvrpc_config_set_comm_type(cfg, UVRPC_COMM_SERVER_CLIENT);
        /* Small buffer: ring buffer full = natural backpressure signal */
        uvrpc_config_set_max_pending_callbacks(cfg, 64);
        
        clients[i].client = uvrpc_client_create(cfg);
        uvrpc_config_free(cfg);
        
        if (clients[i].client) {
            uvrpc_client_connect_with_callback(clients[i].client, on_conn, &clients[i]);
        }
    }
    
    /* Wait for connections - single UV_RUN_DEFAULT call handles all async operations */
    uv_run(&loop, UV_RUN_DEFAULT);
    
    /* Verify all clients connected */
    int all_connected = 1;
    for (int i = 0; i < num_clients; i++) {
        if (!clients[i].connected) {
            all_connected = 0;
            break;
        }
    }
    
    if (!all_connected) {
        printf("Warning: not all clients connected\n");
    }
    printf("Connected: %d\n", all_connected ? num_clients : 0);
    
    /* Send requests - Oneway fire-and-forget, pump handled by client automatically */
    struct timeval start, end;
    gettimeofday(&start, NULL);
    
    int sent = 0;
    int dropped = 0;
    while (sent < num_requests) {
        int client_idx = sent % num_clients;
        
        flatcc_builder_t builder;
        flatcc_builder_init(&builder);
        benchmark_LogRequest_start_as_root(&builder);
        benchmark_LogRequest_level_add(&builder, sent % 4);
        benchmark_LogRequest_message_add(&builder, flatcc_builder_create_string_str(&builder, "Test"));
        benchmark_LogRequest_timestamp_add(&builder, time(NULL));
        benchmark_LogRequest_source_add(&builder, flatcc_builder_create_string_str(&builder, "bench"));
        benchmark_LogRequest_end_as_root(&builder);
        size_t size;
        void* buf = flatcc_builder_finalize_buffer(&builder, &size);
        
        int ret = uvrpc_client_call_oneway(clients[client_idx].client, "Log", buf, size);
        if (ret == UVRPC_OK) {
            sent++;
            atomic_fetch_add(&clients[client_idx].sent, 1);
        } else {
            /* Buffer full - log dropped message */
            dropped++;
            if (dropped <= 10) {
                fprintf(stderr, "[DROP] Oneway buffer full (error=%d)\n", ret);
            }
        }
        free(buf);
        flatcc_builder_clear(&builder);
    }
    
    if (dropped > 10) {
        fprintf(stderr, "[DROP] ... and %d more messages dropped\n", dropped - 10);
    }
    
    /* Add a stop timer to terminate loop after pumping */
    uv_timer_t stop_timer;
    uv_timer_init(&loop, &stop_timer);
    uv_timer_start(&stop_timer, stop_loop_timer, 100, 0); /* 100ms delay for pump */
    
    /* Run event loop to pump all messages via timer */
    uv_run(&loop, UV_RUN_DEFAULT);
    
    gettimeofday(&end, NULL);
    double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;
    printf("Sent: %d, Dropped: %d, Elapsed: %.2fs, Throughput: %.2f ops/sec\n", 
           sent, dropped, elapsed, sent / elapsed);
    
    /* Cleanup */
    for (int i = 0; i < num_clients; i++) {
        if (clients[i].client) uvrpc_client_free(clients[i].client);
    }
    free(clients);
    uv_loop_close(&loop);
    
    return 0;
}

/* Timer callback to stop event loop after pumping */
static void stop_loop_timer(uv_timer_t* handle) {
    uv_stop(handle->loop);
}

/* Keep-alive timer callback to stop loop after pumping is done */
static void pump_keepalive(uv_timer_t* handle) {
    int* pump_done = (int*)handle->data;
    if (*pump_done) {
        uv_stop(handle->loop);
    }
}

/* Compare test */
static int test_compare(const char* address, int num_requests, int num_clients) {
    printf("\n========================================");
    printf("\n=== Oneway vs Regular RPC Comparison ===");
    printf("\n========================================\n");
    
    test_oneway(address, num_requests, num_clients);

    /* Delay to allow server to clean up connections */
    printf("\nWaiting for server to clean up...\n");
    usleep(1000000); /* 1s - wait longer for server to stabilize */

    printf("\n--- Regular ---\n");
    test_regular(address, num_requests, num_clients);
    
    printf("\n========================================\n");
    return 0;
}

/* ============================================================
 * Main
 * ============================================================ */

static void print_usage(const char* prog) {
    printf("Usage: %s [options]\n", prog);
    printf("\nServer Mode:\n");
    printf("  --server    Run in server mode\n");
    printf("  -a <addr>  Server address (default: %s)\n", DEFAULT_ADDRESS);
    printf("\nClient Mode:\n");
    printf("  -n <num>   Requests (default: %d)\n", DEFAULT_REQUESTS);
    printf("  -c <num>   Clients (default: %d)\n", DEFAULT_CLIENTS);
    printf("  -a <addr>  Server address\n");
    printf("  -t <type>  Test: oneway, regular, compare\n");
    printf("  -h         Help\n");
}

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    
    int run_server_mode = 0;
    const char* address = DEFAULT_ADDRESS;
    int num_requests = DEFAULT_REQUESTS;
    int num_clients = DEFAULT_CLIENTS;
    const char* test_type = "compare";
    
    static struct option long_options[] = {
        {"server", no_argument, 0, 's'},
        {0, 0, 0, 0}
    };
    
    int opt;
    int option_index = 0;
    while ((opt = getopt_long(argc, argv, "a:n:c:t:h", long_options, &option_index)) != -1) {
        switch (opt) {
            case 's': run_server_mode = 1; break;
            case 'a': address = optarg; break;
            case 'n': num_requests = atoi(optarg); break;
            case 'c': num_clients = atoi(optarg); break;
            case 't': test_type = optarg; break;
            case 'h': print_usage(argv[0]); return 0;
            default: print_usage(argv[0]); return 1;
        }
    }
    
    if (run_server_mode) {
        printf("========================================\n");
        printf("UVRPC Benchmark Server (UV_RUN_DEFAULT)\n");
        printf("========================================\n");
        return run_server(address);
    }
    
    printf("========================================\n");
    printf("UVRPC Benchmark Client\n");
    printf("========================================\n");
    printf("Server: %s\n", address);
    printf("Requests: %d\n", num_requests);
    printf("Clients: %d\n", num_clients);
    printf("========================================\n");
    
    /* Wait for server */
    printf("Waiting for server...\n");
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    int port = 5555;
    if (strncmp(address, "tcp://", 6) == 0) {
        const char* colon = strchr(address + 6, ':');
        if (colon) port = atoi(colon + 1);
    }
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    
    int tries = 0;
    while (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0 && tries < 50) {
        usleep(100000);
        tries++;
    }
    close(sock);
    
    if (tries >= 50) {
        fprintf(stderr, "Server not ready at %s\n", address);
        return 1;
    }
    printf("Server ready!\n");
    
    int ret = 0;
    if (strcmp(test_type, "oneway") == 0) {
        ret = test_oneway(address, num_requests, num_clients);
    } else if (strcmp(test_type, "regular") == 0) {
        ret = test_regular(address, num_requests, num_clients);
    } else {
        ret = test_compare(address, num_requests, num_clients);
    }
    
    printf("\nDone.\n");
    return ret;
}