/*
 * KV client: exercises the generated bindings the way an application would --
 * connect, then a sequence of calls whose results are checked, not printed and
 * trusted.
 *
 * Start kv_server first (another terminal), then run this. Exit code is 0 only
 * if every check in the script passes, so it doubles as a smoke test:
 *
 *   ./dist/bin/kv_server &
 *   ./dist/bin/kv_client
 */

#include <stdio.h>
#include <string.h>

#include <uv.h>
#include "kv_kvservice_api.h"

#define CALL_TIMEOUT_MS 2000

static int failures;
static int connect_status = -1;   /* -1: connect still in flight */

static void on_connect(int status, void* ctx) {
    (void)ctx;
    connect_status = status;
}

static void check(const char* what, int ok) {
    printf("  %-42s %s\n", what, ok ? "ok" : "FAILED");
    fflush(stdout);
    if (!ok) {
        failures++;
    }
}

int main(int argc, char** argv) {
    const char* address = argc > 1 ? argv[1] : "tcp://127.0.0.1:5555";

    uv_loop_t loop;
    if (uv_loop_init(&loop) != 0) {
        fprintf(stderr, "kv_client: cannot create event loop\n");
        return 1;
    }

    /* NULL registry: TCP has no in-process endpoints to register.
     *
     * create_client() starts the connection but does not wait for it, and a
     * call made while it is in flight is rejected with NOT_CONNECTED. Pump the
     * loop until the callback says which way it went -- this is the
     * application's own responsibility, and doing it explicitly is what makes
     * the difference between "the server is slow" and "the server is not
     * there" visible. */
    uvrpc_client_t* client = uvrpc_kvservice_create_client(
        &loop, address, NULL, on_connect, NULL);
    if (!client) {
        fprintf(stderr, "kv_client: cannot create client for %s\n", address);
        uv_loop_close(&loop);
        return 1;
    }

    uint64_t deadline = uv_hrtime() + 5ULL * 1000 * 1000 * 1000;
    while (connect_status < 0 && uv_hrtime() < deadline) {
        uv_run(&loop, UV_RUN_NOWAIT);
        uv_sleep(1);
    }
    if (connect_status != 0) {
        fprintf(stderr, "kv_client: connect to %s failed: %s\n", address,
                connect_status < 0 ? "timed out" : uvrpc_strerror(connect_status));
        uvrpc_kvservice_free_client(client);
        uv_run(&loop, UV_RUN_DEFAULT);
        uv_loop_close(&loop);
        return 1;
    }

    int ret = 0;
    printf("kv_client: talking to %s\n", address);

    /* Put */
    {
        kv_PutRequest_t req = { .key = "greeting", .value = "hello, kv" };
        kv_PutResponse_t resp = {0};
        ret = uvrpc_kvservice_Put_sync(client, &resp, &req, CALL_TIMEOUT_MS);
        check("Put(greeting) succeeds", ret == UVRPC_OK && resp.ok);
    }

    /* Get the key back. The sync API copies string fields out of the response
     * buffer, so resp.value is valid until the response is freed. */
    {
        kv_GetRequest_t req = { .key = "greeting" };
        kv_GetResponse_t resp = {0};
        ret = uvrpc_kvservice_Get_sync(client, &resp, &req, CALL_TIMEOUT_MS);
        check("Get(greeting) returns hello, kv",
              ret == UVRPC_OK && resp.found && resp.value &&
              strcmp(resp.value, "hello, kv") == 0);
        uvrpc_kvservice_Get_response_free(&resp);
    }

    /* A missing key is a successful call with found == false, not an error. */
    {
        kv_GetRequest_t req = { .key = "not-there" };
        kv_GetResponse_t resp = { .found = true };
        ret = uvrpc_kvservice_Get_sync(client, &resp, &req, CALL_TIMEOUT_MS);
        check("Get(not-there) is found == false", ret == UVRPC_OK && !resp.found);
    }

    /* Delete, then prove the delete took effect. */
    {
        kv_DeleteRequest_t req = { .key = "greeting" };
        kv_DeleteResponse_t resp = {0};
        ret = uvrpc_kvservice_Delete_sync(client, &resp, &req, CALL_TIMEOUT_MS);
        check("Delete(greeting) reports deleted", ret == UVRPC_OK && resp.deleted);
    }
    {
        kv_DeleteRequest_t req = { .key = "greeting" };
        kv_DeleteResponse_t resp = { .deleted = true };
        ret = uvrpc_kvservice_Delete_sync(client, &resp, &req, CALL_TIMEOUT_MS);
        check("Delete(greeting) again reports not deleted", ret == UVRPC_OK && !resp.deleted);
    }
    {
        kv_GetRequest_t req = { .key = "greeting" };
        kv_GetResponse_t resp = { .found = true };
        ret = uvrpc_kvservice_Get_sync(client, &resp, &req, CALL_TIMEOUT_MS);
        check("Get(greeting) is gone", ret == UVRPC_OK && !resp.found);
    }

    /* A refusal is a framed error, so it arrives as a status, not as a
     * successful response with an empty value. */
    {
        char big[512];
        memset(big, 'x', sizeof(big) - 1);
        big[sizeof(big) - 1] = '\0';
        kv_PutRequest_t req = { .key = "too-big", .value = big };
        kv_PutResponse_t resp = {0};
        ret = uvrpc_kvservice_Put_sync(client, &resp, &req, CALL_TIMEOUT_MS);
        check("Put(value too long) is INVALID_PARAM", ret == UVRPC_ERROR_INVALID_PARAM);
    }

    uvrpc_kvservice_free_client(client);
    uv_run(&loop, UV_RUN_DEFAULT);   /* let close callbacks settle */
    uv_loop_close(&loop);

    printf("kv_client: %d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
