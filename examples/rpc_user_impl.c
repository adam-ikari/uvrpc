/*
 * UVRPC User Implementation File
 * 
 * This file contains YOUR implementation of RPC service logic.
 * This file will NOT be overwritten by code generation.
 * 
 * Edit this file to implement your business logic.
 */

#include "rpc_api.h"
#include <stdio.h>
#include <string.h>

/**
 * Handle all RPC requests
 * 
 * This function is called by the auto-generated RPC handler.
 * Implement your business logic here.
 */
int rpc_handle_request(const char* method_name, 
                      const void* request,
                      uvrpc_request_t* req) {
    
    printf("[User Handler] Method: %s\n", method_name);
    
    if (strcmp(method_name, "Add") == 0) {
        /* MathService.Add */
        rpc_MathAddRequest_table_t add_req = rpc_MathAddRequest_as_root(request);
        if (!add_req) return UVRPC_ERROR_INVALID_PARAM;
        
        int32_t a = rpc_MathAddRequest_a(add_req);
        int32_t b = rpc_MathAddRequest_b(add_req);
        int32_t result = a + b;
        
        printf("[MathService] Add: %d + %d = %d\n", a, b, result);
        
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
        return 0;
    }
    
    else if (strcmp(method_name, "Subtract") == 0) {
        /* MathService.Subtract */
        rpc_MathSubtractRequest_table_t sub_req = rpc_MathSubtractRequest_as_root(request);
        if (!sub_req) return UVRPC_ERROR_INVALID_PARAM;
        
        int32_t a = rpc_MathSubtractRequest_a(sub_req);
        int32_t b = rpc_MathSubtractRequest_b(sub_req);
        int32_t result = a - b;
        
        printf("[MathService] Subtract: %d - %d = %d\n", a, b, result);
        
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
        return 0;
    }
    
    else if (strcmp(method_name, "Multiply") == 0) {
        /* MathService.Multiply */
        rpc_MathMultiplyRequest_table_t mul_req = rpc_MathMultiplyRequest_as_root(request);
        if (!mul_req) return UVRPC_ERROR_INVALID_PARAM;
        
        int32_t a = rpc_MathMultiplyRequest_a(mul_req);
        int32_t b = rpc_MathMultiplyRequest_b(mul_req);
        int32_t result = a * b;
        
        printf("[MathService] Multiply: %d * %d = %d\n", a, b, result);
        
        /* Build response */
        flatcc_builder_t builder;
        flatcc_builder_init(&builder);
        
        rpc_MathMultiplyResponse_start_as_root(&builder);
        rpc_MathMultiplyResponse_result_add(&builder, result);
        rpc_MathMultiplyResponse_end_as_root(&builder);
        
        size_t size;
        void* buf = flatcc_builder_finalize_buffer(&builder, &size);
        
        uvrpc_request_send_response(req, UVRPC_OK, buf, size);
        
        flatcc_builder_reset(&builder);
        return 0;
    }
    
    else if (strcmp(method_name, "Divide") == 0) {
        /* MathService.Divide */
        rpc_MathDivideRequest_table_t div_req = rpc_MathDivideRequest_as_root(request);
        if (!div_req) return UVRPC_ERROR_INVALID_PARAM;
        
        int32_t a = rpc_MathDivideRequest_a(div_req);
        int32_t b = rpc_MathDivideRequest_b(div_req);
        
        if (b == 0) {
            fprintf(stderr, "[MathService] Divide by zero!\n");
            uvrpc_request_send_response(req, UVRPC_ERROR, NULL, 0);
            return 0;
        }
        
        int32_t result = a / b;
        printf("[MathService] Divide: %d / %d = %d\n", a, b, result);
        
        /* Build response */
        flatcc_builder_t builder;
        flatcc_builder_init(&builder);
        
        rpc_MathDivideResponse_start_as_root(&builder);
        rpc_MathDivideResponse_result_add(&builder, result);
        rpc_MathDivideResponse_end_as_root(&builder);
        
        size_t size;
        void* buf = flatcc_builder_finalize_buffer(&builder, &size);
        
        uvrpc_request_send_response(req, UVRPC_OK, buf, size);
        
        flatcc_builder_reset(&builder);
        return 0;
    }
    
    else if (strcmp(method_name, "GetUser") == 0) {
        /* UserService.GetUser */
        rpc_UserGetRequest_table_t user_req = rpc_UserGetRequest_as_root(request);
        if (!user_req) return UVRPC_ERROR_INVALID_PARAM;
        
        int32_t user_id = rpc_UserGetRequest_user_id(user_req);
        printf("[UserService] GetUser: id=%d\n", user_id);
        
        /* Build response */
        flatcc_builder_t builder;
        flatcc_builder_init(&builder);
        
        rpc_UserGetResponse_start_as_root(&builder);
        rpc_UserGetResponse_user_id_add(&builder, user_id);
        rpc_UserGetResponse_name_create_str(&builder, "John Doe");
        rpc_UserGetResponse_email_create_str(&builder, "john@example.com");
        rpc_UserGetResponse_end_as_root(&builder);
        
        size_t size;
        void* buf = flatcc_builder_finalize_buffer(&builder, &size);
        
        uvrpc_request_send_response(req, UVRPC_OK, buf, size);
        
        flatcc_builder_reset(&builder);
        return 0;
    }
    
    else if (strcmp(method_name, "CreateUser") == 0) {
        /* UserService.CreateUser */
        rpc_UserCreateRequest_table_t create_req = rpc_UserCreateRequest_as_root(request);
        if (!create_req) return UVRPC_ERROR_INVALID_PARAM;
        
        const char* name = rpc_UserCreateRequest_name(create_req);
        const char* email = rpc_UserCreateRequest_email(create_req);
        printf("[UserService] CreateUser: name=%s, email=%s\n", name, email);
        
        /* Build response */
        flatcc_builder_t builder;
        flatcc_builder_init(&builder);
        
        rpc_UserCreateResponse_start_as_root(&builder);
        rpc_UserCreateResponse_user_id_add(&builder, 123); /* Simulated user ID */
        rpc_UserCreateResponse_success_add(&builder, 1);
        rpc_UserCreateResponse_end_as_root(&builder);
        
        size_t size;
        void* buf = flatcc_builder_finalize_buffer(&builder, &size);
        
        uvrpc_request_send_response(req, UVRPC_OK, buf, size);
        
        flatcc_builder_reset(&builder);
        return 0;
    }
    
    else if (strcmp(method_name, "Echo") == 0) {
        /* EchoService.Echo */
        rpc_EchoRequest_table_t echo_req = rpc_EchoRequest_as_root(request);
        if (!echo_req) return UVRPC_ERROR_INVALID_PARAM;
        
        const char* message = rpc_EchoRequest_message(echo_req);
        printf("[EchoService] Echo: %s\n", message);
        
        /* Build response */
        flatcc_builder_t builder;
        flatcc_builder_init(&builder);
        
        rpc_EchoResponse_start_as_root(&builder);
        rpc_EchoResponse_message_create_str(&builder, message);
        rpc_EchoResponse_timestamp_add(&builder, 1234567890);
        rpc_EchoResponse_end_as_root(&builder);
        
        size_t size;
        void* buf = flatcc_builder_finalize_buffer(&builder, &size);
        
        uvrpc_request_send_response(req, UVRPC_OK, buf, size);
        
        flatcc_builder_reset(&builder);
        return 0;
    }
    
    else if (strcmp(method_name, "Method") == 0) {
        /* Test method */
        printf("[Test] Method called\n");
        uvrpc_request_send_response(req, UVRPC_OK, NULL, 0);
        return 0;
    }
    
    /* Unknown method */
    fprintf(stderr, "[User Handler] Unknown method: %s\n", method_name);
    uvrpc_request_send_response(req, UVRPC_ERROR, NULL, 0);
    return 0;
}