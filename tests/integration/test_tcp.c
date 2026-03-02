/**
 * UVRPC TCP End-to-End Integration Test
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <uv.h>
#include <pthread.h>
#include "../../include/uvrpc.h"

#define TEST_PORT 15555
#define TEST_HOST "127.0.0.1"
#define TIMEOUT_MS 10000

static int server_received = 0;
static int client_received = 0;
static int test_complete = 0;

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
    
    /* Verify request data */
    if (req->params && req->params_size == 4) {
        assert(req->params[0] == 'T');
        assert(req->params[1] == 'E');
        assert(req->params[2] == 'S');
        assert(req->params[3] == 'T');
    }
    
    /* Send response */
    uint8_t reply_data[] = {'R', 'E', 'P', 'L', 'Y', '!'};
    uvrpc_request_send_response(req, 0, reply_data, sizeof(reply_data));
    
    uvrpc_request_free(req);
}

/* Client callback for responses */
static void client_callback(uvrpc_response_t* resp, void* ctx) {
    (void)ctx;
    
    pthread_mutex_lock(&g_mutex);
    client_received++;
    pthread_mutex_unlock(&g_mutex);
    
    printf("Client received response\n");
    
    /* Verify response data */
    if (resp->result && resp->result_size == 6) {
        assert(resp->result[0] == 'R');
        assert(resp->result[1] == 'E');
        assert(resp->result[2] == 'P');
        assert(resp->result[3] == 'L');
        assert(resp->result[4] == 'Y');
        assert(resp->result[5] == '!');
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
} server_thread_data_t;

/* Server thread function */
static void* server_thread_func(void* arg) {
    server_thread_data_t* data = (server_thread_data_t*)arg;
    
    printf("Server thread started\n");
    
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
    uvrpc_server_register(data->server, "test_method", server_handler, NULL);
    
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
    
    return NULL;
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    
    pthread_t server_thread;
    server_thread_data_t server_data;
    uv_loop_t client_loop;
    int should_stop = 0;
    int server_received_copy, client_received_copy;
    
    printf("=== UVRPC TCP End-to-End Test ===\n");
    
    /* Initialize server loop */
    uv_loop_t server_loop;
    uv_loop_init(&server_loop);
    
    /* Initialize server thread data */
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
    uv_timer_start(&timeout_timer, timeout_callback, TIMEOUT_MS, 0);
    
    /* Run event loop to establish connection */
    for (int i = 0; i < 100 && !should_stop; i++) {
        uv_run(&client_loop, UV_RUN_NOWAIT);
    }
    
    printf("Making RPC call...\n");
    
    /* Client makes RPC call */
    uint8_t test_data[] = {'T', 'E', 'S', 'T'};
    rv = uvrpc_client_call(client, "test_method", test_data, sizeof(test_data), client_callback, NULL);
    assert(rv == 0);
    printf("Client called test_method\n");
    
    /* Run event loop to process RPC */
    should_stop = 0;
    uv_timer_start(&timeout_timer, timeout_callback, TIMEOUT_MS, 0);
    
    while (!should_stop) {
        pthread_mutex_lock(&g_mutex);
        server_received_copy = server_received;
        pthread_mutex_unlock(&g_mutex);
        
        if (server_received_copy > 0) {
            break;
        }
        
        uv_run(&client_loop, UV_RUN_NOWAIT);
    }
    
    pthread_mutex_lock(&g_mutex);
    server_received_copy = server_received;
    pthread_mutex_unlock(&g_mutex);
    
    if (server_received_copy == 0) {
        printf("ERROR: Server did not receive request\n");
    }
    
    /* Run event loop to receive response */
    should_stop = 0;
    uv_timer_start(&timeout_timer, timeout_callback, TIMEOUT_MS, 0);
    
    while (!should_stop) {
        pthread_mutex_lock(&g_mutex);
        client_received_copy = client_received;
        pthread_mutex_unlock(&g_mutex);
        
        if (client_received_copy > 0) {
            break;
        }
        
        uv_run(&client_loop, UV_RUN_NOWAIT);
    }
    
    pthread_mutex_lock(&g_mutex);
    server_received_copy = server_received;
    client_received_copy = client_received;
    pthread_mutex_unlock(&g_mutex);
    
    if (client_received_copy == 0) {
        printf("ERROR: Client did not receive response\n");
    }
    
    printf("\n=== Test Results ===\n");
    printf("Server received requests: %d\n", server_received_copy);
    printf("Client received responses: %d\n", client_received_copy);
    
    /* Stop client loop */
    uv_stop(&client_loop);
    
    /* Cleanup client */
    uv_close((uv_handle_t*)&timeout_timer, NULL);
    uvrpc_client_disconnect(client);
    uvrpc_client_free(client);
    uvrpc_config_free(client_config);
    
    /* Run client loop to process cleanup */
    for (int i = 0; i < 10; i++) {
        uv_run(&client_loop, UV_RUN_NOWAIT);
    }
    
    /* Close client loop */
    uv_loop_close(&client_loop);
    
    /* Stop server loop */
    uv_stop(&server_loop);
    
    /* Wait for server thread to finish */
    pthread_join(server_thread, NULL);
    
    /* Close server loop */
    uv_loop_close(&server_loop);
    
    /* Verify test results */
    if (server_received_copy == 1 && client_received_copy == 1) {
        printf("\n=== TCP End-to-End Test PASSED ===\n");
        return 0;
    } else {
        printf("\n=== TCP End-to-End Test FAILED ===\n");
        return 1;
    }
}