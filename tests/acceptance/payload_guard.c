/*
 * The payload a caller hands to uvbus_send(), uvbus_send_to() and
 * uvbus_broadcast() has to be checked before anything reads out of it.
 *
 * Those three entry points pass their arguments straight down to the
 * transport's slot. The target is a per-transport concept -- a client handle
 * on TCP/IPC/INPROC/SAMELOOP, an address on UDP -- so each transport checks
 * that itself. The payload is not: it is bytes and a length, the same on
 * every transport. And three of the five transports built a frame straight
 * out of it, copying `size` bytes with no peer connected to stop them early:
 * IPC, INPROC and SAMELOOP already rejected an empty payload, TCP and UDP
 * read from an address that does not exist.
 *
 * Guarded once at the entry points instead of five times per slot, so a
 * transport added later inherits it rather than having to remember it. The
 * per-transport payload checks that already existed are kept: a vtable slot
 * that can be reached without the bus layer should not depend on the caller
 * having been polite.
 *
 * Run: ctest -R payload_guard
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <uv.h>

#include "uvbus.h"
#include "uvbus_config.h"
#include "../free_port.h"

#define WATCHDOG_MS 30000
#define PUMP_TURNS 400

static int failures;

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

/* Bring a server bus up with one client connected, then broadcast with a
 * payload that is not there.
 *
 * The client matters. Every transport returns early when it has no peers, so
 * with an empty table TCP and UDP never reach the code that builds a frame --
 * they answer UVBUS_OK for a payload they did not look at, which is a
 * contract violation but not yet a read from nowhere. With a peer waiting,
 * both go on to copy `size` bytes out of the payload, and that is the part
 * that must not happen.
 *
 * A transport that answers UVBUS_ERROR_INVALID_PARAM has checked. */
static void run_case(uv_loop_t* loop,
                     uvbus_loop_registry_t* registry,
                     uvbus_transport_type_t type,
                     const char* label,
                     const char* address) {
    uvbus_config_t* cfg;
    uvbus_t* server;
    uvbus_t* client;

    printf("  %-8s ", label);

    cfg = uvbus_config_new();
    uvbus_config_set_loop(cfg, loop);
    uvbus_config_set_loop_registry(cfg, registry);
    uvbus_config_set_transport(cfg, type);
    uvbus_config_set_address(cfg, address);

    server = uvbus_server_new(cfg);
    uvbus_config_free(cfg);
    check(server != NULL, "server bus created");
    if (!server) {
        printf("\n");
        return;
    }

    check(uvbus_listen(server) == UVBUS_OK, "server is listening");

    cfg = uvbus_config_new();
    uvbus_config_set_loop(cfg, loop);
    uvbus_config_set_loop_registry(cfg, registry);
    uvbus_config_set_transport(cfg, type);
    uvbus_config_set_address(cfg, address);

    client = uvbus_client_new(cfg);
    uvbus_config_free(cfg);
    check(client != NULL, "client bus created");
    if (client) {
        check(uvbus_connect(client) == UVBUS_OK, "client connected");
    }

    for (int i = 0; i < PUMP_TURNS; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }

    check(uvbus_broadcast(server, NULL, 4) == UVBUS_ERROR_INVALID_PARAM,
          "broadcast refuses a payload that is not there");
    check(uvbus_send(server, NULL, 4) == UVBUS_ERROR_INVALID_PARAM,
          "send refuses a payload that is not there");
    check(uvbus_send_to(server, NULL, 4, NULL) == UVBUS_ERROR_INVALID_PARAM,
          "send_to refuses a payload that is not there");

    /* The client direction has to hold too: uvbus_send() from a client is
     * what the RPC layer actually uses, and it is a different code path in
     * every transport. */
    if (client) {
        check(uvbus_send(client, NULL, 4) == UVBUS_ERROR_INVALID_PARAM,
              "a client refuses a payload that is not there");
    }

    if (client) {
        uvbus_free(client);
    }
    uvbus_free(server);
    for (int i = 0; i < PUMP_TURNS; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }

    printf("refuses an unreadable payload\n");
}

int main(void) {
    uv_loop_t loop;
    uv_timer_t watchdog;
    uvbus_loop_registry_t* registry;
    char tcp_address[64];
    char udp_address[64];
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

    printf("every send entry point refuses a payload it cannot read\n");

    snprintf(tcp_address, sizeof(tcp_address), "tcp://127.0.0.1:%d",
             acceptance_free_port());
    snprintf(udp_address, sizeof(udp_address), "udp://127.0.0.1:%d",
             acceptance_free_port());
    snprintf(ipc_address, sizeof(ipc_address), "ipc:///tmp/uvrpc_bcast_%d.sock",
             (int)getpid());
    snprintf(inproc_address, sizeof(inproc_address), "inproc://uvrpc_bcast_%d",
             (int)getpid());
    snprintf(sameloop_address, sizeof(sameloop_address), "sameloop://uvrpc_bcast_%d",
             (int)getpid());

    /* A socket file left by an earlier run would fail the listen for reasons
     * that have nothing to do with what this checks. */
    unlink(ipc_address + 6);

    run_case(&loop, registry, UVBUS_TRANSPORT_TCP, "TCP", tcp_address);
    run_case(&loop, registry, UVBUS_TRANSPORT_IPC, "IPC", ipc_address);
    run_case(&loop, registry, UVBUS_TRANSPORT_UDP, "UDP", udp_address);
    run_case(&loop, registry, UVBUS_TRANSPORT_INPROC, "INPROC", inproc_address);
    run_case(&loop, registry, UVBUS_TRANSPORT_SAMELOOP, "SAMELOOP", sameloop_address);

    uvbus_loop_registry_free(registry);
    uv_close((uv_handle_t*)&watchdog, NULL);

    for (int i = 0; i < 200 && uv_loop_alive(&loop); i++) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }

    if (uv_loop_close(&loop) != 0) {
        printf("FAIL: uv_loop_close did not return 0 -- a handle was never closed\n");
        failures++;
    }

    if (failures) {
        printf("\npayload_guard: %d failure(s)\n", failures);
        return 1;
    }
    printf("\npayload_guard: OK\n");
    return 0;
}
