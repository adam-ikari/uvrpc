/**
 * UVRPC Frame Encoding/Decoding Implementation
 */

#include "uvrpc_flatbuffers.h"
#include "uvrpc.h"
#include "rpc_reader.h"
#include "rpc_builder.h"
#include "../include/uvrpc_allocator.h"
#include <string.h>
#include <stdlib.h>

/* Encode request frame */
int uvrpc_encode_request(uint32_t msgid, const char* method,
                         const uint8_t* params, size_t params_size,
                         uint8_t** out_data, size_t* out_size) {
    if (!out_data || !out_size) return UVRPC_ERROR_INVALID_PARAM;

    flatcc_builder_t builder;
    flatcc_builder_init(&builder);

    flatbuffers_uint8_vec_ref_t params_ref = 0;
    if (params && params_size > 0) {
        params_ref = flatbuffers_uint8_vec_create(&builder, params, params_size);
    }

    /* type: 0 = request, 1 = response, 2 = notification, 3 = error */
    uint8_t type = 0;

    uvrpc_RpcFrame_start_as_root(&builder);
    uvrpc_RpcFrame_type_add(&builder, type);
    uvrpc_RpcFrame_msgid_add(&builder, msgid);
    uvrpc_RpcFrame_method_add(&builder, flatbuffers_string_create_str(&builder, method));
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
int uvrpc_encode_response(uint32_t msgid, const uint8_t* result, size_t result_size,
                          uint8_t** out_data, size_t* out_size) {
    if (!out_data || !out_size) return UVRPC_ERROR_INVALID_PARAM;

    flatcc_builder_t builder;
    flatcc_builder_init(&builder);

    flatbuffers_uint8_vec_ref_t result_ref = 0;
    if (result && result_size > 0) {
        result_ref = flatbuffers_uint8_vec_create(&builder, result, result_size);
    }

    /* type: 0 = request, 1 = response, 2 = notification, 3 = error */
    uint8_t type = 1;

    uvrpc_RpcFrame_start_as_root(&builder);
    uvrpc_RpcFrame_type_add(&builder, type);
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

    uvrpc_RpcFrame_table_t frame = uvrpc_RpcFrame_as_root(data);

    if (!frame) {
        return UVRPC_ERROR;
    }

    *out_msgid = uvrpc_RpcFrame_msgid(frame);

    const char* method = uvrpc_RpcFrame_method(frame);
    if (method) {
        *out_method = uvrpc_strdup(method);
    } else {
        *out_method = uvrpc_strdup("");
    }

    flatbuffers_uint8_vec_t params = uvrpc_RpcFrame_params(frame);
    if (params) {
        *out_params = params;
        *out_params_size = flatbuffers_uint8_vec_len(params);
    } else {
        *out_params = NULL;
        *out_params_size = 0;
    }

    return UVRPC_OK;
}

/* Decode response frame */
int uvrpc_decode_response(const uint8_t* data, size_t size,
                          uint32_t* out_msgid,
                          const uint8_t** out_result, size_t* out_result_size) {
    if (!data || !out_msgid || !out_result || !out_result_size) {
        return UVRPC_ERROR_INVALID_PARAM;
    }
    
    uvrpc_RpcFrame_table_t frame = uvrpc_RpcFrame_as_root(data);
    
    if (!frame) {
        return UVRPC_ERROR;
    }
    
    *out_msgid = uvrpc_RpcFrame_msgid(frame);
    
    flatbuffers_uint8_vec_t result = uvrpc_RpcFrame_params(frame);
    if (result) {
        *out_result = result;
        *out_result_size = flatbuffers_uint8_vec_len(result);
    }
    
    return UVRPC_OK;
}

/* Encode error frame */
int uvrpc_encode_error(uint32_t msgid, int32_t error_code, const char* error_message,
                       uint8_t** out_data, size_t* out_size) {
    if (!out_data || !out_size) return UVRPC_ERROR_INVALID_PARAM;

    flatcc_builder_t builder;
    flatcc_builder_init(&builder);

    /* Serialize error info: [error_code (4 bytes), error_message (null-terminated string)] */
    size_t error_data_size = 4 + (error_message ? strlen(error_message) + 1 : 1);
    uint8_t* error_data = (uint8_t*)uvrpc_alloc(error_data_size);
    if (!error_data) {
        flatcc_builder_reset(&builder);
        return UVRPC_ERROR_NO_MEMORY;
    }

    /* Pack error code and message */
    error_data[0] = (error_code >> 24) & 0xFF;
    error_data[1] = (error_code >> 16) & 0xFF;
    error_data[2] = (error_code >> 8) & 0xFF;
    error_data[3] = error_code & 0xFF;
    if (error_message) {
        memcpy(error_data + 4, error_message, strlen(error_message) + 1);
    } else {
        error_data[4] = '\0';
    }

    flatbuffers_uint8_vec_ref_t params_ref = flatbuffers_uint8_vec_create(&builder, error_data, error_data_size);
    uvrpc_free(error_data);

    /* type: 0 = request, 1 = response, 2 = notification, 3 = error */
    uint8_t type = 3;

    uvrpc_RpcFrame_start_as_root(&builder);
    uvrpc_RpcFrame_type_add(&builder, type);
    uvrpc_RpcFrame_msgid_add(&builder, msgid);
    uvrpc_RpcFrame_params_add(&builder, params_ref);
    uvrpc_RpcFrame_end_as_root(&builder);

    void* buf = flatcc_builder_finalize_buffer(&builder, out_size);
    if (buf) {
        *out_data = buf;
    }

    flatcc_builder_reset(&builder);
    return UVRPC_OK;
}

/* Decode error frame */
int uvrpc_decode_error(const uint8_t* data, size_t size,
                       uint32_t* out_msgid, int32_t* out_error_code, char** out_error_message) {
    if (!data || !out_msgid || !out_error_code || !out_error_message) {
        return UVRPC_ERROR_INVALID_PARAM;
    }

    uvrpc_RpcFrame_table_t frame = uvrpc_RpcFrame_as_root(data);

    if (!frame) {
        return UVRPC_ERROR;
    }

    *out_msgid = uvrpc_RpcFrame_msgid(frame);

    flatbuffers_uint8_vec_t params = uvrpc_RpcFrame_params(frame);
    if (!params || flatbuffers_uint8_vec_len(params) < 5) {
        return UVRPC_ERROR;
    }

    /* Unpack error code and message */
    size_t params_size = flatbuffers_uint8_vec_len(params);
    *out_error_code = ((int32_t)params[0] << 24) |
                      ((int32_t)params[1] << 16) |
                      ((int32_t)params[2] << 8) |
                      params[3];

    if (params_size > 5) {
        *out_error_message = uvrpc_strdup((char*)(params + 4));
    } else {
        *out_error_message = uvrpc_strdup("");
    }

    return UVRPC_OK;
}

/* Get frame type */
int uvrpc_get_frame_type(const uint8_t* data, size_t size) {
    fprintf(stderr, "[DEBUG] uvrpc_get_frame_type: Called with data=%p, size=%zu\n", data, size);
    fflush(stderr);

    if (!data || size < 4) {
        fprintf(stderr, "[DEBUG] uvrpc_get_frame_type: Invalid parameters\n");
        fflush(stderr);
        return -1;
    }

    uvrpc_RpcFrame_table_t frame = uvrpc_RpcFrame_as_root(data);
    fprintf(stderr, "[DEBUG] uvrpc_get_frame_type: frame=%p\n", frame);
    fflush(stderr);

    if (!frame) {
        fprintf(stderr, "[DEBUG] uvrpc_get_frame_type: Failed to parse frame\n");
        fflush(stderr);
        return -1;
    }

    int type = (int)uvrpc_RpcFrame_type(frame);
    fprintf(stderr, "[DEBUG] uvrpc_get_frame_type: type=%d\n", type);
    fflush(stderr);

    return type;
}

/* Free decoded data */
void uvrpc_free_decoded(char* method_or_error) {
    if (method_or_error) {
        uvrpc_free(method_or_error);
    }
}
