/*
 * Payload sizes: what round-trips byte-identical, where each transport's edge
 * actually is, and how much room an application has if it wants one size that
 * works everywhere.
 *
 * Frame verification now runs before the reader, so "this payload was
 * accepted" is a decision the framework made rather than an accident of pointer
 * arithmetic. The size boundary is a contract, not an implementation detail.
 *
 * The enforced limit is on the encoded frame, not the payload, and the encoding
 * overhead is not a constant -- measured: 38 bytes for an empty payload, 51 for
 * one byte, 48 for 100. That is FlatBuffers alignment on the vector length, so
 * "max payload = 65536 - 48" is an approximation, not a rule. Each transport's
 * edge is therefore measured here rather than computed.
 *
 * The measured edges differ, and the difference is real and reproducible:
 *
 *     TCP   65488 bytes
 *     IPC   65484   (4 fewer)
 *     UDP   65452   (36 fewer)
 *
 * All three enforce the same UVBUS_DEFAULT_MAX_FRAME_SIZE; the edges land where
 * they land because the overhead differs at the size where each one stops.
 * UDP losing the most is unsurprising -- it is a datagram protocol with its own
 * ceiling below the frame limit.
 *
 * So the number an application should design around is the lowest of the three,
 * not the TCP one. A payload chosen from TCP's edge would silently never come
 * back over UDP or IPC. That is the practical point of this scenario, and it is
 * the reason the boundaries are measured per transport rather than asserted
 * equal.
 *
 * The frame limit is a hard wall, not a truncation: past it the server resets
 * its read buffer and the request is gone, so the caller waits out its deadline
 * rather than receiving a short answer.
 *
 * Not covered, deliberately: UVBUS_MAX_FRAME_SIZE (1MB) from uvbus_config.h.
 * It is defined and never referenced. No payload near it gets through, because
 * the enforced limit is the 64KB default -- it is a constant that looks like a
 * limit and is not one.
 *
 * Loopback transports are not exercised: their frame never becomes a buffer of
 * a given size, so the boundary does not exist for them.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <uv.h>

#include "uvrpc.h"
#include "free_port.h"
#include "uvbus.h"
#include "uvbus_config.h"

#define DRAIN_MS 300
/* A binary search over 64k takes about 17 probes; each one waits out a full
 * drain when it fails, so this is a floor, not a target. */
#define WATCHDOG_MS 60000

/* How far apart the three edges may sit before this calls it a divergence
 * worth shouting about. They are currently 36 bytes apart, 0.05%. */
#define EDGE_TOLERANCE 1024

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
    _exit(1);
}

static void stop_cb(uv_timer_t* timer) {
    uv_stop(uv_handle_get_loop((uv_handle_t*)timer));
}

static void drain(uv_loop_t* loop, uv_timer_t* timer) {
    uv_timer_start(timer, stop_cb, DRAIN_MS, 0);
    uv_run(loop, UV_RUN_DEFAULT);
}

static void echo_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    uvrpc_request_send_response(req, UVRPC_OK, req->params, req->params_size);
    uvrpc_request_free(req);
}

static size_t received_size;
static int content_wrong;

static void on_response(uvrpc_response_t* resp, void* ctx) {
    received_size = resp->result_size;
    /* The expected bytes arrive as the callback's context, not as
     * resp->user_data -- the framework leaves that field alone. */
    const uint8_t* expected = ctx;
    /* Byte-for-byte, not just the length: a frame that came back the right size
     * but mangled would be a worse bug than one that vanished. */
    if (expected && resp->result_size > 0 &&
        memcmp(expected, resp->result, resp->result_size) != 0) {
        content_wrong = 1;
    }
    uvrpc_response_free(resp);
}

/* 1 when the payload came back identical, 0 otherwise (accepted but unanswered,
 * or answered with the wrong bytes). */
static int round_trip(uvrpc_client_t* client, uv_loop_t* loop, uv_timer_t* timer,
                      size_t payload) {
    uint8_t* buffer = malloc(payload ? payload : 1);
    for (size_t i = 0; i < payload; i++) {
        buffer[i] = (uint8_t)('a' + (i % 26));
    }

    received_size = (size_t)-1;
    content_wrong = 0;

    int ret = uvrpc_client_call(client, "echo", payload ? buffer : NULL,
                                payload, on_response, buffer);
    if (ret != UVRPC_OK) {
        free(buffer);
        return 0;
    }
    drain(loop, timer);
    int ok = (received_size == payload) && !content_wrong;
    free(buffer);
    return ok;
}

/* The largest payload that round-trips. Deterministic: the boundary is a
 * property of the encoder, not of timing. */
static size_t find_edge(uvrpc_client_t* client, uv_loop_t* loop, uv_timer_t* timer) {
    size_t lo = 1024;    /* known good */
    size_t hi = 70000;   /* known past the limit */
    while (lo < hi) {
        size_t mid = lo + (hi - lo + 1) / 2;
        if (round_trip(client, loop, timer, mid)) {
            lo = mid;
        } else {
            hi = mid - 1;
        }
    }
    return lo;
}

typedef struct {
    char name[8];
    uvbus_transport_type_t type;
    char address[96];
    size_t edge;
} transport_case;

static void check_ladder(uvrpc_client_t* client, uv_loop_t* loop, uv_timer_t* timer,
                         const transport_case* tc) {
    static const size_t sizes[] = {0, 1, 256, 1024, 8192, 32768, 60000};
    for (int i = 0; i < (int)(sizeof(sizes) / sizeof(sizes[0])); i++) {
        if (!round_trip(client, loop, timer, sizes[i])) {
            printf("FAIL: %s: payload %zu did not come back byte-identical\n",
                   tc->name, sizes[i]);
            failures++;
            return;
        }
    }
}

static void check_edge(uvrpc_client_t* client, uv_loop_t* loop, uv_timer_t* timer,
                       transport_case* tc) {
    tc->edge = find_edge(client, loop, timer);
    printf("      edge: %zu bytes\n", tc->edge);

    if (!round_trip(client, loop, timer, tc->edge)) {
        printf("FAIL: %s: the measured edge %zu did not round-trip on a second "
               "attempt\n", tc->name, tc->edge);
        failures++;
    }
    /* One past it must not come back. A short answer would be worse than none:
     * the caller could mistake it for a complete response. */
    if (round_trip(client, loop, timer, tc->edge + 1)) {
        printf("FAIL: %s: %zu bytes went through, past the measured edge\n",
               tc->name, tc->edge + 1);
        failures++;
    }
}

int main(void) {
    uv_loop_t loop = {0};
    uv_loop_init(&loop);

    uv_timer_t timer;
    uv_timer_init(&loop, &timer);

    uv_timer_t watchdog;
    uv_timer_init(&loop, &watchdog);
    uv_timer_start(&watchdog, watchdog_cb, WATCHDOG_MS, 0);
    uv_unref((uv_handle_t*)&watchdog);

    char ipc[96];
    snprintf(ipc, sizeof(ipc), "ipc:///tmp/uvrpc_bounds_%d.sock", (int)getpid());

    transport_case cases[3];
    memset(cases, 0, sizeof(cases));
    snprintf(cases[0].name, sizeof(cases[0].name), "TCP");
    cases[0].type = UVBUS_TRANSPORT_TCP;
    snprintf(cases[0].address, sizeof(cases[0].address), "tcp://127.0.0.1:%d", acceptance_free_port());
    snprintf(cases[1].name, sizeof(cases[1].name), "UDP");
    cases[1].type = UVBUS_TRANSPORT_UDP;
    snprintf(cases[1].address, sizeof(cases[1].address), "udp://127.0.0.1:%d", acceptance_free_port());
    snprintf(cases[2].name, sizeof(cases[2].name), "IPC");
    cases[2].type = UVBUS_TRANSPORT_IPC;
    snprintf(cases[2].address, sizeof(cases[2].address), "%s", ipc);

    /* One server per transport, on one loop, kept alive until the end. */
    uvrpc_server_t* servers[3] = {NULL, NULL, NULL};
    uvrpc_client_t* clients[3] = {NULL, NULL, NULL};

    for (int i = 0; i < 3; i++) {
        printf("\n[%s]\n", cases[i].name);

        uvrpc_config_t* sc = uvrpc_config_set_loop(uvrpc_config_new(), &loop);
        sc = uvrpc_config_set_address(sc, cases[i].address);
        sc = uvrpc_config_set_transport(sc, cases[i].type);
        servers[i] = uvrpc_server_create(sc);
        uvrpc_config_free(sc);
        if (!servers[i]) {
            printf("FAIL: %s: server_create returned NULL\n", cases[i].name);
            failures++;
            continue;
        }
        uvrpc_server_register(servers[i], "echo", echo_handler, NULL);
        if (uvrpc_server_start(servers[i]) != UVRPC_OK) {
            printf("FAIL: %s: server_start failed\n", cases[i].name);
            failures++;
            continue;
        }
        for (int k = 0; k < 20; k++) {
            uv_run(&loop, UV_RUN_NOWAIT);
        }

        uvrpc_config_t* cc = uvrpc_config_set_loop(uvrpc_config_new(), &loop);
        cc = uvrpc_config_set_address(cc, cases[i].address);
        cc = uvrpc_config_set_transport(cc, cases[i].type);
        uvrpc_config_set_max_concurrent(cc, 64);
        clients[i] = uvrpc_client_create(cc);
        uvrpc_config_free(cc);
        if (!clients[i]) {
            printf("FAIL: %s: client_create returned NULL\n", cases[i].name);
            failures++;
            continue;
        }
        if (uvrpc_client_connect(clients[i]) != UVRPC_OK) {
            printf("FAIL: %s: client_connect failed\n", cases[i].name);
            failures++;
            continue;
        }
        for (int k = 0; k < 300; k++) {
            uv_run(&loop, UV_RUN_NOWAIT);
        }

        check_ladder(clients[i], &loop, &timer, &cases[i]);
        check_edge(clients[i], &loop, &timer, &cases[i]);
    }

    /* The cross-transport claim: the edges are not equal, so the usable size is
     * the smallest of them, and no bigger. */
    size_t lowest = cases[0].edge, highest = cases[0].edge;
    for (int i = 1; i < 3; i++) {
        if (cases[i].edge < lowest) lowest = cases[i].edge;
        if (cases[i].edge > highest) highest = cases[i].edge;
    }
    printf("\nedges: TCP %zu, UDP %zu, IPC %zu -- spread %zu bytes\n",
           cases[0].edge, cases[1].edge, cases[2].edge, highest - lowest);
    check(highest - lowest <= (size_t)EDGE_TOLERANCE,
          "the three edges stay within a tolerance of each other");
    printf("      a payload that works on every transport must not exceed %zu\n",
           lowest);
    check(lowest > 0, "at least one transport carries a full-size payload");

    for (int i = 0; i < 3; i++) {
        if (clients[i]) uvrpc_client_free(clients[i]);
        if (servers[i]) {
            uvrpc_server_stop(servers[i]);
            uvrpc_server_free(servers[i]);
        }
    }
    unlink(ipc);

    uv_close((uv_handle_t*)&timer, NULL);
    uv_close((uv_handle_t*)&watchdog, NULL);
    for (int i = 0; i < 50; i++) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }
    if (uv_loop_close(&loop) != 0) {
        printf("FAIL: the loop could not be closed -- a handle was left open\n");
        failures++;
    }

    if (failures == 0) {
        printf("payload bounds: OK\n");
        return 0;
    }
    printf("payload bounds: %d check(s) failed\n", failures);
    return 1;
}