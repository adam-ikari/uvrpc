/**
 * UVRPC Serialization Module using FlatCC
 * Uses unified RpcFrame format: type, msgid, method, params
 */

#include "uvrpc_msgpack.h"
#include "rpc_builder.h"
#include "rpc_reader.h"
#include "flatcc/flatcc_builder.h"
#include "../include/uvrpc.h"
#include <stdlib.h>
#include <string.h>

/* Pack RPC request using FlatCC */
char* uvrpc_pack_request(uint32_t msgid, const char* method, 
                          const uint8_t* data, size_t data_size, size_t* out_size) {
    if (!method || !out_size) return NULL;
    
    flatcc_builder_t builder;
    flatcc_builder_init(&builder);
    
    flatbuffers_string_ref_t method_ref = flatbuffers_string_create_str(&builder, method);
    flatbuffers_uint8_vec_ref_t data_ref = 0;
    
    if (data && data_size > 0) {
        data_ref = flatbuffers_uint8_vec_create(&builder, data, data_size);
    }
    
    uvrpc_RpcFrame_start_as_root(&builder);
    uvrpc_RpcFrame_type_add(&builder, uvrpc_FrameType_REQUEST);
    uvrpc_RpcFrame_msgid_add(&builder, msgid);
    uvrpc_RpcFrame_method_add(&builder, method_ref);
    uvrpc_RpcFrame_params_add(&builder, data_ref);
    uvrpc_RpcFrame_end_as_root(&builder);
    
    void* buf = flatcc_builder_finalize_buffer(&builder, out_size);
    flatcc_builder_reset(&builder);
    return (char*)buf;
}

/* Pack RPC response using FlatCC */
char* uvrpc_pack_response(uint32_t msgid, int32_t error_code, const uint8_t* data, size_t data_size, size_t* out_size) {
    if (!out_size) return NULL;
    
    flatcc_builder_t builder;
    flatcc_builder_init(&builder);
    
    flatbuffers_uint8_vec_ref_t data_ref = 0;
    if (data && data_size > 0) {
        data_ref = flatbuffers_uint8_vec_create(&builder, data, data_size);
    }
    
    uvrpc_RpcFrame_start_as_root(&builder);
    uvrpc_RpcFrame_type_add(&builder, uvrpc_FrameType_RESPONSE);
    uvrpc_RpcFrame_msgid_add(&builder, msgid);
    uvrpc_RpcFrame_error_code_add(&builder, error_code);
    uvrpc_RpcFrame_params_add(&builder, data_ref);
    uvrpc_RpcFrame_end_as_root(&builder);
    
    void* buf = flatcc_builder_finalize_buffer(&builder, out_size);
    flatcc_builder_reset(&builder);
    return (char*)buf;
}

/* Unpack RPC request using FlatCC */
int uvrpc_unpack_request(const char* buffer, uint32_t* msgid, char** method, 
                          uint8_t** data, size_t* data_size) {
    if (!buffer || !msgid || !method || !data || !data_size) return UVRPC_ERROR_INVALID_PARAM;
    
    uvrpc_RpcFrame_table_t frame = uvrpc_RpcFrame_as_root(buffer);
    if (!frame) return UVRPC_ERROR;
    
    *msgid = uvrpc_RpcFrame_msgid(frame);
    
    flatbuffers_string_t m = uvrpc_RpcFrame_method(frame);
    if (m) {
        *method = strdup(m);
    } else {
        *method = NULL;
    }
    
    flatbuffers_uint8_vec_t d = uvrpc_RpcFrame_params(frame);
    if (d) {
        *data_size = flatbuffers_uint8_vec_len(d);
        if (*data_size > 0) {
            *data = (uint8_t*)malloc(*data_size);
            if (*data) {
                memcpy(*data, d, *data_size);
            }
        } else {
            *data = NULL;
        }
    } else {
        *data = NULL;
        *data_size = 0;
    }
    
    return UVRPC_OK;
}

/* Unpack RPC response using FlatCC */
int uvrpc_unpack_response(const char* buffer, uint32_t* msgid, int32_t* error_code, 
                           uint8_t** data, size_t* data_size) {
    if (!buffer || !msgid || !error_code || !data || !data_size) return UVRPC_ERROR_INVALID_PARAM;
    
    uvrpc_RpcFrame_table_t frame = uvrpc_RpcFrame_as_root(buffer);
    if (!frame) return UVRPC_ERROR;
    
    *msgid = uvrpc_RpcFrame_msgid(frame);
    *error_code = uvrpc_RpcFrame_error_code(frame);
    
    flatbuffers_uint8_vec_t d = uvrpc_RpcFrame_params(frame);
    if (d) {
        *data_size = flatbuffers_uint8_vec_len(d);
        if (*data_size > 0) {
            *data = (uint8_t*)malloc(*data_size);
            if (*data) {
                memcpy(*data, d, *data_size);
            }
        } else {
            *data = NULL;
        }
    } else {
        *data = NULL;
        *data_size = 0;
    }
    
    return UVRPC_OK;
}