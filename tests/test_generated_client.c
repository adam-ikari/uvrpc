/**
 * Test program for generated async client
 */

#include "uvrpc.h"
#include "echoservice_gen.h"
#include "echoservice_gen_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Simple handler for EchoService */
static int echo_service_handler(void* ctx,
                                 const uint8_t* request_data,
                                 size_t request_size,
                                 uint8_t** response_data,
                                 size_t* response_size) {
    (void)ctx;

    printf("  [Server] Received request: %zu bytes\n", request_size);
    for (size_t i = 0; i < request_size && i < 50; i++) {
        printf("%02x ", request_data[i]);
    }
    printf("\n");

    /* Just echo back the request data */
    *response_data = malloc(request_size);
    if (!*response_data) {
        return UVRPC_ERROR_NO_MEMORY;
    }
    memcpy(*response_data, request_data, request_size);
    *response_size = request_size;

    printf("  [Server] Echoing back: %zu bytes\n", *response_size);

    return UVRPC_OK;
}

int main(int argc, char** argv) {
    const char* server_addr = (argc > 1) ? argv[1] : "tcp://127.0.0.1:5555";

    printf("========================================\n");
    printf("Testing Generated Async Client\n");
    printf("========================================\n");
    printf("Server address: %s\n\n", server_addr);

    /* Create libuv event loop */
    uv_loop_t* loop = uv_default_loop();

    /* Create ZMQ context */
    void* zmq_ctx = zmq_ctx_new();

    /* Create server config */
    uvrpc_config_t* server_config = uvrpc_config_new();
    uvrpc_config_set_loop(server_config, loop);
    uvrpc_config_set_address(server_config, server_addr);
    uvrpc_config_set_transport(server_config, UVRPC_TRANSPORT_TCP);
    uvrpc_config_set_mode(server_config, UVRPC_SERVER_CLIENT);
    uvrpc_config_set_zmq_ctx(server_config, zmq_ctx);
    uvrpc_config_set_hwm(server_config, 10000, 10000);

    /* Create server */
    uvrpc_server_t* server = uvrpc_server_create(server_config);
    if (!server) {
        fprintf(stderr, "Failed to create server\n");
        uvrpc_config_free(server_config);
        zmq_ctx_term(zmq_ctx);
        return 1;
    }

    /* Register EchoService */
    uvrpc_server_register_service(server, "EchoService", echo_service_handler, NULL);

    /* Start server */
    uvrpc_server_start(server);
    printf("Server started\n");

    /* Run event loop once to let server start */
    for (int i = 0; i < 10; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }

    /* Create client config */
    uvrpc_config_t* client_config = uvrpc_config_new();
    uvrpc_config_set_loop(client_config, loop);
    uvrpc_config_set_address(client_config, server_addr);
    uvrpc_config_set_transport(client_config, UVRPC_TRANSPORT_TCP);
    uvrpc_config_set_mode(client_config, UVRPC_SERVER_CLIENT);
    uvrpc_config_set_zmq_ctx(client_config, zmq_ctx);
    uvrpc_config_set_hwm(client_config, 10000, 10000);

    /* Create client */
    uvrpc_client_t* client = uvrpc_client_create(client_config);
    if (!client) {
        fprintf(stderr, "Failed to create client\n");
        uvrpc_server_free(server);
        uvrpc_config_free(server_config);
        uvrpc_config_free(client_config);
        zmq_ctx_term(zmq_ctx);
        return 1;
    }

    /* Connect to server */
    if (uvrpc_client_connect(client) != UVRPC_OK) {
        fprintf(stderr, "Failed to connect to server\n");
        uvrpc_client_free(client);
        uvrpc_server_free(server);
        uvrpc_config_free(server_config);
        uvrpc_config_free(client_config);
        zmq_ctx_term(zmq_ctx);
        return 1;
    }
    printf("Client connected\n\n");

    /* Run event loop once to let connection establish */
    for (int i = 0; i < 10; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }

    /* Test simple async call with raw data */
    printf("Testing raw async call...\n");

    const char* test_message = "Hello, UVRPC!";
    uvrpc_async_t* async = uvrpc_async_create(loop);
    if (async) {
        int rc = uvrpc_client_call_async(client, "EchoService", "echo",
                                         (const uint8_t*)test_message,
                                         strlen(test_message), async);
        if (rc == UVRPC_OK) {
            printf("  Request sent successfully\n");

            /* Run event loop to process request/response */
            int completed = 0;
            for (int i = 0; i < 1000 && !completed; i++) {
                uv_run(loop, UV_RUN_NOWAIT);
                const uvrpc_async_result_t* result = uvrpc_async_await(async);
                if (result && result->status != 0) {
                    completed = 1;
                }
                usleep(100);
            }

            const uvrpc_async_result_t* result = uvrpc_async_await(async);
            if (result && result->status == UVRPC_OK) {
                printf("  Raw async call succeeded!\n");
                printf("  Response size: %zu\n", result->response_size);
                if (result->response_size > 0 && result->response_data) {
                    printf("  Response: %.*s\n", (int)result->response_size,
                           (const char*)result->response_data);
                }
            } else {
                printf("  Raw async call failed with status: %d\n",
                       result ? result->status : -1);
            }
        } else {
            printf("  Failed to send async call: %d\n", rc);
        }
        uvrpc_async_free(async);
    }

    printf("\n========================================\n");
    printf("Test completed!\n");
    printf("========================================\n");

    /* Cleanup */
    uvrpc_client_free(client);
    uvrpc_server_free(server);
    uvrpc_config_free(server_config);
    uvrpc_config_free(client_config);
    zmq_ctx_term(zmq_ctx);

    /* Close event loop */
    uv_loop_close(loop);

    return 0;
}
