/**
 * UVRPC Frame Encoding/Decoding using FlatCC
 */

#include "../include/uvrpc.h"
#include "rpc_builder.h"
#include "rpc_reader.h"
#include "flatcc/flatcc_builder.h"
#include <stdlib.h>
#include <string.h>

/* Encode request frame */
int uvrpc_encode_request(uint32_t msgid, const char* method,
                         const uint8_t* params, size_t params_size,
                         uint8_t** out_data, size_t* out_size) {
    if (!method || !out_data || !out_size) return UVRPC_ERROR_INVALID_PARAM;
    
    flatcc_builder_t builder;
    flatcc_builder_init(&builder);
    
    flatbuffers_string_ref_t method_ref = flatbuffers_string_create_str(&builder, method);
    flatbuffers_uint8_vec_ref_t params_ref = 0;
    
    if (params && params_size > 0) {
        params_ref = flatbuffers_uint8_vec_create(&builder, params, params_size);
    }
    
    uvrpc_RpcFrame_start_as_root(&builder);
    uvrpc_RpcFrame_type_add(&builder, uvrpc_FrameType_REQUEST);
    uvrpc_RpcFrame_msgid_add(&builder, msgid);
    uvrpc_RpcFrame_method_add(&builder, method_ref);
    uvrpc_RpcFrame_params_add(&builder, params_ref);
    uvrpc_RpcFrame_end_as_root(&builder);
    
    void* buf = flatcc_builder_finalize_buffer(&builder, out_size);
    if (buf) {
        *out_data = buf;
    }
    
    flatcc_builder_reset(&builder);
    return UVRPC_OK;
}

/* Encode response frame */
int uvrpc_encode_response(uint32_t msgid, int32_t error_code,
                          const uint8_t* result, size_t result_size,
                          uint8_t** out_data, size_t* out_size) {
    if (!out_data || !out_size) return UVRPC_ERROR_INVALID_PARAM;
    
    flatcc_builder_t builder;
    flatcc_builder_init(&builder);
    
    flatbuffers_uint8_vec_ref_t result_ref = 0;
    if (result && result_size > 0) {
        result_ref = flatbuffers_uint8_vec_create(&builder, result, result_size);
    }
    
    uvrpc_RpcFrame_start_as_root(&builder);
    uvrpc_RpcFrame_type_add(&builder, uvrpc_FrameType_RESPONSE);
    uvrpc_RpcFrame_msgid_add(&builder, msgid);
    uvrpc_RpcFrame_params_add(&builder, result_ref);
    uvrpc_RpcFrame_end_as_root(&builder);
    
    void* buf = flatcc_builder_finalize_buffer(&builder, out_size);
    if (buf) {
        *out_data = buf;
    }
    
    flatcc_builder_reset(&builder);
    return UVRPC_OK;
}

/* Decode request frame */
int uvrpc_decode_request(const uint8_t* data, size_t size,
                         uint32_t* out_msgid, char** out_method,
                         const uint8_t** out_params, size_t* out_params_size) {
    if (!data || !out_msgid || !out_method || !out_params || !out_params_size) {
        return UVRPC_ERROR_INVALID_PARAM;
    }
    
    uvrpc_RpcFrame_table_t frame = uvrpc_RpcFrame_as_root((const char*)data);
    if (!frame) return UVRPC_ERROR;
    
    *out_msgid = uvrpc_RpcFrame_msgid(frame);
    
    flatbuffers_string_t m = uvrpc_RpcFrame_method(frame);
    *out_method = m ? strdup(m) : NULL;
    
    flatbuffers_uint8_vec_t p = uvrpc_RpcFrame_params(frame);
    *out_params = p;
    *out_params_size = p ? flatbuffers_uint8_vec_len(p) : 0;
    
    return UVRPC_OK;
}

/* Decode response frame */
int uvrpc_decode_response(const uint8_t* data, size_t size,
                          uint32_t* out_msgid, int32_t* out_error_code,
                          const uint8_t** out_result, size_t* out_result_size) {
    if (!data || !out_msgid || !out_error_code || !out_result || !out_result_size) {
        return UVRPC_ERROR_INVALID_PARAM;
    }
    
    uvrpc_RpcFrame_table_t frame = uvrpc_RpcFrame_as_root((const char*)data);
    if (!frame) return UVRPC_ERROR;
    
    *out_msgid = uvrpc_RpcFrame_msgid(frame);
    *out_error_code = 0;  /* Error code encoded in params for now */
    
    flatbuffers_uint8_vec_t r = uvrpc_RpcFrame_params(frame);
    *out_result = r;
    *out_result_size = r ? flatbuffers_uint8_vec_len(r) : 0;
    
    return UVRPC_OK;
}

/* Free decoded frame data */
void uvrpc_free_decoded(char* method) {
    if (method) {
        free(method);
    }
}