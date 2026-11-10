/*
 * Every vector shape the generator emits, end to end, over SAMELOOP.
 *
 * The request and response tables in tests/schema/shapes.fbs carry an int32
 * vector, a double vector, a bool vector, a string vector and a table vector
 * at once. Two scalar-vector defects lived through a green suite because no
 * schema in the tree had a primitive vector wider than a byte:
 *
 *   - the request was serialized with flatbuffers_uint8_vec_create(), which
 *     copies N *bytes*, not N elements, so an int32 vector arrived truncated;
 *   - the response was read by casting the raw vector to <type>* and
 *     memcpy'ing it, which is only correct on a little-endian host and warns
 *     about incompatible pointer types.
 *
 * Both were shape-dependent and invisible to kv.fbs, strings.fbs and
 * table_arrays.fbs, so this schema is what keeps the shape matrix covered:
 * _sync(), _all(), _any() and the two free helpers, with an empty-vector case.
 */

#include <stdio.h>
#include <string.h>

#include <uv.h>
#include "sh_shapeservice_api.h"
#include "uvrpc_async.h"
#include "shapes_reader.h"

#define CALL_TIMEOUT_MS 5000

static int failures;

static void check(const char* what, int ok) {
    printf("  %-56s %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) {
        failures++;
    }
}

/* Echo with a visible transform per field, so a truncated or byte-swapped
 * vector cannot pass by echoing the input back unchanged. */
uvrpc_error_t uvrpc_shapeservice_handle_request(const char* method_name,
                                               const void* request,
                                               uvrpc_request_t* req) {
    if (strcmp(method_name, "Echo") != 0) {
        return UVRPC_ERROR_NOT_FOUND;
    }

    sh_ShapesRequest_table_t in = sh_ShapesRequest_as_root(request);

    flatbuffers_int32_vec_t values = sh_ShapesRequest_values(in);
    flatbuffers_double_vec_t scores = sh_ShapesRequest_scores(in);
    flatbuffers_uint8_vec_t flags = sh_ShapesRequest_flags(in);
    flatbuffers_string_vec_t tags = sh_ShapesRequest_tags(in);
    sh_Item_vec_t items = sh_ShapesRequest_items(in);

    flatcc_builder_t builder;
    flatcc_builder_init(&builder);
    sh_ShapesResponse_start_as_root(&builder);

    size_t n = flatbuffers_vec_len(values);
    if (n > 0) {
        int32_t* doubled = malloc(n * sizeof(*doubled));
        if (!doubled) {
            flatcc_builder_clear(&builder);
            return UVRPC_ERROR_NO_MEMORY;
        }
        for (size_t i = 0; i < n; i++) {
            doubled[i] = flatbuffers_int32_vec_at(values, i) * 2;
        }
        sh_ShapesResponse_values_create(&builder, doubled, n);
        free(doubled);
    }

    n = flatbuffers_vec_len(scores);
    if (n > 0) {
        double* shifted = malloc(n * sizeof(*shifted));
        if (!shifted) {
            flatcc_builder_clear(&builder);
            return UVRPC_ERROR_NO_MEMORY;
        }
        for (size_t i = 0; i < n; i++) {
            shifted[i] = flatbuffers_double_vec_at(scores, i) + 0.5;
        }
        sh_ShapesResponse_scores_create(&builder, shifted, n);
        free(shifted);
    }

    n = flatbuffers_vec_len(flags);
    if (n > 0) {
        uint8_t* inverted = malloc(n);
        if (!inverted) {
            flatcc_builder_clear(&builder);
            return UVRPC_ERROR_NO_MEMORY;
        }
        for (size_t i = 0; i < n; i++) {
            inverted[i] = flatbuffers_uint8_vec_at(flags, i) ? 0 : 1;
        }
        sh_ShapesResponse_flags_create(&builder, inverted, n);
        free(inverted);
    }

    n = flatbuffers_vec_len(tags);
    if (n > 0) {
        sh_ShapesResponse_tags_start(&builder);
        for (size_t i = 0; i < n; i++) {
            const char* tag = flatbuffers_string_vec_at(tags, i);
            sh_ShapesResponse_tags_push_create(&builder,
                tag ? tag : "", tag ? strlen(tag) : 0);
        }
        sh_ShapesResponse_tags_end(&builder);
    }

    n = flatbuffers_vec_len(items);
    if (n > 0) {
        sh_ShapesResponse_items_start(&builder);
        for (size_t i = 0; i < n; i++) {
            sh_Item_table_t item = sh_Item_vec_at(items, i);
            const char* label = sh_Item_label(item);
            flatbuffers_string_ref_t label_ref = flatbuffers_string_create(
                &builder, label ? label : "", label ? strlen(label) : 0);
            sh_ShapesResponse_items_push_create(&builder, label_ref,
                                                sh_Item_qty(item) + 1);
        }
        sh_ShapesResponse_items_end(&builder);
    }

    sh_ShapesResponse_end_as_root(&builder);

    size_t size = 0;
    void* buf = flatcc_builder_finalize_buffer(&builder, &size);
    uvrpc_request_send_response(req, UVRPC_OK, buf, size);

    free(buf);
    flatcc_builder_clear(&builder);
    return UVRPC_OK;
}

static sh_Item_t make_item(const char* label, uint32_t qty) {
    sh_Item_t item = { .label = label, .qty = qty };
    return item;
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

    uvrpc_server_t* server = uvrpc_shapeservice_create_server(
        &loop, "sameloop://generated_shapes", registry);
    if (!server || uvrpc_shapeservice_start_server(server) != UVRPC_OK) {
        printf("FAIL: could not start server\n");
        uvbus_loop_registry_free(registry);
        uv_loop_close(&loop);
        return 1;
    }

    uvrpc_client_t* client = uvrpc_shapeservice_create_client(
        &loop, "sameloop://generated_shapes", registry, NULL, NULL);
    if (!client) {
        printf("FAIL: could not create client\n");
        uvrpc_shapeservice_stop_server(server);
        uvrpc_shapeservice_free_server(server);
        uvbus_loop_registry_free(registry);
        uv_loop_close(&loop);
        return 1;
    }

    /* --- _sync: every shape in one round trip ------------------------ */
    {
        int32_t values[3] = { 7, -3, 42 };
        double scores[2] = { 1.5, 2.25 };
        bool flags[2] = { false, true };
        const char* tags[2] = { "alpha", "beta" };
        sh_Item_t items[2] = { make_item("bolt", 12), make_item("nut", 7) };

        sh_ShapesRequest_t request = {
            .values = values, .values_size = 3,
            .scores = scores, .scores_size = 2,
            .flags = flags, .flags_size = 2,
            .tags = tags, .tags_size = 2,
            .items = items, .items_size = 2,
        };
        sh_ShapesResponse_t response = {0};

        uvrpc_error_t ret = uvrpc_shapeservice_Echo_sync(client, &response,
                                                         &request, CALL_TIMEOUT_MS);
        check("sync Echo returns UVRPC_OK", ret == UVRPC_OK);

        /* Read after _sync() returned: these are copies, so a truncated or
         * byte-swapped vector shows up as a wrong value, not as a crash. */
        check("int32 vector keeps all three elements",
              response.values_size == 3 && response.values);
        check("int32 vector values survive the round trip",
              response.values_size == 3 &&
              response.values[0] == 14 && response.values[1] == -6 &&
              response.values[2] == 84);
        check("double vector values survive the round trip",
              response.scores_size == 2 && response.scores &&
              response.scores[0] == 2.0 && response.scores[1] == 2.75);
        check("bool vector elements survive the round trip",
              response.flags_size == 2 && response.flags &&
              response.flags[0] == true && response.flags[1] == false);
        check("string vector elements survive the round trip",
              response.tags_size == 2 && response.tags &&
              response.tags[0] && strcmp(response.tags[0], "alpha") == 0 &&
              response.tags[1] && strcmp(response.tags[1], "beta") == 0);
        check("table vector elements survive the round trip",
              response.items_size == 2 && response.items &&
              response.items[0].label &&
              strcmp(response.items[0].label, "bolt") == 0 &&
              response.items[0].qty == 13 &&
              response.items[1].qty == 8);

        uvrpc_shapeservice_Echo_response_free(&response);
        check("response_free clears every vector",
              response.values == NULL && response.values_size == 0 &&
              response.scores == NULL && response.scores_size == 0 &&
              response.flags == NULL && response.flags_size == 0 &&
              response.tags == NULL && response.tags_size == 0 &&
              response.items == NULL && response.items_size == 0);
    }

    /* --- empty vectors ---------------------------------------------- */
    {
        sh_ShapesRequest_t request = {0};
        sh_ShapesResponse_t response = {0};

        uvrpc_error_t ret = uvrpc_shapeservice_Echo_sync(client, &response,
                                                         &request, CALL_TIMEOUT_MS);
        check("empty request returns UVRPC_OK", ret == UVRPC_OK);
        check("empty vectors arrive as NULL with size 0",
              response.values == NULL && response.values_size == 0 &&
              response.scores == NULL && response.scores_size == 0 &&
              response.flags == NULL && response.flags_size == 0 &&
              response.tags == NULL && response.tags_size == 0 &&
              response.items == NULL && response.items_size == 0);
        uvrpc_shapeservice_Echo_response_free(&response);
    }

    /* --- _all / _any: the batch paths serialize and copy the same way -- */
    {
        uvrpc_async_ctx_t* actx = uvrpc_async_ctx_new(&loop);
        if (!actx) {
            printf("FAIL: cannot create async context\n");
        } else {
            int32_t left_values[1] = { 1 };
            int32_t right_values[3] = { 1, 2, 3 };
            sh_ShapesRequest_t requests[2] = {
                { .values = left_values, .values_size = 1 },
                { .values = right_values, .values_size = 3 },
            };
            uvrpc_client_t* clients[2] = { client, client };
            sh_ShapesResponse_t* all = NULL;

            uvrpc_error_t ret = uvrpc_shapeservice_echo_all(
                actx, clients, requests, &all, 2, CALL_TIMEOUT_MS);
            check("echo_all returns UVRPC_OK", ret == UVRPC_OK);
            check("echo_all copied the first int32 vector",
                  all && all[0].values_size == 1 && all[0].values &&
                  all[0].values[0] == 2);
            check("echo_all copied the second int32 vector",
                  all && all[1].values_size == 3 && all[1].values &&
                  all[1].values[0] == 2 && all[1].values[1] == 4 &&
                  all[1].values[2] == 6);
            uvrpc_shapeservice_echo_results_free(all, 2);

            sh_ShapesResponse_t single = {0};
            int index = -1;
            ret = uvrpc_shapeservice_echo_any(actx, clients, requests, &single,
                                              &index, 2, CALL_TIMEOUT_MS);
            check("echo_any returns UVRPC_OK with a valid index",
                  ret == UVRPC_OK && index >= 0 && index < 2);
            check("echo_any copied the winning int32 vector",
                  single.values &&
                  single.values_size == (size_t)(index == 0 ? 1 : 3) &&
                  single.values[0] == 2);
            uvrpc_shapeservice_echo_result_free(&single);

            uvrpc_async_ctx_free(actx);
        }
    }

    /* Same teardown order as generated_strings_test: the client closes first
     * and its close callbacks run before the server is torn down. */
    uvrpc_shapeservice_free_client(client);
    uv_run(&loop, UV_RUN_DEFAULT);
    uvrpc_shapeservice_stop_server(server);
    uvrpc_shapeservice_free_server(server);
    uvbus_loop_registry_free(registry);
    uv_run(&loop, UV_RUN_DEFAULT);
    uv_loop_close(&loop);

    printf("generated_shapes_test: %d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
