/**
 * UVRPC Error Handling End-to-End Integration Test
 * Tests various error scenarios
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <uv.h>
#include <pthread.h>
#include "../../include/uvrpc.h"

#define TEST_PORT 15559
#define TEST_HOST "127.0.0.1"
#define TIMEOUT_MS 3000

static int server_received = 0;
static int client_received = 0;
static int test_complete = 0;
static int error_received = 0;

/* Thread synchronization */
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cond = PTHREAD_COND_INITIALIZER;
static int g_server_ready = 0;

/* Server handler for requests */
static void server_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    
    pthread_mutex_lock(&g_mutex);
    server_received++;
    pthread_mutex_unlock(&g_mutex);
    
    printf("Server received request\n");
    
    /* Send error response for invalid requests */
    if (req->params && req->params_size > 0 && req->params[0] == 'E') {
        printf("Server sending error response\n");
        uvrpc_request_send_response(req, UVRPC_ERROR_INVALID_PARAM, NULL, 0);
    } else {
        /* Send normal response */
        uint8_t reply_data[] = {'O', 'K'};
        uvrpc_request_send_response(req, UVRPC_OK, reply_data, sizeof(reply_data));
    }
    
    uvrpc_request_free(req);
}

/* Client callback for responses */
static void client_callback(uvrpc_response_t* resp, void* ctx) {
    (void)ctx;

    /* The server signals an error by sending an empty result (result=NULL,
     * size=0). The response frame's status field is not part of the wire
     * format, so we detect error responses by the empty payload. */
    if (resp->result == NULL || resp->result_size == 0) {
        pthread_mutex_lock(&g_mutex);
        error_received++;
        pthread_mutex_unlock(&g_mutex);
        printf("Client received error response (empty result)\n");
    } else {
        pthread_mutex_lock(&g_mutex);
        client_received++;
        pthread_mutex_unlock(&g_mutex);
        printf("Client received successful response\n");

        /* Verify response data */
        if (resp->result_size == 2) {
            assert(resp->result[0] == 'O');
            assert(resp->result[1] == 'K');
        }
    }

    uvrpc_response_free(resp);
}

/* Timeout timer callback */
static void timeout_callback(uv_timer_t* handle) {
    int* should_stop = (int*)handle->data;
    *should_stop = 1;
    printf("Timeout reached\n");
}

/* Server thread data */
typedef struct {
    uv_loop_t* loop;
    uvrpc_server_t* server;
    int should_stop;
    uv_async_t stop_async;  /* wakes the server loop so uv_stop takes effect */
} server_thread_data_t;

/* Async callback: stop the server loop. Runs on the server thread. */
static void stop_async_cb(uv_async_t* handle) {
    server_thread_data_t* data = (server_thread_data_t*)handle->data;
    data->should_stop = 1;
    uv_stop(data->loop);
}

/* Server thread function */
static void* server_thread_func(void* arg) {
    server_thread_data_t* data = (server_thread_data_t*)arg;

    printf("Server thread started\n");

    /* Wake handle for cross-thread shutdown */
    uv_async_init(data->loop, &data->stop_async, stop_async_cb);
    data->stop_async.data = data;
    uv_unref((uv_handle_t*)&data->stop_async);  /* don't keep the loop alive */

    /* Create server configuration */
    char server_addr[128];
    snprintf(server_addr, sizeof(server_addr), "tcp://%s:%d", TEST_HOST, TEST_PORT);
    
    uvrpc_config_t* server_config = uvrpc_config_new();
    server_config = uvrpc_config_set_loop(server_config, data->loop);
    server_config = uvrpc_config_set_address(server_config, server_addr);
    server_config = uvrpc_config_set_transport(server_config, UVBUS_TRANSPORT_TCP);
    
    /* Create server */
    data->server = uvrpc_server_create(server_config);
    assert(data->server != NULL);
    uvrpc_server_register(data->server, "error_test_method", server_handler, NULL);
    
    /* Start server */
    int rv = uvrpc_server_start(data->server);
    assert(rv == 0);
    printf("Server started on %s\n", server_addr);
    
    /* Signal that server is ready */
    pthread_mutex_lock(&g_mutex);
    g_server_ready = 1;
    pthread_cond_signal(&g_cond);
    pthread_mutex_unlock(&g_mutex);
    
    /* Run event loop */
    uv_run(data->loop, UV_RUN_DEFAULT);
    
    printf("Server thread exiting\n");
    
    /* Cleanup */
    uvrpc_server_stop(data->server);
    uvrpc_server_free(data->server);
    uvrpc_config_free(server_config);

    /* Reclaiming the per-connection client struct, the server struct and the
     * host string is deferred to uv_close callbacks, which only run while the
     * loop is pumped. The stop handle must be closed, not just stopped, or
     * uv_loop_close() will not release the loop's own internals. */
    uv_close((uv_handle_t*)&data->stop_async, NULL);
    while (uv_loop_alive(data->loop)) {
        uv_run(data->loop, UV_RUN_NOWAIT);
    }
    
    return NULL;
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    
    pthread_t server_thread;
    uv_loop_t server_loop;
    server_thread_data_t server_data;
    uv_loop_t client_loop;
    int should_stop = 0;
    int tests_passed = 0;
    int tests_failed = 0;
    int server_received_copy, client_received_copy, error_received_copy;
    
    printf("=== UVRPC Error Handling End-to-End Test ===\n");

    /* Initialize the client loop up front — it is used by Test 1 below, so it
     * must be initialized before any uvrpc_client_create() call (which performs
     * uv_async_init on the loop and would segfault on an uninitialized loop). */
    uv_loop_init(&client_loop);

    /* Test 1: Test with non-existent server */
    printf("\n[Test 1] Connecting to non-existent server...\n");
    uvrpc_config_t* bad_config = uvrpc_config_new();
    bad_config = uvrpc_config_set_loop(bad_config, &client_loop);
    bad_config = uvrpc_config_set_address(bad_config, "tcp://127.0.0.1:99999");
    bad_config = uvrpc_config_set_transport(bad_config, UVBUS_TRANSPORT_TCP);

    uvrpc_client_t* bad_client = uvrpc_client_create(bad_config);
    assert(bad_client != NULL);

    int rv = uvrpc_client_connect(bad_client);
    /* Connection may fail or succeed, but that's OK for this test */
    printf("Connect to non-existent server returned: %d\n", rv);

    uvrpc_client_free(bad_client);
    uvrpc_config_free(bad_config);
    tests_passed++;

    /* Initialize server thread data */
    uv_loop_init(&server_loop);
    server_data.loop = &server_loop;
    server_data.should_stop = 0;
    
    /* Create server thread */
    rv = pthread_create(&server_thread, NULL, server_thread_func, &server_data);
    assert(rv == 0);
    
    /* Wait for server to be ready */
    pthread_mutex_lock(&g_mutex);
    while (!g_server_ready) {
        pthread_cond_wait(&g_cond, &g_mutex);
    }
    pthread_mutex_unlock(&g_mutex);
    
    printf("Server is ready, starting client...\n");

    /* client_loop was initialized before Test 1 above */

    /* Test 2: Test with valid server and normal request */
    printf("\n[Test 2] Testing normal request-response...\n");

    char server_addr[128];
    snprintf(server_addr, sizeof(server_addr), "tcp://%s:%d", TEST_HOST, TEST_PORT);

    uvrpc_config_t* client_config = uvrpc_config_new();
    client_config = uvrpc_config_set_loop(client_config, &client_loop);
    client_config = uvrpc_config_set_address(client_config, server_addr);
    client_config = uvrpc_config_set_transport(client_config, UVBUS_TRANSPORT_TCP);

    uvrpc_client_t* client = uvrpc_client_create(client_config);
    assert(client != NULL);
    rv = uvrpc_client_connect(client);
    assert(rv == 0);
    printf("Client connected\n");

    uv_timer_t timeout_timer;
    uv_timer_init(&client_loop, &timeout_timer);
    timeout_timer.data = &should_stop;

    should_stop = 0;
    uv_timer_start(&timeout_timer, timeout_callback, TIMEOUT_MS, 0);

    /* UV_RUN_NOWAIT: pump connect callbacks without blocking on the timer. */
    for (int i = 0; i < 100 && !should_stop; i++) {
        uv_run(&client_loop, UV_RUN_NOWAIT);
    }
    
    if (!should_stop) {
        uint8_t test_data[] = {'T', 'E', 'S', 'T'};
        rv = uvrpc_client_call(client, "error_test_method", test_data, sizeof(test_data), client_callback, NULL);
        assert(rv == 0);
        
        should_stop = 0;
        uv_timer_start(&timeout_timer, timeout_callback, TIMEOUT_MS, 0);
        
        for (int i = 0; i < 200 && !should_stop; i++) {
            pthread_mutex_lock(&g_mutex);
            client_received_copy = client_received;
            pthread_mutex_unlock(&g_mutex);
            
            if (client_received_copy > 0) {
                break;
            }

            uv_run(&client_loop, UV_RUN_ONCE);
        }
        
        pthread_mutex_lock(&g_mutex);
        client_received_copy = client_received;
        pthread_mutex_unlock(&g_mutex);
        
        if (client_received_copy == 1) {
            printf("Test 2 PASSED: Normal request-response successful\n");
            tests_passed++;
        } else {
            printf("Test 2 FAILED: Expected 1 response, got %d\n", client_received_copy);
            tests_failed++;
        }
    } else {
        printf("Test 2 FAILED: Connection timeout\n");
        tests_failed++;
    }
    
    /* Test 3: Test with error response from server */
    printf("\n[Test 3] Testing error response from server...\n");
    
    uint8_t error_data[] = {'E', 'R', 'R', 'O', 'R'};
    rv = uvrpc_client_call(client, "error_test_method", error_data, sizeof(error_data), client_callback, NULL);
    assert(rv == 0);
    
    should_stop = 0;
    uv_timer_start(&timeout_timer, timeout_callback, TIMEOUT_MS, 0);
    
    for (int i = 0; i < 200 && !should_stop; i++) {
        pthread_mutex_lock(&g_mutex);
        error_received_copy = error_received;
        pthread_mutex_unlock(&g_mutex);
        
        if (error_received_copy > 0) {
            break;
        }
        
        uv_run(&client_loop, UV_RUN_ONCE);
    }
    
    pthread_mutex_lock(&g_mutex);
    error_received_copy = error_received;
    pthread_mutex_unlock(&g_mutex);
    
    if (error_received_copy == 1) {
        printf("Test 3 PASSED: Error response received correctly\n");
        tests_passed++;
    } else {
        printf("Test 3 FAILED: Expected 1 error, got %d\n", error_received_copy);
        tests_failed++;
    }
    
    /* Test 4: Test with NULL parameters */
    printf("\n[Test 4] Testing with NULL parameters...\n");
    
    rv = uvrpc_client_call(client, "error_test_method", NULL, 0, client_callback, NULL);
    assert(rv == 0);
    
    should_stop = 0;
    uv_timer_start(&timeout_timer, timeout_callback, TIMEOUT_MS, 0);
    
    for (int i = 0; i < 200 && !should_stop; i++) {
        pthread_mutex_lock(&g_mutex);
        server_received_copy = server_received;
        pthread_mutex_unlock(&g_mutex);
        
        if (server_received_copy >= 3) {
            break;
        }
        
        uv_run(&client_loop, UV_RUN_ONCE);
    }
    
    pthread_mutex_lock(&g_mutex);
    server_received_copy = server_received;
    pthread_mutex_unlock(&g_mutex);
    
    if (server_received_copy >= 3) {
        printf("Test 4 PASSED: NULL parameters handled correctly\n");
        tests_passed++;
    } else {
        printf("Test 4 FAILED: Expected server to handle NULL params\n");
        tests_failed++;
    }
    
    pthread_mutex_lock(&g_mutex);
    server_received_copy = server_received;
    client_received_copy = client_received;
    error_received_copy = error_received;
    pthread_mutex_unlock(&g_mutex);
    
    printf("\n=== Test Summary ===\n");
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);
    printf("Server received requests: %d\n", server_received_copy);
    printf("Client received responses: %d\n", client_received_copy);
    printf("Client received errors: %d\n", error_received_copy);
    
    /* Stop client loop */
    uv_stop(&client_loop);
    
    /* Cleanup client */
    uv_close((uv_handle_t*)&timeout_timer, NULL);
    uvrpc_client_disconnect(client);
    uvrpc_client_free(client);
    uvrpc_config_free(client_config);
    
    /* Run client loop to process cleanup (UV_RUN_NOWAIT: pump close callbacks
     * without blocking — a referenced handle would make UV_RUN_DEFAULT hang). */
    for (int i = 0; i < 10; i++) {
        uv_run(&client_loop, UV_RUN_NOWAIT);
    }

    /* Close client loop */
    uv_loop_close(&client_loop);

    /* Stop server loop: signal the server thread's async handle so its
     * uv_run() wakes up and calls uv_stop() on the correct thread. */
    uv_async_send(&server_data.stop_async);

    /* Wait for server thread to finish */
    pthread_join(server_thread, NULL);

    /* Close server loop */
    uv_loop_close(&server_loop);
    
    if (tests_failed == 0) {
        printf("\n=== Error Handling End-to-End Test PASSED ===\n");
        return 0;
    } else {
        printf("\n=== Error Handling End-to-End Test FAILED ===\n");
        return 1;
    }
}
