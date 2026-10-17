/*
 * A TCP server that has run out of room has to tell the peer, not just stop
 * listening to it.
 *
 * The transport keeps its accepted connections in a fixed table of
 * UVBUS_MAX_CLIENTS entries. By the time its connection callback runs, the
 * handshake is already done: the kernel has answered the peer's SYN and the
 * peer is connected. Returning from that callback without uv_accept()ing leaves
 * the connection sitting in the accept queue -- nothing reads it, nothing
 * writes it, and nothing ever ends it. A peer that never writes (a health
 * probe, a pool member opening its socket early, an RPC client that has not
 * sent its first request) sees a healthy, silent connection and waits out its
 * own deadline for a server that will never answer.
 *
 * Accepting it only to close it hands the peer a FIN, which is the one thing a
 * refused connection can be told.
 *
 * The table is filled here with connections that speak no RPC at all: they
 * connect and stay quiet, so they are counted by the transport and by nothing
 * above it. That is the point -- the transport's ceiling is reached by
 * connections, while the RPC layer's count only moves when a client speaks, and
 * the two are not the same number.
 *
 * Two peers are then turned away and both have to learn of it: a plain TCP
 * connection, which can only be told by the socket ending, and a uvrpc client,
 * which additionally has to stop believing it is connected.
 *
 * Heavy by nature: UVBUS_MAX_CLIENTS accepted connections means ~256 MB of
 * server-side client blocks (the read buffer lives inside each one). It carries
 * a "heavy" label so a constrained run can drop it; it does run under the
 * sanitizer rounds, which take it in their stride.
 *
 * Run: ctest -R tcp_capacity
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <uv.h>

#include "uvrpc.h"
#include "uvbus_config.h"   /* UVBUS_MAX_CLIENTS */
#include "../free_port.h"

#define WATCHDOG_MS 180000
#define FILLER_COUNT UVBUS_MAX_CLIENTS
/* Wall-clock seconds spent after the last filler connects, before the refused
 * peers try: enough for the server to have accept()ed all 1024 of them (a few
 * loop turns on loopback), so its table is genuinely full rather than nearly
 * so. */
#define SETTLE_S 0.5
/* How long a refused peer is given to be told. Far longer than the loopback
 * round trip it takes, and far shorter than the caller's deadline below, so
 * "the connection was ended" and "the caller waited out its deadline" cannot be
 * confused for one another. */
#define PEER_BUDGET_S 2.0
/* Far longer than any budget above. */
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

static double now_s(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

/* Turn the loop without blocking for a while. UV_RUN_NOWAIT returns whether or
 * not anything happened, so a fixed turn count can elapse before an event that
 * was always going to arrive; a wall-clock budget cannot. */
static void pump_for(uv_loop_t* loop, double seconds) {
    double end = now_s() + seconds;
    do {
        uv_run(loop, UV_RUN_NOWAIT);
    } while (now_s() < end);
}

/* The same, stopping early once `flag` is set. */
static void pump_until(uv_loop_t* loop, const int* flag, double seconds) {
    double end = now_s() + seconds;
    while (!*flag && now_s() < end) {
        uv_run(loop, UV_RUN_NOWAIT);
    }
}

/* ------------------------------------------------------------------ */
/* Fillers: connect, say nothing, hold the transport's table full.     */

typedef struct {
    uv_tcp_t handle;
    uv_connect_t req;
    int connected;
} filler_t;

static filler_t* fillers;
static int fillers_connected;

static void filler_connect_cb(uv_connect_t* req, int status) {
    filler_t* f = (filler_t*)req->data;
    if (status == 0) {
        f->connected = 1;
        fillers_connected++;
    }
}

/* ------------------------------------------------------------------ */
/* A plain TCP peer, which has only the socket to tell it anything.    */

#define PEER_SCRATCH 64
static char peer_scratch[PEER_SCRATCH];

typedef struct {
    uv_tcp_t handle;
    uv_connect_t req;
    int connected;
    int ended;      /* the connection was closed out from under us */
    ssize_t end_code;
} raw_peer_t;

static void peer_alloc_cb(uv_handle_t* handle, size_t suggested, uv_buf_t* buf) {
    (void)handle; (void)suggested;
    buf->base = peer_scratch;
    buf->len = PEER_SCRATCH;
}

static void peer_read_cb(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    (void)buf;
    raw_peer_t* p = (raw_peer_t*)stream->data;
    if (nread < 0) {
        p->ended = 1;
        p->end_code = nread;
    }
}

static void peer_connect_cb(uv_connect_t* req, int status) {
    raw_peer_t* p = (raw_peer_t*)req->data;
    if (status == 0) {
        p->connected = 1;
        uv_read_start((uv_stream_t*)&p->handle, peer_alloc_cb, peer_read_cb);
    }
}

/* ------------------------------------------------------------------ */
/* An RPC client, which additionally has to stop believing it is conn. */

static int rpc_responses;
static int rpc_last_status;

static void on_rpc_response(uvrpc_response_t* resp, void* ctx) {
    (void)ctx;
    rpc_responses++;
    rpc_last_status = resp->status;
    uvrpc_response_free(resp);
}

/* Connect a bare uv_tcp_t to "tcp://127.0.0.1:port". `data` is what the
 * callbacks will find on both the handle and the request. */
static int tcp_connect(uv_loop_t* loop, const char* address, uv_tcp_t* handle,
                       uv_connect_t* req, void* data,
                       void (*cb)(uv_connect_t*, int)) {
    struct sockaddr_in addr;

    const char* colon = strrchr(address, ':');
    if (!colon) return -1;
    int port = atoi(colon + 1);
    if (port <= 0) return -1;
    if (uv_ip4_addr("127.0.0.1", port, &addr) != 0) return -1;

    uv_tcp_init(loop, handle);
    handle->data = data;
    req->data = data;
    return uv_tcp_connect(req, handle, (const struct sockaddr*)&addr, cb);
}

int main(void) {
    uv_loop_t loop = {0};
    uv_timer_t watchdog;
    char addr[64];
    uvbus_loop_registry_t* registry = NULL;
    uvrpc_server_t* server = NULL;
    uvrpc_client_t* client = NULL;
    raw_peer_t peer;
    struct timespec t0, t1;
    int rc;

    setvbuf(stdout, NULL, _IONBF, 0);
    memset(&peer, 0, sizeof(peer));

    if (uv_loop_init(&loop) != 0) {
        printf("FAIL: uv_loop_init\n");
        return 1;
    }

    uv_timer_init(&loop, &watchdog);
    uv_timer_start(&watchdog, watchdog_cb, WATCHDOG_MS, 0);
    uv_unref((uv_handle_t*)&watchdog);

    clock_gettime(CLOCK_MONOTONIC, &t0);
    printf("a TCP server with no room left, and who is told\n");

    registry = uvbus_loop_registry_new();
    if (!registry) {
        printf("FAIL: registry\n");
        return 1;
    }

    snprintf(addr, sizeof(addr), "tcp://127.0.0.1:%d", acceptance_free_port());

    uvrpc_config_t* scfg = uvrpc_config_new();
    scfg = uvrpc_config_set_loop(scfg, &loop);
    scfg = uvrpc_config_set_loop_registry(scfg, registry);
    scfg = uvrpc_config_set_address(scfg, addr);
    scfg = uvrpc_config_set_transport(scfg, UVBUS_TRANSPORT_TCP);
    server = uvrpc_server_create(scfg);
    uvrpc_config_free(scfg);
    if (!server) {
        printf("FAIL: server_create\n");
        goto cleanup;
    }
    if (uvrpc_server_start(server) != UVRPC_OK) {
        printf("FAIL: server_start\n");
        goto cleanup;
    }

    /* Fill the transport's table with connections that never speak. */
    fillers = (filler_t*)calloc(FILLER_COUNT, sizeof(filler_t));
    if (!fillers) {
        printf("FAIL: filler allocation\n");
        goto cleanup;
    }

    for (int i = 0; i < FILLER_COUNT; i++) {
        if (tcp_connect(&loop, addr, &fillers[i].handle, &fillers[i].req,
                        &fillers[i], filler_connect_cb) != 0) {
            printf("FAIL: filler %d could not be started\n", i);
            failures++;
            goto cleanup;
        }
    }

    for (int i = 0; i < 20000 && fillers_connected < FILLER_COUNT; i++) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }
    check(fillers_connected == FILLER_COUNT,
          "not every filler connection completed the handshake");

    /* Let the server finish accepting all of them, so its table really is
     * full rather than a turn away from being full. */
    pump_for(&loop, SETTLE_S);
    printf("  %d connections held, table ceiling is %d\n",
           fillers_connected, FILLER_COUNT);

    /* Peer one: a plain TCP connection, which can only be told by the socket
     * ending. */
    if (tcp_connect(&loop, addr, &peer.handle, &peer.req, &peer,
                    peer_connect_cb) != 0) {
        printf("FAIL: refused peer could not be started\n");
        failures++;
        goto cleanup;
    }
    pump_until(&loop, &peer.ended, PEER_BUDGET_S);
    check(peer.connected,
          "the refused TCP peer never completed its handshake at all");
    check(peer.ended,
          "a TCP peer refused for want of room was left connected in silence");
    if (peer.ended) {
        printf("  refused raw peer: socket ended (%s)\n", uv_strerror(peer.end_code));
    }

    /* Peer two: a uvrpc client, which additionally has to stop believing it is
     * connected, or its call is accepted and never answered. */
    uvrpc_config_t* ccfg = uvrpc_config_new();
    ccfg = uvrpc_config_set_loop(ccfg, &loop);
    ccfg = uvrpc_config_set_loop_registry(ccfg, registry);
    ccfg = uvrpc_config_set_address(ccfg, addr);
    ccfg = uvrpc_config_set_transport(ccfg, UVBUS_TRANSPORT_TCP);
    ccfg = uvrpc_config_set_timeout(ccfg, CALLER_DEADLINE_MS);
    client = uvrpc_client_create(ccfg);
    uvrpc_config_free(ccfg);
    if (!client) {
        printf("FAIL: client_create\n");
        goto cleanup;
    }
    if (uvrpc_client_connect(client) != UVRPC_OK) {
        printf("FAIL: refused client could not be started\n");
        failures++;
        goto cleanup;
    }
    /* There is no public "is it still connected" to wait on, so this is a flat
     * budget: a full two seconds for the connection to be reported closed
     * before the call is made. */
    pump_for(&loop, PEER_BUDGET_S);

    rpc_responses = 0;
    rpc_last_status = 12345;
    rc = uvrpc_client_call(client, "ping", (const uint8_t*)"ping", 5,
                           on_rpc_response, NULL);
    if (rc == UVRPC_OK) {
        pump_for(&loop, SETTLE_S);
        printf("FAIL: a refused RPC client had its call accepted (%d answer(s))\n",
               rpc_responses);
        failures++;
    } else if (rc != UVRPC_ERROR_NOT_CONNECTED) {
        printf("FAIL: a refused RPC client's call failed with %d, not NOT_CONNECTED\n",
               rc);
        failures++;
    } else {
        printf("  refused RPC client: call returned %d (NOT_CONNECTED=%d)\n",
               rc, UVRPC_ERROR_NOT_CONNECTED);
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    printf("  %.2f s, no answer waited for\n",
           (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) / 1e9);

cleanup:
    if (client) uvrpc_client_free(client);

    /* Close everything that is still open. The handles live in `fillers`, so
     * that array has to outlive the closes -- libuv reads the handle while
     * finishing them -- and is released after the loop has drained below. */
    for (int i = 0; i < FILLER_COUNT; i++) {
        if (fillers && fillers[i].handle.data) {
            uv_close((uv_handle_t*)&fillers[i].handle, NULL);
            fillers[i].handle.data = NULL;
        }
    }
    if (peer.handle.data) {
        uv_close((uv_handle_t*)&peer.handle, NULL);
        peer.handle.data = NULL;
    }

    if (server) uvrpc_server_free(server);
    if (registry) uvbus_loop_registry_free(registry);

    uv_close((uv_handle_t*)&watchdog, NULL);
    for (int i = 0; i < 20000 && uv_loop_alive(&loop); i++) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }

    free(fillers);
    fillers = NULL;

    check(uv_loop_close(&loop) == 0,
          "uv_loop_close did not return 0 -- a handle was never closed");

    if (failures) {
        printf("\ntcp_capacity: %d failure(s)\n", failures);
        return 1;
    }
    printf("\ntcp_capacity: OK\n");
    return 0;
}
