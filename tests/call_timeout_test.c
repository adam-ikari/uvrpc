/*
 * A request whose response never arrives must terminate, and it must
 * terminate distinguishably.
 *
 * The pending-callback table is a fixed-capacity ring. A slot is only released
 * when a response for its msgid arrives, so on a transport that can lose a
 * datagram (UDP) a lost response consumes a slot permanently: the client
 * degrades to UVRPC_ERROR_RATE_LIMITED after enough of them, with nothing in
 * between pointing at the cause. This test pins the behaviour that fixes it --
 * a deadline on every pending callback, reported to the application as
 * UVRPC_ERROR_TIMEOUT.
 *
 * INPROC with a handler that deliberately never answers reproduces it without
 * a network stack: the request is delivered, so the slot is allocated, and
 * nothing ever releases it.
 */

#include <stdio.h>
#include <string.h>
#include <uv.h>

#include "uvrpc.h"

#define ADDR "inproc://uvrpc_call_timeout"

/* Long enough that a healthy INPROC round trip always beats it. */
#define TIMEOUT_MS 60
/* How long to let the loop run before asserting an outcome. */
#define PUMP_TIMEOUT_MS 400
#define PUMP_DISABLED_MS 300

#define RECORD_MAX 8

static int failures;

static int cb_count;
static int cb_status[RECORD_MAX];
static size_t cb_result_size[RECORD_MAX];

static void check(int ok, const char* what) {
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static void on_response(uvrpc_response_t* resp, void* ctx) {
    (void)ctx;
    if (cb_count < RECORD_MAX) {
        cb_status[cb_count] = resp->status;
        cb_result_size[cb_count] = resp->result_size;
    }
    cb_count++;
    uvrpc_response_free(resp);
}

static void reset_cb(void) {
    memset(cb_status, 0, sizeof(cb_status));
    memset(cb_result_size, 0, sizeof(cb_result_size));
    cb_count = 0;
}

/* Accepts the request and deliberately sends nothing back. */
static void silent_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    uvrpc_request_free(req);
}

static void echo_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    static const uint8_t reply[] = {'P', 'O', 'N', 'G'};
    uvrpc_request_send_response(req, UVRPC_OK, reply, sizeof(reply));
    uvrpc_request_free(req);
}

static void stop_cb(uv_timer_t* timer) {
    uv_stop(uv_handle_get_loop((uv_handle_t*)timer));
}

static void pump(uv_loop_t* loop, uv_timer_t* timer, int ms) {
    uv_timer_start(timer, stop_cb, ms, 0);
    uv_run(loop, UV_RUN_DEFAULT);
}

int main(void) {
    uv_loop_t loop = {0};
    uv_loop_init(&loop);
    uv_timer_t timer;
    uv_timer_init(&loop, &timer);

    /* INPROC peers find each other through a registry the caller owns. */
    uvbus_loop_registry_t* registry = uvbus_loop_registry_new();
    check(registry != NULL, "registry created");
    if (!registry) return 1;

    uvrpc_config_t* scfg = uvrpc_config_set_loop_registry(
        uvrpc_config_new(), registry);
    scfg = uvrpc_config_set_loop(scfg, &loop);
    scfg = uvrpc_config_set_address(scfg, ADDR);
    scfg = uvrpc_config_set_transport(scfg, UVBUS_TRANSPORT_INPROC);
    uvrpc_server_t* server = uvrpc_server_create(scfg);
    uvrpc_config_free(scfg);
    check(server != NULL, "server created");
    if (!server) return 1;

    uvrpc_server_register(server, "silent", silent_handler, NULL);
    uvrpc_server_register(server, "echo", echo_handler, NULL);
    check(uvrpc_server_start(server) == UVRPC_OK, "server started");

    for (int i = 0; i < 10; i++) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }

    uvrpc_config_t* ccfg = uvrpc_config_set_loop_registry(
        uvrpc_config_new(), registry);
    ccfg = uvrpc_config_set_loop(ccfg, &loop);
    ccfg = uvrpc_config_set_address(ccfg, ADDR);
    ccfg = uvrpc_config_set_transport(ccfg, UVBUS_TRANSPORT_INPROC);
    uvrpc_client_t* client = uvrpc_client_create(ccfg);
    uvrpc_config_free(ccfg);
    check(client != NULL, "client created");
    if (!client) return 1;
    check(uvrpc_client_connect(client) == UVRPC_OK, "client connected");

    check(uvrpc_client_set_timeout(client, TIMEOUT_MS) == UVRPC_OK,
          "timeout accepted");

    /* The contract the implementation actually offers: an expired slot is
     * reclaimed and reported no later than the next request issued on this
     * client. There is deliberately no per-client timer -- an idle client is
     * not interrupted -- so these two steps are what the test pins. */

    /* --- 1. An unanswered request does not interrupt an idle client --- */
    const uint8_t payload[] = {'n', 'o', 'b', 'o', 'd', 'y'};
    reset_cb();
    check(uvrpc_client_call(client, "silent", payload, sizeof(payload),
                            on_response, NULL) == UVRPC_OK,
          "silent call accepted");
    pump(&loop, &timer, PUMP_TIMEOUT_MS);
    check(cb_count == 0,
          "an unanswered request does not call back while the client is idle");

    /* --- 2. The next request reclaims it and reports UVRPC_ERROR_TIMEOUT,
     *        and the slot it held is returned --- */
    check(uvrpc_client_call(client, "echo", payload, sizeof(payload),
                            on_response, NULL) == UVRPC_OK,
          "a later call is not rejected by the expired slot");
    pump(&loop, &timer, PUMP_TIMEOUT_MS);
    check(cb_count == 2, "the timeout and the later call both reported");
    check(cb_status[0] == UVRPC_ERROR_TIMEOUT,
          "the unanswered request reports UVRPC_ERROR_TIMEOUT");
    check(cb_result_size[0] == 0, "a timeout carries no result");
    check(cb_status[1] == UVRPC_OK, "the later call succeeded");
    check(cb_result_size[1] == 4, "the later call returned its result");

    /* --- 3. A responding call is never mistaken for an expired one --- */
    reset_cb();
    check(uvrpc_client_call(client, "echo", payload, sizeof(payload),
                            on_response, NULL) == UVRPC_OK,
          "second echo accepted");
    pump(&loop, &timer, PUMP_TIMEOUT_MS);
    check(cb_count == 1, "the echo answered once");
    check(cb_status[0] == UVRPC_OK, "the echo is not reported as a timeout");

    /* --- 4. timeout 0 disables the deadline, preserving old behaviour --- */
    check(uvrpc_client_set_timeout(client, 0) == UVRPC_OK,
          "timeout 0 accepted");
    reset_cb();
    check(uvrpc_client_call(client, "silent", payload, sizeof(payload),
                            on_response, NULL) == UVRPC_OK,
          "silent call accepted with the deadline disabled");
    pump(&loop, &timer, PUMP_DISABLED_MS);
    check(uvrpc_client_call(client, "echo", payload, sizeof(payload),
                            on_response, NULL) == UVRPC_OK,
          "echo accepted with the deadline disabled");
    pump(&loop, &timer, PUMP_TIMEOUT_MS);
    check(cb_count == 1, "only the echo reported; the silent call was left alone");
    check(cb_status[0] == UVRPC_OK,
          "no timeout is reported when the deadline is disabled");

    uvrpc_client_free(client);
    uvrpc_server_stop(server);
    uvrpc_server_free(server);
    uvbus_loop_registry_free(registry);
    /* Close, not just stop: a stopped handle is still open, and uv_loop_close
     * will not release the loop's own internals while one is. */
    uv_close((uv_handle_t*)&timer, NULL);
    while (uv_loop_alive(&loop)) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }
    uv_loop_close(&loop);

    if (failures == 0) {
        printf("call timeout: OK\n");
        return 0;
    }
    printf("call timeout: %d check(s) failed\n", failures);
    return 1;
}
