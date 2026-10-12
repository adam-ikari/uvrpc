/*
 * uvbus_send_to() has to refuse a target it cannot use, on every transport.
 *
 * The bus layer passes its client argument straight down to the transport's
 * send_to slot without looking at it, so each transport owns the check. Four
 * of the five made it: TCP, IPC and UDP reject a NULL client, and INPROC
 * additionally rejects a client that is no longer active or has no recv
 * callback. SAMELOOP checked nothing -- it ignored impl_ptr entirely and
 * dereferenced the target to prefetch its callback fields before calling
 * through them, so a NULL target was a NULL dereference.
 *
 * That is a vtable slot whose contract four implementations honour and one
 * does not, with nothing at the type level to keep them aligned. This pins
 * the contract on all five at once, so a transport added later cannot quietly
 * drop it either.
 *
 * Reachable in practice: uvbus_send_to() does not filter the target, so
 * anything that reaches it with an empty handle -- an application calling the
 * bus API directly, or a layer that has lost track of which connection it was
 * answering -- lands in the transport's hands.
 *
 * Run: ctest -R send_to_target_guard
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

/* One transport: bring a server bus up, then send to a target nobody owns.
 * A transport that answers with UVBUS_ERROR_INVALID_PARAM has checked; one
 * that crashes, or reports anything else, has not. */
static void run_case(uv_loop_t* loop,
                     uvbus_loop_registry_t* registry,
                     uvbus_transport_type_t type,
                     const char* label,
                     const char* address) {
    static const uint8_t msg[] = {'n', 'o', 'n', 'e'};
    uvbus_config_t* cfg;
    uvbus_t* server;

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
    for (int i = 0; i < PUMP_TURNS; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }

    /* The whole point. A NULL target has to be refused, not dereferenced. */
    check(uvbus_send_to(server, msg, sizeof(msg), NULL) == UVBUS_ERROR_INVALID_PARAM,
          "a target nobody owns is refused");

    uvbus_free(server);
    for (int i = 0; i < PUMP_TURNS; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }

    printf("refuses an unusable target\n");
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

    /* INPROC and SAMELOOP find each other through a registry the caller
     * creates, so give the test one. */
    registry = uvbus_loop_registry_new();
    check(registry != NULL, "loop registry created");
    if (!registry) {
        return 1;
    }

    printf("every transport refuses a target it cannot use\n");

    snprintf(tcp_address, sizeof(tcp_address), "tcp://127.0.0.1:%d",
             acceptance_free_port());
    snprintf(udp_address, sizeof(udp_address), "udp://127.0.0.1:%d",
             acceptance_free_port());
    snprintf(ipc_address, sizeof(ipc_address), "ipc:///tmp/uvrpc_sendto_%d.sock",
             (int)getpid());
    snprintf(inproc_address, sizeof(inproc_address), "inproc://uvrpc_sendto_%d",
             (int)getpid());
    snprintf(sameloop_address, sizeof(sameloop_address), "sameloop://uvrpc_sendto_%d",
             (int)getpid());

    /* A socket file left by an earlier run makes the listen fail, which would
     * fail this test for a reason that has nothing to do with what it checks. */
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
        printf("\nsend_to_target_guard: %d failure(s)\n", failures);
        return 1;
    }
    printf("\nsend_to_target_guard: OK\n");
    return 0;
}
