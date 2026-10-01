/*
 * Releasing an endpoint while requests are in flight, and a peer disappearing
 * mid-response, must not take the process down.
 *
 * The existing tests/e2e/test_transport_lifetime covers the ordering bug where
 * a server is freed before its client, but it is narrower than it looks: it
 * drives the uvbus layer rather than uvrpc, it only exercises INPROC and
 * SAMELOOP, and it frees both endpoints back to back without a single request
 * in flight. Sequential teardown is the easy direction. Releasing something
 * that still owes work is the shape use-after-free actually takes, and nothing
 * covered it.
 *
 * Four cases, all over TCP because that is the only place a request can still
 * be outstanding when the endpoint goes away -- on INPROC and SAMELOOP the
 * response is produced inline, so by the time uvrpc_client_call returns there
 * is nothing left in flight.
 *
 * The last case is the one that matters most and has nothing to do with
 * ordering. A client that closes its socket while the server is writing a
 * response makes the kernel raise SIGPIPE, whose default action ends the
 * process; libuv only sets SO_NOSIGPIPE, which is a macOS option, so on Linux
 * the signal is delivered. A client that times out, crashes, loses the network
 * or gets reaped by a load balancer all land there. libuv reports the same
 * failure as UV_EPIPE on the write callback, which is where a caller can act
 * on it, so terminating is never the right answer.
 *
 * Run under ASan: the interesting failures here are use-after-free, and they
 * are silent without it.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <uv.h>

#include "uvrpc.h"
#include "free_port.h"
#include "uvbus.h"

#define IN_FLIGHT 5
#define DRAIN_MS 500
#define WATCHDOG_MS 30000

static int failures;
static int served;
static int responded;  /* progress only, not asserted */
static int write_failed_on_dead_peer;

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

/* Bounded: the loop must not be able to keep itself busy forever, which is
 * itself a way this goes wrong. */
static void settle(uv_loop_t* loop, uv_timer_t* timer) {
    for (int i = 0; i < 50; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }
    drain(loop, timer);
}

static void echo_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    served++;
    uvrpc_request_send_response(req, UVRPC_OK, req->params, req->params_size);
    uvrpc_request_free(req);
}

static void on_response(uvrpc_response_t* resp, void* ctx) {
    (void)ctx;
    responded++;
    uvrpc_response_free(resp);
}

typedef struct {
    uvrpc_server_t* server;
    uvrpc_client_t* client;
    int port;
} pair;

/* Build a connected server and client, with the loop pumped far enough for the
 * asynchronous TCP connect to finish. */
static int build_pair(uv_loop_t* loop, pair* p, int port) {
    char address[64];
    snprintf(address, sizeof(address), "tcp://127.0.0.1:%d", port);
    p->port = port;
    p->server = NULL;
    p->client = NULL;

    uvrpc_config_t* sc = uvrpc_config_set_loop(uvrpc_config_new(), loop);
    sc = uvrpc_config_set_address(sc, address);
    sc = uvrpc_config_set_transport(sc, UVBUS_TRANSPORT_TCP);
    p->server = uvrpc_server_create(sc);
    uvrpc_config_free(sc);
    if (!p->server) return 0;

    uvrpc_server_register(p->server, "echo", echo_handler, NULL);
    if (uvrpc_server_start(p->server) != UVRPC_OK) return 0;

    uvrpc_config_t* cc = uvrpc_config_set_loop(uvrpc_config_new(), loop);
    cc = uvrpc_config_set_address(cc, address);
    cc = uvrpc_config_set_transport(cc, UVBUS_TRANSPORT_TCP);
    /* Room for the in-flight batch; the default quota would reject part of it
     * and we would be testing backpressure instead. */
    uvrpc_config_set_max_concurrent(cc, 64);
    p->client = uvrpc_client_create(cc);
    uvrpc_config_free(cc);
    if (!p->client) return 0;

    if (uvrpc_client_connect(p->client) != UVRPC_OK) return 0;
    for (int i = 0; i < 200; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }
    return 1;
}

/* Issue requests and deliberately do not pump: the server has not seen them and
 * no response is on its way back, so both endpoints still owe work. */
static int fill(uv_loop_t* loop, uvrpc_client_t* client) {
    (void)loop;
    for (int i = 0; i < IN_FLIGHT; i++) {
        if (uvrpc_client_call(client, "echo", (const uint8_t*)"x", 1,
                              on_response, NULL) != UVRPC_OK) {
            return 0;
        }
    }
    return 1;
}

/* The loop must come back with no handles of the framework's left on it --
 * otherwise a host application cannot close its loop and hangs at exit. */
static void check_loop_drained(uv_loop_t* loop, uv_timer_t* timer) {
    settle(loop, timer);
    check(!uv_loop_alive(loop),
          "no handle is still referenced once the loop has been pumped");
}

static void case_client_first(uv_loop_t* loop, uv_timer_t* timer) {
    printf("\n[1/4] client released first, %d requests in flight\n", IN_FLIGHT);
    pair p;
    if (!build_pair(loop, &p, acceptance_free_port())) {
        printf("FAIL: could not bring up the pair\n");
        failures++;
        return;
    }

    check(fill(loop, p.client), "requests issued");
    check_eq(uvrpc_client_get_pending_count(p.client), IN_FLIGHT,
             "the client still owes responses");

    uvrpc_client_free(p.client);
    uvrpc_server_stop(p.server);
    uvrpc_server_free(p.server);

    check_loop_drained(loop, timer);
}

static void case_server_first(uv_loop_t* loop, uv_timer_t* timer) {
    printf("\n[2/4] server released first, %d requests in flight\n", IN_FLIGHT);
    pair p;
    if (!build_pair(loop, &p, acceptance_free_port())) {
        printf("FAIL: could not bring up the pair\n");
        failures++;
        return;
    }

    check(fill(loop, p.client), "requests issued");
    check_eq(uvrpc_client_get_pending_count(p.client), IN_FLIGHT,
             "the client still owes responses");

    uvrpc_server_stop(p.server);
    uvrpc_server_free(p.server);
    uvrpc_client_free(p.client);

    check_loop_drained(loop, timer);
}

/* The response arrives, is dispatched into a client that is already gone, and
 * the loop is pumped anyway. This is the shape that used to be a use-after-free
 * on the pending-callback table. */
static void case_response_after_client_gone(uv_loop_t* loop, uv_timer_t* timer) {
    printf("\n[3/4] response delivered after the client is released\n");
    pair p;
    if (!build_pair(loop, &p, acceptance_free_port())) {
        printf("FAIL: could not bring up the pair\n");
        failures++;
        return;
    }

    served = responded = 0;
    check(fill(loop, p.client), "requests issued");

    /* Hand them to the server, but not to the client: the requests are read and
     * answered while the caller no longer exists. */
    for (int i = 0; i < 5; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }
    uvrpc_client_free(p.client);
    settle(loop, timer);

    check_eq(served, IN_FLIGHT, "the server answered all of them");
    /* Deliberately no assertion on `responded`: a response that completes
     * during the pump before the free is ordinary, and one that lands after it
     * is the use-after-free this case exists to catch. Whether either happened
     * is a scheduling detail -- ASan is what decides, not a counter. The
     * observable contract is that the process is still running afterwards. */

    uvrpc_server_stop(p.server);
    uvrpc_server_free(p.server);
    check_loop_drained(loop, timer);
}

/* The client hangs up while the server is writing to it. Under the old
 * behaviour this terminated the process: the kernel raises SIGPIPE, and its
 * default action is to kill. Reaching the end of this function at all is the
 * assertion. */
static void case_peer_disappears(uv_loop_t* loop, uv_timer_t* timer) {
    printf("\n[4/4] peer disappears while the server is writing a response\n");
    pair p;
    if (!build_pair(loop, &p, acceptance_free_port())) {
        printf("FAIL: could not bring up the pair\n");
        failures++;
        return;
    }

    served = responded = 0;
    check(fill(loop, p.client), "requests issued");

    /* Release the client *before* pumping, not after. This is the ordering that
     * produces the write: the requests are still queued on the transport, so
     * pumping after the free delivers them to a server that then answers a
     * socket nobody is listening on. Pumping first would answer a live peer and
     * the case would pass whether or not SIGPIPE is handled. */
    uvrpc_client_free(p.client);
    settle(loop, timer);

    check(served > 0,
          "the server answered requests whose client had already gone away");
    /* As above: reaching this line is the assertion. Under the old behaviour
     * the kernel's SIGPIPE terminated the process inside settle(). */
    printf("      the process survived the peer going away\n");

    uvrpc_server_stop(p.server);
    uvrpc_server_free(p.server);
    check_loop_drained(loop, timer);
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

    case_client_first(&loop, &timer);
    case_server_first(&loop, &timer);
    case_response_after_client_gone(&loop, &timer);
    case_peer_disappears(&loop, &timer);

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
        printf("in-flight lifetime: OK\n");
        return 0;
    }
    printf("in-flight lifetime: %d check(s) failed\n", failures);
    return 1;
}