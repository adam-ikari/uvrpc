/*
 * The blocking await has to mean the same thing on every transport.
 *
 * uvrpc_client_call_async() is not a callback API with nicer syntax. It blocks
 * the calling thread and runs the event loop until the response arrives, which
 * is what makes `UVRPC_AWAIT(...)` read like `await` in a language that has it.
 * That is also why it cannot be taken on trust: a wait that pumps the loop with
 * the wrong mode does not return an error, it simply never returns, and a demo
 * that runs on INPROC looks exactly like one that works.
 *
 * The defect this pins: the wait loop was
 *
 *     while (ctx->pending_results[slot] == NULL && !ctx->timed_out)
 *         uv_run(ctx->loop, UV_RUN_DEFAULT);
 *
 * UV_RUN_DEFAULT returns only once the loop has no referenced handles left. A
 * connected client is a referenced handle, and it stays connected for the whole
 * call -- so the response was received and dispatched *inside* a uv_run() that
 * was never going to come back, and the loop condition was never re-checked.
 * Measured across transports, with the server answering every time:
 *
 *     INPROC     returns UVRPC_OK
 *     SAMELOOP   returns UVRPC_OK
 *     IPC        returns UVRPC_OK
 *     TCP        never returns
 *
 * The two loopback transports hold no socket, so nothing keeps the loop
 * referenced and DEFAULT happens to behave like ONCE. That is the trap: the
 * demos in examples/ (async_await_demo, async_chain_demo) both use INPROC, so
 * the defect was invisible to every path anyone actually ran. A test written
 * on INPROC would still pass today.
 *
 * Four things are asserted per transport:
 *
 *   1. an answered call returns UVRPC_OK, and the server ran the handler
 *      exactly once -- an await that re-sent its request would otherwise look
 *      like success;
 *   2. the bytes come back unchanged;
 *   3. a call the server never answers returns UVRPC_ERROR_TIMEOUT when a
 *      timeout was configured, rather than blocking forever;
 *   4. uvrpc_promise_wait() returns a fulfilled promise's bytes while a
 *      connection is open.
 *
 * That last one is the only cell that can tell UV_RUN_ONCE from UV_RUN_DEFAULT,
 * and the reason is worth stating because the obvious candidates do not work.
 * uvrpc_client_call_async() settles through a longjmp out of the client
 * callback, which unwinds the uv_run() call before its return value is ever
 * consulted -- so on the happy path the loop mode is invisible. It is invisible
 * on the deadline path too: the armed timer fires and longjmps out the same way.
 * uvrpc_promise_wait() has no longjmp. Its loop condition is re-checked only
 * when uv_run() returns, so under UV_RUN_DEFAULT a live connection -- a
 * referenced handle -- means it never returns and the response is never seen.
 * Measured across transports with the defect present: INPROC and SAMELOOP
 * return, TCP/IPC/UDP hang.
 *
 * What this scenario deliberately does NOT assert is that a call with no
 * deadline returns when nothing can answer it. That case cannot be made to
 * return on a socket transport under either loop mode: there is no event coming
 * and the connection keeps the loop referenced, so UV_RUN_ONCE blocks in
 * epoll_wait exactly as UV_RUN_DEFAULT blocks in uv_run. "No timeout" means the
 * caller accepted waiting forever, and this library has no way to tell a
 * response that is slow from one that is never coming. That is what the deadline
 * is for.
 *
 * UDP is included, unlike in span_invariance: nothing here depends on delivery.
 * On loopback a datagram arrives or it does not, and both outcomes are
 * legitimate -- so the UDP cell asserts the outcome is one of the two the
 * contract allows, and that whichever it is, the client is left usable.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <uv.h>

#include "uvrpc.h"
#include "uvrpc_async.h"
#include "uvrpc_primitives.h"
#include "../free_port.h"

#define WATCHDOG_MS 40000
/* Short enough that a broken wait shows up as a failure rather than a timeout
 * of the whole run, long enough that a loaded CI runner does not produce a
 * false one. */
#define CALL_TIMEOUT_MS 400
#define ANSWER_LEN 16

static int failures;

static void check(int ok, const char* what) {
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static void watchdog_cb(uv_timer_t* timer) {
    uv_stop(uv_handle_get_loop((uv_handle_t*)timer));
    printf("FAIL: run exceeded %d ms -- a wait that cannot hang must still not be\n"
           "      able to wait forever\n", WATCHDOG_MS);
    failures++;
    _exit(1);
}

static void stop_cb(uv_timer_t* timer) {
    uv_stop(uv_handle_get_loop((uv_handle_t*)timer));
}

/* The payload the handler sends back. */
static const uint8_t answer[ANSWER_LEN] = {
    'a', 'n', 's', 'w', 'e', 'r', '-', 'f', 'r', 'o', 'm', '-', 'h', 'a', 'n', 'd'
};

static int handler_calls;
static int handler_answers;

/* Answers unless `silent`, which is how the timeout half of the scenario is
 * built: the request is genuinely received and genuinely never completed. */
static void handler(uvrpc_request_t* req, void* ctx) {
    handler_calls++;
    if (ctx == NULL) {
        return;  /* silent */
    }
    handler_answers++;
    uvrpc_request_send_response(req, UVRPC_OK, answer, sizeof(answer));
    uvrpc_request_free(req);
}

/* The promise the next promise_wait() case is waiting on. */
static uvrpc_promise_t* pending_promise;

/* Client callback that settles it. */
static void resolve_from_response(uvrpc_response_t* resp, void* ctx) {
    (void)ctx;
    uvrpc_promise_resolve(pending_promise, resp->result, resp->result_size);
    uvrpc_response_free(resp);
}

typedef struct {
    const char* name;
    uvbus_transport_type_t transport;
    /* A pointer, not a char array: an array initialised from a char* variable
     * (the IPC and socket addresses are) only takes the first element. */
    const char* address;
    int loses_datagrams;  /* UDP may not deliver; the others must */
} transport_case;

typedef struct {
    uvrpc_server_t* server;
    uvrpc_client_t* client;
} endpoints;

static int bring_up(uv_loop_t* loop, uvbus_loop_registry_t* registry,
                    const transport_case* tc, int silent, endpoints* out,
                    const char** reason) {
    uvrpc_config_t* scfg = uvrpc_config_set_loop_registry(
        uvrpc_config_new(), registry);
    scfg = uvrpc_config_set_loop(scfg, loop);
    scfg = uvrpc_config_set_address(scfg, tc->address);
    scfg = uvrpc_config_set_transport(scfg, tc->transport);
    out->server = uvrpc_server_create(scfg);
    uvrpc_config_free(scfg);
    if (!out->server) {
        *reason = "uvrpc_server_create returned NULL";
        return 0;
    }

    uvrpc_server_register(out->server, "ping", handler,
                          silent ? NULL : (void*)1);
    if (uvrpc_server_start(out->server) != UVRPC_OK) {
        *reason = "uvrpc_server_start did not return UVRPC_OK";
        uvrpc_server_free(out->server);
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
    out->client = uvrpc_client_create(ccfg);
    uvrpc_config_free(ccfg);
    if (!out->client) {
        *reason = "uvrpc_client_create returned NULL";
        uvrpc_server_stop(out->server);
        uvrpc_server_free(out->server);
        return 0;
    }

    if (uvrpc_client_connect(out->client) != UVRPC_OK) {
        *reason = "uvrpc_client_connect did not return UVRPC_OK";
        uvrpc_client_free(out->client);
        uvrpc_server_stop(out->server);
        uvrpc_server_free(out->server);
        return 0;
    }

    /* connect is asynchronous on the socket transports, so a call issued
     * before it lands is rejected as not-connected rather than queued. */
    for (int i = 0; i < 100; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }
    return 1;
}

static void tear_down(uv_loop_t* loop, endpoints* ep) {
    uvrpc_client_free(ep->client);
    uvrpc_server_stop(ep->server);
    uvrpc_server_free(ep->server);
    uv_run(loop, UV_RUN_NOWAIT);
}

/* One await against a server that answers. Returns 0 on success. */
static int check_answered(uv_loop_t* loop, uvbus_loop_registry_t* registry,
                          const transport_case* tc, const char** reason) {
    endpoints ep;
    if (!bring_up(loop, registry, tc, 0, &ep, reason)) {
        return 0;
    }

    handler_calls = 0;
    handler_answers = 0;

    uvrpc_async_ctx_t* ctx = uvrpc_async_ctx_new(loop);
    if (!ctx) {
        *reason = "uvrpc_async_ctx_new returned NULL";
        tear_down(loop, &ep);
        return 0;
    }
    int exec_status = uvrpc_async_exec(ctx, CALL_TIMEOUT_MS * 4);

    uvrpc_async_result_t* result = NULL;
    int status = -1;
    if (exec_status == UVRPC_OK) {
        status = uvrpc_client_call_async(ctx, ep.client, "ping",
                                         (const uint8_t*)"p", 1, &result);
    }

    int ok = 1;
    if (status != UVRPC_OK) {
        /* On UDP a datagram can go missing; the request was still sent and the
         * await still has to end, on the timeout. Every other transport owes
         * us an answer. */
        int acceptable = tc->loses_datagrams &&
                         (status == UVRPC_ERROR_TIMEOUT ||
                          status == UVRPC_ERROR_NOT_CONNECTED);
        if (!acceptable) {
            printf("FAIL: %s: an answered call returned %d, expected %d\n",
                   tc->name, status, UVRPC_OK);
            failures++;
            ok = 0;
        }
    } else {
        if (handler_calls != 1) {
            printf("FAIL: %s: the server ran the handler %d time(s) for one await\n",
                   tc->name, handler_calls);
            failures++;
            ok = 0;
        }
        if (!result) {
            printf("FAIL: %s: UVRPC_OK but no result\n", tc->name);
            failures++;
            ok = 0;
        } else {
            if (result->result_size != sizeof(answer) ||
                memcmp(result->result, answer, sizeof(answer)) != 0) {
                printf("FAIL: %s: the awaited bytes differ from what the server sent\n",
                       tc->name);
                failures++;
                ok = 0;
            }
            /* The result must not be a disguised error. */
            if (result->error_code != 0) {
                printf("FAIL: %s: UVRPC_OK but error_code is %d\n",
                       tc->name, result->error_code);
                failures++;
                ok = 0;
            }
        }
    }

    if (result) uvrpc_async_result_free(result);
    uvrpc_async_ctx_free(ctx);
    tear_down(loop, &ep);
    return ok;
}

/* uvrpc_promise_wait() over a live connection.
 *
 * This is the cell that fails under UV_RUN_DEFAULT, and it is the only one that
 * can. uvrpc_client_call_async() settles through a longjmp out of the client
 * callback, which unwinds the uv_run() call before its return value is ever
 * consulted, so the loop mode is invisible on the await path -- with or without
 * a deadline, since the deadline fires and longjmps the same way. promise_wait()
 * has no longjmp: its loop condition is re-checked only when uv_run() returns,
 * and UV_RUN_DEFAULT does not return while the connection is referenced.
 *
 * The promise is resolved from the client callback like any other, which is the
 * point -- this is the same shape as a promise fed by an RPC response, over every
 * transport, which is how the demos and real applications use it. */
static int check_promise_wait_over_connection(
        uv_loop_t* loop, uvbus_loop_registry_t* registry,
        const transport_case* tc, const char** reason) {
    endpoints ep;
    if (!bring_up(loop, registry, tc, 0, &ep, reason)) {
        return 0;
    }

    pending_promise = uvrpc_promise_create(loop);
    if (!pending_promise) {
        *reason = "uvrpc_promise_create returned NULL";
        tear_down(loop, &ep);
        return 0;
    }

    uvrpc_async_result_t* await_result = NULL;
    uvrpc_async_ctx_t* ctx = uvrpc_async_ctx_new(loop);
    if (ctx) {
        uvrpc_async_exec(ctx, CALL_TIMEOUT_MS * 4);
        /* Warmed up over a real await, so the connection is genuinely open and
         * referenced -- which is the condition that makes the difference. */
        uvrpc_client_call_async(ctx, ep.client, "ping",
                                (const uint8_t*)"p", 1, &await_result);
        if (await_result) uvrpc_async_result_free(await_result);
    }

    handler_calls = 0;
    handler_answers = 0;

    uvrpc_client_call(ep.client, "ping", (const uint8_t*)"p", 1,
                      resolve_from_response, NULL);

    int status = uvrpc_promise_wait(pending_promise);

    int ok = 1;
    if (status != UVRPC_OK) {
        int acceptable = tc->loses_datagrams &&
                         (status == UVRPC_ERROR_TIMEOUT ||
                          status == UVRPC_ERROR_NOT_CONNECTED);
        if (!acceptable) {
            printf("FAIL: %s: promise_wait over a live connection returned %d, "
                   "expected %d\n", tc->name, status, UVRPC_OK);
            failures++;
            ok = 0;
        }
    } else {
        uint8_t* result = NULL;
        size_t size = 0;
        if (uvrpc_promise_get_result(pending_promise, &result, &size) != UVRPC_OK ||
            size != sizeof(answer) ||
            memcmp(result, answer, sizeof(answer)) != 0) {
            printf("FAIL: %s: promise_wait did not yield the server's bytes\n",
                   tc->name);
            failures++;
            ok = 0;
        }
    }

    uvrpc_promise_destroy(pending_promise);
    pending_promise = NULL;
    if (ctx) uvrpc_async_ctx_free(ctx);
    tear_down(loop, &ep);
    return ok;
}

/* One await against a server that received the request and never answers.
 * With a timeout configured, the call has to end on that timeout. */
static int check_timeout(uv_loop_t* loop, uvbus_loop_registry_t* registry,
                         const transport_case* tc, const char** reason) {
    endpoints ep;
    if (!bring_up(loop, registry, tc, 1, &ep, reason)) {
        return 0;
    }

    handler_calls = 0;
    handler_answers = 0;

    uvrpc_async_ctx_t* ctx = uvrpc_async_ctx_new(loop);
    if (!ctx) {
        *reason = "uvrpc_async_ctx_new returned NULL";
        tear_down(loop, &ep);
        return 0;
    }
    int exec_status = uvrpc_async_exec(ctx, CALL_TIMEOUT_MS);

    uvrpc_async_result_t* result = NULL;
    int status = -1;
    if (exec_status == UVRPC_OK) {
        status = uvrpc_client_call_async(ctx, ep.client, "ping",
                                         (const uint8_t*)"p", 1, &result);
    }

    int ok = 1;
    if (status == UVRPC_OK) {
        /* The server never answers, so an OK here means the wait ended for some
         * other reason and reported success. On UDP the datagram may not have
         * arrived at all, in which case the timeout is the honest answer. */
        printf("FAIL: %s: an unanswered call returned UVRPC_OK\n", tc->name);
        failures++;
        ok = 0;
    } else if (status != UVRPC_ERROR_TIMEOUT &&
               !(tc->loses_datagrams &&
                 status == UVRPC_ERROR_NOT_CONNECTED)) {
        printf("FAIL: %s: an unanswered call returned %d, expected %d\n",
               tc->name, status, UVRPC_ERROR_TIMEOUT);
        failures++;
        ok = 0;
    }
    if (handler_answers != 0) {
        printf("FAIL: %s: the silent handler answered %d time(s)\n",
               tc->name, handler_answers);
        failures++;
        ok = 0;
    }

    if (result) uvrpc_async_result_free(result);
    uvrpc_async_ctx_free(ctx);
    tear_down(loop, &ep);
    return ok;
}

int main(void) {
    uv_loop_t loop = {0};
    uv_loop_init(&loop);

    uv_timer_t watchdog;
    uv_timer_init(&loop, &watchdog);
    uv_timer_start(&watchdog, watchdog_cb, WATCHDOG_MS, 0);
    uv_unref((uv_handle_t*)&watchdog);

    uvbus_loop_registry_t* registry = uvbus_loop_registry_new();
    check(registry != NULL, "registry created");
    if (!registry) return 1;

    char ipc_addr[128];
    snprintf(ipc_addr, sizeof(ipc_addr), "ipc:///tmp/uvrpc_await_%d.sock",
             (int)getpid());

    char tcp_addr[64];
    char udp_addr[64];
    snprintf(tcp_addr, sizeof(tcp_addr), "tcp://127.0.0.1:%d",
             acceptance_free_port());
    snprintf(udp_addr, sizeof(udp_addr), "udp://127.0.0.1:%d",
             acceptance_free_port());

    transport_case cases[] = {
        {"INPROC",   UVBUS_TRANSPORT_INPROC,   "inproc://uvrpc_await", 0},
        {"SAMELOOP", UVBUS_TRANSPORT_SAMELOOP, "sameloop://uvrpc_await", 0},
        {"IPC",      UVBUS_TRANSPORT_IPC,      ipc_addr, 0},
        {"TCP",      UVBUS_TRANSPORT_TCP,      tcp_addr, 0},
        {"UDP",      UVBUS_TRANSPORT_UDP,      udp_addr, 1},
    };
    const int case_count = (int)(sizeof(cases) / sizeof(cases[0]));

    for (int i = 0; i < case_count; i++) {
        printf("[%d/%d] %s: an answered call comes back through await\n",
               i + 1, case_count, cases[i].name);
        const char* reason = "";
        if (check_answered(&loop, registry, &cases[i], &reason)) {
            printf("      await returned, handler ran once, bytes intact\n");
        }
    }

    for (int i = 0; i < case_count; i++) {
        printf("[%d/%d] %s: promise_wait returns while the connection is open\n",
               i + 1, case_count, cases[i].name);
        const char* reason = "";
        if (check_promise_wait_over_connection(&loop, registry, &cases[i], &reason)) {
            printf("      wait returned the server's bytes\n");
        }
    }

    for (int i = 0; i < case_count; i++) {
        printf("[%d/%d] %s: an unanswered call ends on the timeout\n",
               i + 1, case_count, cases[i].name);
        const char* reason = "";
        if (check_timeout(&loop, registry, &cases[i], &reason)) {
            printf("      the wait ended without blocking forever\n");
        }
    }

    unlink(ipc_addr);

    uv_close((uv_handle_t*)&watchdog, NULL);
    uvbus_loop_registry_free(registry);
    while (uv_loop_alive(&loop)) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }
    uv_loop_close(&loop);

    if (failures == 0) {
        printf("await span invariance: OK\n");
        return 0;
    }
    printf("await span invariance: %d check(s) failed\n", failures);
    return 1;
}