/*
 * Generated bindings, end to end, over SAMELOOP: the sync wrapper, the
 * ownership of string fields, and a refused call.
 *
 * All three regressed silently before this test existed:
 *   - *_sync() never entered an async block, so every call returned
 *     UVRPC_ERROR_INVALID_STATE;
 *   - the server stub sent handler refusals with uvrpc_request_send_response(),
 *     whose frame type is "success", so a refusal arrived as UVRPC_OK with an
 *     empty payload;
 *   - the sync wrapper handed back string fields pointing into a response
 *     buffer that was freed before the call returned.
 *
 * SAMELOOP keeps this in one process and one loop, so it needs no port and no
 * second executable; the sync wrapper runs the loop inline, which also serves
 * the server's end of the call.
 */

#include <stdio.h>
#include <string.h>

#include <uv.h>
#include "kv_kvservice_api.h"
#include "kv_reader.h"

#define KEY_MAX 63
#define VALUE_MAX 255
#define CALL_TIMEOUT_MS 5000

typedef struct {
    int used;
    char key[KEY_MAX + 1];
    char value[VALUE_MAX + 1];
} entry_t;

static entry_t g_entry;

static int failures;

static void check(const char* what, int ok) {
    printf("  %-52s %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) {
        failures++;
    }
}

/* The handler the generated stub calls. Deliberately tiny: the test is about
 * the binding layer, not about a store. */
uvrpc_error_t uvrpc_kvservice_handle_request(const char* method_name,
                                             const void* request,
                                             uvrpc_request_t* req) {
    if (strcmp(method_name, "Put") == 0) {
        kv_PutRequest_table_t put = kv_PutRequest_as_root(request);
        const char* key = kv_PutRequest_key(put);
        const char* value = kv_PutRequest_value(put);

        if (!key || !value) {
            return UVRPC_ERROR_INVALID_PARAM;
        }
        if (strlen(key) > KEY_MAX || strlen(value) > VALUE_MAX) {
            return UVRPC_ERROR_INVALID_PARAM;   /* the stub must frame this */
        }

        snprintf(g_entry.key, sizeof(g_entry.key), "%s", key);
        snprintf(g_entry.value, sizeof(g_entry.value), "%s", value);
        g_entry.used = 1;

        flatcc_builder_t builder;
        flatcc_builder_init(&builder);
        kv_PutResponse_start_as_root(&builder);
        kv_PutResponse_ok_add(&builder, 1);
        kv_PutResponse_end_as_root(&builder);

        size_t size = 0;
        void* buf = flatcc_builder_finalize_buffer(&builder, &size);
        uvrpc_request_send_response(req, UVRPC_OK, buf, size);
        free(buf);
        flatcc_builder_clear(&builder);
        return UVRPC_OK;
    }

    if (strcmp(method_name, "Get") == 0) {
        kv_GetRequest_table_t get = kv_GetRequest_as_root(request);
        const char* key = kv_GetRequest_key(get);
        if (!key) {
            return UVRPC_ERROR_INVALID_PARAM;
        }

        int found = g_entry.used && strcmp(g_entry.key, key) == 0;

        flatcc_builder_t builder;
        flatcc_builder_init(&builder);
        kv_GetResponse_start_as_root(&builder);
        kv_GetResponse_found_add(&builder, found);
        if (found) {
            flatbuffers_string_ref_t v = flatbuffers_string_create(
                &builder, g_entry.value, strlen(g_entry.value));
            kv_GetResponse_value_add(&builder, v);
        }
        kv_GetResponse_end_as_root(&builder);

        size_t size = 0;
        void* buf = flatcc_builder_finalize_buffer(&builder, &size);
        uvrpc_request_send_response(req, UVRPC_OK, buf, size);
        free(buf);
        flatcc_builder_clear(&builder);
        return UVRPC_OK;
    }

    if (strcmp(method_name, "Delete") == 0) {
        kv_DeleteRequest_table_t del = kv_DeleteRequest_as_root(request);
        const char* key = kv_DeleteRequest_key(del);
        if (!key) {
            return UVRPC_ERROR_INVALID_PARAM;
        }

        int deleted = g_entry.used && strcmp(g_entry.key, key) == 0;
        if (deleted) {
            g_entry.used = 0;
        }

        flatcc_builder_t builder;
        flatcc_builder_init(&builder);
        kv_DeleteResponse_start_as_root(&builder);
        kv_DeleteResponse_deleted_add(&builder, deleted);
        kv_DeleteResponse_end_as_root(&builder);

        size_t size = 0;
        void* buf = flatcc_builder_finalize_buffer(&builder, &size);
        uvrpc_request_send_response(req, UVRPC_OK, buf, size);
        free(buf);
        flatcc_builder_clear(&builder);
        return UVRPC_OK;
    }

    return UVRPC_ERROR_NOT_FOUND;
}

int main(void) {
    uv_loop_t loop = {0};
    uv_loop_init(&loop);

    uvbus_loop_registry_t* registry = uvbus_loop_registry_new();
    if (!registry) {
        printf("FAIL: could not create registry\n");
        return 1;
    }

    uvrpc_server_t* server = uvrpc_kvservice_create_server(
        &loop, "sameloop://generated_api", registry);
    if (!server || uvrpc_kvservice_start_server(server) != UVRPC_OK) {
        printf("FAIL: could not start server\n");
        uvbus_loop_registry_free(registry);
        return 1;
    }

    /* SAMELOOP connects synchronously inside create_client, so no connect
     * callback and no loop pumping are needed here. */
    uvrpc_client_t* client = uvrpc_kvservice_create_client(
        &loop, "sameloop://generated_api", registry, NULL, NULL);
    if (!client) {
        printf("FAIL: could not create client\n");
        uvrpc_kvservice_stop_server(server);
        uvrpc_kvservice_free_server(server);
        uvbus_loop_registry_free(registry);
        return 1;
    }

    kv_PutRequest_t put = { .key = "k", .value = "a value longer than a word" };
    kv_PutResponse_t put_resp = {0};
    uvrpc_error_t ret = uvrpc_kvservice_Put_sync(client, &put_resp, &put, CALL_TIMEOUT_MS);
    check("sync Put returns UVRPC_OK", ret == UVRPC_OK);
    check("sync Put reports ok", put_resp.ok);

    kv_GetRequest_t get = { .key = "k" };
    kv_GetResponse_t get_resp = {0};
    ret = uvrpc_kvservice_Get_sync(client, &get_resp, &get, CALL_TIMEOUT_MS);
    check("sync Get returns UVRPC_OK", ret == UVRPC_OK);
    check("sync Get found the key", get_resp.found);
    /* Read the string after _sync() returned: before the fields were copied
     * this dereferenced freed memory, and ASan/valgrind saw it. */
    check("sync Get string survives the call",
          get_resp.value && strcmp(get_resp.value, "a value longer than a word") == 0);
    uvrpc_kvservice_Get_response_free(&get_resp);
    check("response_free clears the string", get_resp.value == NULL);

    kv_GetRequest_t missing = { .key = "absent" };
    kv_GetResponse_t missing_resp = { .found = 1 };
    ret = uvrpc_kvservice_Get_sync(client, &missing_resp, &missing, CALL_TIMEOUT_MS);
    check("missing key is UVRPC_OK with found == false",
          ret == UVRPC_OK && !missing_resp.found);
    uvrpc_kvservice_Get_response_free(&missing_resp);

    char too_long[VALUE_MAX + 32];
    memset(too_long, 'x', sizeof(too_long) - 1);
    too_long[sizeof(too_long) - 1] = '\0';
    kv_PutRequest_t bad_put = { .key = "k", .value = too_long };
    kv_PutResponse_t bad_resp = {0};
    ret = uvrpc_kvservice_Put_sync(client, &bad_resp, &bad_put, CALL_TIMEOUT_MS);
    check("handler refusal arrives as INVALID_PARAM",
          ret == UVRPC_ERROR_INVALID_PARAM);

    uvrpc_kvservice_free_client(client);
    uv_run(&loop, UV_RUN_DEFAULT);
    uvrpc_kvservice_stop_server(server);
    uvrpc_kvservice_free_server(server);
    uv_run(&loop, UV_RUN_DEFAULT);
    uvbus_loop_registry_free(registry);
    uv_loop_close(&loop);

    printf("generated_api: %d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
