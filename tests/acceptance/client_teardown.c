/*
 * Freeing a client has to outlive the closes it queues.
 *
 * uvrpc_client_free() walks down to uvasync_scheduler_destroy(), and that
 * scheduler owns two uv_async_t handles *embedded in its own block*: one in
 * the concurrency semaphore, one in the completion waitgroup. Both teardowns
 * call uv_close(), which queues the close and returns. The block was then
 * handed straight back to the allocator, so libuv kept pointing at two handles
 * inside memory that no longer belonged to anybody.
 *
 * Measured on this test, before the fix:
 *
 *     Invalid read of size 8 at uv_run
 *       by client_teardown (the turn after uvrpc_client_free)
 *       256 bytes inside a block of size 344 free'd by client_destroy
 *       alloc'd at uvasync_scheduler_create
 *
 * Nothing crashes -- the block is normally still mapped and unclaimed, so the
 * reads land on stale-but-readable memory -- which is why this never surfaced
 * as a failure. It is the same shape as the promise handle fixed in 0ff646c,
 * and it fired on *every* uvrpc_client_free() followed by a loop turn, not on
 * an edge case: await_span alone accounted for 88 such reads.
 *
 * The fix cannot be "drain the loop before freeing", which is the obvious
 * attempt and is wrong twice over:
 *
 *   - uv_run() skips its whole body when loop->stop_flag != 0, and resets the
 *     flag only on the way out. The await path sets it via uv_stop(), so the
 *     drain would be a silent no-op exactly where it was needed most;
 *   - when the flag *was* clear, running the loop from inside client_destroy
 *     also drives uvbus's own queued closes before uvbus_free() has run, which
 *     is re-entering a teardown halfway through.
 *
 * So the block outlives its closes instead: both callbacks release it, and the
 * last one to fire actually frees it.
 *
 * Three things are asserted per transport:
 *
 *   1. the call is answered and the handler ran exactly once, so the teardown
 *      being exercised is a real client with real state behind it;
 *   2. the loop can be turned over many times after the free without anything
 *      noticing -- which is where libuv reads the handles;
 *   3. the loop closes cleanly at the end, so no handle was left unaccounted
 *      for by deferring.
 *
 * Like promise_lifetime, the functional half passes without valgrind; the
 * memory half is what carries the defect, so the memcheck entry is the gate.
 *
 * Run: ctest -R client_teardown_memcheck
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <uv.h>

#include "uvrpc.h"
#include "../free_port.h"

#define WATCHDOG_MS 30000
/* Long enough that a loaded runner does not produce a false timeout. */
#define REPLY_TURNS 500
/* Generous on purpose: the point is that many turns happen, not how few. */
#define POST_FREE_TURNS 50

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

static int handler_calls;

static void handler(uvrpc_request_t* req, void* ctx) {
    static const uint8_t pong[] = {'p', 'o', 'n', 'g'};
    (void)ctx;
    handler_calls++;
    uvrpc_request_send_response(req, UVRPC_OK, pong, sizeof(pong));
    uvrpc_request_free(req);
}

static int replied;

static void on_reply(uvrpc_response_t* resp, void* ctx) {
    static const uint8_t pong[] = {'p', 'o', 'n', 'g'};
    (void)ctx;
    if (resp && resp->error_code == UVRPC_OK && resp->result_size == sizeof(pong)
        && resp->result && memcmp(resp->result, pong, sizeof(pong)) == 0) {
        replied = 1;
    }
    uvrpc_response_free(resp);
}

static void turn(uv_loop_t* loop, int turns) {
    for (int i = 0; i < turns; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }
}

static void run_cycle(uv_loop_t* loop, uvbus_loop_registry_t* registry,
                      const char* name, uvbus_transport_type_t transport,
                      const char* address) {
    printf("  %-8s ", name);

    uvrpc_config_t* scfg = uvrpc_config_set_loop_registry(uvrpc_config_new(), registry);
    scfg = uvrpc_config_set_loop(scfg, loop);
    scfg = uvrpc_config_set_address(scfg, address);
    scfg = uvrpc_config_set_transport(scfg, transport);
    uvrpc_server_t* server = uvrpc_server_create(scfg);
    uvrpc_config_free(scfg);
    if (!server) {
        check(0, "uvrpc_server_create returned NULL");
        return;
    }

    uvrpc_server_register(server, "ping", handler, NULL);
    if (uvrpc_server_start(server) != UVRPC_OK) {
        check(0, "uvrpc_server_start failed");
        uvrpc_server_free(server);
        return;
    }
    turn(loop, 50);

    uvrpc_config_t* ccfg = uvrpc_config_set_loop_registry(uvrpc_config_new(), registry);
    ccfg = uvrpc_config_set_loop(ccfg, loop);
    ccfg = uvrpc_config_set_address(ccfg, address);
    ccfg = uvrpc_config_set_transport(ccfg, transport);
    uvrpc_client_t* client = uvrpc_client_create(ccfg);
    uvrpc_config_free(ccfg);
    if (!client) {
        check(0, "uvrpc_client_create returned NULL");
        uvrpc_server_stop(server);
        uvrpc_server_free(server);
        return;
    }

    if (uvrpc_client_connect(client) != UVRPC_OK) {
        check(0, "uvrpc_client_connect failed");
    }
    /* connect is asynchronous on the socket transports, so a call issued
     * before it lands is rejected as not-connected rather than queued. */
    turn(loop, 100);

    handler_calls = 0;
    replied = 0;
    check(uvrpc_client_call(client, "ping", (const uint8_t*)"x", 1,
                            on_reply, NULL) == UVRPC_OK,
          "call was not accepted");
    turn(loop, REPLY_TURNS);

    check(replied, "the call was never answered");
    check(handler_calls == 1, "the handler did not run exactly once");

    /* --- the span under test -------------------------------------------------
     * Everything queued here lives inside the scheduler's block. */
    uvrpc_client_free(client);

    turn(loop, POST_FREE_TURNS);

    uvrpc_server_stop(server);
    uvrpc_server_free(server);
    turn(loop, POST_FREE_TURNS);

    printf("freed, loop turned over %d times\n", POST_FREE_TURNS * 2);
}

int main(void) {
    uv_loop_t loop;
    uv_timer_t watchdog;
    uvbus_loop_registry_t* registry;
    char tcp_address[64];
    char ipc_address[128];

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

    snprintf(tcp_address, sizeof(tcp_address), "tcp://127.0.0.1:%d",
             acceptance_free_port());
    snprintf(ipc_address, sizeof(ipc_address), "ipc:///tmp/uvrpc_teardown_%d.sock",
             (int)getpid());

    printf("freeing a client, then turning its loop over\n");

    run_cycle(&loop, registry, "INPROC", UVBUS_TRANSPORT_INPROC,
              "inproc://uvrpc_teardown");
    run_cycle(&loop, registry, "TCP", UVBUS_TRANSPORT_TCP, tcp_address);
    run_cycle(&loop, registry, "IPC", UVBUS_TRANSPORT_IPC, ipc_address);

    uvbus_loop_registry_free(registry);
    uv_close((uv_handle_t*)&watchdog, NULL);

    for (int i = 0; i < 200 && uv_loop_alive(&loop); i++) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }

    check(uv_loop_close(&loop) == 0,
          "uv_loop_close did not return 0 -- deferring the free left a handle "
          "unaccounted for");

    if (failures) {
        printf("\nclient_teardown: %d failure(s)\n", failures);
        return 1;
    }
    printf("\nclient_teardown: OK\n");
    return 0;
}
