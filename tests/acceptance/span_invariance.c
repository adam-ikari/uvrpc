/*
 * Span invariance: the same application logic must produce the same observable
 * result on every transport. Transport may change speed; it may not change the
 * answer.
 *
 * This is a stronger claim than "each transport works". A per-transport test
 * passes on all five paths even when two of them disagree with each other, and
 * disagreement between paths is exactly what breaks an application the moment
 * it moves from loopback to the network. So the assertion here is cross-
 * transport: the first transport run becomes the reference, and every other one
 * must match it exactly.
 *
 * The application logic is deliberately trivial -- echo the request back -- so
 * that any divergence points at the framework rather than at the scenario.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <uv.h>

#include "uvrpc.h"

#define REQ_COUNT 5
#define PAYLOAD_LEN 5
#define PUMP_MS 800
/* Upper bound on the whole run. A scenario that cannot hang must also not wait
 * forever to fail: without this the test is a way to burn CI minutes. */
#define WATCHDOG_MS 15000

static int failures;

static unsigned char seen[REQ_COUNT][PAYLOAD_LEN];
static int seen_count;
static int got_count;

static void check(int ok, const char* what) {
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

/* Server side. Records what actually arrived, then replies with the same bytes.
 * The reply goes out through the supported API -- uvrpc_request_send_response
 * -- never through req->client_ctx. That field is public and documented as
 * "Client context for sending response (from UVBus)", but its type is
 * transport-specific: a client handle on TCP/IPC/INPROC/SAMELOOP, a sockaddr*
 * on UDP. The framework's own path is self-consistent because each transport
 * hands back what its own send_to expects, but application code that reaches
 * for the field directly gets a differently-typed pointer per transport. This
 * scenario therefore also documents the contract it relies on. */
static void echo_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    if (seen_count < REQ_COUNT && req->params_size >= PAYLOAD_LEN) {
        memcpy(seen[seen_count], req->params, PAYLOAD_LEN);
        seen_count++;
    }
    uvrpc_request_send_response(req, UVRPC_OK, req->params, req->params_size);
    uvrpc_request_free(req);
}

static void on_response(uvrpc_response_t* resp, void* ctx) {
    (void)ctx;
    got_count++;
    uvrpc_response_free(resp);
}

static void watchdog_cb(uv_timer_t* timer) {
    uv_stop(uv_handle_get_loop((uv_handle_t*)timer));
    printf("FAIL: run exceeded %d ms -- a scenario that cannot hang must still\n"
           "      not be able to wait forever\n", WATCHDOG_MS);
    failures++;
    _exit(1);
}

static void stop_cb(uv_timer_t* timer) {
    uv_stop(uv_handle_get_loop((uv_handle_t*)timer));
}

typedef struct {
    const char* name;
    uvbus_transport_type_t transport;
    /* A pointer, not a char array: an array member initialised from a char*
     * variable (the IPC address is one) only takes the first element, which
     * silently truncates the pointer. */
    const char* address;
} transport_case;

/* Runs the identical application logic once and records what the server
 * observed. Returns 0 on success. */
static int run_case(uv_loop_t* loop, uvbus_loop_registry_t* registry,
                    uv_timer_t* timer, const transport_case* tc,
                    const char** failure_reason) {
    seen_count = 0;
    got_count = 0;

    uvrpc_config_t* scfg = uvrpc_config_set_loop_registry(
        uvrpc_config_new(), registry);
    scfg = uvrpc_config_set_loop(scfg, loop);
    scfg = uvrpc_config_set_address(scfg, tc->address);
    scfg = uvrpc_config_set_transport(scfg, tc->transport);
    uvrpc_server_t* server = uvrpc_server_create(scfg);
    uvrpc_config_free(scfg);
    if (!server) {
        *failure_reason = "uvrpc_server_create returned NULL";
        return 0;
    }

    uvrpc_server_register(server, "echo", echo_handler, NULL);
    if (uvrpc_server_start(server) != UVRPC_OK) {
        *failure_reason = "uvrpc_server_start did not return UVRPC_OK";
        uvrpc_server_free(server);
        return 0;
    }

    for (int i = 0; i < 10; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }

    uvrpc_config_t* ccfg = uvrpc_config_set_loop_registry(
        uvrpc_config_new(), registry);
    ccfg = uvrpc_config_set_loop(ccfg, loop);
    ccfg = uvrpc_config_set_address(ccfg, tc->address);
    ccfg = uvrpc_config_set_transport(ccfg, tc->transport);
    uvrpc_client_t* client = uvrpc_client_create(ccfg);
    uvrpc_config_free(ccfg);
    if (!client) {
        *failure_reason = "uvrpc_client_create returned NULL";
        uvrpc_server_stop(server);
        uvrpc_server_free(server);
        return 0;
    }

    if (uvrpc_client_connect(client) != UVRPC_OK) {
        *failure_reason = "uvrpc_client_connect did not return UVRPC_OK";
        uvrpc_client_free(client);
        uvrpc_server_stop(server);
        uvrpc_server_free(server);
        return 0;
    }

    /* connect is asynchronous on the socket transports, so a request issued
     * before it completes is dropped without a diagnostic. Pump until it is. */
    for (int i = 0; i < 100; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }

    for (int i = 0; i < REQ_COUNT; i++) {
        uint8_t payload[PAYLOAD_LEN];
        for (int j = 0; j < PAYLOAD_LEN; j++) {
            payload[j] = (uint8_t)(i * PAYLOAD_LEN + j);
        }
        if (uvrpc_client_call(client, "echo", payload, sizeof(payload),
                              on_response, NULL) != UVRPC_OK) {
            *failure_reason = "uvrpc_client_call was rejected";
            uvrpc_client_free(client);
            uvrpc_server_stop(server);
            uvrpc_server_free(server);
            return 0;
        }
    }

    uv_timer_start(timer, stop_cb, PUMP_MS, 0);
    uv_run(loop, UV_RUN_DEFAULT);

    int ok = (seen_count == REQ_COUNT && got_count == REQ_COUNT);
    if (!ok) {
        *failure_reason = "not every request completed";
    }

    uvrpc_client_free(client);
    uvrpc_server_stop(server);
    uvrpc_server_free(server);
    uv_run(loop, UV_RUN_NOWAIT);
    return ok;
}

int main(void) {
    uv_loop_t loop = {0};
    uv_loop_init(&loop);

    uv_timer_t pump_timer;
    uv_timer_init(&loop, &pump_timer);

    uv_timer_t watchdog;
    uv_timer_init(&loop, &watchdog);
    uv_timer_start(&watchdog, watchdog_cb, WATCHDOG_MS, 0);
    /* The watchdog must not itself hold the loop open. */
    uv_unref((uv_handle_t*)&watchdog);

    /* INPROC and SAMELOOP peers find each other through a registry the caller
     * owns; the socket transports ignore it. */
    uvbus_loop_registry_t* registry = uvbus_loop_registry_new();
    check(registry != NULL, "registry created");
    if (!registry) return 1;

    char ipc_addr[128];
    snprintf(ipc_addr, sizeof(ipc_addr), "ipc:///tmp/uvrpc_span_%d.sock",
             (int)getpid());

    transport_case cases[] = {
        {"INPROC",   UVBUS_TRANSPORT_INPROC,   "inproc://uvrpc_span"},
        {"SAMELOOP", UVBUS_TRANSPORT_SAMELOOP, "sameloop://uvrpc_span"},
        {"TCP",      UVBUS_TRANSPORT_TCP,      "tcp://127.0.0.1:45191"},
        {"UDP",      UVBUS_TRANSPORT_UDP,      "udp://127.0.0.1:45192"},
        {"IPC",      UVBUS_TRANSPORT_IPC,      ipc_addr},
    };
    const int case_count = (int)(sizeof(cases) / sizeof(cases[0]));

    unsigned char reference[REQ_COUNT][PAYLOAD_LEN];
    int have_reference = 0;

    for (int i = 0; i < case_count; i++) {
        const char* reason = "";
        if (!run_case(&loop, registry, &pump_timer, &cases[i], &reason)) {
            printf("FAIL: %s: %s\n", cases[i].name, reason);
            failures++;
            continue;
        }

        if (!have_reference) {
            memcpy(reference, seen, sizeof(reference));
            have_reference = 1;
            printf("  %-9s is the reference\n", cases[i].name);
            continue;
        }

        if (seen_count != REQ_COUNT) {
            printf("FAIL: %s: server observed %d of %d requests\n",
                   cases[i].name, seen_count, REQ_COUNT);
            failures++;
            continue;
        }
        if (memcmp(reference, seen, sizeof(reference)) != 0) {
            printf("FAIL: %s: the server observed a different request sequence "
                   "than the reference transport\n", cases[i].name);
            printf("      reference: ");
            for (int r = 0; r < REQ_COUNT; r++) printf("%02x", reference[r][0]);
            printf("\n      %-9s: ", cases[i].name);
            for (int r = 0; r < REQ_COUNT; r++) printf("%02x", seen[r][0]);
            printf("\n");
            failures++;
        } else {
            printf("  %-9s matches the reference\n", cases[i].name);
        }
    }

    unlink(ipc_addr);

    uv_timer_stop(&pump_timer);
    uvbus_loop_registry_free(registry);
    uv_timer_stop(&watchdog);
    uv_loop_close(&loop);

    if (failures == 0) {
        printf("span invariance: OK\n");
        return 0;
    }
    printf("span invariance: %d check(s) failed\n", failures);
    return 1;
}