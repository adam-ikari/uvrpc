/**
 * The generated client API, end to end.
 *
 * Creating a client is a config plus uvrpc_client_create; the connection runs
 * asynchronously, so the loop has to be pumped. Calling a method is a typed
 * wrapper the generator derives from the schema's service block -- here
 * uvrpc_benchmarkservice_Add, with the request type from benchmark.fbs.
 *
 * Run the server first:
 *   ./rpc_dsl_demo server
 *   ./generated_client_example [address]
 */

#include "../include/uvrpc.h"
#include "benchmark_benchmarkservice_api.h"
#include "benchmark_builder.h"
#include "benchmark_reader.h"

#include <stdio.h>
#include <stdlib.h>
#include <uv.h>

static void on_connect(int status, void* ctx) {
    (void)ctx;
    if (status == UVRPC_OK) {
        printf("Client connected successfully!\n");
    } else {
        printf("Connection failed with status: %d\n", status);
    }
}

static void on_response(uvrpc_response_t* resp, void* ctx) {
    (void)ctx;
    /* Check status before reading result: a failed request carries no result,
     * and saying so is the point of the status field. */
    if (resp->status == UVRPC_OK && resp->result && resp->result_size > 0) {
        benchmark_AddResponse_table_t result = benchmark_AddResponse_as_root(resp->result);
        printf("Add result: %lld\n", (long long)benchmark_AddResponse_result(result));
    } else {
        fprintf(stderr, "Add failed: status=%d %s\n", (int)resp->status,
                resp->error_message ? resp->error_message : "");
    }
    uvrpc_response_free(resp);
}

int main(int argc, char** argv) {
    const char* address = (argc > 1) ? argv[1] : "tcp://127.0.0.1:5555";

    uv_loop_t loop;
    uv_loop_init(&loop);

    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, &loop);
    uvrpc_config_set_address(config, address);

    printf("Creating client and initiating connection...\n");
    uvrpc_client_t* client = uvrpc_client_create(config);
    uvrpc_config_free(config);
    if (!client) {
        fprintf(stderr, "Failed to create client\n");
        uv_loop_close(&loop);
        return 1;
    }

    if (uvrpc_client_connect_with_callback(client, on_connect, NULL) != UVRPC_OK) {
        fprintf(stderr, "Failed to start connecting\n");
        uvrpc_client_free(client);
        uv_loop_close(&loop);
        return 1;
    }

    /* The connect happens in the loop; pumping is what lets it finish. */
    for (int i = 0; i < 100; i++) {
        uv_run(&loop, UV_RUN_DEFAULT);
    }

    /* The generated wrapper takes the schema's POJO and serializes it; there
     * is no buffer to build or free here. */
    benchmark_AddRequest_t request = { .a = 10, .b = 20 };

    printf("Calling Add(10, 20)...\n");
    uvrpc_benchmarkservice_Add(client, on_response, NULL, &request);


    for (int i = 0; i < 100; i++) {
        uv_run(&loop, UV_RUN_DEFAULT);
    }

    uvrpc_client_free(client);
    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);

    printf("Example completed\n");
    return 0;
}
