/*
 * A connect that fails has to give back the reference it borrowed.
 *
 * uvrpc_client_connect() takes a reference before calling uvbus_connect():
 *
 *      client->ref_count++;      // hold for the pending callback
 *      err = uvbus_connect(...);
 *      if (err != UVBUS_OK) return UVRPC_ERROR_TRANSPORT;
 *
 * The comment above the increment says why the hold exists -- the callback
 * carries this pointer, so the client has to outlive the attempt -- and
 * client_connect_callback() is what drops it again. When uvbus_connect()
 * refuses synchronously there is no callback coming: the increment is left
 * unmatched, uvrpc_client_free() walks down to 1 and stops, and the client is
 * never destroyed. uvrpc_client_connect_with_callback() takes the same hold at
 * the same place and returns the same way, so it leaks the same way. Both
 * entry points are exercised here, one round each.
 *
 * SAMELOOP is what makes this reachable from a test. Its server table is a
 * fixed 64 entries, sameloop_connect() fills them from real connects, and the
 * 65th connect fails on the spot with UVBUS_ERROR_MAX_CLIENTS -- synchronously,
 * from inside uvbus_connect(), before any callback has run. Same call, same
 * client, two outcomes differing only in whether the callback happens to
 * arrive; the reference accounting has to survive both.
 *
 * The 64 that succeed are in the test for the contrast: they take the hold
 * and drop it in the callback that sameloop_connect() runs before it returns.
 * So they are destroyed by uvrpc_client_free() and only the rejected one is
 * left behind -- carrying its uvasync scheduler's handle, which is still on
 * the loop. That is what the plain entry catches: uv_loop_close() refuses
 * while the handle is there. valgrind sees further, naming the block itself,
 * and the memcheck entry is what reports the size of what was lost.
 *
 * The constant is private to uvbus_transport_sameloop.c. Hardcoding 64 is the
 * point: if the table ever grows, the 65th connect starts succeeding and the
 * check below fails loudly instead of this test quietly going to sleep.
 *
 * Run: ctest -R connect_refund_memcheck
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <uv.h>

#include "uvrpc.h"

#define WATCHDOG_MS 30000
#define SAMELOOP_CAPACITY 64
#define ATTEMPTS (SAMELOOP_CAPACITY + 1)

static int failures;
static int callback_fired;

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

static void handler(uvrpc_request_t* req, void* ctx) {
    static const uint8_t pong[] = {'p', 'o', 'n', 'g'};
    (void)ctx;
    uvrpc_request_send_response(req, UVRPC_OK, pong, sizeof(pong));
    uvrpc_request_free(req);
}

static void on_connect(int status, void* ctx) {
    (void)status;
    (void)ctx;
    callback_fired++;
}

static void pump(uv_loop_t* loop, int turns) {
    for (int i = 0; i < turns; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }
}

/*
 * Fill the SAMELOOP table one client at a time and ask for one more than it
 * holds. `with_callback` selects which uvrpc_client entry point takes the
 * hold; the two are separate code paths and each needs its own evidence.
 */
static void run_round(uv_loop_t* loop,
                      uvrpc_config_t* template_config,
                      const char* label,
                      int with_callback) {
    uvrpc_client_t* clients[ATTEMPTS];
    int connected = 0;
    int refused = 0;

    callback_fired = 0;

    for (int i = 0; i < ATTEMPTS; i++) {
        clients[i] = uvrpc_client_create(template_config);
        if (!clients[i]) {
            check(0, "client_create returned NULL");
            break;
        }

        int rc;
        if (with_callback) {
            rc = uvrpc_client_connect_with_callback(clients[i], on_connect, NULL);
        } else {
            rc = uvrpc_client_connect(clients[i]);
        }

        if (i < SAMELOOP_CAPACITY) {
            if (rc == UVRPC_OK) {
                connected++;
            } else {
                printf("FAIL: %s client %d of %d was refused (rc=%d)",
                       label, i + 1, SAMELOOP_CAPACITY, rc);
                failures++;
            }
        } else if (rc != UVRPC_OK) {
            refused++;
        } else {
            printf("FAIL: %s client %d connected -- the table holds %d, "
                   "so this round is no longer reaching the limit",
                   label, i + 1, SAMELOOP_CAPACITY);
            failures++;
        }
    }

    pump(loop, 20);

    printf("  round %-14s %d connected, %d refused\n", label, connected, refused);
    check(connected == SAMELOOP_CAPACITY, "not every client below the limit connected");
    check(refused == 1, "the client over the limit was not refused");

    if (with_callback) {
        /* Only the connects that were accepted reach a callback; the one the
         * transport refused on the spot returns its error code and that is the
         * whole of the signal it gives. */
        check(callback_fired == SAMELOOP_CAPACITY,
              "the accepted connects did not all report through the callback");
    }

    /* The two shapes of teardown. The 64 that connected took a reference and
     * dropped it in the callback, so their count is back to 1 and one free
     * ends them. The rejected one still carries its unmatched increment, so
     * this free stops one short and the block is never returned. */
    for (int i = 0; i < ATTEMPTS; i++) {
        if (clients[i]) {
            uvrpc_client_free(clients[i]);
        }
    }

    /* sameloop_disconnect() prunes the server table as each client goes, so
     * the next round starts from an empty one. */
    pump(loop, 20);
}

int main(void) {
    uv_loop_t loop;
    uv_timer_t watchdog;
    uvbus_loop_registry_t* registry;
    uvrpc_config_t* server_config;
    uvrpc_config_t* client_config;
    uvrpc_server_t* server;

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

    printf("filling the SAMELOOP table, then asking for one more\n");

    server_config = uvrpc_config_new();
    uvrpc_config_set_loop(server_config, &loop);
    uvrpc_config_set_loop_registry(server_config, registry);
    uvrpc_config_set_address(server_config, "sameloop://connect_refund");
    uvrpc_config_set_transport(server_config, UVBUS_TRANSPORT_SAMELOOP);

    server = uvrpc_server_create(server_config);
    uvrpc_config_free(server_config);
    check(server != NULL, "server_create returned NULL");
    if (!server) {
        return 1;
    }
    uvrpc_server_register(server, "ping", handler, NULL);
    check(uvrpc_server_start(server) == UVRPC_OK, "server_start failed");

    client_config = uvrpc_config_new();
    uvrpc_config_set_loop(client_config, &loop);
    uvrpc_config_set_loop_registry(client_config, registry);
    uvrpc_config_set_address(client_config, "sameloop://connect_refund");
    uvrpc_config_set_transport(client_config, UVBUS_TRANSPORT_SAMELOOP);

    run_round(&loop, client_config, "plain connect", 0);
    run_round(&loop, client_config, "with callback", 1);

    uvrpc_config_free(client_config);

    uvrpc_server_stop(server);
    uvrpc_server_free(server);
    uvbus_loop_registry_free(registry);

    uv_close((uv_handle_t*)&watchdog, NULL);

    pump(&loop, 50);
    while (uv_loop_alive(&loop)) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }

    check(uv_loop_close(&loop) == 0,
          "uv_loop_close did not return 0 -- a handle was never closed");

    if (failures) {
        printf("\nconnect_refund: %d failure(s)\n", failures);
        return 1;
    }
    printf("\nconnect_refund: OK\n");
    return 0;
}
