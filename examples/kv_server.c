/*
 * KV server: a real service definition, not a demo of one call.
 *
 * The generated server stub owns transport, dispatch and error framing; this
 * file owns the store and the three methods. Run it and talk to it with
 * kv_client in another terminal.
 *
 * The store is a file-scope table because the generated handler signature has
 * no user context parameter -- the stub passes ctx through to the RPC handler
 * but the per-service handler is called without it. That is fine for a single
 * service per process; two KV servers in one process would share the store.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <uv.h>
#include "kv_kvservice_api.h"
#include "kv_reader.h"

#define KV_SLOTS 64
#define KV_KEY_MAX 63
#define KV_VALUE_MAX 255

typedef struct {
    int used;
    char key[KV_KEY_MAX + 1];
    char value[KV_VALUE_MAX + 1];
} kv_slot_t;

static kv_slot_t g_store[KV_SLOTS];

static kv_slot_t* slot_for(const char* key) {
    unsigned hash = 2166136261u;   /* FNV-1a */
    for (const unsigned char* p = (const unsigned char*)key; *p; p++) {
        hash = (hash ^ *p) * 16777619u;
    }
    size_t start = hash % KV_SLOTS;

    /* Linear probe: a table with no deletion tombstones, so deleting clears
     * the slot and lookup stops at the first unused one. */
    for (size_t i = 0; i < KV_SLOTS; i++) {
        kv_slot_t* slot = &g_store[(start + i) % KV_SLOTS];
        if (!slot->used || strcmp(slot->key, key) == 0) {
            return slot;
        }
    }
    return NULL;   /* full */
}

static void send_put_response(uvrpc_request_t* req, int ok) {
    flatcc_builder_t builder;
    flatcc_builder_init(&builder);

    kv_PutResponse_start_as_root(&builder);
    kv_PutResponse_ok_add(&builder, ok);
    kv_PutResponse_end_as_root(&builder);

    size_t size = 0;
    void* buf = flatcc_builder_finalize_buffer(&builder, &size);
    uvrpc_request_send_response(req, UVRPC_OK, buf, size);

    free(buf);                     /* flatcc owns this buffer; free() releases it */
    flatcc_builder_clear(&builder);
}

static void send_get_response(uvrpc_request_t* req, int found, const char* value) {
    flatcc_builder_t builder;
    flatcc_builder_init(&builder);

    kv_GetResponse_start_as_root(&builder);
    kv_GetResponse_found_add(&builder, found);
    if (found) {
        flatbuffers_string_ref_t v = flatbuffers_string_create(&builder, value, strlen(value));
        kv_GetResponse_value_add(&builder, v);
    }
    kv_GetResponse_end_as_root(&builder);

    size_t size = 0;
    void* buf = flatcc_builder_finalize_buffer(&builder, &size);
    uvrpc_request_send_response(req, UVRPC_OK, buf, size);

    free(buf);
    flatcc_builder_clear(&builder);
}

static void send_delete_response(uvrpc_request_t* req, int deleted) {
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
}

/* The wire carries a FlatBuffers root, so every field is read through the
 * generated readers. Casting the payload to the client-side struct would read
 * a table pointer as if it were the struct. */
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
        if (strlen(key) > KV_KEY_MAX || strlen(value) > KV_VALUE_MAX) {
            /* A typed refusal, framed as an RPC error by the stub. */
            return UVRPC_ERROR_INVALID_PARAM;
        }

        kv_slot_t* slot = slot_for(key);
        if (!slot) {
            return UVRPC_ERROR_POOL_EXHAUSTED;
        }
        snprintf(slot->key, sizeof(slot->key), "%s", key);
        snprintf(slot->value, sizeof(slot->value), "%s", value);
        slot->used = 1;
        send_put_response(req, 1);
        return UVRPC_OK;
    }

    if (strcmp(method_name, "Get") == 0) {
        kv_GetRequest_table_t get = kv_GetRequest_as_root(request);
        const char* key = kv_GetRequest_key(get);
        if (!key) {
            return UVRPC_ERROR_INVALID_PARAM;
        }

        kv_slot_t* slot = slot_for(key);
        if (slot && slot->used) {
            send_get_response(req, 1, slot->value);
        } else {
            send_get_response(req, 0, NULL);
        }
        return UVRPC_OK;
    }

    if (strcmp(method_name, "Delete") == 0) {
        kv_DeleteRequest_table_t del = kv_DeleteRequest_as_root(request);
        const char* key = kv_DeleteRequest_key(del);
        if (!key) {
            return UVRPC_ERROR_INVALID_PARAM;
        }

        kv_slot_t* slot = slot_for(key);
        int deleted = slot && slot->used;
        if (deleted) {
            slot->used = 0;
        }
        send_delete_response(req, deleted);
        return UVRPC_OK;
    }

    return UVRPC_ERROR_NOT_FOUND;
}
int main(int argc, char** argv) {
    const char* address = argc > 1 ? argv[1] : "tcp://127.0.0.1:5555";

    uv_loop_t loop;
    if (uv_loop_init(&loop) != 0) {
        fprintf(stderr, "kv_server: cannot create event loop\n");
        return 1;
    }

    /* The registry is only needed by the in-process transports; TCP ignores
     * it, so NULL is the honest argument here. */
    uvrpc_server_t* server = uvrpc_kvservice_create_server(&loop, address, NULL);
    if (!server) {
        fprintf(stderr, "kv_server: cannot create server on %s\n", address);
        uv_loop_close(&loop);
        return 1;
    }

    int ret = uvrpc_kvservice_start_server(server);
    if (ret != UVRPC_OK) {
        fprintf(stderr, "kv_server: start failed: %s\n", uvrpc_strerror(ret));
        uvrpc_kvservice_free_server(server);
        uv_loop_close(&loop);
        return 1;
    }

    printf("kv_server: listening on %s\n", address);
    fflush(stdout);

    uv_run(&loop, UV_RUN_DEFAULT);

    uvrpc_kvservice_free_server(server);
    uv_run(&loop, UV_RUN_DEFAULT);   /* let close callbacks settle */
    uv_loop_close(&loop);
    return 0;
}
