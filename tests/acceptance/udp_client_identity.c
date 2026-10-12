/*
 * A UDP server has to be able to tell one client from another.
 *
 * uvbus's UDP transport hands the RPC layer a client_ctx that is the address
 * it just read the datagram from, and the server keys its client table off
 * that pointer. But the pointer is not owned by anything: libuv builds the
 * peer address in a struct sockaddr_storage local to uv__udp_recvmsg()
 * (deps/libuv/src/unix/udp.c) and passes its address into the callback, so
 * the storage dies with that stack frame.
 *
 * The framework's own send path is self-consistent anyway, because a handler
 * that replies synchronously is still running inside that frame -- so the
 * bytes go out to the right place and nothing appears broken from outside.
 * What breaks is identity. Every datagram from every peer resolves to the
 * same stack slot, so every peer compares equal to the first one recorded,
 * and uvrpc_server_get_client_count() reports 1 no matter how many clients
 * are actually talking. The count is public API, and max_clients is enforced
 * against it.
 *
 * What this pins: two distinct UDP clients, both served, are counted as two.
 * It does not assert that a departing UDP client stops counting -- a
 * connectionless transport has no departure signal, and inventing one is a
 * design change rather than a repair.
 *
 * Run: ctest -R udp_client_identity
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <uv.h>

#include "uvrpc.h"
#include "../free_port.h"

#define WATCHDOG_MS 30000
#define PUMP_TURNS 400

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

static uvrpc_client_t* connect_client(uv_loop_t* loop,
                                      uvbus_loop_registry_t* registry,
                                      const char* address) {
    uvrpc_config_t* cfg = uvrpc_config_set_loop_registry(uvrpc_config_new(), registry);
    cfg = uvrpc_config_set_loop(cfg, loop);
    cfg = uvrpc_config_set_address(cfg, address);
    cfg = uvrpc_config_set_transport(cfg, UVBUS_TRANSPORT_UDP);

    uvrpc_client_t* client = uvrpc_client_create(cfg);
    uvrpc_config_free(cfg);
    if (!client) {
        return NULL;
    }
    if (uvrpc_client_connect(client) != UVRPC_OK) {
        uvrpc_client_free(client);
        return NULL;
    }
    /* The connect is asynchronous: turn the loop until it is up. */
    for (int i = 0; i < PUMP_TURNS; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }
    return client;
}

int main(void) {
    uv_loop_t loop;
    uv_timer_t watchdog;
    uvbus_loop_registry_t* registry;
    char address[64];
    uvrpc_config_t* cfg;
    uvrpc_server_t* server;
    uvrpc_client_t* first;
    uvrpc_client_t* second;

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

    printf("a UDP server counts its clients apart\n");

    snprintf(address, sizeof(address), "udp://127.0.0.1:%d", acceptance_free_port());

    cfg = uvrpc_config_set_loop_registry(uvrpc_config_new(), registry);
    cfg = uvrpc_config_set_loop(cfg, &loop);
    cfg = uvrpc_config_set_address(cfg, address);
    cfg = uvrpc_config_set_transport(cfg, UVBUS_TRANSPORT_UDP);

    server = uvrpc_server_create(cfg);
    uvrpc_config_free(cfg);
    check(server != NULL, "server created");
    if (!server) {
        return 1;
    }
    uvrpc_server_register(server, "ping", handler, NULL);
    check(uvrpc_server_start(server) == UVRPC_OK, "server started");

    /* One peer: counted as one. This half passes either way. */
    first = connect_client(&loop, registry, address);
    check(first != NULL, "first client connected");
    if (first) {
        replies = 0;
        last_error_code = -1;
        check(uvrpc_client_call(first, "ping", (const uint8_t*)"p", 1,
                                on_response, NULL) == UVRPC_OK,
              "the first call was accepted");
        for (int i = 0; i < PUMP_TURNS && replies == 0; i++) {
            uv_run(&loop, UV_RUN_NOWAIT);
        }
        check(replies > 0, "the first client was answered");
        check(last_error_code == UVRPC_OK, "the first call succeeded");
    }
    check(uvrpc_server_get_client_count(server) == 1,
          "one client is counted while one is talking");

    /* A second, distinct peer, with the first still connected. It has its own
     * socket and so its own source address -- if those two are told apart,
     * the count goes to two. */
    second = connect_client(&loop, registry, address);
    check(second != NULL, "second client connected");
    if (second) {
        replies = 0;
        last_error_code = -1;
        check(uvrpc_client_call(second, "ping", (const uint8_t*)"q", 1,
                                on_response, NULL) == UVRPC_OK,
              "the second call was accepted");
        for (int i = 0; i < PUMP_TURNS && replies == 0; i++) {
            uv_run(&loop, UV_RUN_NOWAIT);
        }
        check(replies > 0, "the second client was answered");
        check(last_error_code == UVRPC_OK, "the second call succeeded");
    }
    check(uvrpc_server_get_client_count(server) == 2,
          "a second peer is counted apart from the first");

    if (second) {
        uvrpc_client_free(second);
    }
    if (first) {
        uvrpc_client_free(first);
    }

    uvrpc_server_stop(server);
    uvrpc_server_free(server);
    for (int i = 0; i < PUMP_TURNS; i++) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }

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
        printf("\nudp_client_identity: %d failure(s)\n", failures);
        return 1;
    }
    printf("\nudp_client_identity: OK\n");
    return 0;
}
