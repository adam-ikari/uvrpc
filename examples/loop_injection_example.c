/**
 * UVRPC Loop Injection Example
 * Demonstrates that UVRPC does NOT own or manage the loop lifecycle
 * The loop is fully controlled by the user
 */

#include "../include/uvrpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

static volatile int g_running = 1;

void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
}

void echo_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    
    /* Echo back the params */
    uvrpc_request_send_response(req, UVRPC_OK,
                                  req->params, req->params_size);
}

void response_callback(uvrpc_response_t* resp, void* ctx) {
    (void)ctx;
    
    if (resp->status == UVRPC_OK) {
        printf("Response: %.*s\n", (int)resp->result_size, (char*)resp->result);
    }
}

int main(int argc, char** argv) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("UVRPC Loop Injection Example\n");
    printf("=============================\n\n");
    printf("Key points:\n");
    printf("1. User creates and owns the uv_loop_t\n");
    printf("2. User injects the loop into UVRPC via config\n");
    printf("3. UVRPC never creates or destroys the loop\n");
    printf("4. User manages the entire loop lifecycle\n\n");
    
    /* STEP 1: User creates and owns the loop */
    printf("Step 1: User creates uv_loop_t\n");
    uv_loop_t loop;
    uv_loop_init(&loop);
    printf("  ✓ Loop created at %p\n", (void*)&loop);
    
    /* STEP 2: User injects loop into UVRPC config */
    printf("\nStep 2: User injects loop into UVRPC config\n");
    uvrpc_config_t* server_config = uvrpc_config_new();
    uvrpc_config_set_loop(server_config, &loop);  /* INJECTION */
    uvrpc_config_set_address(server_config, "tcp://127.0.0.1:5558");
    printf("  ✓ Loop injected into server config\n");
    
    uvrpc_config_t* client_config = uvrpc_config_new();
    uvrpc_config_set_loop(client_config, &loop);  /* INJECTION */
    uvrpc_config_set_address(client_config, "tcp://127.0.0.1:5558");
    printf("  ✓ Loop injected into client config\n");
    
    /* STEP 3: Create server and client (they don't own the loop) */
    printf("\nStep 3: Create server and client\n");
    uvrpc_server_t* server = uvrpc_server_create(server_config);
    uvrpc_server_register(server, "echo", echo_handler, NULL);
    uvrpc_server_start(server);
    printf("  ✓ Server created (loop is injected, not owned)\n");
    
    uvrpc_client_t* client = uvrpc_client_create(client_config);
    uvrpc_client_connect(client);
    printf("  ✓ Client created (loop is injected, not owned)\n");
    
    /* STEP 4: User runs the event loop */
    printf("\nStep 4: User runs the event loop\n");
    printf("  User controls when and how to run the loop\n");
    
    int count = 0;
    while (g_running && count < 5) {
        /* User decides how to run the loop */
        uv_run(&loop, UV_RUN_NOWAIT);
        
        /* User can do other work here */
        if (count == 0) {
            printf("\nStep 5: Send RPC call\n");
            const char* msg = "Hello from injected loop!";
            uvrpc_client_call(client, "echo",
                             (const uint8_t*)msg, strlen(msg),
                             response_callback, NULL);
            printf("  ✓ RPC call sent\n");
        }
        
        count++;
        usleep(100000);  /* 100ms */
    }
    
    /* STEP 6: User destroys the loop after UVRPC cleanup */
    printf("\nStep 6: Cleanup\n");
    printf("  Order matters: UVRPC cleanup first, then loop cleanup\n");
    
    /* UVRPC cleanup (does NOT destroy the loop) */
    printf("\n  Cleanup UVRPC objects:\n");
    uvrpc_client_disconnect(client);
    uvrpc_client_free(client);
    printf("    ✓ Client freed (loop still valid)\n");
    
    uvrpc_server_stop(server);
    uvrpc_server_free(server);
    printf("    ✓ Server freed (loop still valid)\n");
    
    uvrpc_config_free(server_config);
    uvrpc_config_free(client_config);
    printf("    ✓ Config freed (loop still valid)\n");
    
    /* User destroys the loop */
    printf("\n  User destroys the loop:\n");
    uv_loop_close(&loop);
    printf("    ✓ Loop destroyed by user\n");
    
    printf("\n=============================\n");
    printf("Loop injection pattern demonstrated!\n");
    printf("UVRPC never owns or manages the loop.\n");
    
    return 0;
}
