/**
 * UVRPC Async Chain Demo
 * Demonstrates chained async/await calls with dependencies
 */

#include "../include/uvrpc.h"
#include "../include/uvrpc_async.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Handler for get_user */
static void get_user_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    uint32_t user_id;
    memcpy(&user_id, req->params, sizeof(user_id));
    printf("[Server] get_user(%u)\n", user_id);
    
    /* Simulate user data */
    char user_data[64];
    snprintf(user_data, sizeof(user_data), "User%u", user_id);
    uvrpc_request_send_response(req, 0, (uint8_t*)user_data, strlen(user_data));
    uvrpc_request_free(req);
}

/* Handler for get_user_posts */
static void get_user_posts_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    char user_name[64];
    size_t len = req->params_size < sizeof(user_name) ? req->params_size : sizeof(user_name) - 1;
    memcpy(user_name, req->params, len);
    user_name[len] = '\0';
    printf("[Server] get_user_posts(%s)\n", user_name);
    
    /* Simulate posts data */
    char posts_data[128];
    snprintf(posts_data, sizeof(posts_data), "Post1,Post2,Post3 for %s", user_name);
    uvrpc_request_send_response(req, 0, (uint8_t*)posts_data, strlen(posts_data));
    uvrpc_request_free(req);
}

/* Handler for count_posts */
static void count_posts_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    char posts_str[128];
    size_t len = req->params_size < sizeof(posts_str) ? req->params_size : sizeof(posts_str) - 1;
    memcpy(posts_str, req->params, len);
    posts_str[len] = '\0';
    
    /* Count posts */
    int count = 0;
    const char* p = posts_str;
    while (*p) {
        if (*p == ',') count++;
        p++;
    }
    count++; /* at least one post */
    
    printf("[Server] count_posts -> %d\n", count);
    uvrpc_request_send_response(req, 0, (uint8_t*)&count, sizeof(count));
    uvrpc_request_free(req);
}

int main(void) {
    printf("=== UVRPC Async Chain Demo ===\n");
    printf("Demonstrates chained async calls: user -> posts -> count\n\n");
    
    /* Create event loop */
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* Create async context */
    uvrpc_async_ctx_t* async_ctx = uvrpc_async_ctx_new(&loop);
    
    /* Create server */
    printf("1. Creating server...\n");
    uvrpc_config_t* server_config = uvrpc_config_new();
    uvrpc_config_set_loop(server_config, &loop);
    uvrpc_config_set_address(server_config, "inproc://async_chain");
    uvrpc_config_set_transport(server_config, UVRPC_TRANSPORT_INPROC);
    uvrpc_config_set_comm_type(server_config, UVRPC_COMM_SERVER_CLIENT);
    
    uvrpc_server_t* server = uvrpc_server_create(server_config);
    uvrpc_server_register(server, "get_user", get_user_handler, NULL);
    uvrpc_server_register(server, "get_user_posts", get_user_posts_handler, NULL);
    uvrpc_server_register(server, "count_posts", count_posts_handler, NULL);
    uvrpc_server_start(server);
    
    /* Create client */
    printf("2. Creating client...\n");
    uvrpc_config_t* client_config = uvrpc_config_new();
    uvrpc_config_set_loop(client_config, &loop);
    uvrpc_config_set_address(client_config, "inproc://async_chain");
    uvrpc_config_set_transport(client_config, UVRPC_TRANSPORT_INPROC);
    uvrpc_config_set_comm_type(client_config, UVRPC_COMM_SERVER_CLIENT);
    
    uvrpc_client_t* client = uvrpc_client_create(client_config);
    uvrpc_client_connect(client);
    
    /* Run event loop briefly to establish connection */
    for (int i = 0; i < 5; i++) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }
    
    printf("\n3. Chained async calls:\n");
    
    /* Execute async chain */
    UVRPC_ASYNC(async_ctx, 5000) {
        /* Step 1: Get user info */
        uvrpc_async_result_t* user_result = NULL;
        uint32_t user_id = 123;
        printf("  Step 1: get_user(%u)...\n", user_id);
        UVRPC_AWAIT(uvrpc_client_call_async(async_ctx, client, "get_user",
                                               (uint8_t*)&user_id, sizeof(user_id), &user_result));
        
        if (user_result && user_result->error_code == 0) {
            printf("  -> User: %.*s\n", (int)user_result->result_size, user_result->result);
            
            /* Step 2: Get user's posts (depends on Step 1) */
            uvrpc_async_result_t* posts_result = NULL;
            printf("  Step 2: get_user_posts()...\n");
            UVRPC_AWAIT(uvrpc_client_call_async(async_ctx, client, "get_user_posts",
                                                   user_result->result, user_result->result_size, &posts_result));
            
            if (posts_result && posts_result->error_code == 0) {
                printf("  -> Posts: %.*s\n", (int)posts_result->result_size, posts_result->result);
                
                /* Step 3: Count posts (depends on Step 2) */
                uvrpc_async_result_t* count_result = NULL;
                printf("  Step 3: count_posts()...\n");
                UVRPC_AWAIT(uvrpc_client_call_async(async_ctx, client, "count_posts",
                                                       posts_result->result, posts_result->result_size, &count_result));
                
                if (count_result && count_result->error_code == 0) {
                    int post_count = *(int*)count_result->result;
                    printf("  -> Total posts: %d\n", post_count);
                    printf("\n  Chain completed successfully!\n");
                }
                
                if (count_result) uvrpc_async_result_free(count_result);
            }
            
            if (posts_result) uvrpc_async_result_free(posts_result);
        }
        
        if (user_result) uvrpc_async_result_free(user_result);
    }

async_cleanup:
    /* Cleanup */
    printf("\n4. Cleanup...\n");
    uvrpc_client_free(client);
    uvrpc_server_free(server);
    uvrpc_config_free(client_config);
    uvrpc_config_free(server_config);
    uvrpc_async_ctx_free(async_ctx);
    uv_loop_close(&loop);
    
    printf("\n=== Demo Complete ===\n");
    return 0;
}