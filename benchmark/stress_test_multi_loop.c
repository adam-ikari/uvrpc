/**
 * UVRPC Multi-Loop Stress Test
 * Tests multiple independent event loops with sequential client access
 */

#include "../include/uvrpc.h"
#include "../generated/rpc_benchmark/rpc_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#define NUM_CLIENTS 5
#define REQUESTS_PER_CLIENT 5000
#define SERVER_ADDRESS "tcp://127.0.0.1:6666"

static volatile sig_atomic_t g_running = 1;

typedef struct {
    int client_id;
    uv_loop_t loop;
    uvrpc_client_t* client;
    int requests_sent;
    int requests_received;
    int failed;
} client_context_t;

void signal_handler(int signum) {
    (void)signum;
    g_running = 0;
}

void on_response_callback(uvrpc_response_t* resp, void* ctx) {
    (void)resp;
    client_context_t* client_ctx = (client_context_t*)ctx;
    client_ctx->requests_received++;
}

void on_connect_callback(int status, void* ctx) {
    client_context_t* client_ctx = (client_context_t*)ctx;
    if (status == 0) {
        printf("[Client %d] Connected\n", client_ctx->client_id);
    } else {
        fprintf(stderr, "[Client %d] Connection failed: %d\n", client_ctx->client_id, status);
        client_ctx->failed = 1;
    }
}

int run_client(client_context_t* client_ctx) {
    printf("[Client %d] Starting...\n", client_ctx->client_id);
    fflush(stdout);
    
    /* Create configuration */
    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, &client_ctx->loop);
    uvrpc_config_set_address(config, SERVER_ADDRESS);
    uvrpc_config_set_comm_type(config, UVRPC_COMM_SERVER_CLIENT);
    
    /* Create client */
    client_ctx->client = uvrpc_client_create(config);
    uvrpc_config_free(config);
    
    if (!client_ctx->client) {
        fprintf(stderr, "[Client %d] Failed to create client\n", client_ctx->client_id);
        client_ctx->failed = 1;
        return -1;
    }
    
    /* Connect */
    int ret = uvrpc_client_connect_with_callback(
        client_ctx->client, 
        on_connect_callback, 
        client_ctx
    );
    
    if (ret != UVRPC_OK) {
        fprintf(stderr, "[Client %d] Failed to connect: %d\n", client_ctx->client_id, ret);
        client_ctx->failed = 1;
        uvrpc_client_free(client_ctx->client);
        return -1;
    }
    
    /* Run loop until connected (check via callback) */
    int connect_attempts = 0;
    while (client_ctx->requests_received == 0 && connect_attempts < 100) {
        uv_run(&client_ctx->loop, UV_RUN_ONCE);
        connect_attempts++;
    }
    
    if (connect_attempts >= 100) {
        fprintf(stderr, "[Client %d] Connection timeout\n", client_ctx->client_id);
        client_ctx->failed = 1;
        uvrpc_client_free(client_ctx->client);
        return -1;
    }
    
    /* Send requests */
    printf("[Client %d] Sending %d requests...\n", client_ctx->client_id, REQUESTS_PER_CLIENT);
    fflush(stdout);
    
    flatcc_builder_t builder;
    
    for (int i = 0; i < REQUESTS_PER_CLIENT && g_running; i++) {
        flatcc_builder_init(&builder);
        
        rpc_BenchmarkAddRequest_start_as_root(&builder);
        rpc_BenchmarkAddRequest_a_add(&builder, i);
        rpc_BenchmarkAddRequest_b_add(&builder, i + 1);
        rpc_BenchmarkAddRequest_end_as_root(&builder);
        
        size_t size;
        void* buf = flatcc_builder_finalize_buffer(&builder, &size);
        
        if (buf) {
            ret = uvrpc_client_call(
                client_ctx->client,
                "Add",
                buf,
                size,
                on_response_callback,
                client_ctx
            );
            
            if (ret == UVRPC_OK) {
                client_ctx->requests_sent++;
            } else {
                client_ctx->failed++;
            }
            
            flatcc_builder_reset(&builder);
        }
        
        /* Process events every 100 requests */
        if (i % 100 == 0) {
            for (int j = 0; j < 10; j++) {
                uv_run(&client_ctx->loop, UV_RUN_ONCE);
            }
        }
    }
    
    /* Process remaining events */
    for (int i = 0; i < 100; i++) {
        uv_run(&client_ctx->loop, UV_RUN_ONCE);
    }
    
    /* Cleanup */
    uvrpc_client_free(client_ctx->client);
    
    printf("[Client %d] Completed: sent=%d, received=%d, failed=%d\n",
           client_ctx->client_id,
           client_ctx->requests_sent,
           client_ctx->requests_received,
           client_ctx->failed);
    fflush(stdout);
    
    return 0;
}

int main(int argc, char** argv) {
    const char* address = (argc > 1) ? argv[1] : SERVER_ADDRESS;
    int num_clients = (argc > 2) ? atoi(argv[2]) : NUM_CLIENTS;
    int requests_per_client = (argc > 3) ? atoi(argv[3]) : REQUESTS_PER_CLIENT;
    
    printf("=== UVRPC Multi-Loop Stress Test ===\n");
    printf("Server Address: %s\n", address);
    printf("Clients: %d\n", num_clients);
    printf("Requests per Client: %d\n", requests_per_client);
    printf("Total Requests: %d\n", num_clients * requests_per_client);
    printf("=====================================\n\n");
    fflush(stdout);
    
    /* Setup signal handler */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    /* Start server in background */
    printf("Starting server...\n");
    fflush(stdout);
    
    uv_loop_t server_loop;
    uv_loop_init(&server_loop);
    
    uvrpc_config_t* server_config = uvrpc_config_new();
    uvrpc_config_set_loop(server_config, &server_loop);
    uvrpc_config_set_address(server_config, address);
    
    uvrpc_server_t* server = uvrpc_server_create(server_config);
    uvrpc_config_free(server_config);
    
    if (!server) {
        fprintf(stderr, "Failed to create server\n");
        return 1;
    }
    
    rpc_register_all(server, NULL);
    
    int ret = uvrpc_server_start(server);
    if (ret != UVRPC_OK) {
        fprintf(stderr, "Failed to start server: %d\n", ret);
        uvrpc_server_free(server);
        uv_loop_close(&server_loop);
        return 1;
    }
    
    printf("Server started on %s\n", address);
    fflush(stdout);
    
    /* Give server time to start */
    sleep(2);
    
    /* Create client contexts */
    client_context_t* clients = (client_context_t*)calloc(num_clients, sizeof(client_context_t));
    if (!clients) {
        fprintf(stderr, "Failed to allocate client contexts\n");
        uvrpc_server_free(server);
        uv_loop_close(&server_loop);
        return 1;
    }
    
    /* Initialize client loops */
    for (int i = 0; i < num_clients; i++) {
        uv_loop_init(&clients[i].loop);
        clients[i].client_id = i;
        clients[i].requests_sent = 0;
        clients[i].requests_received = 0;
        clients[i].failed = 0;
    }
    
    /* Run clients sequentially (each with its own loop) */
    printf("\nStarting %d clients sequentially...\n", num_clients);
    fflush(stdout);
    
    for (int i = 0; i < num_clients; i++) {
        if (!g_running) break;
        run_client(&clients[i]);
    }
    
    /* Wait for server to process all requests */
    printf("\nWaiting for server to process all requests...\n");
    fflush(stdout);
    
    for (int i = 0; i < 100; i++) {
        uv_run(&server_loop, UV_RUN_ONCE);
        usleep(10000);  // 10ms
    }
    
    /* Get server statistics */
    uint64_t total_requests = uvrpc_server_get_total_requests(server);
    uint64_t total_responses = uvrpc_server_get_total_responses(server);
    
    printf("\n=== Server Statistics ===\n");
    printf("Total Requests: %lu\n", total_requests);
    printf("Total Responses: %lu\n", total_responses);
    printf("Success Rate: %.2f%%\n", total_requests > 0 ? (double)total_responses / total_requests * 100 : 0);
    printf("================================\n\n");
    
    /* Calculate client statistics */
    int total_sent = 0;
    int total_received = 0;
    int total_failed = 0;
    
    for (int i = 0; i < num_clients; i++) {
        total_sent += clients[i].requests_sent;
        total_received += clients[i].requests_received;
        total_failed += clients[i].failed;
    }
    
    printf("=== Client Statistics ===\n");
    printf("Total Sent: %d\n", total_sent);
    printf("Total Received: %d\n", total_received);
    printf("Total Failed: %d\n", total_failed);
    printf("Success Rate: %.2f%%\n", total_sent > 0 ? (double)total_received / total_sent * 100 : 0);
    printf("==========================\n\n");
    
    /* Cleanup */
    for (int i = 0; i < num_clients; i++) {
        uv_loop_close(&clients[i].loop);
    }
    free(clients);
    
    uvrpc_server_free(server);
    uv_loop_close(&server_loop);
    
    printf("Stress test completed\n");
    fflush(stdout);
    
    return 0;
}