/*
 * Vectors of strings, end to end, over SAMELOOP.
 *
 * A [string] field is the one array shape whose elements cannot be copied by
 * value: each element is a FlatBuffers string living inside the response
 * buffer, which is freed before the wrapper returns. The generator used to
 * reject the shape outright; the templates now duplicate every element and the
 * generated free helpers release them one at a time.
 *
 * Three properties are asserted here, because each has a silent failure mode:
 *   - the elements survive the call (before, they dangled);
 *   - freeing a response clears both the elements and the pointer array;
 *   - an empty vector arrives as size 0 with a NULL array, and freeing it is
 *     harmless.
 */

#include <stdio.h>
#include <string.h>

#include <uv.h>
#include "strings_stringservice_api.h"
#include "uvrpc_async.h"
#include "strings_reader.h"

#define CALL_TIMEOUT_MS 5000

static int failures;

static void check(const char* what, int ok) {
    printf("  %-56s %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) {
        failures++;
    }
}

/* Echo everything back, including the element count, so the client can tell a
 * copied vector from one that decoded to length 0. */
uvrpc_error_t uvrpc_stringservice_handle_request(const char* method_name,
                                                 const void* request,
                                                 uvrpc_request_t* req) {
    if (strcmp(method_name, "Echo") != 0) {
        return UVRPC_ERROR_NOT_FOUND;
    }

    strings_EchoRequest_table_t echo = strings_EchoRequest_as_root(request);
    flatbuffers_string_vec_t tags = strings_EchoRequest_tags(echo);
    size_t count = flatbuffers_string_vec_len(tags);

    flatcc_builder_t builder;
    flatcc_builder_init(&builder);

    strings_EchoResponse_start_as_root(&builder);
    if (count > 0) {
        strings_EchoResponse_tags_start(&builder);
        for (size_t i = 0; i < count; i++) {
            const char* tag = flatbuffers_string_vec_at(tags, i);
            strings_EchoResponse_tags_push_create(&builder,
                tag ? tag : "", tag ? strlen(tag) : 0);
        }
        strings_EchoResponse_tags_end(&builder);
    }
    const char* note = strings_EchoRequest_note(echo);
    if (note) {
        flatbuffers_string_ref_t note_ref =
            flatbuffers_string_create(&builder, note, strlen(note));
        strings_EchoResponse_note_add(&builder, note_ref);
    }
    strings_EchoResponse_count_add(&builder, (int32_t)count);
    strings_EchoResponse_end_as_root(&builder);

    size_t size = 0;
    void* buf = flatcc_builder_finalize_buffer(&builder, &size);
    uvrpc_request_send_response(req, UVRPC_OK, buf, size);

    free(buf);
    flatcc_builder_clear(&builder);
    return UVRPC_OK;
}

int main(void) {
    uv_loop_t loop;
    if (uv_loop_init(&loop) != 0) {
        printf("FAIL: cannot create event loop\n");
        return 1;
    }

    uvbus_loop_registry_t* registry = uvbus_loop_registry_new();
    if (!registry) {
        printf("FAIL: cannot create loop registry\n");
        uv_loop_close(&loop);
        return 1;
    }

    uvrpc_server_t* server = uvrpc_stringservice_create_server(
        &loop, "sameloop://generated_strings", registry);
    if (!server || uvrpc_stringservice_start_server(server) != UVRPC_OK) {
        printf("FAIL: could not start server\n");
        uvbus_loop_registry_free(registry);
        uv_loop_close(&loop);
        return 1;
    }

    uvrpc_client_t* client = uvrpc_stringservice_create_client(
        &loop, "sameloop://generated_strings", registry, NULL, NULL);
    if (!client) {
        printf("FAIL: could not create client\n");
        uvrpc_stringservice_stop_server(server);
        uvrpc_stringservice_free_server(server);
        uvbus_loop_registry_free(registry);
        uv_loop_close(&loop);
        return 1;
    }

    /* --- a vector with three elements ------------------------------- */
    {
        const char* tags[3] = { "alpha", "beta", "" };
        strings_EchoRequest_t request = {
            .tags = tags, .tags_size = 3, .note = "three tags"
        };
        strings_EchoResponse_t response = {0};

        uvrpc_error_t ret = uvrpc_stringservice_Echo_sync(client, &response,
                                                         &request, CALL_TIMEOUT_MS);
        check("sync Echo returns UVRPC_OK", ret == UVRPC_OK);
        check("server saw every element", response.count == 3);
        check("response vector has three elements", response.tags_size == 3);
        /* Reading after _sync() returned: these are copies, not pointers into
         * the freed response buffer. */
        check("element 0 survives the call",
              response.tags && response.tags[0] && strcmp(response.tags[0], "alpha") == 0);
        check("element 1 survives the call",
              response.tags && response.tags[1] && strcmp(response.tags[1], "beta") == 0);
        check("empty element round-trips as empty",
              response.tags && response.tags[2] && response.tags[2][0] == '\0');
        check("scalar string field still copied",
              response.note && strcmp(response.note, "three tags") == 0);

        uvrpc_stringservice_Echo_response_free(&response);
        check("response_free releases the elements and the array",
              response.tags == NULL && response.tags_size == 0 && response.note == NULL);
    }

    /* --- an absent vector ------------------------------------------- */
    {
        strings_EchoRequest_t request = { .tags = NULL, .tags_size = 0,
                                          .note = "no tags" };
        strings_EchoResponse_t response = {0};

        uvrpc_error_t ret = uvrpc_stringservice_Echo_sync(client, &response,
                                                          &request, CALL_TIMEOUT_MS);
        check("empty request vector returns UVRPC_OK", ret == UVRPC_OK);
        check("empty vector arrives as NULL with size 0",
              response.tags == NULL && response.tags_size == 0);
        check("empty vector counts zero", response.count == 0);

        uvrpc_stringservice_Echo_response_free(&response);
        check("response_free tolerates an empty vector",
              response.tags == NULL && response.tags_size == 0);
    }

    /* --- combinators ------------------------------------------------- */
    uvrpc_async_ctx_t* actx = uvrpc_async_ctx_new(&loop);
    check("async context created", actx != NULL);
    if (actx) {
        uvrpc_client_t* clients[2] = { client, client };
        const char* first_tags[1] = { "one" };
        const char* second_tags[2] = { "two", "three" };
        strings_EchoRequest_t requests[2] = {
            { .tags = first_tags, .tags_size = 1, .note = "first" },
            { .tags = second_tags, .tags_size = 2, .note = "second" }
        };

        strings_EchoResponse_t* all = NULL;
        uvrpc_error_t ret = uvrpc_stringservice_echo_all(actx, clients, requests,
                                                         &all, 2, CALL_TIMEOUT_MS);
        check("echo_all returns UVRPC_OK", ret == UVRPC_OK);
        check("echo_all copies the first vector",
              all && all[0].tags_size == 1 && all[0].tags[0] &&
              strcmp(all[0].tags[0], "one") == 0);
        check("echo_all copies the second vector",
              all && all[1].tags_size == 2 && all[1].tags[1] &&
              strcmp(all[1].tags[1], "three") == 0);
        uvrpc_stringservice_echo_results_free(all, 2);

        strings_EchoResponse_t single = {0};
        int index = -1;
        ret = uvrpc_stringservice_echo_any(actx, clients, requests, &single, &index,
                                           2, CALL_TIMEOUT_MS);
        check("echo_any returns UVRPC_OK with a valid index",
              ret == UVRPC_OK && (index == 0 || index == 1));
        if (index == 0) {
            check("echo_any copies the first vector",
                  single.tags_size == 1 && single.tags[0] &&
                  strcmp(single.tags[0], "one") == 0);
        } else if (index == 1) {
            check("echo_any copies the second vector",
                  single.tags_size == 2 && single.tags[0] &&
                  strcmp(single.tags[0], "two") == 0);
        }
        uvrpc_stringservice_echo_result_free(&single);
        check("result_free leaves the struct addressable",
              single.tags == NULL && single.tags_size == 0 && single.note == NULL);

        uvrpc_async_ctx_free(actx);
    }

    uvrpc_stringservice_free_client(client);
    uv_run(&loop, UV_RUN_DEFAULT);
    uvrpc_stringservice_stop_server(server);
    uvrpc_stringservice_free_server(server);
    uvbus_loop_registry_free(registry);
    uv_run(&loop, UV_RUN_DEFAULT);
    uv_loop_close(&loop);

    printf("generated_strings_test: %d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
