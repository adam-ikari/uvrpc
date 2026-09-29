/**
 * UVRPC Performance Benchmark — clean, correct loop usage.
 *
 * Measures request/response round-trip latency and throughput for each
 * transport. Uses UV_RUN_NOWAIT polling (a live connection would make
 * UV_RUN_DEFAULT block forever), so the same pattern works for all transports.
 *
 * Usage: ./perf_benchmark [requests] [transport]
 *   transport: tcp | ipc | inproc | sameloop  (default: inproc)
 *   requests:  default 100000
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <uv.h>
#include "../include/uvrpc.h"

#define DEFAULT_PORT 16555
#define DEFAULT_HOST "127.0.0.1"

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* Per-benchmark state shared with the response callback. Single-threaded, so
 * no atomic/volatile needed (the callback runs inside uv_run on the same
 * thread that reads `received`). */
typedef struct {
    uint64_t received;
    uint64_t target;
} bench_state_t;

/* Server handler: echo back the params verbatim. */
static void server_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    uvrpc_request_send_response(req, UVRPC_OK, req->params, req->params_size);
    uvrpc_request_free(req);
}

/* Client response callback. */
static void on_response(uvrpc_response_t* resp, void* ctx) {
    bench_state_t* st = (bench_state_t*)ctx;
    st->received++;
    uvrpc_response_free(resp);
}

#define WARMUP_REQUESTS 1000

/* Pump the loop until `st->received >= st->target`. Returns 0 on success,
 * -1 when the overall deadline passes, -2 when no response arrived for
 * stall_limit_ms (a wedged transport, as opposed to a merely slow host).
 * UV_RUN_ONCE blocks until an event is ready instead of busy-polling. */
static int pump_until(uv_loop_t* loop, bench_state_t* st, uint64_t deadline_ms,
                      uint64_t stall_limit_ms) {
    uint64_t last = st->received;
    uint64_t progressed_at = now_ms();
    while (st->received < st->target) {
        uint64_t now = now_ms();
        if (now > deadline_ms) return -1;
        if (st->received != last) {
            last = st->received;
            progressed_at = now;
        } else if (now - progressed_at > stall_limit_ms) {
            return -2;
        }
        uv_run(loop, UV_RUN_ONCE);
    }
    return 0;
}

/* Budget override for CI on hosts that are unusually slow; ms, 0 = derive. */
static uint64_t env_ms(const char* name) {
    const char* v = getenv(name);
    if (!v) return 0;
    unsigned long long n = strtoull(v, NULL, 10);
    return (uint64_t)n;
}

int main(int argc, char** argv) {
    /* Debug logging (UVRPC_DEBUG/UVBUS_DEBUG) writes to stderr on every
     * send/recv and dwarfs the RPC cost, corrupting timing. The library must
     * be built with -DUVRPC_DEBUG_LOGGING=OFF for these numbers to be valid. */
#if defined(UVRPC_DEBUG) || defined(UVBUS_DEBUG)
    fprintf(stderr, "WARNING: library built with debug logging ON — timings are "
                    "NOT valid. Rebuild with -DUVRPC_DEBUG_LOGGING=OFF.\n");
#endif

    uint64_t num_requests = 100000;
    const char* transport = "inproc";
    if (argc >= 2) num_requests = strtoull(argv[1], NULL, 10);
    if (argc >= 3) transport = argv[2];

    /* UDP is connectionless and nothing here retransmits. A dropped datagram
     * does not lower the measured throughput -- the response never arrives,
     * the stall detector fires, and the run aborts. Say so up front, because
     * "the run failed" and "the network was slow" are different conclusions
     * and the numbers alone do not distinguish them. */
    if (strcmp(transport, "udp") == 0) {
        fprintf(stderr, "note: udp has no retransmission here; a dropped "
                        "datagram aborts the run with 'measure stalled' rather "
                        "than showing up as a lower number. Treat the result as "
                        "loopback-only.\n");
    }


    uvbus_transport_type_t ttype;
    char address[128];
    if (strcmp(transport, "tcp") == 0) {
        ttype = UVBUS_TRANSPORT_TCP;
        snprintf(address, sizeof(address), "tcp://%s:%d", DEFAULT_HOST, DEFAULT_PORT);
    } else if (strcmp(transport, "udp") == 0) {
        ttype = UVBUS_TRANSPORT_UDP;
        snprintf(address, sizeof(address), "udp://%s:%d", DEFAULT_HOST, DEFAULT_PORT + 1);
    } else if (strcmp(transport, "ipc") == 0) {
        ttype = UVBUS_TRANSPORT_IPC;
        snprintf(address, sizeof(address), "ipc:///tmp/uvrpc_perf.sock");
        unlink(address + 6);
    } else if (strcmp(transport, "inproc") == 0) {
        ttype = UVBUS_TRANSPORT_INPROC;
        snprintf(address, sizeof(address), "inproc://uvrpc_perf");
    } else if (strcmp(transport, "sameloop") == 0) {
        ttype = UVBUS_TRANSPORT_SAMELOOP;
        snprintf(address, sizeof(address), "sameloop://uvrpc_perf");
    } else {
        fprintf(stderr, "unknown transport: %s\n", transport);
        return 2;
    }

    /* For TCP/IPC the server runs in a thread with its own loop; INPROC/SAMELOOP
     * share the client's loop. */
    uv_loop_t loop = {0};
    uv_loop_init(&loop);

    uvrpc_server_t* server = NULL;
    uvrpc_config_t* sconfig = uvrpc_config_new();
    sconfig = uvrpc_config_set_loop(sconfig, &loop);
    sconfig = uvrpc_config_set_address(sconfig, address);
    sconfig = uvrpc_config_set_transport(sconfig, ttype);
    server = uvrpc_server_create(sconfig);
    if (!server) { fprintf(stderr, "server_create failed\n"); return 1; }
    uvrpc_server_register(server, "echo", server_handler, NULL);
    if (uvrpc_server_start(server) != 0) { fprintf(stderr, "server_start failed\n"); return 1; }

    /* For TCP/IPC, the server needs its loop pumped concurrently. We use a
     * single shared loop for all transports here (INPROC/SAMELOOP require it;
     * TCP/IPC also work on a shared single-threaded loop since the server's
     * recv_cb runs in the same loop). */
    for (int i = 0; i < 10; i++) uv_run(&loop, UV_RUN_NOWAIT);

    uvrpc_config_t* cconfig = uvrpc_config_new();
    cconfig = uvrpc_config_set_loop(cconfig, &loop);
    cconfig = uvrpc_config_set_address(cconfig, address);
    cconfig = uvrpc_config_set_transport(cconfig, ttype);
    uvrpc_client_t* client = uvrpc_client_create(cconfig);
    if (!client) { fprintf(stderr, "client_create failed\n"); return 1; }
    if (uvrpc_client_connect(client) != 0) { fprintf(stderr, "connect failed\n"); return 1; }
    for (int i = 0; i < 50; i++) uv_run(&loop, UV_RUN_NOWAIT);

    bench_state_t st = {0, 0};
    uint8_t payload[8] = {0,0,0,0,0,0,0,0};

    /* Deadlines are derived from what this host actually delivers, not from an
     * assumed per-request cost: the same code runs 5-10x slower on virtualised
     * or noisy machines, and a budget that is merely tight aborts a valid run
     * with "measure timeout". Warmup measures the rate; the measured run gets
     * 4x that budget plus slack. */
    uint64_t stall_limit_ms = env_ms("UVRPC_BENCH_STALL_MS");
    if (stall_limit_ms == 0) stall_limit_ms = 5000;
    uint64_t warmup_deadline = now_ms() + 30000;

    /* Warmup: WARMUP_REQUESTS requests (excluded from timing). */
    st.target = WARMUP_REQUESTS;
    uint64_t warmup_start = now_ms();
    for (uint64_t i = 0; i < st.target; i++) {
        if (uvrpc_client_call(client, "echo", payload, sizeof(payload), on_response, &st) != 0) {
            fprintf(stderr, "warmup call failed at %llu\n", (unsigned long long)i);
            return 1;
        }
        uv_run(&loop, UV_RUN_NOWAIT);
    }
    int wr = pump_until(&loop, &st, warmup_deadline, stall_limit_ms);
    if (wr != 0) {
        fprintf(stderr, "warmup %s (received %llu/%llu)\n",
                wr == -2 ? "stalled" : "timeout",
                (unsigned long long)st.received, (unsigned long long)st.target);
        return 1;
    }

    uint64_t warmup_ms = now_ms() - warmup_start;
    if (warmup_ms == 0) warmup_ms = 1;
    uint64_t warmup_us_per_req = warmup_ms * 1000 / WARMUP_REQUESTS + 1;
    uint64_t measure_budget_ms = env_ms("UVRPC_BENCH_BUDGET_MS");
    if (measure_budget_ms == 0) {
        measure_budget_ms = num_requests * warmup_us_per_req * 4 / 1000 + 10000;
    }

    /* Measured run: strict sequential ping-pong — wait for each response
     * before sending the next (exactly one request in flight at a time). The
     * reported throughput is therefore the reciprocal of round-trip latency,
     * NOT pipelined/async throughput. */
    st.received = 0;
    st.target = num_requests;
    uint64_t start = now_ms();
    uint64_t per_call_deadline = start + measure_budget_ms;
    for (uint64_t i = 0; i < num_requests; i++) {
        st.target = i + 1;  /* wait for this specific response */
        if (uvrpc_client_call(client, "echo", payload, sizeof(payload), on_response, &st) != 0) {
            fprintf(stderr, "call failed at %llu\n", (unsigned long long)i);
            return 1;
        }
        int mr = pump_until(&loop, &st, per_call_deadline, stall_limit_ms);
        if (mr != 0) {
            fprintf(stderr, "measure %s (received %llu/%llu, budget %llu ms)\n",
                    mr == -2 ? "stalled" : "timeout",
                    (unsigned long long)st.received, (unsigned long long)num_requests,
                    (unsigned long long)measure_budget_ms);
            return 1;
        }
    }
    st.target = num_requests;
    uint64_t elapsed_ms = now_ms() - start;
    if (elapsed_ms == 0) elapsed_ms = 1;

    double rps = (double)st.received / ((double)elapsed_ms / 1000.0);
    double us_per_req = (double)elapsed_ms * 1000.0 / (double)st.received;

    printf("transport=%-9s requests=%llu  received=%llu  elapsed=%llu ms  (sequential ping-pong)\n",
           transport, (unsigned long long)num_requests,
           (unsigned long long)st.received, (unsigned long long)elapsed_ms);
    printf("  round-trip latency: %.2f us/req\n", us_per_req);
    printf("  throughput (= 1/latency, 1 in-flight): %.0f req/s\n", rps);

    uvrpc_client_disconnect(client);
    uvrpc_client_free(client);
    uvrpc_server_stop(server);
    uvrpc_server_free(server);
    uvrpc_config_free(sconfig);
    uvrpc_config_free(cconfig);
    /* Drain async close callbacks (transports schedule uv_close on listen /
     * client handles). UV_RUN_DEFAULT blocks until all handles close, then
     * returns — safe here because no references remain. */
    uv_run(&loop, UV_RUN_DEFAULT);
    int lc = uv_loop_close(&loop);
    if (lc != 0) {
        fprintf(stderr, "warning: uv_loop_close returned %d (unclosed handles)\n", lc);
    }
    if (ttype == UVBUS_TRANSPORT_IPC) unlink(address + 6);
    return 0;
}
