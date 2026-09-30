/**
 * UVRPC INPROC End-to-End Integration Test
 * Tests in-process transport (same process, same event loop).
 *
 * INPROC is a same-loop transport by design: the server endpoint and the
 * client must share one uv_loop_t so the server's recv callback runs in the
 * same thread that drives the client. This test therefore runs server and
 * client on a single loop (no server thread).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <uv.h>
#include "../../include/uvrpc.h"

#define TEST_INPROC_ADDR "inproc://uvrpc_test"
#define TIMEOUT_MS 5000

static int server_received = 0;
static int client_received = 0;

/* Server handler for requests */
static void server_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;

    server_received++;
    printf("Server received INPROC request\n");

    /* Verify request data */
    if (req->params && req->params_size == 6) {
        assert(req->params[0] == 'I');
        assert(req->params[1] == 'N');
        assert(req->params[2] == 'P');
        assert(req->params[3] == 'R');
        assert(req->params[4] == 'O');
        assert(req->params[5] == 'C');
    }

    /* Send response */
    uint8_t reply_data[] = {'I', 'N', 'P', 'R', 'O', 'C', '_', 'O', 'K'};
    uvrpc_request_send_response(req, 0, reply_data, sizeof(reply_data));

    uvrpc_request_free(req);
}

/* Client callback for responses */
static void client_callback(uvrpc_response_t* resp, void* ctx) {
    (void)ctx;

    client_received++;
    printf("Client received INPROC response\n");

    /* Verify response data */
    if (resp->result && resp->result_size == 9) {
        assert(resp->result[0] == 'I');
        assert(resp->result[1] == 'N');
        assert(resp->result[2] == 'P');
        assert(resp->result[3] == 'R');
        assert(resp->result[4] == 'O');
        assert(resp->result[5] == 'C');
        assert(resp->result[6] == '_');
        assert(resp->result[7] == 'O');
        assert(resp->result[8] == 'K');
    }

    uvrpc_response_free(resp);
}

/* Timeout timer callback: stops the shared loop. */
static void timeout_callback(uv_timer_t* handle) {
    printf("Timeout reached\n");
    uv_stop(handle->loop);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    /* Zero-initialize the loop: uv_loop_init preserves loop->data, so a
     * struct declared without an initializer carries whatever the stack
     * held. The framework no longer reads that field, but zeroing a struct
     * you are about to hand to a C library is the only safe habit. */
    uv_loop_t loop = {0};
    int rv = uv_loop_init(&loop);
    assert(rv == 0);

    printf("=== UVRPC INPROC End-to-End Test ===\n");

    /* --- Server (shares the same loop as the client) --- */
        /* INPROC peers find each other through a registry the caller owns. */
    uvbus_loop_registry_t* registry = uvbus_loop_registry_new();
    assert(registry != NULL);

uvrpc_config_t* server_config = uvrpc_config_set_loop_registry(
        uvrpc_config_new(), registry);
    server_config = uvrpc_config_set_loop(server_config, &loop);
    server_config = uvrpc_config_set_address(server_config, TEST_INPROC_ADDR);
    server_config = uvrpc_config_set_transport(server_config, UVBUS_TRANSPORT_INPROC);

    uvrpc_server_t* server = uvrpc_server_create(server_config);
    assert(server != NULL);

    uvrpc_server_register(server, "inproc_test_method", server_handler, NULL);

    rv = uvrpc_server_start(server);
    assert(rv == 0);
    printf("Server started on %s\n", TEST_INPROC_ADDR);

    /* Pump the loop briefly so the INPROC endpoint is registered before the
     * client connects. UV_RUN_NOWAIT: process pending registration without
     * blocking. */
    for (int i = 0; i < 10; i++) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }

    /* --- Client (same loop) --- */
    uvrpc_config_t* client_config = uvrpc_config_set_loop_registry(
        uvrpc_config_new(), registry);
    client_config = uvrpc_config_set_loop(client_config, &loop);
    client_config = uvrpc_config_set_address(client_config, TEST_INPROC_ADDR);
    client_config = uvrpc_config_set_transport(client_config, UVBUS_TRANSPORT_INPROC);

    uvrpc_client_t* client = uvrpc_client_create(client_config);
    assert(client != NULL);

    rv = uvrpc_client_connect(client);
    assert(rv == 0);
    printf("Client connecting via INPROC...\n");

    /* Pump connect setup. */
    for (int i = 0; i < 10; i++) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }

    /* Timeout backstop: if the roundtrip does not complete, stop the loop. */
    uv_timer_t timeout_timer;
    uv_timer_init(&loop, &timeout_timer);
    uv_timer_start(&timeout_timer, timeout_callback, TIMEOUT_MS, 0);

    printf("Making INPROC RPC call...\n");

    /* Client makes RPC call */
    uint8_t test_data[] = {'I', 'N', 'P', 'R', 'O', 'C'};
    rv = uvrpc_client_call(client, "inproc_test_method", test_data, sizeof(test_data), client_callback, NULL);
    assert(rv == 0);
    printf("Client called inproc_test_method\n");

    /* Run the shared loop until both sides complete or the timeout stops it.
     * UV_RUN_DEFAULT is correct here: once the request and response are
     * processed and no handles remain active (other than the unref'd timer),
     * the loop drains and returns naturally. The timer stops the loop on
     * timeout as a backstop. */
    while (client_received == 0 && server_received == 0) {
        if (uv_run(&loop, UV_RUN_DEFAULT) == 0) {
            break;
        }
    }
    /* A second pass to ensure the response callback completes. */
    for (int i = 0; i < 10 && client_received == 0; i++) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }

    printf("\n=== Test Results ===\n");
    printf("Server received requests: %d\n", server_received);
    printf("Client received responses: %d\n", client_received);

    /* Cleanup */
    uv_timer_stop(&timeout_timer);
    uv_close((uv_handle_t*)&timeout_timer, NULL);
    uvrpc_client_disconnect(client);
    uvrpc_client_free(client);
    uvrpc_config_free(client_config);

    uvrpc_server_stop(server);
    uvrpc_server_free(server);
    uvrpc_config_free(server_config);

    /* Drain any pending close callbacks. */
    for (int i = 0; i < 10; i++) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }

    uvbus_loop_registry_free(registry);
    uv_loop_close(&loop);

    if (server_received == 1 && client_received == 1) {
        printf("\n=== INPROC End-to-End Test PASSED ===\n");
        return 0;
    } else {
        printf("\n=== INPROC End-to-End Test FAILED ===\n");
        return 1;
    }
}
