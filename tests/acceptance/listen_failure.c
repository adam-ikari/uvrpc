/*
 * A server that never managed to listen still has to be freeable.
 *
 * tcp_listen() and ipc_listen() both build their server struct, put a handle
 * inside it, discover that bind() or listen() cannot work, and return an
 * error. What they did at that point was queue the close and free the struct
 * on the spot -- and uv_close() only queues. libuv keeps walking a handle it
 * has been pointed at until a later loop turn has run the close, so the block
 * was handed back while it was still on the list.
 *
 * These are the paths nobody reaches by accident: bind fails because the port
 * is already taken or the directory is missing, listen fails because of a
 * backlog the kernel refused, the realloc of the client array fails, uv_accept
 * fails, uv_read_start fails. Every ordinary test starts a server that starts
 * fine, so all five sites in uvbus_transport_tcp.c and both in
 * uvbus_transport_ipc.c stayed put -- the normal teardown already passed a
 * close callback, and only the error paths passed NULL.
 *
 * Two of them can be provoked without fault injection, and this exercises
 * both:
 *
 *   TCP  -- bind a port that is already held by a listening socket, which
 *          fails with EADDRINUSE before anything is set up;
 *   IPC  -- bind a socket path whose parent directory does not exist, which
 *          fails with ENOENT.
 *
 * In each case the server struct is freed and only then is the loop turned
 * over, which is when uv__finish_close() reaches the handle inside it. Under
 * valgrind that is an invalid read; without it, nothing happens at all -- the
 * block is still mapped and unclaimed -- which is precisely why this survived
 * every run of every test. The memcheck entry is the gate.
 *
 * The test also pins the contract that matters for callers: a start that fails
 * returns an error and leaves an object that uvrpc_server_free() can take, with
 * no leak of its own once the closes have run.
 *
 * Run: ctest -R listen_failure_memcheck
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <uv.h>

#include "uvrpc.h"
#include "../free_port.h"

#define WATCHDOG_MS 30000
/* Generous on purpose: the point is that several turns happen, not how few. */
#define TURNS 50

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

static void handler(uvrpc_request_t* req, void* ctx) {
    static const uint8_t pong[] = {'p', 'o', 'n', 'g'};
    (void)ctx;
    uvrpc_request_send_response(req, UVRPC_OK, pong, sizeof(pong));
    uvrpc_request_free(req);
}

static void turn(uv_loop_t* loop, int turns) {
    for (int i = 0; i < turns; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }
}

/*
 * Start a server that is expected to fail, then free it and let the loop run
 * the close it queued. `label` names the scenario in the output.
 */
static void expect_bind_failure(uv_loop_t* loop,
                                uvbus_loop_registry_t* registry,
                                const char* label,
                                uvbus_transport_type_t transport,
                                const char* address) {
    printf("  %-8s ", label);

    uvrpc_config_t* cfg = uvrpc_config_set_loop_registry(uvrpc_config_new(), registry);
    cfg = uvrpc_config_set_loop(cfg, loop);
    cfg = uvrpc_config_set_address(cfg, address);
    cfg = uvrpc_config_set_transport(cfg, transport);

    uvrpc_server_t* server = uvrpc_server_create(cfg);
    uvrpc_config_free(cfg);
    if (!server) {
        check(0, "uvrpc_server_create returned NULL");
        return;
    }

    uvrpc_server_register(server, "ping", handler, NULL);

    int rc = uvrpc_server_start(server);
    check(rc != UVRPC_OK,
          "the start was accepted -- bind should have refused this address");
    if (rc == UVRPC_OK) {
        /* It worked after all; free it properly rather than leak the surprise. */
        uvrpc_server_stop(server);
    }

    /* Free, then turn the loop: this is the turn that walks the handle sitting
     * inside the block uvrpc_server_free() just released. */
    uvrpc_server_free(server);
    turn(loop, TURNS);

    printf("start rc=%d, freed, loop turned over %d times\n", rc, TURNS);
}

int main(void) {
    uv_loop_t loop;
    uv_timer_t watchdog;
    uvbus_loop_registry_t* registry;
    char port_addr[64];
    char ipc_addr[128];
    int port;

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

    printf("freeing a server that could not listen\n");

    /* --- TCP: two servers, one port. The first holds it, so the second's
     * uv_tcp_bind() fails with EADDRINUSE before anything is listening. */
    port = acceptance_free_port();
    snprintf(port_addr, sizeof(port_addr), "tcp://127.0.0.1:%d", port);

    {
        uvrpc_config_t* cfg = uvrpc_config_set_loop_registry(uvrpc_config_new(), registry);
        cfg = uvrpc_config_set_loop(cfg, &loop);
        cfg = uvrpc_config_set_address(cfg, port_addr);
        cfg = uvrpc_config_set_transport(cfg, UVBUS_TRANSPORT_TCP);

        uvrpc_server_t* holder = uvrpc_server_create(cfg);
        uvrpc_config_free(cfg);
        check(holder != NULL, "the port holder was created");
        if (holder) {
            uvrpc_server_register(holder, "ping", handler, NULL);
            check(uvrpc_server_start(holder) == UVRPC_OK,
                  "the first server bound its port");
            turn(&loop, 20);

            expect_bind_failure(&loop, registry, "TCP", UVBUS_TRANSPORT_TCP,
                                port_addr);

            uvrpc_server_stop(holder);
            uvrpc_server_free(holder);
            turn(&loop, TURNS);
        }
    }

    /* --- IPC: a socket path whose parent directory does not exist, so
     * uv_pipe_bind() fails. The log line says "permission denied" and that is
     * not a mistake to chase: libuv converts ENOENT to EACCES in uv_pipe_bind2()
     * for Windows compatibility, and the parent directory never exists, so this
     * failure is deterministic. */
    snprintf(ipc_addr, sizeof(ipc_addr), "ipc:///tmp/uvrpc_nodir_%d/u.sock",
             (int)getpid());
    expect_bind_failure(&loop, registry, "IPC", UVBUS_TRANSPORT_IPC, ipc_addr);

    uvbus_loop_registry_free(registry);
    uv_close((uv_handle_t*)&watchdog, NULL);

    for (int i = 0; i < 200 && uv_loop_alive(&loop); i++) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }

    check(uv_loop_close(&loop) == 0,
          "uv_loop_close did not return 0 -- a handle was never closed");

    if (failures) {
        printf("\nlisten_failure: %d failure(s)\n", failures);
        return 1;
    }
    printf("\nlisten_failure: OK\n");
    return 0;
}
