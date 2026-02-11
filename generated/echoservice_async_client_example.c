/**
 * Auto-generated async client example for EchoService
 * Generated from /home/zhaodi-chen/project/uvrpc/examples/echo_service.yaml
 *
 * This example demonstrates how to use the generated async API
 */

#include "uvrpc.h"
#include "echoservice_gen.h"
#include "echoservice_gen_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

int main(int argc, char** argv) {
    const char* server_addr = (argc > 1) ? argv[1] : "tcp://127.0.0.1:5555";

    printf("========================================\n");
    printf("EchoService Async Client Example\n");
    printf("========================================\n");
    printf("Connecting to: %s\n\n", server_addr);

    /* Create libuv event loop */
    uv_loop_t* loop = uv_default_loop();

    /* Create RPC config */
    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, loop);
    uvrpc_config_set_address(config, server_addr);
    uvrpc_config_set_transport(config, UVRPC_TRANSPORT_TCP);
    uvrpc_config_set_mode(config, UVRPC_SERVER_CLIENT);
    uvrpc_config_set_hwm(config, 10000, 10000);

    /* Create RPC client */
    uvrpc_client_t* client = uvrpc_client_create(config);
    if (!client) {
        fprintf(stderr, "Failed to create client\n");
        uvrpc_config_free(config);
        return 1;
    }

    /* Connect to server */
    if (uvrpc_client_connect(client) != UVRPC_OK) {
        fprintf(stderr, "Failed to connect to server\n");
        uvrpc_client_free(client);
        uvrpc_config_free(config);
        return 1;
    }

    printf("Connected to server\n\n");

    /* Run event loop to let connection establish */
    for (int i = 0; i < 10; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }

    /* Result variable for RPC calls */
    int rc = UVRPC_OK;

    /* Async context for explicit async/await calls */
    uvrpc_async_t* async = NULL;

    /* ============================================
     * echo - Async Call Example
     * ============================================ */
    printf("Testing echo...\n");

    /* Prepare request */
    EchoService_echo_Request_t echo_request = {0};
    echo_request.message = "message";

    /* Call async - Method 1: Simple CallAsync (one-shot) */
    EchoService_echo_Response_t echo_response = {0};
    rc = EchoService_echo_CallAsync(client, &echo_request, &echo_response, loop);
    if (rc == UVRPC_OK) {
        printf("  echo succeeded!\n");
        printf("  Response echo: %s\n", echo_response.echo);
        printf("  Response timestamp: %" PRId64 "\n", echo_response.timestamp);
    } else {
        printf("  echo failed with error: %d (this is expected if the server is not running)\n", rc);
    }

    /* Run event loop to process pending events */
    for (int i = 0; i < 10; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }

    /* Free response */
    EchoService_echo_FreeResponse(&echo_response);

    /* Call async - Method 2: Explicit Async/Await (more control) */
    async = uvrpc_async_create(loop);
    if (async) {
        rc = EchoService_echo_Async(client, &echo_request, async);
        if (rc == UVRPC_OK) {
            const uvrpc_async_result_t* result = uvrpc_async_await(async);
            if (result && result->status == UVRPC_OK) {
                printf("  echo (explicit) succeeded!\n");
                EchoService_echo_Response_t response2 = {0};
                EchoService_echo_Await(result, &response2);
                EchoService_echo_FreeResponse(&response2);
            }
        }
        uvrpc_async_free(async);
        async = NULL;
    }

    /* ============================================
     * add - Async Call Example
     * ============================================ */
    printf("Testing add...\n");

    /* Prepare request */
    EchoService_add_Request_t add_request = {0};
    add_request.a = 1.5;
    add_request.b = 2.5;

    /* Call async - Method 1: Simple CallAsync (one-shot) */
    EchoService_add_Response_t add_response = {0};
    rc = EchoService_add_CallAsync(client, &add_request, &add_response, loop);
    if (rc == UVRPC_OK) {
        printf("  add succeeded!\n");
        printf("  Response result: %.2f\n", add_response.result);
    } else {
        printf("  add failed with error: %d (this is expected if the server is not running)\n", rc);
    }

    /* Run event loop to process pending events */
    for (int i = 0; i < 10; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }

    /* Free response */
    EchoService_add_FreeResponse(&add_response);

    /* Call async - Method 2: Explicit Async/Await (more control) */
    async = uvrpc_async_create(loop);
    if (async) {
        rc = EchoService_add_Async(client, &add_request, async);
        if (rc == UVRPC_OK) {
            const uvrpc_async_result_t* result = uvrpc_async_await(async);
            if (result && result->status == UVRPC_OK) {
                printf("  add (explicit) succeeded!\n");
                EchoService_add_Response_t response2 = {0};
                EchoService_add_Await(result, &response2);
                EchoService_add_FreeResponse(&response2);
            }
        }
        uvrpc_async_free(async);
        async = NULL;
    }

    /* ============================================
     * get_info - Async Call Example
     * ============================================ */
    printf("Testing get_info...\n");

    /* Prepare request */
    EchoService_getInfo_Request_t get_info_request;

    /* Call async - Method 1: Simple CallAsync (one-shot) */
    EchoService_getInfo_Response_t get_info_response = {0};
    rc = EchoService_getInfo_CallAsync(client, &get_info_request, &get_info_response, loop);
    if (rc == UVRPC_OK) {
        printf("  get_info succeeded!\n");
        printf("  Response service: %s\n", get_info_response.service);
        printf("  Response version: %s\n", get_info_response.version);
        printf("  Response uptime: %" PRId64 "\n", get_info_response.uptime);
    } else {
        printf("  get_info failed with error: %d (this is expected if the server is not running)\n", rc);
    }

    /* Run event loop to process pending events */
    for (int i = 0; i < 10; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }

    /* Free response */
    EchoService_getInfo_FreeResponse(&get_info_response);

    /* Call async - Method 2: Explicit Async/Await (more control) */
    async = uvrpc_async_create(loop);
    if (async) {
        rc = EchoService_getInfo_Async(client, &get_info_request, async);
        if (rc == UVRPC_OK) {
            const uvrpc_async_result_t* result = uvrpc_async_await(async);
            if (result && result->status == UVRPC_OK) {
                printf("  get_info (explicit) succeeded!\n");
                EchoService_getInfo_Response_t response2 = {0};
                EchoService_getInfo_Await(result, &response2);
                EchoService_getInfo_FreeResponse(&response2);
            }
        }
        uvrpc_async_free(async);
        async = NULL;
    }

    printf("\n========================================\n");
    printf("All tests completed!\n");
    printf("========================================\n");

    /* Cleanup */
    uvrpc_client_free(client);
    uvrpc_config_free(config);

    return 0;
}
