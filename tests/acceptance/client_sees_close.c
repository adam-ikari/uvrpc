/*
 * A client whose server has gone away has to find out.
 *
 * The stream transports read the client's own connection through the same
 * on_client_read that serves a server's accepted connections. Its EOF branch
 * does the right thing on the server side -- report the departure, prune the
 * table, uv_close() the block -- and the guard that keeps that work on the
 * server side is correct: on the client side the block belongs to uvrpc_client,
 * which closes and frees it itself.
 *
 * What the guard also swallowed was the report. tcp_forget_client() and
 * ipc_forget_client() fire the connection-lost notification *before* their own
 * is_server guard, so they are already safe to call from either side -- they
 * simply were not being called from the client's. And uvrpc_client never
 * subscribed to a transport error callback in the first place, so nothing at
 * all reached it.
 *
 * The result: client->is_connected stayed 1 for the rest of the process once it
 * had been set. A call made after the server is gone is accepted, written (the
 * first write after the peer's FIN succeeds), and answered by nobody -- the
 * caller waits out its whole deadline for a server that is not there. The same
 * applies to a server that crashed, restarted, or dropped the connection for
 * any reason, which is the ordinary way a client discovers a lost peer
 * everywhere else.
 *
 * The in-process two have the mirror-image of the same gap: a client's
 * departure was reported to its server, and a server's departure was reported
 * to nobody -- it detached its clients in silence, so their is_connected
 * outlived it too.
 *
 * What this pins, on all four transports a client can hold a connection on:
 * once the server is gone, the next call comes back with
 * UVRPC_ERROR_NOT_CONNECTED promptly rather than being accepted and left to
 * time out.
 *
 * UDP is absent, and deliberately: it has no connection to lose, so there is no
 * close for it to report and a call to a departed server can only time out.
 *
 * Run: ctest -R client_sees_close
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <uv.h>

#include "uvrpc.h"
#include "../free_port.h"

#define WATCHDOG_MS 30000
#define MAX_TURNS 400
/* Far longer than the pump budget below, so "the call was accepted" and "the
 * call waited out its deadline" cannot be confused for one another. */
#define CALLER_DEADLINE_MS 60000

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

static void on_response(uvrpc_response_t* resp, void* ctx) {
    (void)ctx;
    responses++;
    last_status = resp->status;
    uvrpc_response_free(resp);
}

static void pump(uv_loop_t* loop, int turns) {
    for (int i = 0; i < turns; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }
}

static int pump_until(uv_loop_t* loop, int want) {
    for (int i = 0; i < MAX_TURNS && responses < want; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }
    return responses;
}

typedef struct {
    const char* name;
    uvbus_transport_type_t transport;
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
                    const transport_case* tc,
                    const char** reason) {
    static const char ping[] = "ping";
    int alive_status = 0;
    int after_close_status = 0;
    uvrpc_client_t* client = NULL;

    uvrpc_config_t* scfg = make_config(loop, registry, tc);
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
    ccfg = uvrpc_config_set_timeout(ccfg, CALLER_DEADLINE_MS);

    client = uvrpc_client_create(ccfg);
    uvrpc_config_free(ccfg);
    if (!client) {
        *reason = "client_create returned NULL";
        uvrpc_server_free(server);
        return 0;
    }
    if (uvrpc_client_connect(client) != UVRPC_OK) {
        *reason = "the client could not connect";
        goto done;
    }
    pump(loop, 40);

    /* Speak once, so the pair is known good and the later failure is about the
     * server going away rather than about a client that never worked. */
    responses = 0;
    last_status = 12345;
    if (uvrpc_client_call(client, "ping", (const uint8_t*)ping, sizeof(ping),
                          on_response, NULL) != UVRPC_OK) {
        *reason = "the first call could not be issued";
        goto done;
    }
    if (pump_until(loop, 1) != 1) {
        *reason = "the running server did not answer";
        goto done;
    }
    alive_status = last_status;

    /* The server goes away. Its side of every connection is closed, which is
     * how a client learns on every other transport and in every other
     * library. */
    uvrpc_server_free(server);
    server = NULL;

    /* Give the close plenty of loop turns to arrive. The budget is in turns,
     * not in the caller's deadline, so a client that never notices cannot pass
     * by simply waiting. */
    pump(loop, MAX_TURNS);

    responses = 0;
    last_status = 12345;
    int rc = uvrpc_client_call(client, "ping", (const uint8_t*)ping, sizeof(ping),
                               on_response, NULL);
    if (rc == UVRPC_OK) {
        /* Accepted. Nothing threw it out early, so the caller is now parked on
         * a response that has no server behind it. */
        pump_until(loop, 1);
        after_close_status = (responses > 0) ? last_status : 12345;
    }

    if (alive_status != UVRPC_OK) {
        *reason = "the running server answered with an error";
        goto done;
    }
    if (rc == UVRPC_OK) {
        if (responses > 0) {
            *reason = "a call after the server was freed came back with an answer";
        } else {
            *reason = "a call after the server was freed was accepted and left to time out";
        }
        goto done;
    }
    if (rc != UVRPC_ERROR_NOT_CONNECTED) {
        *reason = "the call after the server was freed failed with the wrong code";
        goto done;
    }

    printf("  %-4s alive=%d after-close=%d (no answer waited for)\n",
           tc->name, alive_status, rc);

done:
    if (client) uvrpc_client_free(client);
    if (server) uvrpc_server_free(server);
    return *reason[0] == '\0';
}

int main(void) {
    uv_loop_t loop = {0};
    uv_timer_t watchdog;
    char ipc_addr[128];
    char tcp_addr[64];

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

    snprintf(ipc_addr, sizeof(ipc_addr), "ipc:///tmp/uvrpc_sees_close_%d.sock",
             (int)getpid());
    snprintf(tcp_addr, sizeof(tcp_addr), "tcp://127.0.0.1:%d",
             acceptance_free_port());

    /* UDP is absent: it has no connection to lose, so there is no close for a
     * client to notice. The other four are the transports a client can hold a
     * connection on. */
    transport_case cases[] = {
        {"TCP",      UVBUS_TRANSPORT_TCP,      tcp_addr},
        {"IPC",      UVBUS_TRANSPORT_IPC,      ipc_addr},
        {"INPROC",   UVBUS_TRANSPORT_INPROC,   "inproc://uvrpc_sees_close"},
        {"SAMELOOP", UVBUS_TRANSPORT_SAMELOOP, "sameloop://uvrpc_sees_close"},
    };
    const int case_count = (int)(sizeof(cases) / sizeof(cases[0]));

    printf("a client, and a server that is no longer there\n");

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
        printf("\nclient_sees_close: %d failure(s)\n", failures);
        return 1;
    }
    printf("\nclient_sees_close: OK\n");
    return 0;
}
