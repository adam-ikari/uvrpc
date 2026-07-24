/**
 * UVRPC Frame Encoding/Decoding Implementation
 */

/* Forward declaration */
struct uvrpc_server;

#include "uvrpc_flatbuffers.h"
#include "uvrpc.h"
#include "rpc_reader.h"
#include "rpc_builder.h"
#include "../include/uvrpc_allocator.h"
#include <string.h>

/* Encode request frame */
int uvrpc_encode_request(uint32_t msgid, const char* method,
                         const uint8_t* params, size_t params_size,
                         uint8_t** out_data, size_t* out_size) {
    if (!out_data || !out_size) return UVRPC_ERROR_INVALID_PARAM;

    flatcc_builder_t builder;
    flatcc_builder_init(&builder);

    flatbuffers_uint8_vec_ref_t data_ref = 0;
    if (params && params_size > 0) {
        data_ref = flatbuffers_uint8_vec_create(&builder, params, params_size);
    }

    /* type: 0 = Request, 1 = Response */
    uint8_t type = 0;

    uvrpc_RpcFrame_start_as_root(&builder);
    uvrpc_RpcFrame_type_add(&builder, type);
    uvrpc_RpcFrame_msgid_add(&builder, msgid);
    uvrpc_RpcFrame_method_add(&builder, flatbuffers_string_create_str(&builder, method));
    uvrpc_RpcFrame_data_add(&builder, data_ref);
    uvrpc_RpcFrame_end_as_root(&builder);

    void* buf = flatcc_builder_finalize_buffer(&builder, out_size);
    if (buf) {
        *out_data = buf;
    }

    flatcc_builder_clear(&builder);
    return UVRPC_OK;
}

/* Encode response frame (type=1, last response) */
int uvrpc_encode_response(uint32_t msgid, const uint8_t* result, size_t result_size,
                          uint8_t** out_data, size_t* out_size) {
    if (!out_data || !out_size) return UVRPC_ERROR_INVALID_PARAM;

    flatcc_builder_t builder;
    flatcc_builder_init(&builder);

    flatbuffers_uint8_vec_ref_t data_ref = 0;
    if (result && result_size > 0) {
        data_ref = flatbuffers_uint8_vec_create(&builder, result, result_size);
    }

    /* type: 0 = Request, 1 = Response (last), 2 = ResponseMore */
    uint8_t type = 1;

    uvrpc_RpcFrame_start_as_root(&builder);
    uvrpc_RpcFrame_type_add(&builder, type);
    uvrpc_RpcFrame_msgid_add(&builder, msgid);
    uvrpc_RpcFrame_data_add(&builder, data_ref);
    uvrpc_RpcFrame_end_as_root(&builder);

    void* buf = flatcc_builder_finalize_buffer(&builder, out_size);
    if (buf) {
        *out_data = buf;
    }

    flatcc_builder_clear(&builder);
    return UVRPC_OK;
}

/* Encode response frame (type=2, more responses to come) */
int uvrpc_encode_response_more(uint32_t msgid, const uint8_t* result, size_t result_size,
                                uint8_t** out_data, size_t* out_size) {
    if (!out_data || !out_size) return UVRPC_ERROR_INVALID_PARAM;

    flatcc_builder_t builder;
    flatcc_builder_init(&builder);

    flatbuffers_uint8_vec_ref_t data_ref = 0;
    if (result && result_size > 0) {
        data_ref = flatbuffers_uint8_vec_create(&builder, result, result_size);
    }

    /* type: 2 = ResponseMore (more to come) */
    uint8_t type = 2;

    uvrpc_RpcFrame_start_as_root(&builder);
    uvrpc_RpcFrame_type_add(&builder, type);
    uvrpc_RpcFrame_msgid_add(&builder, msgid);
    uvrpc_RpcFrame_data_add(&builder, data_ref);
    uvrpc_RpcFrame_end_as_root(&builder);

    void* buf = flatcc_builder_finalize_buffer(&builder, out_size);
    if (buf) {
        *out_data = buf;
    }

    flatcc_builder_clear(&builder);
    return UVRPC_OK;
}

/* Decode request frame */
int uvrpc_decode_request(const uint8_t* data, size_t size,
                         uint32_t* out_msgid, char** out_method,
                         const uint8_t** out_params, size_t* out_params_size) {
    if (!data || !out_msgid || !out_method || !out_params || !out_params_size) {
        return UVRPC_ERROR_INVALID_PARAM;
    }

    /* A FlatBuffers root needs at least the root uoffset (4 bytes) + a table
     * header (4 bytes). Reject frames too small to be a valid root before the
     * FlatCC reader trusts embedded offsets. */
    if (size < 8) {
        return UVRPC_ERROR;
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

    flatbuffers_uint8_vec_t data_vec = uvrpc_RpcFrame_data(frame);
    if (data_vec) {
        *out_params = data_vec;
        *out_params_size = flatbuffers_uint8_vec_len(data_vec);
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

    /* Reject frames too small to contain a valid FlatBuffers root (root
     * uoffset + table header) before the FlatCC reader trusts offsets. */
    if (size < 8) {
        return UVRPC_ERROR;
    }
    
    uvrpc_RpcFrame_table_t frame = uvrpc_RpcFrame_as_root(data);
    
    if (!frame) {
        return UVRPC_ERROR;
    }
    
    *out_msgid = uvrpc_RpcFrame_msgid(frame);
    
    flatbuffers_uint8_vec_t data_vec = uvrpc_RpcFrame_data(frame);
    if (data_vec) {
        *out_result = data_vec;
        *out_result_size = flatbuffers_uint8_vec_len(data_vec);
    }
    
    return UVRPC_OK;
}

/* Get frame type */
int uvrpc_get_frame_type(const uint8_t* data, size_t size) {
    if (!data || size < 1) return -1;
    
    uvrpc_RpcFrame_table_t frame = uvrpc_RpcFrame_as_root(data);
    if (!frame) return -1;
    
    return (int)uvrpc_RpcFrame_type(frame);
}

/* Free decoded data */
void uvrpc_free_decoded(char* method) {
    if (method) {
        uvrpc_free(method);
    }
}

/* Check if response type is Response (type=1, last) */
int uvrpc_response_is_stream_end(uvrpc_response_t* resp) {
    if (!resp) {
        return 0;
    }
    
    /* Check frame_type field directly */
    return (resp->frame_type == 1);
}

/* Check if response type is ResponseMore (type=2, more to come) */
int uvrpc_response_is_stream_more(uvrpc_response_t* resp) {
    if (!resp) {
        return 0;
    }
    
    /* Check frame_type field directly */
    return (resp->frame_type == 2);
}