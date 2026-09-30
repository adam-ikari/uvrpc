/**
 * UVRPC Connection Reconnection End-to-End Integration Test
 * Tests client reconnection after server restart
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <uv.h>
#include <pthread.h>
#include <unistd.h>  /* usleep */
#include "../../include/uvrpc.h"

#define TEST_PORT 15558
#define TEST_HOST "127.0.0.1"
#define TIMEOUT_MS 5000

static int server_received = 0;
static int client_received = 0;
static int test_complete = 0;

/* Thread synchronization */
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cond = PTHREAD_COND_INITIALIZER;
static int g_server_ready = 0;
static int g_server_should_restart = 0;
static int g_server_restarted = 0;

/* Server handler for requests */
static void server_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    
    pthread_mutex_lock(&g_mutex);
    server_received++;
    pthread_mutex_unlock(&g_mutex);
    
    printf("Server received request (instance %d)\n", server_received);
    
    /* Send response */
    uint8_t reply_data[] = {'R', 'E', 'C', 'O', 'N', 'N', 'E', 'C', 'T', '!', 'O', 'K'};
    uvrpc_request_send_response(req, 0, reply_data, sizeof(reply_data));
    
    uvrpc_request_free(req);
}

/* Client callback for responses */
static void client_callback(uvrpc_response_t* resp, void* ctx) {
    (void)ctx;
    
    pthread_mutex_lock(&g_mutex);
    client_received++;
    pthread_mutex_unlock(&g_mutex);
    
    printf("Client received response (%d)\n", client_received);
    
    /* Verify response data */
    if (resp->result && resp->result_size == 12) {
        assert(resp->result[0] == 'R');
        assert(resp->result[1] == 'E');
        assert(resp->result[2] == 'C');
        assert(resp->result[3] == 'O');
        assert(resp->result[4] == 'N');
        assert(resp->result[5] == 'N');
        assert(resp->result[6] == 'E');
        assert(resp->result[7] == 'C');
        assert(resp->result[8] == 'T');
        assert(resp->result[9] == '!');
        assert(resp->result[10] == 'O');
        assert(resp->result[11] == 'K');
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

/* Async callback: stop the server loop (breaks the current uv_run so the
 * restart loop can re-evaluate g_server_should_restart). Runs on the server
 * thread. */
static void stop_async_cb(uv_async_t* handle) {
    server_thread_data_t* data = (server_thread_data_t*)handle->data;
    data->should_stop = 1;
    uv_stop(data->loop);
}

/* Reclaiming the per-connection client struct, the server struct and the host
 * string is deferred to uv_close callbacks, which only run while the loop is
 * pumped. A single client struct alone is 256 KB, and this test rebuilds the
 * server several times, so an undrained free leaks the whole set each round. */
static void drain_server_loop(server_thread_data_t* data) {
    while (uv_loop_alive(data->loop)) {
        uv_run(data->loop, UV_RUN_NOWAIT);
    }
}

static void* server_thread_func(void* arg) {
    server_thread_data_t* data = (server_thread_data_t*)arg;
    int restart_count = 0;

    printf("Server thread started\n");

    /* Wake handle for cross-thread shutdown (bound to the persistent loop,
     * reused across server restarts). */
    uv_async_init(data->loop, &data->stop_async, stop_async_cb);
    data->stop_async.data = data;
    uv_unref((uv_handle_t*)&data->stop_async);  /* don't keep the loop alive */

    while (1) {
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
        
        /* Register server handler */
        uvrpc_server_register(data->server, "reconnect_test_method", server_handler, NULL);
        
        /* Start server */
        int rv = uvrpc_server_start(data->server);
        assert(rv == 0);
        printf("Server started on %s (restart %d)\n", server_addr, restart_count);
        
        /* Signal that server is ready */
        pthread_mutex_lock(&g_mutex);
        g_server_ready = 1;
        g_server_restarted = restart_count;
        pthread_cond_signal(&g_cond);
        pthread_mutex_unlock(&g_mutex);
        
        /* Run event loop until stop is requested */
        uv_run(data->loop, UV_RUN_DEFAULT);
        
        /* Check if we should exit or restart */
        pthread_mutex_lock(&g_mutex);
        int should_exit = !g_server_should_restart;
        pthread_mutex_unlock(&g_mutex);
        
        if (should_exit) {
            /* Cleanup and exit */
            uvrpc_server_stop(data->server);
            uvrpc_server_free(data->server);
            uvrpc_config_free(server_config);
            drain_server_loop(data);
            break;
        }
        
        /* Restart sequence */
        printf("Server restarting...\n");
        uvrpc_server_stop(data->server);
        uvrpc_server_free(data->server);
        uvrpc_config_free(server_config);
        drain_server_loop(data);
        
        /* Reset server ready flag */
        pthread_mutex_lock(&g_mutex);
        g_server_ready = 0;
        g_server_should_restart = 0;
        pthread_mutex_unlock(&g_mutex);
        
        restart_count++;
    }
    
    printf("Server thread exiting\n");
    /* Close, not just stop: uv_loop_close() will not release the loop's
     * internals while a handle is still open. */
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
    int server_received_copy, client_received_copy;
    
    printf("=== UVRPC Connection Reconnection End-to-End Test ===\n");
    
    /* Initialize server thread data */
    uv_loop_init(&server_loop);
    server_data.loop = &server_loop;
    server_data.should_stop = 0;
    
    /* Create server thread */
    int rv = pthread_create(&server_thread, NULL, server_thread_func, &server_data);
    assert(rv == 0);
    
    /* Wait for server to be ready */
    pthread_mutex_lock(&g_mutex);
    while (!g_server_ready) {
        pthread_cond_wait(&g_cond, &g_mutex);
    }
    pthread_mutex_unlock(&g_mutex);
    
    printf("Server is ready, starting client...\n");
    
    /* Initialize client loop */
    uv_loop_init(&client_loop);
    
    /* Create client configuration */
    char client_addr[128];
    snprintf(client_addr, sizeof(client_addr), "tcp://%s:%d", TEST_HOST, TEST_PORT);
    
    uvrpc_config_t* client_config = uvrpc_config_new();
    client_config = uvrpc_config_set_loop(client_config, &client_loop);
    client_config = uvrpc_config_set_address(client_config, client_addr);
    client_config = uvrpc_config_set_transport(client_config, UVBUS_TRANSPORT_TCP);
    
    /* Create client */
    uvrpc_client_t* client = uvrpc_client_create(client_config);
    assert(client != NULL);
    
    /* Connect client */
    rv = uvrpc_client_connect(client);
    assert(rv == 0);
    printf("Client connecting...\n");
    
    /* Setup timeout timer */
    uv_timer_t timeout_timer;
    uv_timer_init(&client_loop, &timeout_timer);
    timeout_timer.data = &should_stop;
    
    /* Run event loop to establish connection */
    should_stop = 0;
    uv_timer_start(&timeout_timer, timeout_callback, TIMEOUT_MS, 0);
    
    for (int i = 0; i < 100 && !should_stop; i++) {
        uv_run(&client_loop, UV_RUN_NOWAIT);
    }
    
    if (should_stop) {
        printf("ERROR: Initial connection timeout\n");
        uv_close((uv_handle_t*)&timeout_timer, NULL);
        uv_async_send(&server_data.stop_async);
        pthread_join(server_thread, NULL);
        uv_loop_close(&server_loop);
        uv_loop_close(&client_loop);
        return 1;
    }
    
    printf("Making first RPC call...\n");
    
    /* Client makes first RPC call */
    uint8_t test_data[] = {'T', 'E', 'S', 'T'};
    rv = uvrpc_client_call(client, "reconnect_test_method", test_data, sizeof(test_data), client_callback, NULL);
    assert(rv == 0);
    
    /* Run event loop to process first RPC */
    should_stop = 0;
    uv_timer_start(&timeout_timer, timeout_callback, TIMEOUT_MS, 0);
    
    for (int i = 0; i < 200 && !should_stop; i++) {
        pthread_mutex_lock(&g_mutex);
        server_received_copy = server_received;
        pthread_mutex_unlock(&g_mutex);
        
        if (server_received_copy > 0) {
            break;
        }
        
        uv_run(&client_loop, UV_RUN_ONCE);
    }
    
    pthread_mutex_lock(&g_mutex);
    server_received_copy = server_received;
    pthread_mutex_unlock(&g_mutex);
    
    if (server_received_copy == 0) {
        printf("ERROR: Server did not receive first request\n");
        uv_close((uv_handle_t*)&timeout_timer, NULL);
        uv_async_send(&server_data.stop_async);
        pthread_join(server_thread, NULL);
        uv_loop_close(&server_loop);
        uv_loop_close(&client_loop);
        return 1;
    }
    
    /* Run event loop to receive first response */
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
    server_received_copy = server_received;
    client_received_copy = client_received;
    pthread_mutex_unlock(&g_mutex);
    
    if (client_received_copy == 0) {
        printf("ERROR: Client did not receive first response\n");
        uv_close((uv_handle_t*)&timeout_timer, NULL);
        uv_async_send(&server_data.stop_async);
        pthread_join(server_thread, NULL);
        uv_loop_close(&server_loop);
        uv_loop_close(&client_loop);
        return 1;
    }
    
    printf("First RPC call successful. Disconnecting client...\n");

    /* Disconnect client */
    uvrpc_client_disconnect(client);

    /* Signal server to restart, THEN wake it. The flag must be set before the
     * async wake so that when the server's uv_run() breaks and it checks
     * g_server_should_restart, it sees 1 (restart) rather than 0 (exit). */
    pthread_mutex_lock(&g_mutex);
    g_server_should_restart = 1;
    g_server_ready = 0;
    pthread_mutex_unlock(&g_mutex);

    /* Wake the server loop so uv_run() returns and the restart is evaluated. */
    uv_async_send(&server_data.stop_async);

    /* Wait for server to stop */
    usleep(100000); /* 100ms */

    printf("Server stopped. Restarting server...\n");

    /* Wait for server to restart */
    pthread_mutex_lock(&g_mutex);
    while (!g_server_ready) {
        pthread_cond_wait(&g_cond, &g_mutex);
    }
    pthread_mutex_unlock(&g_mutex);
    
    printf("Server restarted. Reconnecting client...\n");
    
    /* Reconnect client */
    rv = uvrpc_client_connect(client);
    assert(rv == 0);
    
    /* Run event loop to establish reconnection */
    should_stop = 0;
    uv_timer_start(&timeout_timer, timeout_callback, TIMEOUT_MS, 0);
    
    for (int i = 0; i < 100 && !should_stop; i++) {
        uv_run(&client_loop, UV_RUN_NOWAIT);
    }
    
    if (should_stop) {
        printf("ERROR: Reconnection timeout\n");
        uv_close((uv_handle_t*)&timeout_timer, NULL);
        uv_async_send(&server_data.stop_async);
        pthread_join(server_thread, NULL);
        uv_loop_close(&server_loop);
        uv_loop_close(&client_loop);
        return 1;
    }
    
    printf("Making second RPC call after reconnection...\n");
    
    /* Client makes second RPC call after reconnection */
    rv = uvrpc_client_call(client, "reconnect_test_method", test_data, sizeof(test_data), client_callback, NULL);
    assert(rv == 0);
    
    /* Run event loop to process second RPC */
    should_stop = 0;
    uv_timer_start(&timeout_timer, timeout_callback, TIMEOUT_MS, 0);
    
    for (int i = 0; i < 200 && !should_stop; i++) {
        pthread_mutex_lock(&g_mutex);
        server_received_copy = server_received;
        pthread_mutex_unlock(&g_mutex);
        
        if (server_received_copy >= 2) {
            break;
        }
        
        uv_run(&client_loop, UV_RUN_ONCE);
    }
    
    pthread_mutex_lock(&g_mutex);
    server_received_copy = server_received;
    pthread_mutex_unlock(&g_mutex);
    
    if (server_received_copy < 2) {
        printf("ERROR: Server did not receive second request\n");
        uv_close((uv_handle_t*)&timeout_timer, NULL);
        uv_async_send(&server_data.stop_async);
        pthread_join(server_thread, NULL);
        uv_loop_close(&server_loop);
        uv_loop_close(&client_loop);
        return 1;
    }
    
    /* Run event loop to receive second response */
    should_stop = 0;
    uv_timer_start(&timeout_timer, timeout_callback, TIMEOUT_MS, 0);
    
    for (int i = 0; i < 200 && !should_stop; i++) {
        pthread_mutex_lock(&g_mutex);
        client_received_copy = client_received;
        pthread_mutex_unlock(&g_mutex);
        
        if (client_received_copy >= 2) {
            break;
        }
        
        uv_run(&client_loop, UV_RUN_ONCE);
    }
    
    pthread_mutex_lock(&g_mutex);
    server_received_copy = server_received;
    client_received_copy = client_received;
    pthread_mutex_unlock(&g_mutex);
    
    if (client_received_copy < 2) {
        printf("ERROR: Client did not receive second response\n");
    }
    
    printf("\n=== Test Results ===\n");
    printf("Server received requests: %d\n", server_received_copy);
    printf("Client received responses: %d\n", client_received_copy);
    printf("Expected: 2 requests and 2 responses\n");
    
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
    
    /* Stop server loop */
    uv_async_send(&server_data.stop_async);
    
    /* Wait for server thread to finish */
    pthread_join(server_thread, NULL);
    
    /* Close server loop */
    uv_loop_close(&server_loop);
    
    /* Verify test results */
    if (server_received_copy == 2 && client_received_copy == 2) {
        printf("\n=== Reconnection End-to-End Test PASSED ===\n");
        return 0;
    } else {
        printf("\n=== Reconnection End-to-End Test FAILED ===\n");
        return 1;
    }
}
