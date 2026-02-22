/* Test oneway RPC method functionality */
#include <stdio.h>
#include <stdlib.h>
#include <uv.h>
#include "benchmark_benchmarkservice_api.h"
#include "include/uvrpc.h"

/* User-implemented handler for all RPC requests */
uvrpc_error_t uvrpc_benchmarkservice_handle_request(const char* method_name,
                                                      const void* request,
                                                      uvrpc_request_t* req) {
    printf("Server received method: %s\n", method_name);

    if (strcmp(method_name, "Log") == 0) {
        /* Handle oneway Log method */
        benchmark_LogRequest_t* log_req = benchmark_LogRequest_as_root(request);

        const char* level_str = "UNKNOWN";
        switch (log_req->level) {
            case 0: level_str = "DEBUG"; break;
            case 1: level_str = "INFO"; break;
            case 2: level_str = "WARNING"; break;
            case 3: level_str = "ERROR"; break;
        }

        printf("  [%s] %s (source: %s, timestamp: %ld)\n",
               level_str,
               log_req->message ? log_req->message : "(null)",
               log_req->source ? log_req->source : "(null)",
               (long)log_req->timestamp);

        /* Oneway method - send empty response (server will not wait) */
        flatcc_builder_t builder;
        flatcc_builder_init(&builder);
        benchmark_EmptyResponse_start_as_root(&builder);
        benchmark_EmptyResponse_end_as_root(&builder);

        size_t size;
        void* buf = flatcc_builder_finalize_buffer(&builder, &size);

        uvrpc_error_t ret = uvrpc_request_send_response(req, UVRPC_OK, buf, size);

        free(buf);
        flatcc_builder_clear(&builder);

        return ret;
    } else if (strcmp(method_name, "Add") == 0) {
        /* Handle regular Add method */
        benchmark_AddRequest_t* add_req = benchmark_AddRequest_as_root(request);

        int32_t result = add_req->a + add_req->b;
        printf("  Add: %d + %d = %d\n", add_req->a, add_req->b, result);

        /* Send response */
        flatcc_builder_t builder;
        flatcc_builder_init(&builder);
        benchmark_AddResponse_start_as_root(&builder);
        benchmark_AddResponse_result_add(&builder, result);
        benchmark_AddResponse_end_as_root(&builder);

        size_t size;
        void* buf = flatcc_builder_finalize_buffer(&builder, &size);

        uvrpc_error_t ret = uvrpc_request_send_response(req, UVRPC_OK, buf, size);

        free(buf);
        flatcc_builder_clear(&builder);

        return ret;
    }

    return UVRPC_ERROR_NOT_FOUND;
}

/* Client connection callback */
void on_client_connect(uvrpc_client_t* client, uvrpc_error_t status, void* ctx) {
    if (status == UVRPC_OK) {
        printf("Client connected successfully\n");

        /* Test oneway Log method */
        printf("\n=== Testing Oneway Log Method ===\n");
        uvrpc_error_t ret = uvrpc_benchmarkservice_Log(
            client,
            1,  /* INFO level */
            "Test oneway log message",
            time(NULL),
            "test_oneway.c"
        );

        if (ret == UVRPC_OK) {
            printf("Oneway Log method called successfully (fire-and-forget)\n");
        } else {
            printf("Failed to call oneway Log method: %d\n", ret);
        }

        /* Test regular Add method */
        printf("\n=== Testing Regular Add Method ===\n");
        benchmark_AddResponse_table_t response;

        ret = uvrpc_benchmarkservice_Add_sync(client, &response, 10, 20, 5000);
        if (ret == UVRPC_OK) {
            printf("Add result: %d\n", response.result);
        } else {
            printf("Failed to call Add method: %d\n", ret);
        }

        /* Multiple oneway calls */
        printf("\n=== Testing Multiple Oneway Calls ===\n");
        for (int i = 0; i < 3; i++) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Oneway message #%d", i);
            uvrpc_benchmarkservice_Log(client, 1, msg, time(NULL), "test_oneway.c");
        }
        printf("Sent 3 oneway log messages\n");

        /* Stop the event loop */
        uv_stop(uvrpc_client_get_loop(client));
    } else {
        printf("Client connection failed: %d\n", status);
        uv_stop(uvrpc_client_get_loop(client));
    }
}

int main() {
    uv_loop_t loop;
    uv_loop_init(&loop);

    /* Create server */
    printf("=== Starting UVRPC Oneway Test Server ===\n");
    uvrpc_server_t* server = uvrpc_benchmarkservice_create_server(&loop, "tcp://127.0.0.1:5555");
    if (!server) {
        fprintf(stderr, "Failed to create server\n");
        return 1;
    }

    uvrpc_error_t ret = uvrpc_benchmarkservice_start_server(server);
    if (ret != UVRPC_OK) {
        fprintf(stderr, "Failed to start server: %d\n", ret);
        return 1;
    }
    printf("Server started on tcp://127.0.0.1:5555\n");

    /* Create client */
    printf("\n=== Creating Client ===\n");
    uvrpc_client_t* client = uvrpc_benchmarkservice_create_client(
        &loop,
        "tcp://127.0.0.1:5555",
        on_client_connect,
        NULL
    );

    if (!client) {
        fprintf(stderr, "Failed to create client\n");
        return 1;
    }

    /* Run event loop */
    printf("\n=== Running Event Loop ===\n");
    uv_run(&loop, UV_RUN_DEFAULT);

    /* Cleanup */
    printf("\n=== Cleanup ===\n");
    uvrpc_benchmarkservice_free_client(client);
    uvrpc_benchmarkservice_stop_server(server);
    uvrpc_benchmarkservice_free_server(server);
    uv_loop_close(&loop);

    printf("\n=== Test Complete ===\n");
    return 0;
}