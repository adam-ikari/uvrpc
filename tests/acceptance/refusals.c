/*
 * A refusal has to reach the client as a refusal.
 *
 * When a server will not take a client on, it answers with an error frame --
 * UVRPC_FRAME_TYPE_RESPONSE_ERROR, type 3 -- whose payload is an int32 code
 * followed by the message bytes. uvrpc_encode_error() builds exactly that, and
 * uvrpc_request_send_response() uses it for every failure it reports.
 *
 * The server's own refusal, the one it sends when it is full, did not. It
 * hand-built a frame and tagged it type 1, UVRPC_FRAME_TYPE_RESPONSE, the type
 * that says "here is your result". So a client that was refused was told
 * UVRPC_OK, and what arrived as its result was the raw [code, length, message]
 * the refusal had assembled. A caller could not tell a refusal from an answer,
 * which is the only thing a refusal is for. The comment on the correct sibling
 * further down the same file explains the frame type; this branch predates it
 * and was never brought along.
 *
 * This pins the client-visible half, on all five transports. With a server
 * configured for one client:
 *
 *   - the client that cannot get in must come back with
 *     UVRPC_ERROR_MAX_CLIENTS rather than UVRPC_OK;
 *   - the client that is already in must keep being served, because a refusal
 *     that took the whole server down with it would be a worse bug than the
 *     one being fixed.
 *
 * Run: ctest -R refusals
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <uv.h>

#include "uvrpc.h"
#include "../free_port.h"

#define WATCHDOG_MS 60000
#define MAX_TURNS 400

static int failures;

static void check(int ok, const char* what) {
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static void watchdog_cb(uv_timer_t* timer) {
    uv_stop(uv_handle_get_loop((uv_handle_t*)timer));
    printf("FAIL: run exceeded %d ms\n", WATCHDOG_MS);
    failures++;
    _exit(1);
}

static void echo_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    uvrpc_request_send_response(req, UVRPC_OK, req->params, req->params_size);
    uvrpc_request_free(req);
}

static int responses;
static int last_status;
static int last_error_code;

static void on_response(uvrpc_response_t* resp, void* ctx) {
    (void)ctx;
    responses++;
    last_status = resp->status;
    last_error_code = resp->error_code;
    uvrpc_response_free(resp);
}

static void pump(uv_loop_t* loop, int turns) {
    for (int i = 0; i < turns; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }
}

/* Pump until `want` responses have arrived or the budget runs out. The budget
 * is what makes a client that is left hanging observable: it fails the check
 * afterwards instead of spinning here. */
static int pump_until(uv_loop_t* loop, int want) {
    for (int i = 0; i < MAX_TURNS && responses < want; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }
    for (int i = 0; i < 5; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }
    return responses;
}

typedef struct {
    const char* name;
    uvbus_transport_type_t transport;
    /* A pointer, not an array: an array member initialised from a char*
     * variable takes only the first element and truncates the pointer. */
    const char* address;
} transport_case;

static uvrpc_config_t* make_config(uv_loop_t* loop,
                                   uvbus_loop_registry_t* registry,
                                   const transport_case* tc) {
    uvrpc_config_t* cfg = uvrpc_config_new();
    cfg = uvrpc_config_set_loop(cfg, loop);
    cfg = uvrpc_config_set_loop_registry(cfg, registry);
    cfg = uvrpc_config_set_address(cfg, tc->address);
    cfg = uvrpc_config_set_transport(cfg, tc->transport);
    return cfg;
}

/* Returns 0 on success, with `reason` naming the first thing that went wrong. */
static int run_case(uv_loop_t* loop, uvbus_loop_registry_t* registry,
                    const transport_case* tc, const char** reason) {
    static const char ping[] = "ping";
    int got_in_first = 0;
    int refused_responses = 0;
    int refused_status = 0;
    int refused_error_code = 0;
    int got_in_second = 0;
    uvrpc_client_t* inside = NULL;
    uvrpc_client_t* outside = NULL;

    /* One client is all this server will take. */
    uvrpc_config_t* scfg = make_config(loop, registry, tc);
    scfg = uvrpc_config_set_max_clients(scfg, 1);
    uvrpc_server_t* server = uvrpc_server_create(scfg);
    uvrpc_config_free(scfg);
    if (!server) {
        *reason = "server_create returned NULL";
        return 0;
    }

    uvrpc_server_register(server, "ping", echo_handler, NULL);
    if (uvrpc_server_start(server) != UVRPC_OK) {
        *reason = "server_start did not return UVRPC_OK";
        uvrpc_server_free(server);
        return 0;
    }

    uvrpc_config_t* ccfg = make_config(loop, registry, tc);

    inside = uvrpc_client_create(ccfg);
    if (!inside) {
        *reason = "client_create returned NULL";
        uvrpc_config_free(ccfg);
        uvrpc_server_free(server);
        return 0;
    }
    if (uvrpc_client_connect(inside) != UVRPC_OK) {
        *reason = "the first client could not connect";
        goto done;
    }
    /* The socket transports connect asynchronously; only the in-process two
     * run their callback before uvbus_connect() returns. */
    pump(loop, 40);

    /* Speak once, so the server counts it and the gate has something to
     * measure. This is also why the later check is comparing like with like
     * and not just watching a client that never worked. */
    responses = 0;
    last_status = 12345;
    if (uvrpc_client_call(inside, "ping", (const uint8_t*)ping, sizeof(ping),
                          on_response, NULL) != UVRPC_OK) {
        *reason = "the first client could not call";
        goto done;
    }
    if (pump_until(loop, 1) != 1) {
        *reason = "the running server did not answer the first client";
        goto done;
    }
    got_in_first = last_status;

    /* The gate is closed now: one of one. This client is the one out. */
    outside = uvrpc_client_create(ccfg);
    if (!outside) {
        *reason = "the second client could not be created";
        goto done;
    }
    if (uvrpc_client_connect(outside) != UVRPC_OK) {
        *reason = "the second client could not reach the transport";
        goto done;
    }
    pump(loop, 40);

    responses = 0;
    last_status = 12345;
    last_error_code = 12345;
    if (uvrpc_client_call(outside, "ping", (const uint8_t*)ping, sizeof(ping),
                          on_response, NULL) != UVRPC_OK) {
        *reason = "the second client could not call";
        goto done;
    }
    refused_responses = pump_until(loop, 1);
    refused_status = last_status;
    refused_error_code = last_error_code;

    /* The client that was already in still has to be served. */
    responses = 0;
    last_status = 12345;
    if (uvrpc_client_call(inside, "ping", (const uint8_t*)ping, sizeof(ping),
                          on_response, NULL) != UVRPC_OK) {
        *reason = "the first client could not call again";
        goto done;
    }
    if (pump_until(loop, 1) != 1) {
        *reason = "refusing one client cut off the client that was already in";
        goto done;
    }
    got_in_second = last_status;

    if (got_in_first != UVRPC_OK) {
        *reason = "the running server answered the first client with an error";
        goto done;
    }
    if (refused_responses != 1) {
        *reason = "the client that could not get in was left without any answer";
        goto done;
    }
    if (refused_status == UVRPC_OK) {
        *reason = "a full server answered the client it refused with UVRPC_OK";
        goto done;
    }
    if (refused_status != UVRPC_ERROR_MAX_CLIENTS ||
        refused_error_code != UVRPC_ERROR_MAX_CLIENTS) {
        *reason = "the refusal did not arrive as UVRPC_ERROR_MAX_CLIENTS";
        goto done;
    }
    if (got_in_second != UVRPC_OK) {
        *reason = "the refusal changed the answer to the client that was in";
        goto done;
    }

    printf("  %-9s in=%d out=%d/%d in-again=%d\n",
           tc->name, got_in_first, refused_status, refused_error_code,
           got_in_second);

done:
    if (outside) uvrpc_client_free(outside);
    if (inside) uvrpc_client_free(inside);
    uvrpc_config_free(ccfg);
    uvrpc_server_free(server);
    return *reason[0] == '\0';
}

int main(void) {
    uv_loop_t loop = {0};
    uv_timer_t watchdog;
    char ipc_addr[128];
    char tcp_addr[64];
    char udp_addr[64];

    setvbuf(stdout, NULL, _IONBF, 0);

    if (uv_loop_init(&loop) != 0) {
        printf("FAIL: uv_loop_init\n");
        return 1;
    }

    uv_timer_init(&loop, &watchdog);
    uv_timer_start(&watchdog, watchdog_cb, WATCHDOG_MS, 0);
    uv_unref((uv_handle_t*)&watchdog);

    uvbus_loop_registry_t* registry = uvbus_loop_registry_new();
    check(registry != NULL, "registry created");
    if (!registry) return 1;

    snprintf(ipc_addr, sizeof(ipc_addr), "ipc:///tmp/uvrpc_refusals_%d.sock",
             (int)getpid());
    snprintf(tcp_addr, sizeof(tcp_addr), "tcp://127.0.0.1:%d",
             acceptance_free_port());
    snprintf(udp_addr, sizeof(udp_addr), "udp://127.0.0.1:%d",
             acceptance_free_port());

    transport_case cases[] = {
        {"INPROC",   UVBUS_TRANSPORT_INPROC,   "inproc://uvrpc_refusals"},
        {"SAMELOOP", UVBUS_TRANSPORT_SAMELOOP, "sameloop://uvrpc_refusals"},
        {"TCP",      UVBUS_TRANSPORT_TCP,      tcp_addr},
        {"UDP",      UVBUS_TRANSPORT_UDP,      udp_addr},
        {"IPC",      UVBUS_TRANSPORT_IPC,      ipc_addr},
    };
    const int case_count = (int)(sizeof(cases) / sizeof(cases[0]));

    printf("a server that is full, and what it tells the client it will not take\n");

    for (int i = 0; i < case_count; i++) {
        const char* reason = "";
        if (!run_case(&loop, registry, &cases[i], &reason)) {
            printf("FAIL: %s: %s\n", cases[i].name, reason);
            failures++;
        }
    }

    uvbus_loop_registry_free(registry);
    uv_close((uv_handle_t*)&watchdog, NULL);

    for (int i = 0; i < 200 && uv_loop_alive(&loop); i++) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }

    check(uv_loop_close(&loop) == 0,
          "uv_loop_close did not return 0 -- a handle was never closed");

    if (failures) {
        printf("\nrefusals: %d failure(s)\n", failures);
        return 1;
    }
    printf("\nrefusals: OK\n");
    return 0;
}
