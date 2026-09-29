/**
 * Streaming API Demo - New API Usage
 * Demonstrates the new streaming API with type=2 (ResponseMore) and type=1 (Response)
 * 
 * Server API:
 *   - uvrpc_request_send_response_more(): Send intermediate responses (type=2)
 *   - uvrpc_request_send_response(): Send final response (type=1)
 * 
 * Client API:
 *   - resp->frame_type: Frame type field (1=Response/last, 2=ResponseMore/more)
 *   - uvrpc_response_is_stream_more(resp): Check if more responses will follow
 *   - uvrpc_response_is_stream_end(resp): Check if this is the last response
 */

#include <uvrpc.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define NUM_CHUNKS 5

/* Server handler - sends multiple chunks */
static void stream_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    static int request_count = 0;
    request_count++;
    
    printf("[SERVER] Request #%d: Sending %d chunks\n", request_count, NUM_CHUNKS);

    /* Send NUM_CHUNKS-1 intermediate chunks using ResponseMore (type=2) */
    for (int i = 0; i < NUM_CHUNKS - 1; i++) {
        char chunk[64];
        snprintf(chunk, sizeof(chunk), "Chunk %d", i + 1);
        
        printf("[SERVER]   -> Sending ResponseMore: %s\n", chunk);
        uvrpc_request_send_response_more(req, (uint8_t*)chunk, strlen(chunk) + 1);
        
        usleep(50000);  // 50ms delay between chunks
    }

    /* Send final chunk using Response (type=1) */
    char final_chunk[64];
    snprintf(final_chunk, sizeof(final_chunk), "Final Chunk %d", NUM_CHUNKS);
    printf("[SERVER]   -> Sending Response (last): %s\n", final_chunk);
    uvrpc_request_send_response(req, UVRPC_OK, (uint8_t*)final_chunk, strlen(final_chunk) + 1);
    
    printf("[SERVER] Request #%d complete\n", request_count);
}

/* Client callback - receives chunks */
static void stream_callback(uvrpc_response_t* resp, void* ctx) {
    static int response_count = 0;
    static int chunks_received = 0;
    
    (void)ctx;
    chunks_received++;
    
    if (uvrpc_response_is_stream_more(resp)) {
        /* More responses will follow */
        printf("[CLIENT] Chunk %d (more): %s\n", 
               chunks_received, 
               resp->result ? (char*)resp->result : "(empty)");
               
    } else if (uvrpc_response_is_stream_end(resp)) {
        /* This is the last response */
        response_count++;
        printf("[CLIENT] Chunk %d (last): %s\n", 
               chunks_received, 
               resp->result ? (char*)resp->result : "(empty)");
        printf("[CLIENT] Request #%d complete (total: %d chunks)\n", 
               response_count, chunks_received);
        chunks_received = 0;
        
        /* Stop after 2 complete requests */
        if (response_count >= 2) {
            uv_stop(uv_default_loop());
        }
    } else {
        printf("[CLIENT] Unknown frame type: %d\n", resp->frame_type);
    }
}

int main(int argc, char** argv) {
    const char* address = argc > 1 ? argv[1] : "inproc://stream_demo";

    printf("=== UVRPC Streaming API Demo ===\n");
    printf("Address: %s\n\n", address);

    /* Initialize libuv loop */
    uv_loop_t loop = {0};
    uv_loop_init(&loop);

    /* Create server */
    uvrpc_config_t* server_config = uvrpc_config_new();
    uvrpc_config_set_loop(server_config, &loop);
    /* INPROC and SAMELOOP peers meet through a registry the caller owns. */
    uvbus_loop_registry_t* registry = uvbus_loop_registry_new();

    uvrpc_config_set_loop_registry(server_config, registry);
    uvrpc_config_set_address(server_config, address);

    uvrpc_server_t* server = uvrpc_server_create(server_config);
    if (!server) {
        fprintf(stderr, "Failed to create server\n");
        uvrpc_config_free(server_config);
        return 1;
    }

    uvrpc_server_register(server, "stream_api", stream_handler, NULL);
    uvrpc_server_start(server);
    printf("[SERVER] Started and registered 'stream_api' handler\n\n");

    /* Create client */
    uvrpc_config_t* client_config = uvrpc_config_new();
    uvrpc_config_set_loop(client_config, &loop);
    uvrpc_config_set_loop_registry(client_config, registry);
    uvrpc_config_set_address(client_config, address);

    uvrpc_client_t* client = uvrpc_client_create(client_config);
    if (!client) {
        fprintf(stderr, "Failed to create client\n");
        uvrpc_server_free(server);
        uvrpc_config_free(server_config);
        return 1;
    }

    uvrpc_client_connect(client);
    printf("[CLIENT] Connected\n\n");

    /* Send 2 stream requests */
    for (int i = 0; i < 2; i++) {
        printf("[CLIENT] Sending stream request #%d...\n", i + 1);
        const char* params = "stream_data";
        uvrpc_client_call(client, "stream_api",
                         (uint8_t*)params, strlen(params) + 1,
                         stream_callback, NULL);
    }

    printf("\n[MAIN] Running event loop...\n");
    uv_run(&loop, UV_RUN_DEFAULT);

    /* Cleanup */
    printf("\n[MAIN] Cleaning up...\n");
    uvrpc_client_free(client);
    uvrpc_server_free(server);
    uvrpc_config_free(server_config);
    uvrpc_config_free(client_config);
    uvbus_loop_registry_free(registry);
    uv_loop_close(&loop);

    printf("=== Demo Complete ===\n");
    return 0;
}