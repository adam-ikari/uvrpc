/**
 * UVRPC Serialization Module using FlatCC
 */

#include "uvrpc_msgpack.h"
#include "rpc_builder.h"
#include "rpc_reader.h"
#include "flatcc/flatcc_builder.h"
#include <stdlib.h>
#include <string.h>

/* Pack RPC request using FlatCC */
char* uvrpc_pack_request(const char* service, const char* method, 
                          const uint8_t* data, size_t data_size, size_t* out_size) {
    if (!service || !method || !out_size) return NULL;
    
    flatcc_builder_t builder;
    flatcc_builder_init(&builder);
    
    flatbuffers_string_ref_t service_ref = flatbuffers_string_create_str(&builder, service);
    flatbuffers_string_ref_t method_ref = flatbuffers_string_create_str(&builder, method);
    flatbuffers_uint8_vec_ref_t data_ref = 0;
    
    if (data && data_size > 0) {
        data_ref = flatbuffers_uint8_vec_create(&builder, data, data_size);
    }
    
    uvrpc_Request_create_as_root(&builder, service_ref, method_ref, data_ref);
    
    size_t size = flatcc_builder_get_buffer_size(&builder);
    *out_size = size;
    char* buffer = (char*)malloc(size);
    if (buffer) {
        void* buf = flatcc_builder_get_direct_buffer(&builder, &size);
        if (buf) {
            memcpy(buffer, buf, size);
        }
    }
    
    flatcc_builder_reset(&builder);
    return buffer;
}

/* Pack RPC response using FlatCC */
char* uvrpc_pack_response(int status, const uint8_t* data, size_t data_size, size_t* out_size) {
    if (!out_size) return NULL;
    
    flatcc_builder_t builder;
    flatcc_builder_init(&builder);
    
    flatbuffers_uint8_vec_ref_t data_ref = 0;
    if (data && data_size > 0) {
        data_ref = flatbuffers_uint8_vec_create(&builder, data, data_size);
    }
    
    uvrpc_Response_create_as_root(&builder, status, data_ref);
    
    size_t size = flatcc_builder_get_buffer_size(&builder);
    *out_size = size;
    char* buffer = (char*)malloc(size);
    if (buffer) {
        void* buf = flatcc_builder_get_direct_buffer(&builder, &size);
        if (buf) {
            memcpy(buffer, buf, size);
        }
    }
    
    flatcc_builder_reset(&builder);
    return buffer;
}

/* Unpack RPC request using FlatCC */
int uvrpc_unpack_request(const char* buffer, size_t size,
                         char** service, char** method,
                         const uint8_t** data, size_t* data_size) {
    if (!buffer || !service || !method || !data || !data_size) return -1;
    
    uvrpc_Request_table_t req = uvrpc_Request_as_root(buffer);
    if (!req) return -2;
    
    *service = NULL;
    *method = NULL;
    *data = NULL;
    *data_size = 0;
    
    /* Get service */
    flatbuffers_string_t s = uvrpc_Request_service(req);
    if (s) *service = strdup(s);
    
    /* Get method */
    flatbuffers_string_t m = uvrpc_Request_method(req);
    if (m) *method = strdup(m);
    
    /* Get data - flatbuffers_uint8_vec_t is const uint8_t* */
    flatbuffers_uint8_vec_t d = uvrpc_Request_data(req);
    if (d) {
        *data = d;
        *data_size = flatbuffers_uint8_vec_len(d);
    }
    
    return 0;
}

/* Unpack RPC response using FlatCC */
int uvrpc_unpack_response(const char* buffer, size_t size,
                          int* status, const uint8_t** data, size_t* data_size) {
    if (!buffer || !status || !data || !data_size) return -1;
    
    uvrpc_Response_table_t resp = uvrpc_Response_as_root(buffer);
    if (!resp) return -2;
    
    *status = 0;
    *data = NULL;
    *data_size = 0;
    
    *status = uvrpc_Response_status(resp);
    
    flatbuffers_uint8_vec_t d = uvrpc_Response_data(resp);
    if (d) {
        *data = d;
        *data_size = flatbuffers_uint8_vec_len(d);
    }
    
    return 0;
}