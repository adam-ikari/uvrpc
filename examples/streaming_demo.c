/**
 * Streaming Response Demo
 * 
 * This example demonstrates how to implement streaming responses
 * where the server sends multiple chunks of data to the client.
 */

#include <uvrpc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Streaming handler - sends data in chunks */
void stream_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    
    const char* method = req->method;
    printf("[Server] Received streaming request: %s\n", method);
    
    if (strcmp(method, "stream_numbers") == 0) {
        /* Send numbers 1-10 in chunks */
        for (int i = 1; i <= 10; i++) {
            char chunk[32];
            int chunk_size = snprintf(chunk, sizeof(chunk), "Number: %d\n", i);
            
            int is_last = (i == 10);
            printf("[Server] Sending chunk %d/10 (is_last=%d): %s", i, is_last, chunk);
            
            if (uvrpc_response_send_stream(req, (uint8_t*)chunk, chunk_size, is_last) != UVRPC_OK) {
                fprintf(stderr, "[Server] Failed to send chunk %d\n", i);
                break;
            }
        }
        printf("[Server] Finished streaming numbers\n");
    }
}

/* Client streaming callback */
int total_chunks = 0;

void on_stream_chunk(uvrpc_response_t* resp, void* ctx) {
    (void)ctx;
    
    total_chunks++;
    
    if (resp->is_stream) {
        printf("[Client] Received stream chunk %d (is_last=%d): ", total_chunks, resp->is_last_chunk);
        if (resp->result && resp->result_size > 0) {
            fwrite(resp->result, 1, resp->result_size, stdout);
        }
        
        if (resp->is_last_chunk) {
            printf("[Client] Streaming complete: received %d chunks\n", total_chunks);
            total_chunks = 0;
        }
    } else {
        printf("[Client] Received regular response\n");
    }
}

int main() {
    printf("=== UVRPC Streaming Demo ===\n\n");
    
    /* Create event loop */
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* Create server */
    uvrpc_config_t* server_config = uvrpc_config_new();
    uvrpc_config_set_loop(server_config, &loop);
    uvrpc_config_set_address(server_config, "ipc://uvrpc_streaming_demo");
    uvrpc_config_set_transport(server_config, UVBUS_TRANSPORT_IPC);
    
    uvrpc_server_t* server = uvrpc_server_create(server_config);
    if (!server) {
        fprintf(stderr, "Failed to create server\n");
        return 1;
    }
    
    uvrpc_server_register(server, "stream_numbers", stream_handler, NULL);
    uvrpc_server_start(server);
    printf("[Server] Started on ipc://uvrpc_streaming_demo\n\n");
    
    /* Create client */
    uvrpc_config_t* client_config = uvrpc_config_new();
    uvrpc_config_set_loop(client_config, &loop);
    uvrpc_config_set_address(client_config, "ipc://uvrpc_streaming_demo");
    uvrpc_config_set_transport(client_config, UVBUS_TRANSPORT_IPC);
    
    uvrpc_client_t* client = uvrpc_client_create(client_config);
    if (!client) {
        fprintf(stderr, "Failed to create client\n");
        return 1;
    }
    
    /* Connect client */
    uvrpc_client_connect(client);
    printf("[Client] Connecting...\n");
    
    /* Run event loop to establish connection */
    uv_run(&loop, UV_RUN_DEFAULT);
    
    /* Send streaming request */
    printf("\n[Client] Sending streaming request...\n");
    
    /* Send empty request (no parameters) */
    uvrpc_client_call(client, "stream_numbers", NULL, 0, on_stream_chunk, NULL);
    
    /* Run event loop to receive streaming chunks */
    printf("[Client] Waiting for streaming response...\n\n");
    uv_run(&loop, UV_RUN_DEFAULT);
    
    /* Cleanup */
    uvrpc_client_disconnect(client);
    uvrpc_client_free(client);
    uvrpc_config_free(client_config);
    
    uvrpc_server_stop(server);
    uvrpc_server_free(server);
    uvrpc_config_free(server_config);
    
    uv_loop_close(&loop);
    
    printf("\n=== Demo Complete ===\n");
    return 0;
}