/*
 * A UDP server's client table has to forget peers that went quiet.
 *
 * uvbus hands the RPC layer a stable address from client_addrs[] as the
 * client_ctx. Without expiry, a long-running server fills 1024 slots with
 * NAT addresses that stopped talking long ago; new peers get no slot, and
 * uvrpc_server_get_client_count() stays pinned at the historical high-water
 * mark so max_clients rejects everyone after the first N clients.
 *
 * Run: ctest -R udp_peer_expiry
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <uv.h>

#include "uvrpc.h"
#include "../free_port.h"

#define PUMP_TURNS 400
#define IDLE_MS 100u
#define IDLE_WAIT_MS 350u
#define CALL_TIMEOUT_MS 2000u

static int failures;
static int replies;
static int last_error_code;

static void check(int ok, const char* what) {
    if (!ok) {
        printf("      FAIL: %s\n", what);
        failures++;
    }
}

static void stop_loop_cb(uv_timer_t* timer) {
    uv_stop(uv_handle_get_loop((uv_handle_t*)timer));
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
    for (int i = 0; i < PUMP_TURNS; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }
    return client;
}

static int call_once(uvrpc_client_t* client, uv_loop_t* loop, uv_timer_t* timer) {
    replies = 0;
    last_error_code = -1;
    if (uvrpc_client_call(client, "ping", (const uint8_t*)"p", 1,
                          on_response, NULL) != UVRPC_OK) {
        return 0;
    }
    uv_timer_start(timer, stop_loop_cb, CALL_TIMEOUT_MS, 0);
    while (replies == 0) {
        uv_run(loop, UV_RUN_ONCE);
    }
    uv_timer_stop(timer);
    return replies > 0 && last_error_code == UVRPC_OK;
}

static void wait_ms(uv_loop_t* loop, uv_timer_t* timer, uint64_t ms) {
    uv_timer_start(timer, stop_loop_cb, ms, 0);
    uv_run(loop, UV_RUN_DEFAULT);
    uv_timer_stop(timer);
}

int main(void) {
    uv_loop_t loop;
    uv_timer_t timer;
    uvbus_loop_registry_t* registry;
    char address[64];
    char idle_value[16];
    uvrpc_config_t* cfg;
    uvrpc_server_t* server;
    uvrpc_client_t* first;
    uvrpc_client_t* second;

    setvbuf(stdout, NULL, _IONBF, 0);

    if (uv_loop_init(&loop) != 0) {
        printf("FAIL: uv_loop_init\n");
        return 1;
    }
    uv_timer_init(&loop, &timer);

    snprintf(idle_value, sizeof(idle_value), "%u", IDLE_MS);
    setenv("UVRPC_UDP_PEER_IDLE_MS", idle_value, 1);

    registry = uvbus_loop_registry_new();
    check(registry != NULL, "loop registry created");
    if (!registry) {
        return 1;
    }

    printf("an idle UDP peer gives its client_ctx slot back\n");

    snprintf(address, sizeof(address), "udp://127.0.0.1:%d", acceptance_free_port());

    cfg = uvrpc_config_set_loop_registry(uvrpc_config_new(), registry);
    cfg = uvrpc_config_set_loop(cfg, &loop);
    cfg = uvrpc_config_set_address(cfg, address);
    cfg = uvrpc_config_set_transport(cfg, UVBUS_TRANSPORT_UDP);
    cfg = uvrpc_config_set_max_clients(cfg, 1);

    server = uvrpc_server_create(cfg);
    uvrpc_config_free(cfg);
    check(server != NULL, "server created");
    if (!server) {
        return 1;
    }
    uvrpc_server_register(server, "ping", handler, NULL);
    check(uvrpc_server_start(server) == UVRPC_OK, "server started");

    first = connect_client(&loop, registry, address);
    check(first != NULL, "first client connected");
    check(first && call_once(first, &loop, &timer), "first client was answered");
    check(uvrpc_server_get_client_count(server) == 1,
          "the talking client is counted");

    /* The first peer stops sending. The transport must report its expiry
     * through conn_close_cb so the RPC layer drops the same client_ctx. */
    wait_ms(&loop, &timer, IDLE_WAIT_MS);
    check(uvrpc_server_get_client_count(server) == 0,
          "the idle client is forgotten");

    /* With max_clients = 1 this call is rejected unless the expired identity
     * was really removed and its slot reused for the new peer. */
    second = connect_client(&loop, registry, address);
    check(second != NULL, "second client connected");
    check(second && call_once(second, &loop, &timer), "second client was answered");
    check(uvrpc_server_get_client_count(server) == 1,
          "the new client owns the reused slot");

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
    uv_close((uv_handle_t*)&timer, NULL);
    for (int i = 0; i < 200 && uv_loop_alive(&loop); i++) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }
    if (uv_loop_close(&loop) != 0) {
        printf("FAIL: uv_loop_close did not return 0 -- a handle was never closed\n");
        failures++;
    }

    unsetenv("UVRPC_UDP_PEER_IDLE_MS");

    if (failures) {
        printf("\nudp_peer_expiry: %d failure(s)\n", failures);
        return 1;
    }
    printf("\nudp_peer_expiry: OK\n");
    return 0;
}
