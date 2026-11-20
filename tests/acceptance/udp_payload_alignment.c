/*
 * A UDP server must hand its handler an 8-byte-aligned payload pointer.
 *
 * The encoder aligns the nested [ubyte] payload vector to 8 bytes relative to
 * the frame start. libuv's UDP receive buffer, however, starts at an address
 * that is only 4-byte aligned once the 4-byte length prefix is skipped. The
 * transport has to copy the frame into an aligned buffer before dispatching,
 * otherwise a handler that reads an int64 or double from its params pointer
 * performs a misaligned load.
 *
 * What this pins: a server-side handler receives an 8-byte-aligned params
 * pointer for an 8-byte payload, sees the exact bytes the client sent, and can
 * still reply over UDP.
 *
 * Run: ctest -R udp_payload_alignment
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <uv.h>

#include "uvrpc.h"
#include "../free_port.h"

#define WATCHDOG_MS 30000
#define PUMP_TURNS 400

static const uint8_t payload[8] = {1, 2, 3, 4, 5, 6, 7, 8};

static int failures;
static int replies;
static int last_error_code;
static int params_aligned;
static int handler_calls;

static void check(int ok, const char* what) {
    if (!ok) {
        printf("      FAIL: %s\n", what);
        failures++;
    }
}

static void watchdog_cb(uv_timer_t* timer) {
    uv_stop(uv_handle_get_loop((uv_handle_t*)timer));
    printf("FAIL: run exceeded %d ms\n", WATCHDOG_MS);
    failures++;
    _exit(1);
}

static void handler(uvrpc_request_t* req, void* ctx) {
    static const uint8_t pong[] = {'o', 'k'};
    uint64_t expected;
    uint64_t observed;

    (void)ctx;
    handler_calls++;
    memcpy(&expected, payload, sizeof(expected));

    params_aligned = (((uintptr_t)req->params & 7) == 0);
    check(params_aligned, "server params pointer is 8-byte aligned");
    check(req->params_size == sizeof(payload), "server params size is 8 bytes");

    /* Deliberately read through the pointer: the misaligned path this test
     * guards against is undefined behaviour here. */
    observed = *(const uint64_t*)req->params;
    check(observed == expected, "server read the payload value intact");

    uvrpc_request_send_response(req, UVRPC_OK, pong, sizeof(pong));
    uvrpc_request_free(req);
}

static void on_response(uvrpc_response_t* resp, void* ctx) {
    (void)ctx;
    if (resp) {
        last_error_code = resp->error_code;
    }
    replies++;
    uvrpc_response_free(resp);
}

int main(void) {
    uv_loop_t loop;
    uv_timer_t watchdog;
    uvbus_loop_registry_t* registry;
    char address[64];
    uvrpc_config_t* cfg;
    uvrpc_server_t* server;
    uvrpc_client_t* client;

    setvbuf(stdout, NULL, _IONBF, 0);

    if (uv_loop_init(&loop) != 0) {
        printf("FAIL: uv_loop_init\n");
        return 1;
    }

    uv_timer_init(&loop, &watchdog);
    uv_timer_start(&watchdog, watchdog_cb, WATCHDOG_MS, 0);
    uv_unref((uv_handle_t*)&watchdog);

    registry = uvbus_loop_registry_new();
    check(registry != NULL, "loop registry created");
    if (!registry) {
        return 1;
    }

    printf("a UDP server receives an aligned 8-byte payload\n");

    snprintf(address, sizeof(address), "udp://127.0.0.1:%d", acceptance_free_port());

    cfg = uvrpc_config_set_loop_registry(uvrpc_config_new(), registry);
    cfg = uvrpc_config_set_loop(cfg, &loop);
    cfg = uvrpc_config_set_address(cfg, address);
    cfg = uvrpc_config_set_transport(cfg, UVBUS_TRANSPORT_UDP);

    server = uvrpc_server_create(cfg);
    uvrpc_config_free(cfg);
    check(server != NULL, "server created");
    if (!server) {
        return 1;
    }
    /* Fixed method name keeps the encoded payload at an 8-aligned offset
     * inside the frame, so this test isolates the transport's frame start. */
    uvrpc_server_register(server, "payload8", handler, NULL);
    check(uvrpc_server_start(server) == UVRPC_OK, "server started");

    cfg = uvrpc_config_set_loop_registry(uvrpc_config_new(), registry);
    cfg = uvrpc_config_set_loop(cfg, &loop);
    cfg = uvrpc_config_set_address(cfg, address);
    cfg = uvrpc_config_set_transport(cfg, UVBUS_TRANSPORT_UDP);

    client = uvrpc_client_create(cfg);
    uvrpc_config_free(cfg);
    check(client != NULL, "client created");
    if (client) {
        check(uvrpc_client_connect(client) == UVRPC_OK, "client connected");
        for (int i = 0; i < PUMP_TURNS; i++) {
            uv_run(&loop, UV_RUN_NOWAIT);
        }

        replies = 0;
        last_error_code = -1;
        check(uvrpc_client_call(client, "payload8", payload, sizeof(payload),
                                on_response, NULL) == UVRPC_OK,
              "the call was accepted");
        for (int i = 0; i < PUMP_TURNS && replies == 0; i++) {
            uv_run(&loop, UV_RUN_NOWAIT);
        }
        check(handler_calls > 0, "the server handled the request");
        check(replies > 0, "the client was answered");
        check(last_error_code == UVRPC_OK, "the call succeeded");
        check(params_aligned, "the handler saw an aligned params pointer");
    }

    if (client) {
        uvrpc_client_free(client);
    }

    uvrpc_server_stop(server);
    uvrpc_server_free(server);
    for (int i = 0; i < PUMP_TURNS; i++) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }

    uvbus_loop_registry_free(registry);
    uv_close((uv_handle_t*)&watchdog, NULL);

    for (int i = 0; i < 200 && uv_loop_alive(&loop); i++) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }

    if (uv_loop_close(&loop) != 0) {
        printf("FAIL: uv_loop_close did not return 0 -- a handle was never closed\n");
        failures++;
    }

    if (failures) {
        printf("\nudp_payload_alignment: %d failure(s)\n", failures);
        return 1;
    }
    printf("\nudp_payload_alignment: OK\n");
    return 0;
}
