/**
 * UVRPC Frame Encoding/Decoding Implementation
 */

/* Forward declaration */
struct uvrpc_server;

#include "uvrpc_flatbuffers.h"
#include "uvrpc.h"
#include "rpc_reader.h"
#include "rpc_verifier.h"
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
/* Encode a server-to-client frame. `type` is one of UVRPC_FRAME_TYPE_*; it is
 * what tells the client whether the payload is a result or a failure. */
static int encode_response_typed(uint32_t msgid, const uint8_t* result,
                                 size_t result_size, uint8_t type,
                                 uint8_t** out_data, size_t* out_size) {
    if (!out_data || !out_size) return UVRPC_ERROR_INVALID_PARAM;

    flatcc_builder_t builder;
    flatcc_builder_init(&builder);

    flatbuffers_uint8_vec_ref_t data_ref = 0;
    if (result && result_size > 0) {
        data_ref = flatbuffers_uint8_vec_create(&builder, result, result_size);
    }

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

int uvrpc_encode_response(uint32_t msgid, const uint8_t* result, size_t result_size,
                          uint8_t** out_data, size_t* out_size) {
    return encode_response_typed(msgid, result, result_size,
                                 UVRPC_FRAME_TYPE_RESPONSE, out_data, out_size);
}

/* Encode a failure: the payload is an int32 error code followed by the message
 * bytes, and the frame type says so. Without the type a client cannot tell this
 * from a result that happens to start with a small integer. */
int uvrpc_encode_error(uint32_t msgid, int32_t error_code, const char* message,
                       uint8_t** out_data, size_t* out_size) {
    size_t message_len = message ? strlen(message) : 0;
    size_t payload_size = sizeof(int32_t) + message_len;

    uint8_t* payload = uvrpc_alloc(payload_size);
    if (!payload) return UVRPC_ERROR_NO_MEMORY;

    memcpy(payload, &error_code, sizeof(int32_t));
    if (message_len > 0) {
        memcpy(payload + sizeof(int32_t), message, message_len);
    }

    int ret = encode_response_typed(msgid, payload, payload_size,
                                    UVRPC_FRAME_TYPE_RESPONSE_ERROR,
                                    out_data, out_size);
    uvrpc_free(payload);
    return ret;
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

/* Verify a buffer is a well-formed RpcFrame, then hand it to the reader.
 *
 * The generated as_root() does no bounds checking. Once a buffer merely looks
 * like a root it dereferences offsets the peer chose, so a single malformed
 * frame makes the process read out of bounds -- reachable by anyone who can
 * open a connection. FlatCC's verifier walks the vtable and every offset
 * against the buffer bounds first; only what it accepts reaches the reader.
 *
 * Returns NULL when the buffer is not a valid frame. */
static uvrpc_RpcFrame_table_t verified_root(const uint8_t* data, size_t size) {
    /* A FlatBuffers root needs at least the root uoffset (4 bytes) plus a
     * table header (4 bytes). */
    if (!data || size < 8) {
        return NULL;
    }
    if (uvrpc_RpcFrame_verify_as_root(data, size) != flatcc_verify_ok) {
        return NULL;
    }
    return uvrpc_RpcFrame_as_root(data);
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

    uvrpc_RpcFrame_table_t frame = verified_root(data, size);

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
    
    uvrpc_RpcFrame_table_t frame = verified_root(data, size);
    
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
    /* verified_root rejects a NULL buffer and anything too small to be a
     * root, so a single length check here would only be half the answer. */
    
    uvrpc_RpcFrame_table_t frame = verified_root(data, size);
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