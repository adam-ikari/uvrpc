/*
 * The 8-byte alignment guarantee is cross-transport. The encoder aligns the
 * nested payload vector to 8 relative to the frame start, but only a transport
 * that hands the RPC layer an 8-aligned frame makes that useful to a handler
 * reading 64-bit fields. TCP/IPC/UDP copy frames into heap buffers, while
 * INPROC/SAMELOOP pass the encoder's buffer directly; all five must expose the
 * same aligned params pointer.
 *
 * This is a table-driven matrix rather than five separate tests so a new
 * transport can be added by adding one row.
 *
 * Run: ctest -R transport_payload_alignment
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <uv.h>

#include "uvrpc.h"
#include "../free_port.h"

#define PUMP_MS 800
#define WATCHDOG_MS 15000
#define PAYLOAD_SIZE 8

static const uint8_t payload[PAYLOAD_SIZE] = {1, 2, 3, 4, 5, 6, 7, 8};

static int failures;
static int handler_calls;
static int aligned_calls;
static int value_ok;
static int replies;
static int last_error_code;

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

static void stop_cb(uv_timer_t* timer) {
    uv_stop(uv_handle_get_loop((uv_handle_t*)timer));
}

static void handler(uvrpc_request_t* req, void* ctx) {
    uint64_t expected;
    uint64_t observed;
    (void)ctx;

    handler_calls++;
    check(req->params_size == PAYLOAD_SIZE, "params size is 8 bytes");
    if (req->params_size != PAYLOAD_SIZE) {
        return;
    }

    if (((uintptr_t)req->params & 7u) == 0) {
        aligned_calls++;
        /* The alignment assertion above is the invariant; memcpy keeps the
         * value check well defined even if a transport regresses. */
        memcpy(&expected, payload, sizeof(expected));
        memcpy(&observed, req->params, sizeof(observed));
        value_ok = (observed == expected);
    }

    uvrpc_request_send_response(req, UVRPC_OK, payload, sizeof(payload));
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

typedef struct {
    const char* name;
    uvbus_transport_type_t transport;
    const char* address;
} transport_case;

static int run_case(uv_loop_t* loop, uvbus_loop_registry_t* registry,
                    uv_timer_t* timer, const transport_case* tc) {
    handler_calls = 0;
    aligned_calls = 0;
    value_ok = 0;
    replies = 0;
    last_error_code = -1;

    uvrpc_config_t* scfg = uvrpc_config_set_loop_registry(
        uvrpc_config_new(), registry);
    scfg = uvrpc_config_set_loop(scfg, loop);
    scfg = uvrpc_config_set_address(scfg, tc->address);
    scfg = uvrpc_config_set_transport(scfg, tc->transport);
    uvrpc_server_t* server = uvrpc_server_create(scfg);
    uvrpc_config_free(scfg);
    if (!server) {
        printf("      FAIL: %s: server create failed\n", tc->name);
        failures++;
        return 0;
    }

    /* Fixed name: it keeps the encoded payload at an 8-aligned offset, so
     * this test isolates the transport's frame-start alignment. */
    uvrpc_server_register(server, "payload8", handler, NULL);
    if (uvrpc_server_start(server) != UVRPC_OK) {
        printf("      FAIL: %s: server start failed\n", tc->name);
        failures++;
        uvrpc_server_free(server);
        return 0;
    }
    for (int i = 0; i < 10; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }

    uvrpc_config_t* ccfg = uvrpc_config_set_loop_registry(
        uvrpc_config_new(), registry);
    ccfg = uvrpc_config_set_loop(ccfg, loop);
    ccfg = uvrpc_config_set_address(ccfg, tc->address);
    ccfg = uvrpc_config_set_transport(ccfg, tc->transport);
    uvrpc_client_t* client = uvrpc_client_create(ccfg);
    uvrpc_config_free(ccfg);
    if (!client) {
        printf("      FAIL: %s: client create failed\n", tc->name);
        failures++;
        uvrpc_server_stop(server);
        uvrpc_server_free(server);
        return 0;
    }
    if (uvrpc_client_connect(client) != UVRPC_OK) {
        printf("      FAIL: %s: client connect failed\n", tc->name);
        failures++;
        uvrpc_client_free(client);
        uvrpc_server_stop(server);
        uvrpc_server_free(server);
        return 0;
    }
    for (int i = 0; i < 100; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }

    int accepted = (uvrpc_client_call(client, "payload8", payload,
                                      sizeof(payload), on_response, NULL) == UVRPC_OK);
    uv_timer_start(timer, stop_cb, PUMP_MS, 0);
    uv_run(loop, UV_RUN_DEFAULT);

    int ok = accepted && handler_calls == 1 && aligned_calls == 1 &&
             value_ok && replies == 1 && last_error_code == UVRPC_OK;
    if (!ok) {
        printf("      FAIL: %s: calls=%d aligned=%d value=%d replies=%d error=%d\n",
               tc->name, handler_calls, aligned_calls, value_ok,
               replies, last_error_code);
        failures++;
    } else {
        printf("  %-9s aligned payload OK\n", tc->name);
    }

    uvrpc_client_free(client);
    uvrpc_server_stop(server);
    uvrpc_server_free(server);
    uv_run(loop, UV_RUN_NOWAIT);
    return ok;
}

int main(void) {
    uv_loop_t loop = {0};
    uv_timer_t pump_timer;
    uv_timer_t watchdog;
    uvbus_loop_registry_t* registry;
    char ipc_addr[128];
    char tcp_addr[64];
    char udp_addr[64];

    uv_loop_init(&loop);
    uv_timer_init(&loop, &pump_timer);
    uv_timer_init(&loop, &watchdog);
    uv_timer_start(&watchdog, watchdog_cb, WATCHDOG_MS, 0);
    uv_unref((uv_handle_t*)&watchdog);

    registry = uvbus_loop_registry_new();
    if (!registry) {
        printf("FAIL: registry create failed\n");
        return 1;
    }

    snprintf(ipc_addr, sizeof(ipc_addr), "ipc:///tmp/uvrpc_align_%d.sock",
             (int)getpid());
    snprintf(tcp_addr, sizeof(tcp_addr), "tcp://127.0.0.1:%d",
             acceptance_free_port());
    snprintf(udp_addr, sizeof(udp_addr), "udp://127.0.0.1:%d",
             acceptance_free_port());

    transport_case cases[] = {
        {"INPROC",   UVBUS_TRANSPORT_INPROC,   "inproc://uvrpc_align"},
        {"SAMELOOP", UVBUS_TRANSPORT_SAMELOOP, "sameloop://uvrpc_align"},
        {"TCP",      UVBUS_TRANSPORT_TCP,      tcp_addr},
        {"UDP",      UVBUS_TRANSPORT_UDP,      udp_addr},
        {"IPC",      UVBUS_TRANSPORT_IPC,      ipc_addr},
    };
    const int case_count = (int)(sizeof(cases) / sizeof(cases[0]));

    printf("payload alignment across all five transports\n");
    for (int i = 0; i < case_count; i++) {
        run_case(&loop, registry, &pump_timer, &cases[i]);
    }

    unlink(ipc_addr);
    uv_close((uv_handle_t*)&pump_timer, NULL);
    uv_close((uv_handle_t*)&watchdog, NULL);
    uvbus_loop_registry_free(registry);
    while (uv_loop_alive(&loop)) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }
    if (uv_loop_close(&loop) != 0) {
        printf("FAIL: uv_loop_close did not return 0\n");
        failures++;
    }

    if (failures) {
        printf("transport payload alignment: %d check(s) failed\n", failures);
        return 1;
    }
    printf("transport payload alignment: OK\n");
    return 0;
}
