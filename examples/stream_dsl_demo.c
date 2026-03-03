/**
 * Streaming Service Demo - DSL Generated Code
 * Demonstrates streaming RPC using FlatBuffers DSL generated code
 */

#include <uvrpc.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "stream_service_reader.h"
#include "stream_service_builder.h"

/* Server handler - sends multiple chunks using DSL generated code */
uvrpc_error_t uvrpc_streamservice_handle_request(const char* method_name,
                                                    const void* request,
                                                    uvrpc_request_t* req) {
    if (strcmp(method_name, "StreamRequest") == 0) {
        uvrpc_stream_StreamRequest_t* stream_req = uvrpc_stream_StreamRequest_as_root(request);
        
        int chunk_count = stream_req->chunk_count;
        int chunk_size = stream_req->chunk_size;
        int delay_ms = stream_req->delay_ms;
        
        printf("[SERVER] Received stream request: %d chunks, %d bytes each, %dms delay\n",
               chunk_count, chunk_size, delay_ms);
        
        /* Send chunks using DSL generated code */
        for (int i = 0; i < chunk_count; i++) {
            flatcc_builder_t builder;
            flatcc_builder_init(&builder);
            
            /* Create chunk data */
            uint8_t* chunk_data = (uint8_t*)malloc(chunk_size);
            memset(chunk_data, 'A' + (i % 26), chunk_size);
            
            flatbuffers_uint8_vec_ref_t data_ref = 
                flatbuffers_uint8_vec_create(&builder, chunk_data, chunk_size);
            
            uvrpc_stream_StreamChunk_start_as_root(&builder);
            uvrpc_stream_StreamChunk_chunk_id_add(&builder, i + 1);
            uvrpc_stream_StreamChunk_total_chunks_add(&builder, chunk_count);
            uvrpc_stream_StreamChunk_data_add(&builder, data_ref);
            uvrpc_stream_StreamChunk_is_last_add(&builder, (i == chunk_count - 1));
            uvrpc_stream_StreamChunk_end_as_root(&builder);
            
            size_t size;
            void* buf = flatcc_builder_finalize_buffer(&builder, &size);
            
            /* Send chunk - use ResponseMore for intermediate chunks, Response for last */
            if (i < chunk_count - 1) {
                printf("[SERVER]   -> Sending chunk %d (ResponseMore)\n", i + 1);
                uvrpc_request_send_response_more(req, buf, size);
            } else {
                printf("[SERVER]   -> Sending chunk %d (Response, last)\n", i + 1);
                uvrpc_request_send_response(req, UVRPC_OK, buf, size);
            }
            
            free(chunk_data);
            free(buf);
            flatcc_builder_clear(&builder);
            
            if (delay_ms > 0 && i < chunk_count - 1) {
                usleep(delay_ms * 1000);
            }
        }
        
        printf("[SERVER] Stream complete\n");
        return UVRPC_OK;
    }
    
    return UVRPC_ERROR_NOT_FOUND;
}

/* Client callback - receives chunks */
static void stream_callback(uvrpc_response_t* resp, void* ctx) {
    static int total_chunks = 0;
    static int requests_completed = 0;
    
    (void)ctx;
    
    if (resp->result && resp->result_size > 0) {
        uvrpc_stream_StreamChunk_t* chunk = uvrpc_stream_StreamChunk_as_root(resp->result);
        
        total_chunks++;
        
        if (uvrpc_response_is_stream_more(resp)) {
            printf("[CLIENT] Chunk %d/%d: %zu bytes (more)\n",
                   chunk->chunk_id, chunk->total_chunks,
                   flatbuffers_uint8_vec_len(uvrpc_stream_StreamChunk_data(chunk)));
        } else if (uvrpc_response_is_stream_end(resp)) {
            requests_completed++;
            printf("[CLIENT] Chunk %d/%d: %zu bytes (last)\n",
                   chunk->chunk_id, chunk->total_chunks,
                   flatbuffers_uint8_vec_len(uvrpc_stream_StreamChunk_data(chunk)));
            printf("[CLIENT] Request #%d complete (total chunks: %d)\n",
                   requests_completed, total_chunks);
            
            /* Stop after 2 complete requests */
            if (requests_completed >= 2) {
                uv_stop(uv_default_loop());
            }
        }
    }
}

int main(int argc, char** argv) {
    const char* address = argc > 1 ? argv[1] : "inproc://stream_dsl_demo";

    printf("=== UVRPC Streaming DSL Demo ===\n");
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

    /* Register handler - use a wrapper to call DSL handler */
    static void stream_handler_wrapper(uvrpc_request_t* req, void* ctx) {
        (void)ctx;
        uvrpc_streamservice_handle_request(req->method, req->params, req);
    }
    
    uvrpc_server_register(server, "uvrpc_stream.StreamRequest", stream_handler_wrapper, NULL);
    uvrpc_server_start(server);
    printf("[SERVER] Started with DSL handler\n\n");

    /* Create client */
    uvrpc_config_t* client_config = uvrpc_config_new();
    uvrpc_config_set_loop(client_config, &loop);
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

    /* Send 2 stream requests using DSL generated code */
    for (int i = 0; i < 2; i++) {
        printf("[CLIENT] Sending stream request #%d...\n", i + 1);
        
        /* Create request using DSL generated code */
        flatcc_builder_t builder;
        flatcc_builder_init(&builder);
        
        uvrpc_stream_StreamRequest_start_as_root(&builder);
        uvrpc_stream_StreamRequest_chunk_count_add(&builder, 5);
        uvrpc_stream_StreamRequest_chunk_size_add(&builder, 1024);
        uvrpc_stream_StreamRequest_delay_ms_add(&builder, 10);
        uvrpc_stream_StreamRequest_end_as_root(&builder);
        
        size_t size;
        void* buf = flatcc_builder_finalize_buffer(&builder, &size);
        
        uvrpc_client_call(client, "uvrpc_stream.StreamRequest",
                         buf, size,
                         stream_callback, NULL);
        
        free(buf);
        flatcc_builder_clear(&builder);
    }

    printf("\n[MAIN] Running event loop...\n");
    uv_run(&loop, UV_RUN_DEFAULT);

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