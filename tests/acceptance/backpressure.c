/*
 * Backpressure must fail fast, say which limit it hit, and give the resource
 * back.
 *
 * The framework has no blocking primitive: a single-threaded event loop cannot
 * wait for capacity without stalling every other request. So the three limits
 * are synchronous returns, and the contract an application depends on is:
 *
 *   - the error code names the limit, so the caller knows whether to retry,
 *     shed load, or give up;
 *   - a rejected request consumes nothing -- no frame leaves, no callback slot
 *     is taken;
 *   - once the in-flight work drains, the limit is usable again.
 *
 * That last one is the part worth testing. A limit that leaks the resource it
 * protects is worse than no limit: the client degrades to permanently refusing
 * every call, and the only symptom is an error code that does not mention the
 * original cause.
 *
 * Transport choice is not incidental. On INPROC and SAMELOOP a handler runs
 * inline and its response completes the request before uvrpc_client_call()
 * returns, so slots are released immediately and no limit can ever be reached
 * -- measured: 200 calls against a 64-slot ring, all accepted, pending count
 * stayed 0. Backpressure only exists where the response is asynchronous, so
 * this uses TCP. That asymmetry is itself a finding worth knowing: the same
 * application behaves differently under load on loopback than on a network.
 *
 * Not covered: UVRPC_ERROR_TRANSPORT_BUSY. It is returned when uv_write()
 * reports UV_ENOBUFS, which depends on kernel socket buffer state rather than
 * on anything the caller controls, so it cannot be provoked deterministically
 * from the public API. A test for it would be flaky by construction, which is
 * worse than no test -- the mapping to that code is what matters and it is
 * visible in uvrpc_client.c.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <uv.h>

#include "uvrpc.h"
#include "free_port.h"

#define QUOTA 4
#define RING_SLOTS 64
/* Each case fills the client, so it needs its own server. */
#define DRAIN_MS 400
#define WATCHDOG_MS 25000

static int failures;
static int served;

static void check(int ok, const char* what) {
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static void check_eq(long got, long want, const char* what) {
    if (got != want) {
        printf("FAIL: %s (expected %ld, got %ld)\n", what, want, got);
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
    served++;
    uvrpc_request_send_response(req, UVRPC_OK, req->params, req->params_size);
    uvrpc_request_free(req);
}

static void on_response(uvrpc_response_t* resp, void* ctx) {
    (void)ctx;
    uvrpc_response_free(resp);
}

/* Build a server and a client on the given loop. The client takes `quota` and
 * `ring`; the caller is responsible for freeing both. */
static uvrpc_server_t* start_server(uv_loop_t* loop, int port) {
    char address[64];
    snprintf(address, sizeof(address), "tcp://127.0.0.1:%d", port);

    uvrpc_config_t* config = uvrpc_config_set_loop(uvrpc_config_new(), loop);
    config = uvrpc_config_set_address(config, address);
    config = uvrpc_config_set_transport(config, UVBUS_TRANSPORT_TCP);
    uvrpc_server_t* server = uvrpc_server_create(config);
    uvrpc_config_free(config);

    if (server) {
        uvrpc_server_register(server, "echo", echo_handler, NULL);
        uvrpc_server_start(server);
    }
    return server;
}

static uvrpc_client_t* connect_client(uv_loop_t* loop, int port, int quota, int ring) {
    char address[64];
    snprintf(address, sizeof(address), "tcp://127.0.0.1:%d", port);

    uvrpc_config_t* config = uvrpc_config_set_loop(uvrpc_config_new(), loop);
    config = uvrpc_config_set_loop(config, loop);
    config = uvrpc_config_set_address(config, address);
    config = uvrpc_config_set_transport(config, UVBUS_TRANSPORT_TCP);
    uvrpc_config_set_max_concurrent(config, quota);
    uvrpc_config_set_max_pending_callbacks(config, ring);
    uvrpc_client_t* client = uvrpc_client_create(config);
    uvrpc_config_free(config);

    if (!client) return NULL;

    if (uvrpc_client_connect(client) != UVRPC_OK) {
        uvrpc_client_free(client);
        return NULL;
    }
    /* connect is asynchronous on TCP; a request issued before it completes is
     * dropped without a diagnostic. */
    for (int i = 0; i < 200; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }
    return client;
}

static void teardown(uvrpc_client_t* client, uvrpc_server_t* server, uv_loop_t* loop) {
    uvrpc_client_free(client);
    uvrpc_server_stop(server);
    uvrpc_server_free(server);
    /* Reclaiming the transport and its per-connection structs is deferred to
     * close callbacks, which only run while the loop is pumped. */
    while (uv_loop_alive(loop)) {
        uv_run(loop, UV_RUN_NOWAIT);
    }
}

/* The concurrency quota is hit at exactly the configured count, and the quota
 * returns once the in-flight requests have been answered. */
static void case_quota(uv_loop_t* loop, uv_timer_t* timer, int port) {
    printf("\n[1/3] concurrency quota (max_concurrent=%d)\n", QUOTA);
    served = 0;

    uvrpc_server_t* server = start_server(loop, port);
    check(server != NULL, "server created");
    if (!server) return;

    uvrpc_client_t* client = connect_client(loop, port, QUOTA, RING_SLOTS);
    check(client != NULL, "client connected");
    if (!client) { uvrpc_server_free(server); return; }

    /* Nothing pumps the loop, so each accepted call keeps its slot. */
    int accepted = 0;
    int first_reject = UVRPC_OK;
    for (int i = 0; i < QUOTA * 4; i++) {
        int ret = uvrpc_client_call(client, "echo", (const uint8_t*)"x", 1,
                                    on_response, NULL);
        if (ret == UVRPC_OK) {
            accepted++;
        } else {
            first_reject = ret;
            break;
        }
    }

    check_eq(accepted, QUOTA, "calls accepted before the quota bites");
    check_eq(first_reject, UVRPC_ERROR_RATE_LIMITED,
             "the quota reports UVRPC_ERROR_RATE_LIMITED");
    check_eq(uvrpc_client_get_pending_count(client), QUOTA,
             "in-flight count matches what was accepted");
    check_eq(served, 0, "the server has not answered yet (loop never pumped)");

    drain(loop, timer);

    check_eq(uvrpc_client_get_pending_count(client), 0,
             "every slot is returned after the responses arrive");
    check_eq(served, QUOTA, "the server answered all of them");

    check_eq(uvrpc_client_call(client, "echo", (const uint8_t*)"x", 1,
                               on_response, NULL), UVRPC_OK,
             "the quota is usable again once drained");
    drain(loop, timer);

    teardown(client, server, loop);
}

/* The callback ring refuses rather than overwriting a live entry. Hitting this
 * needs the quota raised: otherwise the quota always bites first. */
static void case_ring(uv_loop_t* loop, uv_timer_t* timer, int port) {
    printf("\n[2/3] callback ring (%d slots, quota raised)\n", RING_SLOTS);
    served = 0;

    uvrpc_server_t* server = start_server(loop, port);
    check(server != NULL, "server created");
    if (!server) return;

    uvrpc_client_t* client = connect_client(loop, port, 4096, RING_SLOTS);
    check(client != NULL, "client connected");
    if (!client) { uvrpc_server_free(server); return; }

    int accepted = 0;
    int callback_limit = 0;
    int rate_limited = 0;
    int other = -1;
    for (int i = 0; i < RING_SLOTS * 4; i++) {
        int ret = uvrpc_client_call(client, "echo", (const uint8_t*)"x", 1,
                                    on_response, NULL);
        if (ret == UVRPC_OK) {
            accepted++;
        } else if (ret == UVRPC_ERROR_CALLBACK_LIMIT) {
            callback_limit++;
        } else if (ret == UVRPC_ERROR_RATE_LIMITED) {
            rate_limited++;
        } else if (other < 0) {
            other = ret;
        }
    }

    /* Exactly the ring, no more: overwriting a live slot would strand its
     * caller until the request deadline. */
    check_eq(accepted, RING_SLOTS, "calls accepted before the ring fills");
    check_eq(callback_limit, RING_SLOTS * 3, "every later call reports CALLBACK_LIMIT");
    check_eq(rate_limited, 0, "the quota did not fire first");
    if (other >= 0) {
        printf("FAIL: an unexpected error code %d came back from a full ring\n", other);
        failures++;
    }

    drain(loop, timer);
    check_eq(uvrpc_client_get_pending_count(client), 0,
             "the ring is emptied once the responses arrive");
    check_eq(uvrpc_client_call(client, "echo", (const uint8_t*)"x", 1,
                               on_response, NULL), UVRPC_OK,
             "the ring is usable again once drained");
    drain(loop, timer);

    teardown(client, server, loop);
}

/* A batch is all-or-nothing: if it cannot fit, nothing goes out and no slot is
 * taken. A partial batch would leave the caller unable to tell which requests
 * are in flight. */
static void case_batch(uv_loop_t* loop, uv_timer_t* timer, int port) {
    printf("\n[3/3] batch rejection is all-or-nothing\n");
    served = 0;

    uvrpc_server_t* server = start_server(loop, port);
    check(server != NULL, "server created");
    if (!server) return;

    uvrpc_client_t* client = connect_client(loop, port, QUOTA, RING_SLOTS);
    check(client != NULL, "client connected");
    if (!client) { uvrpc_server_free(server); return; }

    for (int i = 0; i < QUOTA; i++) {
        uvrpc_client_call(client, "echo", (const uint8_t*)"x", 1, on_response, NULL);
    }
    check_eq(uvrpc_client_get_pending_count(client), QUOTA, "quota filled");

    const char* methods[4] = {"echo", "echo", "echo", "echo"};
    const uint8_t* params[4] = {(const uint8_t*)"a", (const uint8_t*)"b",
                                (const uint8_t*)"c", (const uint8_t*)"d"};
    size_t sizes[4] = {1, 1, 1, 1};
    uvrpc_callback_t callbacks[4] = {on_response, on_response, on_response, on_response};
    void* contexts[4] = {NULL, NULL, NULL, NULL};

    int ret = uvrpc_client_call_batch(client, methods, params, sizes,
                                      callbacks, contexts, 4);
    check_eq(ret, UVRPC_ERROR_RATE_LIMITED, "a batch that cannot fit is rejected");
    check_eq(served, 0, "not one frame of the rejected batch was sent");
    check_eq(uvrpc_client_get_pending_count(client), QUOTA,
             "the rejected batch took no slots");

    drain(loop, timer);
    check_eq(uvrpc_client_get_pending_count(client), 0, "drained");
    check_eq(uvrpc_client_call_batch(client, methods, params, sizes,
                                     callbacks, contexts, 4), UVRPC_OK,
             "a batch that fits is accepted once drained");
    drain(loop, timer);

    teardown(client, server, loop);
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

    /* Each case runs on its own port: a freed server's socket may linger, and
     * the three cases would otherwise share the address. */
    case_quota(&loop, &timer, acceptance_free_port());
    case_ring(&loop, &timer, acceptance_free_port());
    case_batch(&loop, &timer, acceptance_free_port());

    uv_close((uv_handle_t*)&timer, NULL);
    uv_close((uv_handle_t*)&watchdog, NULL);
    while (uv_loop_alive(&loop)) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }
    uv_loop_close(&loop);

    if (failures == 0) {
        printf("backpressure: OK\n");
        return 0;
    }
    printf("backpressure: %d check(s) failed\n", failures);
    return 1;
}