/**
 * UVRPC Multi-Client End-to-End Integration Test
 * Tests server handling multiple concurrent clients
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <uv.h>
#include <pthread.h>
#include "../../include/uvrpc.h"

#define TEST_PORT 15557
#define TEST_HOST "127.0.0.1"
#define NUM_CLIENTS 5
#define REQUESTS_PER_CLIENT 3
#define TIMEOUT_MS 10000

static int server_received = 0;
static int client_received = 0;
static int test_complete = 0;

/* Thread synchronization */
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cond = PTHREAD_COND_INITIALIZER;
static int g_server_ready = 0;

/* Client-specific data */
typedef struct {
    int client_id;
    int requests_sent;
    int responses_received;
} client_data_t;

/* Server handler for requests */
static void server_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    
    pthread_mutex_lock(&g_mutex);
    server_received++;
    pthread_mutex_unlock(&g_mutex);
    
    /* Verify request data */
    if (req->params && req->params_size >= 4) {
        int client_id = req->params[0];
        printf("Server received request from client %d\n", client_id);
    }
    
    /* Send response */
    uint8_t reply_data[] = {'O', 'K'};
    uvrpc_request_send_response(req, 0, reply_data, sizeof(reply_data));
    
    uvrpc_request_free(req);
}

/* Client callback for responses */
static void client_callback(uvrpc_response_t* resp, void* ctx) {
    client_data_t* data = (client_data_t*)ctx;
    
    pthread_mutex_lock(&g_mutex);
    data->responses_received++;
    client_received++;
    pthread_mutex_unlock(&g_mutex);
    
    printf("Client %d received response (%d/%d)\n", 
           data->client_id, data->responses_received, REQUESTS_PER_CLIENT);
    
    /* Verify response data */
    if (resp->result && resp->result_size == 2) {
        assert(resp->result[0] == 'O');
        assert(resp->result[1] == 'K');
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
    uvrpc_server_register(data->server, "multi_test_method", server_handler, NULL);
    
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
    uv_loop_t server_loop;
    server_thread_data_t server_data;
    uv_loop_t client_loop;
    int should_stop = 0;
    int server_received_copy, client_received_copy;
    
    printf("=== UVRPC Multi-Client End-to-End Test ===\n");
    printf("Testing with %d clients, %d requests each\n", NUM_CLIENTS, REQUESTS_PER_CLIENT);
    
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
    
    printf("Server is ready, starting clients...\n");
    
    /* Initialize client loop */
    uv_loop_init(&client_loop);
    
    /* Create multiple clients */
    uvrpc_client_t* clients[NUM_CLIENTS];
    client_data_t client_data[NUM_CLIENTS];
    
    for (int i = 0; i < NUM_CLIENTS; i++) {
        char client_addr[128];
        snprintf(client_addr, sizeof(client_addr), "tcp://%s:%d", TEST_HOST, TEST_PORT);
        
        uvrpc_config_t* client_config = uvrpc_config_new();
        client_config = uvrpc_config_set_loop(client_config, &client_loop);
        client_config = uvrpc_config_set_address(client_config, client_addr);
        client_config = uvrpc_config_set_transport(client_config, UVBUS_TRANSPORT_TCP);
        
        /* Create client */
        clients[i] = uvrpc_client_create(client_config);
        assert(clients[i] != NULL);
        
        /* Initialize client data */
        client_data[i].client_id = i;
        client_data[i].requests_sent = 0;
        client_data[i].responses_received = 0;
        
        /* Connect client */
        rv = uvrpc_client_connect(clients[i]);
        assert(rv == 0);
        printf("Client %d connecting...\n", i);
        
        uvrpc_config_free(client_config);
    }
    
    /* Setup timeout timer */
    uv_timer_t timeout_timer;
    uv_timer_init(&client_loop, &timeout_timer);
    timeout_timer.data = &should_stop;
    
    /* Run event loop to establish connections */
    should_stop = 0;
    uv_timer_start(&timeout_timer, timeout_callback, TIMEOUT_MS, 0);
    
    for (int i = 0; i < 100 && !should_stop; i++) {
        uv_run(&client_loop, UV_RUN_NOWAIT);
    }
    
    if (should_stop) {
        printf("ERROR: Connection timeout\n");
        uv_close((uv_handle_t*)&timeout_timer, NULL);
        uv_stop(&server_loop);
        pthread_join(server_thread, NULL);
        uv_loop_close(&server_loop);
        uv_loop_close(&client_loop);
        return 1;
    }
    
    printf("All clients connected, making requests...\n");
    
    /* All clients make multiple requests */
    for (int i = 0; i < NUM_CLIENTS; i++) {
        for (int j = 0; j < REQUESTS_PER_CLIENT; j++) {
            uint8_t test_data[] = {(uint8_t)i, (uint8_t)j};
            rv = uvrpc_client_call(clients[i], "multi_test_method", test_data, 
                                  sizeof(test_data), client_callback, &client_data[i]);
            assert(rv == 0);
            client_data[i].requests_sent++;
        }
    }
    
    printf("Sent %d total requests\n", NUM_CLIENTS * REQUESTS_PER_CLIENT);
    
    /* Run event loop to process all requests and responses */
    should_stop = 0;
    uv_timer_start(&timeout_timer, timeout_callback, TIMEOUT_MS, 0);
    
    int expected_responses = NUM_CLIENTS * REQUESTS_PER_CLIENT;
    for (int i = 0; i < 500 && !should_stop; i++) {
        pthread_mutex_lock(&g_mutex);
        client_received_copy = client_received;
        pthread_mutex_unlock(&g_mutex);
        
        if (client_received_copy >= expected_responses) {
            break;
        }
        
        uv_run(&client_loop, UV_RUN_NOWAIT);
    }
    
    pthread_mutex_lock(&g_mutex);
    server_received_copy = server_received;
    client_received_copy = client_received;
    pthread_mutex_unlock(&g_mutex);
    
    printf("\n=== Test Results ===\n");
    printf("Server received requests: %d\n", server_received_copy);
    printf("Client received responses: %d\n", client_received_copy);
    printf("Expected: %d\n", expected_responses);
    
    printf("\nPer-client breakdown:\n");
    for (int i = 0; i < NUM_CLIENTS; i++) {
        printf("  Client %d: sent %d, received %d\n", 
               i, client_data[i].requests_sent, client_data[i].responses_received);
    }
    
    /* Stop client loop */
    uv_stop(&client_loop);
    
    /* Cleanup client */
    uv_close((uv_handle_t*)&timeout_timer, NULL);
    
    for (int i = 0; i < NUM_CLIENTS; i++) {
        uvrpc_client_disconnect(clients[i]);
        uvrpc_client_free(clients[i]);
    }
    
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
    if (server_received_copy == expected_responses && client_received_copy == expected_responses) {
        printf("\n=== Multi-Client End-to-End Test PASSED ===\n");
        return 0;
    } else {
        printf("\n=== Multi-Client End-to-End Test FAILED ===\n");
        return 1;
    }
}

