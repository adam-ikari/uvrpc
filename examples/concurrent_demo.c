/**
 * UVRPC Concurrent and Async Demo
 * Demonstrates:
 * 1. Request batching
 * 2. Concurrent request limiting
 * 3. Async retry with exponential backoff
 * 4. Request cancellation
 */

#include <uvrpc.h>
#include <uvrpc_async.h>
#include <uvrpc_allocator.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#define SERVER_ADDRESS "tcp://127.0.0.1:15555"
#define NUM_CONCURRENT_REQUESTS 1000
#define NUM_BATCH_REQUESTS 100

/* Echo handler for server */
void echo_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;

    /* Simply echo back the params */
    uvrpc_request_send_response(req, UVRPC_OK, req->params, req->params_size);

    printf("[Server] Echoed %zu bytes for msgid %u\n", req->params_size, req->msgid);
}

/* Batch request callback */
void batch_callback(uvrpc_response_t* resp, void* ctx) {
    int* index = (int*)ctx;

    if (resp->status == UVRPC_OK) {
        printf("[Batch] Request %d completed successfully, result size: %zu\n",
               *index, resp->result_size);
    } else {
        printf("[Batch] Request %d failed with error: %d\n", *index, resp->error_code);
    }
}

/* Simple callback for individual requests */
void simple_callback(uvrpc_response_t* resp, void* ctx) {
    const char* label = (const char*)ctx;

    if (resp->status == UVRPC_OK) {
        printf("[%s] Success! Result size: %zu\n", label, resp->result_size);
    } else {
        printf("[%s] Failed! Error code: %d\n", label, resp->error_code);
    }
}

int main(int argc, char* argv[]) {
    uv_loop_t loop;
    uv_loop_init(&loop);

    /* Create configuration with concurrency settings */
    uvrpc_config_t* server_config = uvrpc_config_new();
    uvrpc_config_set_loop(server_config, &loop);
    uvrpc_config_set_address(server_config, SERVER_ADDRESS);

    uvrpc_config_t* client_config = uvrpc_config_new();
    uvrpc_config_set_loop(client_config, &loop);
    uvrpc_config_set_address(client_config, SERVER_ADDRESS);
    uvrpc_config_set_performance_mode(client_config, UVRPC_PERF_HIGH_THROUGHPUT);
    uvrpc_config_set_max_concurrent(client_config, 20);  /* Limit concurrent requests */

    printf("=== UVRPC Concurrent and Async Demo ===\n\n");

    /* Start server */
    uvrpc_server_t* server = uvrpc_server_create(server_config);
    if (!server) {
        fprintf(stderr, "Failed to create server\n");
        return 1;
    }

    uvrpc_server_register(server, "echo", echo_handler, NULL);

    if (uvrpc_server_start(server) != UVRPC_OK) {
        fprintf(stderr, "Failed to start server\n");
        return 1;
    }

    printf("[Server] Started on %s\n\n", SERVER_ADDRESS);

    /* Give server time to start */
    sleep(1);

    /* Create client */
    uvrpc_client_t* client = uvrpc_client_create(client_config);
    if (!client) {
        fprintf(stderr, "Failed to create client\n");
        return 1;
    }

    if (uvrpc_client_connect(client) != UVRPC_OK) {
        fprintf(stderr, "Failed to connect client\n");
        return 1;
    }

    printf("[Client] Connected to server\n\n");

    /* === Demo 1: Individual concurrent requests === */
    printf("=== Demo 1: Individual Concurrent Requests ===\n");
    printf("Sending %d concurrent requests...\n", NUM_CONCURRENT_REQUESTS);

    const char* test_data = "Hello from UVRPC!";

    for (int i = 0; i < NUM_CONCURRENT_REQUESTS; i++) {
        uvrpc_client_call(client, "echo",
                          (const uint8_t*)test_data, strlen(test_data),
                          simple_callback, (void*)"Individual");
    }

    /* Wait for all requests to complete */
    int pending = uvrpc_client_get_pending_count(client);
    int iterations = 0;
    while (pending > 0 && iterations < 100) {
        uv_run(&loop, UV_RUN_ONCE);
        pending = uvrpc_client_get_pending_count(client);
        iterations++;
        usleep(10000);  /* 10ms */
    }

    printf("Completed %d individual requests\n\n", NUM_CONCURRENT_REQUESTS - pending);

    /* === Demo 2: Batch requests === */
    printf("=== Demo 2: Batch Requests ===\n");
    printf("Sending %d requests in a single batch...\n", NUM_BATCH_REQUESTS);

    /* Prepare batch data */
    const char* batch_methods[NUM_BATCH_REQUESTS];
    const uint8_t* batch_params[NUM_BATCH_REQUESTS];
    size_t batch_sizes[NUM_BATCH_REQUESTS];
    uvrpc_callback_t batch_callbacks[NUM_BATCH_REQUESTS];
    void* batch_contexts[NUM_BATCH_REQUESTS];
    int* batch_indices[NUM_BATCH_REQUESTS];

    for (int i = 0; i < NUM_BATCH_REQUESTS; i++) {
        batch_methods[i] = "echo";
        batch_params[i] = (const uint8_t*)test_data;
        batch_sizes[i] = strlen(test_data);
        batch_callbacks[i] = batch_callback;
        batch_indices[i] = uvrpc_alloc(sizeof(int));
        *batch_indices[i] = i;
        batch_contexts[i] = batch_indices[i];
    }

    int batch_result = uvrpc_client_call_batch(client,
                                                batch_methods,
                                                batch_params,
                                                batch_sizes,
                                                batch_callbacks,
                                                batch_contexts,
                                                NUM_BATCH_REQUESTS);

    if (batch_result == UVRPC_OK) {
        printf("Batch sent successfully\n");
    } else if (batch_result == UVRPC_ERROR_RATE_LIMITED) {
        printf("Batch rejected: rate limited\n");
    } else {
        printf("Batch failed with error: %d\n", batch_result);
    }

    /* Wait for batch to complete */
    pending = uvrpc_client_get_pending_count(client);
    iterations = 0;
    while (pending > 0 && iterations < 100) {
        uv_run(&loop, UV_RUN_ONCE);
        pending = uvrpc_client_get_pending_count(client);
        iterations++;
        usleep(10000);
    }

    printf("Batch completed\n\n");

    /* Free batch indices */
    for (int i = 0; i < NUM_BATCH_REQUESTS; i++) {
        uvrpc_free(batch_indices[i]);
    }

    /* === Demo 3: Async retry with exponential backoff === */
    printf("=== Demo 3: Async Retry with Exponential Backoff ===\n");

    uvrpc_async_ctx_t* async_ctx = uvrpc_async_ctx_new(&loop);
    uvrpc_async_result_t* async_result = NULL;

    /* Retry with exponential backoff */
    int retry_status = uvrpc_async_retry_with_backoff(async_ctx, client, "echo",
                                                       (const uint8_t*)test_data,
                                                       strlen(test_data),
                                                       &async_result,
                                                       3,  /* max retries */
                                                       100,  /* initial delay 100ms */
                                                       2.0);  /* backoff multiplier */

    if (retry_status == UVRPC_OK && async_result) {
        printf("[Async] Retry succeeded after backoff\n");
        uvrpc_async_result_free(async_result);
    } else {
        printf("[Async] Retry failed\n");
    }

    /* === Demo 4: Async cancellation === */
    printf("\n=== Demo 4: Async Cancellation ===\n");

    uvrpc_async_ctx_t* cancel_ctx = uvrpc_async_ctx_new(&loop);

    /* Start multiple async operations */
    for (int i = 0; i < 5; i++) {
        uvrpc_client_call_async(cancel_ctx, client, "echo",
                                (const uint8_t*)test_data,
                                strlen(test_data),
                                &async_result);
    }

    int pending_async = uvrpc_async_get_pending_count(cancel_ctx);
    printf("Started %d async operations\n", pending_async);

    /* Cancel all pending operations */
    uvrpc_async_cancel_all(cancel_ctx);
    printf("Cancelled all async operations\n");

    pending_async = uvrpc_async_get_pending_count(cancel_ctx);
    printf("Remaining pending operations: %d\n", pending_async);

    uvrpc_async_ctx_free(cancel_ctx);
    uvrpc_async_ctx_free(async_ctx);

    /* Cleanup */
    printf("\n=== Cleanup ===\n");
    uvrpc_client_disconnect(client);
    uvrpc_client_free(client);
    uvrpc_server_stop(server);
    uvrpc_server_free(server);

    uvrpc_config_free(server_config);
    uvrpc_config_free(client_config);

    uv_run(&loop, UV_RUN_DEFAULT);
    uv_loop_close(&loop);

    printf("Demo completed successfully!\n");
    return 0;
}