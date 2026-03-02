/**
 * Streaming Demo
 * Demonstrates server-to-client streaming using multiple Response frames
 */

#include <uvrpc.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define NUM_CHUNKS 5

/* Handler for stream requests */
static void stream_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;

    printf("[SERVER] Received stream request\n");

    /* Send multiple chunks as separate Response frames */
    for (int i = 0; i < NUM_CHUNKS; i++) {
        char chunk[64];
        snprintf(chunk, sizeof(chunk), "Chunk %d of %d", i + 1, NUM_CHUNKS);
        
        printf("[SERVER] Sending chunk: %s\n", chunk);
        
        uvrpc_request_send_response(req, UVRPC_OK, (uint8_t*)chunk, strlen(chunk) + 1);
        
        /* Small delay between chunks */
        usleep(100000);  // 100ms
    }

    /* Send end marker (result_size=0) to indicate stream completion */
    uvrpc_request_send_response(req, UVRPC_OK, NULL, 0);

    printf("[SERVER] Streaming complete\n");
}

/* Client callback for receiving responses */
static void stream_callback(uvrpc_response_t* resp, void* ctx) {
    (void)ctx;

    printf("[CLIENT] Received response (msgid: %u, size: %zu)\n",
           resp->msgid, resp->result_size);

    if (resp->result && resp->result_size > 0) {
        printf("[CLIENT] data: %s\n", (char*)resp->result);
    } else if (resp->is_last_chunk) {
        printf("[CLIENT] Stream closed by server\n");
    }

    if (resp->status != UVRPC_OK) {
        printf("[CLIENT] Error: %s\n", resp->error_message);
    }
}

int main(int argc, char** argv) {
    const char* address = argc > 1 ? argv[1] : "inproc://test_stream";

    printf("=== UVRPC Streaming Demo ===\n");
    printf("Address: %s\n\n", address);

    /* Initialize libuv loop */
    uv_loop_t loop;
    uv_loop_init(&loop);

    /* Create server */
    uvrpc_config_t* server_config = uvrpc_config_new();
    uvrpc_config_set_loop(server_config, &loop);
    uvrpc_config_set_address(server_config, address);

    uvrpc_server_t* server = uvrpc_server_create(server_config);
    if (!server) {
        fprintf(stderr, "Failed to create server\n");
        uvrpc_config_free(server_config);
        return 1;
    }

    /* Register stream handler */
    uvrpc_server_register(server, "sub_stream", stream_handler, NULL);
    printf("[SERVER] Registered stream handler\n");

    /* Start server */
    int ret = uvrpc_server_start(server);
    if (ret != UVRPC_OK) {
        fprintf(stderr, "Failed to start server: %d\n", ret);
        uvrpc_server_free(server);
        uvrpc_config_free(server_config);
        return 1;
    }
    printf("[SERVER] Started\n\n");

    /* Create client */
    uvrpc_config_t* client_config = uvrpc_config_new();
    uvrpc_config_set_loop(client_config, &loop);
    uvrpc_config_set_address(client_config, address);

    uvrpc_client_t* client = uvrpc_client_create(client_config);
    if (!client) {
        fprintf(stderr, "Failed to create client\n");
        uvrpc_server_free(server);
        uvrpc_config_free(server_config);
        uvrpc_config_free(client_config);
        return 1;
    }
    printf("[CLIENT] Created\n");

    /* Connect client */
    ret = uvrpc_client_connect(client);
    if (ret != UVRPC_OK) {
        fprintf(stderr, "Failed to connect client: %d\n", ret);
        uvrpc_client_free(client);
        uvrpc_server_free(server);
        uvrpc_config_free(server_config);
        uvrpc_config_free(client_config);
        return 1;
    }
    printf("[CLIENT] Connected\n\n");

    /* Send stream request */
    printf("[CLIENT] Sending stream request...\n");
    const char* params = "Start streaming";
    ret = uvrpc_client_call(client, "sub_stream",
                            (uint8_t*)params, strlen(params) + 1,
                            stream_callback, NULL);
    if (ret != UVRPC_OK) {
        fprintf(stderr, "Failed to call stream: %d\n", ret);
    }

    /* Run event loop for a limited time */
    printf("\n[MAIN] Running event loop for 3 seconds...\n");
    uv_run(&loop, UV_RUN_DEFAULT);
    sleep(3);
    uv_stop(&loop);

    /* Cleanup */
    printf("\n[MAIN] Cleaning up...\n");
    uvrpc_client_free(client);
    uvrpc_server_free(server);
    uvrpc_config_free(server_config);
    uvrpc_config_free(client_config);
    uv_loop_close(&loop);

    printf("=== Demo Complete ===\n");
    return 0;
}