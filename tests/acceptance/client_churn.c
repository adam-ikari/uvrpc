/*
 * A client that leaves has to stop counting.
 *
 * uvrpc_server_get_client_count() was a high-water mark, not a count: it
 * incremented the first time a connection spoke and never went down again,
 * because nothing told the server when a connection died. The same number
 * drives the max_clients gate, so a server started with max_clients = 1
 * accepted one client, ever. The second client -- arriving after the first had
 * disconnected, with nothing else connected -- was refused with
 * UVRPC_ERROR_MAX_CLIENTS, and every client after that was refused too. The
 * server looked fine from the outside: it answered calls, it did not crash, it
 * simply never took another client again.
 *
 * That silence came from the transports, and it was not the same defect
 * everywhere. TCP and IPC read a disconnected client with
 *
 *     if (nread < 0) { if (nread != UV_EOF) error_cb(...); return; }
 *
 * which reports an error and otherwise does nothing: no uv_close(), no read
 * stop, no removal from the server's client table. The connection's block --
 * 256 KB for TCP, since it embeds the read buffer -- stayed in that table
 * until teardown, and the layer above was never told. uvbus even had a slot
 * for the notification, uvbus_close_callback_t, which was stored and assigned
 * on every transport and had never once been invoked.
 *
 * INPROC and SAMELOOP have no socket and so no EOF. A client leaves them by
 * way of its own disconnect, which did prune their tables -- but stopped
 * there, so the layer above heard nothing either way. Same count, same gate,
 * same permanent lockout, reached by a different route.
 *
 * What this pins, on all four transports, across three rounds of connect ->
 * call -> disconnect:
 *
 *   1. the server counts exactly one client while one is connected;
 *   2. it counts none once that client has gone;
 *   3. the next client is served normally, rather than refused for a limit
 *      that nothing has actually reached.
 *
 * Steps 2 and 3 are the defect. Without them the first round passes and the
 * server has already become permanently unreachable -- which is why a test
 * that only ever used one client would have passed here too.
 *
 * Run: ctest -R client_churn
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <uv.h>

#include "uvrpc.h"
#include "../free_port.h"

#define WATCHDOG_MS 30000
/* Generous on purpose. NOWAIT returns whether or not anything happened, so
 * this cannot block; a missing notification just exhausts the budget. */
#define PUMP_TURNS 400
#define ROUNDS 3

static int failures;
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

static void handler(uvrpc_request_t* req, void* ctx) {
    static const uint8_t pong[] = {'p', 'o', 'n', 'g'};
    (void)ctx;
    uvrpc_request_send_response(req, UVRPC_OK, pong, sizeof(pong));
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

/*
 * Turn the loop until the server agrees it holds `want` clients, or until the
 * budget runs out. NOWAIT cannot block, and the FIN the server is waiting for
 * is already in the socket buffer once the client is freed -- so a working
 * notification settles this in a handful of turns, and a missing one shows up
 * as the budget running out rather than as a hang.
 */
static void pump_until(uv_loop_t* loop, uvrpc_server_t* server, int want) {
    for (int i = 0; i < PUMP_TURNS &&
                    uvrpc_server_get_client_count(server) != want; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }
}

static uvrpc_client_t* connect_client(uv_loop_t* loop,
                                      uvbus_loop_registry_t* registry,
                                      uvbus_transport_type_t transport,
                                      const char* address) {
    uvrpc_config_t* cfg = uvrpc_config_set_loop_registry(uvrpc_config_new(), registry);
    cfg = uvrpc_config_set_loop(cfg, loop);
    cfg = uvrpc_config_set_address(cfg, address);
    cfg = uvrpc_config_set_transport(cfg, transport);

    uvrpc_client_t* client = uvrpc_client_create(cfg);
    uvrpc_config_free(cfg);
    if (!client) {
        return NULL;
    }
    if (uvrpc_client_connect(client) != UVRPC_OK) {
        uvrpc_client_free(client);
        return NULL;
    }
    /* The connect is asynchronous: uvrpc_client_connect() returns once the
     * attempt has been queued, so a call issued straight afterwards has no
     * connection to go out on yet. Turn the loop until it is up. */
    for (int i = 0; i < PUMP_TURNS; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }
    return client;
}

static void run_churn(uv_loop_t* loop,
                      uvbus_loop_registry_t* registry,
                      uvbus_transport_type_t transport,
                      const char* label,
                      const char* address) {
    printf("  %-8s ", label);

    uvrpc_config_t* cfg = uvrpc_config_set_loop_registry(uvrpc_config_new(), registry);
    cfg = uvrpc_config_set_loop(cfg, loop);
    cfg = uvrpc_config_set_address(cfg, address);
    cfg = uvrpc_config_set_transport(cfg, transport);
    /* One. So that the count going down is the only thing that lets a second
     * client in at all. */
    cfg = uvrpc_config_set_max_clients(cfg, 1);

    uvrpc_server_t* server = uvrpc_server_create(cfg);
    uvrpc_config_free(cfg);
    check(server != NULL, "server created");
    if (!server) {
        return;
    }
    uvrpc_server_register(server, "ping", handler, NULL);
    check(uvrpc_server_start(server) == UVRPC_OK, "server started");

    for (int round = 0; round < ROUNDS; round++) {
        replies = 0;
        last_error_code = -1;

        uvrpc_client_t* client = connect_client(loop, registry, transport, address);
        check(client != NULL, "client connected");
        if (!client) {
            break;
        }

        check(uvrpc_client_call(client, "ping", (const uint8_t*)"p", 1,
                                on_response, NULL) == UVRPC_OK,
              "the call was accepted");
        for (int i = 0; i < PUMP_TURNS && replies == 0; i++) {
            uv_run(loop, UV_RUN_NOWAIT);
        }
        check(replies > 0, "the call was answered");
        check(last_error_code == UVRPC_OK,
              "the call succeeded, so this client is the one holding the slot");

        /* While it is here, it is counted -- that half already worked. */
        check(uvrpc_server_get_client_count(server) == 1,
              "one client is counted while one is connected");

        /* Gone. From here on the count has to come back down, or the next
         * round is refused for a limit nothing is holding. */
        uvrpc_client_free(client);
        pump_until(loop, server, 0);

        check(uvrpc_server_get_client_count(server) == 0,
              "the count returns to zero once the client has gone");
    }

    uvrpc_server_stop(server);
    uvrpc_server_free(server);
    for (int i = 0; i < PUMP_TURNS; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }

    printf("%d round(s) of connect/call/disconnect\n", ROUNDS);
}

int main(void) {
    uv_loop_t loop;
    uv_timer_t watchdog;
    uvbus_loop_registry_t* registry;
    char tcp_address[64];
    char ipc_address[128];
    char inproc_address[64];
    char sameloop_address[64];

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

    printf("a client that disconnects stops counting\n");

    snprintf(tcp_address, sizeof(tcp_address), "tcp://127.0.0.1:%d",
             acceptance_free_port());
    snprintf(ipc_address, sizeof(ipc_address), "ipc:///tmp/uvrpc_churn_%d.sock",
             (int)getpid());

    snprintf(inproc_address, sizeof(inproc_address), "inproc://uvrpc_churn_%d",
             (int)getpid());
    snprintf(sameloop_address, sizeof(sameloop_address), "sameloop://uvrpc_churn_%d",
             (int)getpid());

    run_churn(&loop, registry, UVBUS_TRANSPORT_TCP, "TCP", tcp_address);
    run_churn(&loop, registry, UVBUS_TRANSPORT_IPC, "IPC", ipc_address);
    run_churn(&loop, registry, UVBUS_TRANSPORT_INPROC, "INPROC", inproc_address);
    run_churn(&loop, registry, UVBUS_TRANSPORT_SAMELOOP, "SAMELOOP", sameloop_address);

    uvbus_loop_registry_free(registry);
    uv_close((uv_handle_t*)&watchdog, NULL);

    for (int i = 0; i < 200 && uv_loop_alive(&loop); i++) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }

    check(uv_loop_close(&loop) == 0,
          "uv_loop_close did not return 0 -- a handle was never closed");

    if (failures) {
        printf("\nclient_churn: %d failure(s)\n", failures);
        return 1;
    }
    printf("\nclient_churn: OK\n");
    return 0;
}
