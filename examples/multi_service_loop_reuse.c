/**
 * Example: Multiple Services Sharing a Single Event Loop
 * 
 * This example demonstrates how to run multiple UVRPC services
 * in a single libuv event loop using the code generation mode.
 * 
 * The key insight is that each service gets its own function prefix
 * (e.g., uvrpc_mathservice_*, uvrpc_echoservice_*), so there's no
 * naming conflict when running multiple services in the same loop.
 */

#include <stdio.h>
#include <stdlib.h>
#include <uv.h>
#include "generated/rpc_mathservice_api.h"
#include "generated/rpc_echoservice_api.h"
#include "generated/rpc_userservice_api.h"

/* MathService handler implementation */
int uvrpc_mathservice_handle_request(const char* method_name, 
                                     const void* request,
                                     uvrpc_request_t* req) {
    printf("MathService: Received method '%s'\n", method_name);
    
    /* Parse request based on method_name */
    if (strcmp(method_name, "Add") == 0) {
        rpc_MathAddRequest_table_t add_req = rpc_MathAddRequest_as_root(request);
        int32_t result = rpc_MathAddRequest_a(add_req) + rpc_MathAddRequest_b(add_req);
        
        /* Build response */
        flatcc_builder_t builder;
        flatcc_builder_init(&builder);
        rpc_MathAddResponse_start_as_root(&builder);
        rpc_MathAddResponse_result_add(&builder, result);
        rpc_MathAddResponse_end_as_root(&builder);
        
        size_t size;
        void* buf = flatcc_builder_finalize_buffer(&builder, &size);
        
        uvrpc_request_send_response(req, UVRPC_OK, buf, size);
        
        flatcc_builder_reset(&builder);
    }
    else if (strcmp(method_name, "Subtract") == 0) {
        rpc_MathSubtractRequest_table_t sub_req = rpc_MathSubtractRequest_as_root(request);
        int32_t result = rpc_MathSubtractRequest_a(sub_req) - rpc_MathSubtractRequest_b(sub_req);
        
        /* Build response */
        flatcc_builder_t builder;
        flatcc_builder_init(&builder);
        rpc_MathSubtractResponse_start_as_root(&builder);
        rpc_MathSubtractResponse_result_add(&builder, result);
        rpc_MathSubtractResponse_end_as_root(&builder);
        
        size_t size;
        void* buf = flatcc_builder_finalize_buffer(&builder, &size);
        
        uvrpc_request_send_response(req, UVRPC_OK, buf, size);
        
        flatcc_builder_reset(&builder);
    }
    else {
        uvrpc_request_send_response(req, UVRPC_ERROR_NOT_FOUND, NULL, 0);
    }
    
    return 0;
}

/* EchoService handler implementation */
int uvrpc_echoservice_handle_request(const char* method_name, 
                                     const void* request,
                                     uvrpc_request_t* req) {
    printf("EchoService: Received method '%s'\n", method_name);
    
    if (strcmp(method_name, "Echo") == 0) {
        rpc_EchoRequest_table_t echo_req = rpc_EchoRequest_as_root(request);
        const char* message = rpc_EchoRequest_message(echo_req);
        
        /* Build response */
        flatcc_builder_t builder;
        flatcc_builder_init(&builder);
        rpc_EchoResponse_start_as_root(&builder);
        rpc_EchoResponse_message_create_str(&builder, message);
        rpc_EchoResponse_timestamp_add(&builder, rpc_EchoRequest_timestamp(echo_req));
        rpc_EchoResponse_server_time_add(&builder, (int64_t)time(NULL));
        rpc_EchoResponse_end_as_root(&builder);
        
        size_t size;
        void* buf = flatcc_builder_finalize_buffer(&builder, &size);
        
        uvrpc_request_send_response(req, UVRPC_OK, buf, size);
        
        flatcc_builder_reset(&builder);
    }
    else {
        uvrpc_request_send_response(req, UVRPC_ERROR_NOT_FOUND, NULL, 0);
    }
    
    return 0;
}

/* UserService handler implementation */
int uvrpc_userservice_handle_request(const char* method_name, 
                                    const void* request,
                                    uvrpc_request_t* req) {
    printf("UserService: Received method '%s'\n", method_name);
    
    if (strcmp(method_name, "GetUser") == 0) {
        rpc_UserGetRequest_table_t get_req = rpc_UserGetRequest_as_root(request);
        int64_t user_id = rpc_UserGetRequest_user_id(get_req);
        
        /* Build response */
        flatcc_builder_t builder;
        flatcc_builder_init(&builder);
        rpc_UserGetResponse_start_as_root(&builder);
        rpc_UserGetResponse_user_id_add(&builder, user_id);
        rpc_UserGetResponse_name_create_str(&builder, "John Doe");
        rpc_UserGetResponse_email_create_str(&builder, "john@example.com");
        rpc_UserGetResponse_age_add(&builder, 30);
        rpc_UserGetResponse_end_as_root(&builder);
        
        size_t size;
        void* buf = flatcc_builder_finalize_buffer(&builder, &size);
        
        uvrpc_request_send_response(req, UVRPC_OK, buf, size);
        
        flatcc_builder_reset(&builder);
    }
    else {
        uvrpc_request_send_response(req, UVRPC_ERROR_NOT_FOUND, NULL, 0);
    }
    
    return 0;
}

int main() {
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    printf("Starting multiple services in a single event loop...\n");
    
    /* Create and start MathService */
    uvrpc_server_t* math_server = uvrpc_mathservice_create_server(&loop, "tcp://127.0.0.1:5555");
    if (math_server) {
        uvrpc_mathservice_start_server(math_server);
        printf("MathService started on tcp://127.0.0.1:5555\n");
    }
    
    /* Create and start EchoService */
    uvrpc_server_t* echo_server = uvrpc_echoservice_create_server(&loop, "tcp://127.0.0.1:5556");
    if (echo_server) {
        uvrpc_echoservice_start_server(echo_server);
        printf("EchoService started on tcp://127.0.0.1:5556\n");
    }
    
    /* Create and start UserService */
    uvrpc_server_t* user_server = uvrpc_userservice_create_server(&loop, "tcp://127.0.0.1:5557");
    if (user_server) {
        uvrpc_userservice_start_server(user_server);
        printf("UserService started on tcp://127.0.0.1:5557\n");
    }
    
    printf("\nAll services running in a single event loop.\n");
    printf("Press Ctrl+C to stop.\n\n");
    
    /* Run all services in a single event loop */
    uv_run(&loop, UV_RUN_DEFAULT);
    
    /* Cleanup */
    printf("\nStopping services...\n");
    uvrpc_mathservice_free_server(math_server);
    uvrpc_echoservice_free_server(echo_server);
    uvrpc_userservice_free_server(user_server);
    
    uv_loop_close(&loop);
    
    printf("All services stopped.\n");
    return 0;
}