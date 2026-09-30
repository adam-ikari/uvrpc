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

static void check(int ok, const char* what) {
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

/* The handler cannot reach main's stack, so it only records that it ran; a
 * watchdog timer armed in main stops the loop once handled is set (or after a
 * timeout if the server never answers). The timer recovers the loop from its
 * own handle, so no file-scope state is involved. */
static void watchdog(uv_timer_t* timer) {
    static int ticks;
    if (handled || ++ticks > 100) {
        uv_stop(uv_handle_get_loop((uv_handle_t*)timer));
    }
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
    handled = 1;
    return UVRPC_OK;
}

static void on_connect(int status, void* ctx) {
    (void)ctx;
    /* SAMELOOP connects synchronously inside create_client, so this fires
     * before the caller has the handle; the batch is sent from main once it
     * does. A non-OK status means the send below cannot reach the server, and
     * the watchdog stops the loop so it surfaces as handled == 0. */
    if (status != UVRPC_OK) {
        failures++;
    }
}

static void send_batch(uvrpc_client_t* client) {
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

    uvrpc_error_t ret = uvrpc_logservice_LogBatch(client, &request);
    if (ret != UVRPC_OK) {
        printf("FAIL: generated oneway call rejected with %d\n", (int)ret);
        failures++;
    }
    /* The oneway has no completion callback; the watchdog in main stops the
     * loop once the handler has run (or after a timeout), and handled == 0
     * catches the case where the server never answered. */
}

int main(void) {
    /* The framework never touches loop->data any more, so the struct does not
     * have to be zeroed for that reason -- though zero-initialising is still
     * the only safe way to declare one. */
    uv_loop_t loop = {0};
    uv_loop_init(&loop);

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
        uvbus_loop_registry_free(registry);
        return 1;
    }
    if (uvrpc_logservice_start_server(server) != UVRPC_OK) {
        printf("FAIL: could not start server\n");
        uvbus_loop_registry_free(registry);
        return 1;
    }

    uvrpc_client_t* client = uvrpc_logservice_create_client(&loop, "sameloop://dsl_codegen",
                                                            registry, on_connect, NULL);
    if (!client) {
        printf("FAIL: could not create client\n");
        uvrpc_logservice_stop_server(server);
        uvrpc_logservice_free_server(server);
        uvbus_loop_registry_free(registry);
        return 1;
    }

    /* SAMELOOP connects synchronously inside create_client, so the client is
     * already usable here. A watchdog stops the loop once the handler has run,
     * or after ~2 s if it never did, so a lost message fails the test instead of
     * hanging it. */
    uv_timer_t watchdog_timer;
    uv_timer_init(&loop, &watchdog_timer);
    uv_timer_start(&watchdog_timer, watchdog, 20, 20);

    send_batch(client);
    uv_run(&loop, UV_RUN_DEFAULT);

    check(handled == 1, "the generated stub dispatched to the user handler");

    uv_timer_stop(&watchdog_timer);
    uvrpc_logservice_free_client(client);
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
