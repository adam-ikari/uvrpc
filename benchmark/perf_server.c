/**
 * UVRPC Performance Server
 * Optimized for high-throughput testing
 * 
 * IMPORTANT: This server does NOT run the event loop internally.
 * The event loop must be provided externally and run by the caller.
 * All I/O is driven by the external event loop.
 */

#include "../include/uvrpc.h"
#include "../../generated/rpc_benchmark/rpc_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

/* Statistics timer */
static uv_timer_t g_stats_timer;
static uvrpc_server_t* g_server = NULL;
static uint64_t g_last_requests = 0;
static uint64_t g_last_responses = 0;
static uv_loop_t* g_loop = NULL;

void on_stats_timer(uv_timer_t* handle) {
    if (g_server) {
        uint64_t total_requests = uvrpc_server_get_total_requests(g_server);
        uint64_t total_responses = uvrpc_server_get_total_responses(g_server);
        
        uint64_t requests_delta = total_requests - g_last_requests;
        uint64_t responses_delta = total_responses - g_last_responses;
        
        printf("[SERVER] Total: %lu req, %lu resp | Delta: %lu req/s, %lu resp/s\n",
               total_requests, total_responses, requests_delta, responses_delta);
        fflush(stdout);
        
        g_last_requests = total_requests;
        g_last_responses = total_responses;
    }
}

/* Signal handler for graceful shutdown */
void on_signal(uv_signal_t* handle, int signum) {
    printf("\n[SERVER] Received signal %d, shutting down...\n", signum);
    fflush(stdout);
    
    /* Stop the event loop */
    if (g_loop) {
        uv_stop(g_loop);
    }
}

/**
 * Create and start a performance server
 * 
 * @param loop External event loop (caller is responsible for running uv_run)
 * @param address Server address (e.g., "tcp://127.0.0.1:5555")
 * @return uvrpc_server_t* Server handle, or NULL on failure
 * 
 * Usage:
 *   uv_loop_t loop;
 *   uv_loop_init(&loop);
 *   uvrpc_server_t* server = server_start(&loop, "tcp://127.0.0.1:5555");
 *   if (server) {
 *       printf("Server started, running event loop...\n");
 *       uv_run(&loop, UV_RUN_DEFAULT);
 *       server_stop(server);
 *       uv_loop_close(&loop);
 *   }
 */
uvrpc_server_t* server_start(uv_loop_t* loop, const char* address) {
    if (!loop || !address) {
        fprintf(stderr, "Invalid parameters: loop or address is NULL\n");
        return NULL;
    }
    
    /* Create config */
    uvrpc_config_t* config = uvrpc_config_new();
    if (!config) {
        fprintf(stderr, "Failed to create config\n");
        return NULL;
    }
    
    uvrpc_config_set_loop(config, loop);
    uvrpc_config_set_address(config, address);
    /* Transport type auto-detected from address prefix */
    uvrpc_config_set_comm_type(config, UVRPC_COMM_SERVER_CLIENT);
    
    /* Create server */
    uvrpc_server_t* server = uvrpc_server_create(config);
    if (!server) {
        fprintf(stderr, "Failed to create server\n");
        uvrpc_config_free(config);
        return NULL;
    }
    
    /* Register handlers using generated stub */
    rpc_register_all(server);
    
    /* Start server */
    int ret = uvrpc_server_start(server);
    if (ret != UVRPC_OK) {
        fprintf(stderr, "Failed to start server: %d\n", ret);
        uvrpc_server_free(server);
        uvrpc_config_free(config);
        return NULL;
    }
    
    printf("Server started on %s (event loop is external)\n", address);
    printf("Press Ctrl+C to stop the server\n");
    fflush(stdout);
    
    /* Store server globally for stats timer */
    g_server = server;
    g_loop = loop;
    
    /* Start stats timer (print every 1 second) */
    uv_timer_init(loop, &g_stats_timer);
    uv_timer_start(&g_stats_timer, on_stats_timer, 1000, 1000);
    
    /* Setup signal handlers for graceful shutdown */
    static uv_signal_t sigint_sig, sigterm_sig;
    uv_signal_init(loop, &sigint_sig);
    uv_signal_start(&sigint_sig, on_signal, SIGINT);
    
    uv_signal_init(loop, &sigterm_sig);
    uv_signal_start(&sigterm_sig, on_signal, SIGTERM);
    
    return server;
}

/**
 * Stop and cleanup server
 * 
 * @param server Server handle to stop and free
 */
void server_stop(uvrpc_server_t* server) {
    if (server) {
        /* Stop stats timer */
        uv_timer_stop(&g_stats_timer);
        uv_close((uv_handle_t*)&g_stats_timer, NULL);
        
        /* Print final statistics */
        uint64_t total_requests = uvrpc_server_get_total_requests(server);
        uint64_t total_responses = uvrpc_server_get_total_responses(server);
        printf("[SERVER] Final statistics: %lu total requests, %lu total responses\n",
               total_requests, total_responses);
        fflush(stdout);
        
        uvrpc_server_free(server);
        printf("Server stopped\n");
        fflush(stdout);
        
        g_server = NULL;
        g_loop = NULL;
    }
}

int main(int argc, char** argv) {
    const char* address = (argc > 1) ? argv[1] : "tcp://127.0.0.1:5555";
    
    /* Create event loop */
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* Start server (server does NOT run the loop) */
    uvrpc_server_t* server = server_start(&loop, address);
    if (!server) {
        uv_loop_close(&loop);
        return 1;
    }
    
    /* Run event loop externally (will stop on signal) */
    printf("Running event loop (server is driven by external loop)...\n");
    fflush(stdout);
    uv_run(&loop, UV_RUN_DEFAULT);
    printf("Event loop exited\n");
    fflush(stdout);
    
    /* Cleanup */
    server_stop(server);
    uv_loop_close(&loop);
    
    return 0;
}