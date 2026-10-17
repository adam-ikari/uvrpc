/*
 * Teardown must not close a handle twice, whatever order it arrives in.
 *
 * A transport's disconnect is reachable from more than one direction: the bus
 * it belongs to can be disconnected, and its own vtable free calls it again on
 * the way out. Whether a second pass finds work to do is decided by each driver
 * for itself -- the in-process and socket drivers null the impl pointer, which
 * makes the second pass a no-op -- and a handle that is uv_close()d twice takes
 * the process down inside libuv rather than failing politely.
 *
 * The other half is the order two peers are released in. A server that goes
 * first leaves its clients' connections closing under them; a client that goes
 * first leaves the server holding a connection whose read callback may already
 * be queued. Both are ordinary, both are what a caller does on any shutdown
 * path, and neither may double-close or reach freed memory.
 *
 * Every row here ends by draining the loop and requiring uv_loop_close() to
 * succeed, so a leaked or double-closed handle is visible even when it is not
 * fatal. Run it under valgrind for the freed-memory half; that is what the
 * _memcheck target is.
 *
 * Run: ctest -R teardown_orders
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

static void pump(uv_loop_t* loop, int turns) {
    for (int i = 0; i < turns; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }
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

static int pump_until(uv_loop_t* loop, int want) {
    for (int i = 0; i < MAX_TURNS && responses < want; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }
    return responses;
}

typedef enum {
    ORDER_SERVER_THEN_CLIENT,   /* the server goes first */
    ORDER_CLIENT_THEN_SERVER,   /* the client goes first */
    ORDER_DISCONNECT_TWICE,     /* disconnecting twice must be a no-op */
    ORDER_STOP_THEN_FREE,       /* stopped, then released */
    ORDER_COUNT
} teardown_order;

static const char* order_name(teardown_order o) {
    switch (o) {
        case ORDER_SERVER_THEN_CLIENT: return "server-first";
        case ORDER_CLIENT_THEN_SERVER: return "client-first";
        case ORDER_DISCONNECT_TWICE:   return "disconnect-twice";
        case ORDER_STOP_THEN_FREE:     return "stop-then-free";
        default:                       return "?";
    }
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

/* Returns nonzero on success, with `reason` naming the first thing that went
 * wrong. Whatever happens, both peers are released exactly once by the time
 * this returns. */
static int run_case(uv_loop_t* loop, uvbus_loop_registry_t* registry,
                    const transport_case* tc, teardown_order order,
                    const char** reason) {
    static const char ping[] = "ping";
    uvrpc_server_t* server = NULL;
    uvrpc_client_t* client = NULL;
    int rc = 0;

    uvrpc_config_t* scfg = make_config(loop, registry, tc);
    server = uvrpc_server_create(scfg);
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

    /* One exchange, so the pair is known good and what follows is about
     * teardown rather than about a connection that never worked. */
    responses = 0;
    last_status = 12345;
    if (uvrpc_client_call(client, "ping", (const uint8_t*)ping, sizeof(ping),
                          on_response, NULL) != UVRPC_OK) {
        *reason = "the call could not be issued";
        goto done;
    }
    if (pump_until(loop, 1) != 1 || last_status != UVRPC_OK) {
        *reason = "the server did not answer";
        goto done;
    }

    switch (order) {
        case ORDER_SERVER_THEN_CLIENT:
            uvrpc_server_free(server);
            server = NULL;
            pump(loop, MAX_TURNS);
            uvrpc_client_free(client);
            client = NULL;
            break;

        case ORDER_CLIENT_THEN_SERVER:
            uvrpc_client_free(client);
            client = NULL;
            pump(loop, MAX_TURNS);
            uvrpc_server_free(server);
            server = NULL;
            break;

        case ORDER_DISCONNECT_TWICE:
            uvrpc_client_disconnect(client);
            /* The second one has nothing left to do; if it closes the same
             * handle again, libuv ends the process here. */
            uvrpc_client_disconnect(client);
            pump(loop, MAX_TURNS);
            uvrpc_client_free(client);
            client = NULL;
            uvrpc_server_free(server);
            server = NULL;
            break;

        case ORDER_STOP_THEN_FREE:
            uvrpc_server_stop(server);
            pump(loop, MAX_TURNS);
            uvrpc_client_free(client);
            client = NULL;
            uvrpc_server_free(server);
            server = NULL;
            break;

        default:
            *reason = "unknown order";
            goto done;
    }

    /* Let whatever the teardown queued run out. A handle closed twice, or a
     * block released while a callback still points at it, surfaces here. */
    pump(loop, MAX_TURNS);
    rc = 1;

done:
    if (client) uvrpc_client_free(client);
    if (server) uvrpc_server_free(server);
    return rc;
}

int main(void) {
    uv_loop_t loop = {0};
    uv_timer_t watchdog;
    char ipc_addr[128];
    char tcp_addr[64];
    char udp_addr[64];
    uvbus_loop_registry_t* registry = NULL;

    setvbuf(stdout, NULL, _IONBF, 0);

    if (uv_loop_init(&loop) != 0) {
        printf("FAIL: uv_loop_init\n");
        return 1;
    }

    uv_timer_init(&loop, &watchdog);
    uv_timer_start(&watchdog, watchdog_cb, WATCHDOG_MS, 0);
    uv_unref((uv_handle_t*)&watchdog);

    registry = uvbus_loop_registry_new();
    check(registry != NULL, "registry created");
    if (!registry) return 1;

    snprintf(ipc_addr, sizeof(ipc_addr), "ipc:///tmp/uvrpc_teardown_%d.sock",
             (int)getpid());
    snprintf(tcp_addr, sizeof(tcp_addr), "tcp://127.0.0.1:%d",
             acceptance_free_port());
    snprintf(udp_addr, sizeof(udp_addr), "udp://127.0.0.1:%d",
             acceptance_free_port());

    transport_case cases[] = {
        {"TCP",      UVBUS_TRANSPORT_TCP,      tcp_addr},
        {"IPC",      UVBUS_TRANSPORT_IPC,      ipc_addr},
        {"UDP",      UVBUS_TRANSPORT_UDP,      udp_addr},
        {"INPROC",   UVBUS_TRANSPORT_INPROC,   "inproc://uvrpc_teardown"},
        {"SAMELOOP", UVBUS_TRANSPORT_SAMELOOP, "sameloop://uvrpc_teardown"},
    };
    const int case_count = (int)(sizeof(cases) / sizeof(cases[0]));

    printf("shutting down in every order, once\n");

    for (int i = 0; i < case_count; i++) {
        for (int o = 0; o < ORDER_COUNT; o++) {
            const char* reason = "";
            if (!run_case(&loop, registry, &cases[i], (teardown_order)o,
                          &reason)) {
                printf("FAIL: %s/%s: %s\n", cases[i].name,
                       order_name((teardown_order)o), reason);
                failures++;
            } else {
                printf("  %-9s %-16s released once, clean\n", cases[i].name,
                       order_name((teardown_order)o));
            }
        }
    }

    uvbus_loop_registry_free(registry);
    uv_close((uv_handle_t*)&watchdog, NULL);

    for (int i = 0; i < 2000 && uv_loop_alive(&loop); i++) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }

    check(uv_loop_close(&loop) == 0,
          "uv_loop_close did not return 0 -- a handle was never closed");

    if (failures) {
        printf("\nteardown_orders: %d failure(s)\n", failures);
        return 1;
    }
    printf("\nteardown_orders: OK\n");
    return 0;
}
