/**
 * Regression test: allocator ownership across the codec boundary.
 *
 * Buffers produced by uvrpc_encode_*() come from flatcc's own builder
 * allocator, not from uvrpc_alloc(). Under UVRPC_ALLOCATOR_DEFAULT=custom the
 * registered pool is the only thing uvrpc_free() knows how to release, so any
 * encode buffer handed to it is a foreign free. The system and mimalloc builds
 * cannot observe this (uvrpc_free() bottoms out in free()), which is why this
 * test only exists in custom builds.
 *
 * The check is deliberately behavioural: run real request/response round trips
 * over a real transport, then assert the pool never saw a pointer it did not
 * hand out.
 */

#include <uvrpc.h>
#include <uvrpc_allocator.h>
#include <uvbus.h>
#include <uv.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TRACKED 65536

static void* g_tracked[MAX_TRACKED];
static int g_tracked_count;
static int g_foreign_frees;

static void track(void* p) {
    if (p && g_tracked_count < MAX_TRACKED) {
        g_tracked[g_tracked_count++] = p;
    }
}

static void* pool_alloc(size_t size) {
    void* p = malloc(size);
    track(p);
    return p;
}

static void* pool_calloc(size_t count, size_t size) {
    void* p = calloc(count, size);
    track(p);
    return p;
}

static void* pool_realloc(void* ptr, size_t size) {
    void* p = realloc(ptr, size);
    for (int i = 0; i < g_tracked_count; i++) {
        if (g_tracked[i] == ptr) g_tracked[i] = p;
    }
    track(p);
    return p;
}

static void pool_free(void* ptr) {
    if (!ptr) return;
    for (int i = 0; i < g_tracked_count; i++) {
        if (g_tracked[i] == ptr) {
            g_tracked[i] = NULL;
            free(ptr);
            return;
        }
    }
    g_foreign_frees++;
    fprintf(stderr, "FOREIGN FREE: uvrpc_free() released %p, "
                    "which the pool never allocated\n", ptr);
    free(ptr);
}

static int g_failures;

#define CHECK(cond, ...)                 \
    do {                                 \
        if (!(cond)) {                   \
            g_failures++;                \
            printf("FAIL: ");            \
            printf(__VA_ARGS__);         \
            printf("\n");                \
        }                                \
    } while (0)

static void add_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    int32_t sum = 0;
    if (req->params_size == 2 * sizeof(int32_t)) {
        const int32_t* a = (const int32_t*)req->params;
        sum = a[0] + a[1];
    }
    uvrpc_request_send_response(req, UVRPC_OK, (const uint8_t*)&sum, sizeof(sum));
}

static int g_responses;

static void on_response(uvrpc_response_t* resp, void* ctx) {
    (void)ctx;
    if (resp->status == UVRPC_OK && resp->result_size == sizeof(int32_t)) {
        g_responses++;
    }
}

static void pump(uv_loop_t* loop, int ms) {
    for (int elapsed = 0; elapsed < ms; elapsed++) {
        uv_run(loop, UV_RUN_NOWAIT);
        uv_sleep(1);
    }
}

static void run_until(uv_loop_t* loop, const int* flag, int timeout_ms) {
    for (int elapsed = 0; !*flag && elapsed < timeout_ms; elapsed++) {
        uv_run(loop, UV_RUN_NOWAIT);
        if (!*flag) uv_sleep(1);
    }
}

int main(void) {
    uvrpc_custom_allocator_t pool = {pool_alloc, pool_calloc, pool_realloc, pool_free,
                                     "ownership-pool", NULL};
    uvrpc_allocator_init(UVRPC_ALLOCATOR_CUSTOM, &pool);
    printf("allocator: %s\n", uvrpc_allocator_get_name());

    /* libuv's uv_loop_init() deliberately preserves loop->data (it is the
     * user's field, and INPROC/SAMELOOP hang their registry there), so the
     * struct must start zeroed. */
    uv_loop_t loop = {0};
    uv_loop_init(&loop);

    uvrpc_config_t* server_config = uvrpc_config_new();
    uvrpc_config_set_loop(server_config, &loop);
    uvrpc_config_set_address(server_config, "sameloop://allocator_ownership");
    uvrpc_config_set_transport(server_config, UVBUS_TRANSPORT_SAMELOOP);

    uvrpc_server_t* server = uvrpc_server_create(server_config);
    CHECK(server != NULL, "server_create returned NULL");
    if (!server) return 1;
    uvrpc_server_register(server, "add", add_handler, NULL);
    CHECK(uvrpc_server_start(server) == UVRPC_OK, "server_start failed");

    uvrpc_config_t* client_config = uvrpc_config_new();
    uvrpc_config_set_loop(client_config, &loop);
    uvrpc_config_set_address(client_config, "sameloop://allocator_ownership");
    uvrpc_config_set_transport(client_config, UVBUS_TRANSPORT_SAMELOOP);

    uvrpc_client_t* client = uvrpc_client_create(client_config);
    CHECK(client != NULL, "client_create returned NULL");
    if (!client) return 1;
    CHECK(uvrpc_client_connect(client) == UVRPC_OK, "connect failed");
    pump(&loop, 20);

    /* One-way request: encodes a frame, releases it right after the send. */
    int32_t oneway_params[2] = {1, 2};
    CHECK(uvrpc_client_call_oneway(client, "add", (const uint8_t*)oneway_params,
                                   sizeof(oneway_params)) == UVRPC_OK,
          "call_oneway failed");
    pump(&loop, 20);

    /* Request/response round trip: encodes a request and a response. */
    int32_t params[2] = {10, 20};
    CHECK(uvrpc_client_call(client, "add", (const uint8_t*)params, sizeof(params),
                            on_response, NULL) == UVRPC_OK,
          "client_call failed");
    run_until(&loop, &g_responses, 500);
    CHECK(g_responses == 1, "expected 1 response, got %d", g_responses);

    /* Batch call: same encode path in a loop. */
    const char* methods[2] = {"add", "add"};
    const uint8_t* batch_params[2] = {(const uint8_t*)params, (const uint8_t*)params};
    size_t batch_sizes[2] = {sizeof(params), sizeof(params)};
    uvrpc_callback_t callbacks[2] = {on_response, on_response};
    void* contexts[2] = {NULL, NULL};
    g_responses = 0;
    CHECK(uvrpc_client_call_batch(client, methods, batch_params, batch_sizes,
                                  callbacks, contexts, 2) == UVRPC_OK,
          "call_batch failed");
    run_until(&loop, &g_responses, 500);
    CHECK(g_responses == 2, "expected 2 batch responses, got %d", g_responses);

    uvrpc_client_free(client);
    uvrpc_server_free(server);
    uvrpc_config_free(client_config);
    uvrpc_config_free(server_config);
    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);

    CHECK(g_foreign_frees == 0, "%d foreign free(s) into the pool",
          g_foreign_frees);

    if (g_failures) {
        printf("allocator ownership test FAILED (%d checks)\n", g_failures);
        return 1;
    }
    printf("allocator ownership test passed\n");
    return 0;
}
