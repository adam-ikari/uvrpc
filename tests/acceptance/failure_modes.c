/*
 * A request that cannot succeed must fail in a way the caller can see.
 *
 * The failure modes here are the ones any deployed service meets on its first
 * bad afternoon: the server is not there, the method name is wrong, the peer
 * goes away mid-flight. What matters is not that they fail -- it is that the
 * caller can tell which one happened.
 *
 * The case this pins hardest is a wrong method name, because the previous
 * behaviour was the worst available. The server encoded the failure into the
 * response payload -- a small integer followed by the text -- and the client
 * decoded it as a *result*. The application received:
 *
 *     status     = 0            (success)
 *     error_code = 0
 *     result     = 20 bytes of "\x02\0\0\0Method not found"
 *
 * So a typo in a method name produced a success whose payload was the ASCII of
 * an error message. Nothing about it looked like a failure. Any caller that
 * checks status and then parses result would read four bytes of integer and
 * fifteen of text as if they were a typed answer.
 *
 * The root cause was that an error frame and a result frame were the same
 * frame. A handler that legitimately returned an int32 followed by text was
 * indistinguishable from a server reporting a failure, so the payload alone
 * cannot carry it. Errors now go out as UVRPC_FRAME_TYPE_RESPONSE_ERROR, which
 * the client reads before deciding what the payload means.
 *
 * The other thing pinned here is quieter and just as load-bearing: a failure
 * must release its callback slot. An error is a complete answer, so holding the
 * slot would quietly drain the concurrency quota until the client refused every
 * call -- the same silent degradation as a slot leak, reached by a different
 * route.
 *
 * Transport is TCP throughout, because it is the only place a request can still
 * be outstanding when things go wrong.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <uv.h>

#include "uvrpc.h"
#include "../free_port.h"

#define DRAIN_MS 400
#define WATCHDOG_MS 40000

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
    _exit(1);
}

static void stop_cb(uv_timer_t* timer) {
    uv_stop(uv_handle_get_loop((uv_handle_t*)timer));
}

static void drain(uv_loop_t* loop, uv_timer_t* timer) {
    uv_timer_start(timer, stop_cb, DRAIN_MS, 0);
    uv_run(loop, UV_RUN_DEFAULT);
}

static void echo_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    uvrpc_request_send_response(req, UVRPC_OK, req->params, req->params_size);
    uvrpc_request_free(req);
}

static int called;
static int last_status;
static int last_error_code;
static size_t last_result_size;
static int last_frame_type;
static char last_message[256];

static void on_response(uvrpc_response_t* resp, void* ctx) {
    (void)ctx;
    called++;
    last_status = resp->status;
    last_error_code = resp->error_code;
    last_result_size = resp->result_size;
    last_frame_type = resp->frame_type;
    if (resp->error_message) {
        snprintf(last_message, sizeof(last_message), "%s", resp->error_message);
    } else {
        last_message[0] = '\0';
    }
    uvrpc_response_free(resp);
}

static void reset(void) {
    called = 0;
    last_status = 12345;
    last_error_code = 12345;
    last_result_size = (size_t)-1;
    last_frame_type = -1;
    last_message[0] = '\0';
}

typedef struct {
    uvrpc_server_t* server;
    uvrpc_client_t* client;
} pair;

static void bring_up(uv_loop_t* loop, pair* p, int port, int quota) {
    char address[64];
    snprintf(address, sizeof(address), "tcp://127.0.0.1:%d", port);

    uvrpc_config_t* sc = uvrpc_config_set_loop(uvrpc_config_new(), loop);
    sc = uvrpc_config_set_address(sc, address);
    sc = uvrpc_config_set_transport(sc, UVBUS_TRANSPORT_TCP);
    p->server = uvrpc_server_create(sc);
    uvrpc_config_free(sc);

    uvrpc_server_register(p->server, "echo", echo_handler, NULL);
    uvrpc_server_start(p->server);
    for (int i = 0; i < 20; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }

    uvrpc_config_t* cc = uvrpc_config_set_loop(uvrpc_config_new(), loop);
    cc = uvrpc_config_set_address(cc, address);
    cc = uvrpc_config_set_transport(cc, UVBUS_TRANSPORT_TCP);
    uvrpc_config_set_max_concurrent(cc, quota);
    p->client = uvrpc_client_create(cc);
    uvrpc_config_free(cc);
    uvrpc_client_connect(p->client);
    for (int i = 0; i < 300; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }
}

static void tear_down(uv_loop_t* loop, uv_timer_t* timer, pair* p) {
    uvrpc_client_free(p->client);
    uvrpc_server_stop(p->server);
    uvrpc_server_free(p->server);
    for (int i = 0; i < 50; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }
    check(!uv_loop_alive(loop), "the loop has no handle left after teardown");
}

/* A method the server does not have must come back as a failure the caller can
 * read, not as a success carrying the failure's own bytes. */
static void case_unknown_method(uv_loop_t* loop, uv_timer_t* timer) {
    printf("\n[1/3] a method that does not exist\n");
    pair p;
    bring_up(loop, &p, acceptance_free_port(), 64);

    reset();
    uvrpc_client_call(p.client, "no_such_method", (const uint8_t*)"x", 1,
                      on_response, NULL);
    drain(loop, timer);

    check(called == 1, "the callback ran exactly once");
    check(last_status != UVRPC_OK,
          "a method that does not exist does not report success");
    check(last_error_code != 0, "error_code says something went wrong");
    check(last_error_code == last_status,
          "status and error_code agree on what went wrong");
    check(last_result_size == 0,
          "no result bytes: the caller is not handed the error payload as data");
    check(last_frame_type == UVRPC_FRAME_TYPE_RESPONSE_ERROR,
          "the frame says it is an error, not a result");
    check(last_message[0] != '\0', "a message says what happened");
    printf("      status=%d error_code=%d frame_type=%d message=\"%s\"\n",
           last_status, last_error_code, last_frame_type, last_message);

    /* The control, so the assertions above cannot pass by everything erroring. */
    reset();
    uvrpc_client_call(p.client, "echo", (const uint8_t*)"x", 1, on_response, NULL);
    drain(loop, timer);
    check(called == 1 && last_status == UVRPC_OK && last_result_size == 1,
          "a method that does exist still succeeds normally");

    tear_down(loop, timer, &p);
}

/* Same failure, reached without a method name at all. The server used to risk
 * running strcmp() on the NULL key here; it now answers through the same path. */
static void case_empty_method(uv_loop_t* loop, uv_timer_t* timer) {
    printf("\n[2/3] an empty method name\n");
    pair p;
    bring_up(loop, &p, acceptance_free_port(), 64);

    reset();
    uvrpc_client_call(p.client, "", (const uint8_t*)"x", 1, on_response, NULL);
    drain(loop, timer);

    check(called == 1, "the callback ran exactly once");
    check(last_status != UVRPC_OK, "an empty method name does not report success");
    check(last_result_size == 0, "no result bytes");
    printf("      status=%d error_code=%d message=\"%s\"\n",
           last_status, last_error_code, last_message);

    tear_down(loop, timer, &p);
}

/* Failures must not cost the caller a slot. If an error frame kept its slot the
 * client would look healthy until the quota ran out, and then refuse every call
 * with no error pointing at any of them. */
static void case_failure_releases_the_slot(uv_loop_t* loop, uv_timer_t* timer) {
    printf("\n[3/3] a failure returns its callback slot\n");
    pair p;
    bring_up(loop, &p, acceptance_free_port(), 4);

    reset();

    /* Four bad calls with a quota of four: if a failure kept its slot, the fifth
     * call would be refused with RATE_LIMITED. */
    for (int i = 0; i < 4; i++) {
        uvrpc_client_call(p.client, "no_such_method", (const uint8_t*)"x", 1,
                          on_response, NULL);
    }
    drain(loop, timer);
    check(called == 4, "all four failures were reported");
    check(uvrpc_client_get_pending_count(p.client) == 0,
          "no slot is still held once the failures have been delivered");

    /* And the client is still usable afterwards. */
    reset();
    int ret = uvrpc_client_call(p.client, "echo", (const uint8_t*)"x", 1,
                                on_response, NULL);
    check(ret == UVRPC_OK, "the client still accepts calls after four failures");
    drain(loop, timer);
    check(called == 1 && last_status == UVRPC_OK,
          "and the call succeeds");

    tear_down(loop, timer, &p);
}

int main(void) {
    uv_loop_t loop = {0};
    uv_loop_init(&loop);

    uv_timer_t timer;
    uv_timer_init(&loop, &timer);

    uv_timer_t watchdog;
    uv_timer_init(&loop, &watchdog);
    uv_timer_start(&watchdog, watchdog_cb, WATCHDOG_MS, 0);
    uv_unref((uv_handle_t*)&watchdog);

    case_unknown_method(&loop, &timer);
    case_empty_method(&loop, &timer);
    case_failure_releases_the_slot(&loop, &timer);

    uv_close((uv_handle_t*)&timer, NULL);
    uv_close((uv_handle_t*)&watchdog, NULL);
    for (int i = 0; i < 50; i++) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }
    if (uv_loop_close(&loop) != 0) {
        printf("FAIL: the loop could not be closed -- a handle was left open\n");
        failures++;
    }

    if (failures == 0) {
        printf("failure modes: OK\n");
        return 0;
    }
    printf("failure modes: %d check(s) failed\n", failures);
    return 1;
}