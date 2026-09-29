/* End-to-end test for the DSL generator.
 *
 * The generator used to crash outright on any schema with a struct array, so
 * the code it emits for that path had never been run. Two defects lived there,
 * both invisible to a compile check:
 *
 *   1. building a detached vector and adding its ref to the table field
 *      compiles, and decodes to length 0;
 *   2. passing a const char* where a table's _create() wants a
 *      flatbuffers_string_ref_t, so reading the field back dereferences
 *      whatever landed in that int -- a segfault.
 *
 * This drives the *generated* client function over a live SAMELOOP connection
 * and checks what the server decoded. It deliberately does not re-implement
 * the encoder in C: a test that duplicates the template's logic would still
 * pass against a generator that emits broken code.
 *
 * The server side is the generated stub, which dispatches to
 * uvrpc_logservice_handle_request() below -- the same hook a user implements.
 */
#include <stdio.h>
#include <string.h>
#include <uv.h>

#include "log_logservice_api.h"
#include "log_service_reader.h"
#include <uvrpc.h>

static int failures;
static int handled;
static int stop_timer_armed;

/* Server and client share one loop, so the handler can stop it directly. */
static uv_loop_t* g_loop;

static void check(int ok, const char* what) {
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static void stop_soon(uv_loop_t* loop) {
    static uv_timer_t timer;
    if (stop_timer_armed) {
        return;
    }
    stop_timer_armed = 1;
    uv_timer_init(loop, &timer);
    uv_timer_start(&timer, (uv_timer_cb)uv_stop, 150, 0);
}

/* The user half of the generated service: decode the request and check it. */
uvrpc_error_t uvrpc_logservice_handle_request(const char* method_name,
                                       const void* request,
                                       uvrpc_request_t* req) {
    if (strcmp(method_name, "LogBatch") != 0) {
        return UVRPC_ERROR_NOT_FOUND;
    }
    handled++;

    log_LogEntry_vec_t vec = log_LogBatchRequest_entries(
        log_LogBatchRequest_as_root(request));
    size_t count = log_LogEntry_vec_len(vec);

    /* Defect 1: a detached vector decodes to length 0 here. */
    check(count == 3, "three entries reach the server");

    if (count == 3) {
        log_LogEntry_table_t e0 = log_LogEntry_vec_at(vec, 0);
        /* Defect 2: reading a string field out of a garbage offset segfaults. */
        check(log_LogEntry_level(e0) == log_LogLevel_INFO, "entry 0 level");
        check(log_LogEntry_timestamp(e0) == 1000, "entry 0 timestamp");
        check(log_LogEntry_thread_id(e0) == 11, "entry 0 thread id");
        check(log_LogEntry_message(e0) != NULL &&
                  strcmp(log_LogEntry_message(e0), "first message") == 0,
              "entry 0 message");
        check(log_LogEntry_source(e0) != NULL &&
                  strcmp(log_LogEntry_source(e0), "alpha") == 0,
              "entry 0 source");
        check(log_LogEntry_component(e0) != NULL &&
                  strcmp(log_LogEntry_component(e0), "comp-a") == 0,
              "entry 0 component");

        log_LogEntry_table_t e1 = log_LogEntry_vec_at(vec, 1);
        check(log_LogEntry_level(e1) == log_LogLevel_WARNING, "entry 1 level");
        check(log_LogEntry_message(e1) != NULL &&
                  strcmp(log_LogEntry_message(e1), "second message") == 0,
              "entry 1 message");

        log_LogEntry_table_t e2 = log_LogEntry_vec_at(vec, 2);
        check(log_LogEntry_level(e2) == log_LogLevel_ERROR, "entry 2 level");
        check(log_LogEntry_message(e2) != NULL &&
                  strcmp(log_LogEntry_message(e2), "third message") == 0,
              "entry 2 message");
        /* A table's _create() cannot omit a field, so a null string is
         * encoded as an empty one rather than an absent field. */
        check(log_LogEntry_source(e2) != NULL &&
                  strcmp(log_LogEntry_source(e2), "") == 0,
              "null source round-trips as empty");
        check(log_LogEntry_component(e2) != NULL &&
                  strcmp(log_LogEntry_component(e2), "") == 0,
              "null component round-trips as empty");
    }

    (void)req;
    stop_soon(g_loop);
    return UVRPC_OK;
}

static uvrpc_client_t* g_client;

static void on_connect(int status, void* ctx) {
    (void)ctx;
    /* SAMELOOP connects synchronously inside create_client, so this fires
     * before main has assigned g_client. Nothing to do here -- the batch is
     * sent from a timer started in main once the handle is known. */
    if (status != UVRPC_OK) {
        printf("FAIL: client connection failed with %d\n", status);
        failures++;
        uv_stop(g_loop);
    }
}

static void send_batch(uv_timer_t* timer) {
    (void)timer;
    log_LogEntry_t entries[3];
    memset(entries, 0, sizeof(entries));

    entries[0].level = log_LogLevel_INFO;
    entries[0].message = "first message";
    entries[0].timestamp = 1000;
    entries[0].source = "alpha";
    entries[0].thread_id = 11;
    entries[0].component = "comp-a";

    entries[1].level = log_LogLevel_WARNING;
    entries[1].message = "second message";
    entries[1].timestamp = 2000;
    entries[1].source = "beta";
    entries[1].thread_id = 22;
    entries[1].component = "comp-b";

    entries[2].level = log_LogLevel_ERROR;
    entries[2].message = "third message";
    entries[2].timestamp = 3000;
    entries[2].source = NULL;
    entries[2].thread_id = 33;
    entries[2].component = NULL;

    log_LogBatchRequest_t request;
    request.entries = entries;
    request.entries_size = 3;

    uvrpc_error_t ret = uvrpc_logservice_LogBatch(g_client, &request);
    if (ret != UVRPC_OK) {
        printf("FAIL: generated oneway call rejected with %d\n", (int)ret);
        failures++;
        uv_stop(g_loop);
    }
    /* The oneway has no completion callback. If the server answered, its
     * handler calls stop_soon; if it never did, a guard timer started below
     * does, and handled == 0 catches that. */
}

int main(void) {
    /* INPROC/SAMELOOP keep their registry on loop->data, and libuv 1.47 leaves
     * that field untouched by uv_loop_init(), so the struct must be zeroed. */
    uv_loop_t loop = {0};
    uv_loop_init(&loop);
    g_loop = &loop;

    /* SAMELOOP peers meet through a registry the caller owns. */
    uvbus_loop_registry_t* registry = uvbus_loop_registry_new();
    if (!registry) {
        printf("FAIL: could not create registry\n");
        return 1;
    }

    uvrpc_server_t* server = uvrpc_logservice_create_server(&loop,
                                                            "sameloop://dsl_codegen",
                                                            registry);
    if (!server) {
        printf("FAIL: could not create server\n");
        return 1;
    }
    if (uvrpc_logservice_start_server(server) != UVRPC_OK) {
        printf("FAIL: could not start server\n");
        return 1;
    }

    g_client = uvrpc_logservice_create_client(&loop, "sameloop://dsl_codegen",
                                              registry, on_connect, NULL);
    if (!g_client) {
        printf("FAIL: could not create client\n");
        return 1;
    }

    /* g_client is set now; SAMELOOP already connected synchronously. Send
     * from a 0-delay timer so the loop is running when it fires, and arm a
     * guard that stops the loop if the server never answers. */
    static uv_timer_t send_timer;
    uv_timer_init(&loop, &send_timer);
    uv_timer_start(&send_timer, send_batch, 0, 0);
    stop_soon(&loop);

    uv_run(&loop, UV_RUN_DEFAULT);

    check(handled == 1, "the generated stub dispatched to the user handler");

    uvrpc_logservice_free_client(g_client);
    uvrpc_logservice_stop_server(server);
    uvrpc_logservice_free_server(server);
    uvbus_loop_registry_free(registry);
    uv_loop_close(&loop);

    if (failures == 0) {
        printf("dsl codegen round trip: OK\n");
        return 0;
    }
    printf("dsl codegen round trip: %d check(s) failed\n", failures);
    return 1;
}
