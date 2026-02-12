/**
 * UVRPC Service Registry using FlatCC DSL
 */

#include "uvrpc_service.h"
#include "rpc_builder.h"
#include "rpc_reader.h"
#include "flatcc/flatcc_builder.h"
#include <stdlib.h>
#include <string.h>

/* Pack service definition to FlatCC buffer */
char* uvrpc_pack_service_def(const char* name, const char** methods, 
                              int method_count, size_t* out_size) {
    if (!name || !methods || !out_size) return NULL;
    
    flatcc_builder_t builder;
    flatcc_builder_init(&builder);
    
    /* Create method array */
    flatbuffers_string_ref_t name_ref = flatbuffers_string_create_str(&builder, name);
    
    /* Start methods vector */
    uvrpc_Method_vec_start(&builder);
    for (int i = 0; i < method_count; i++) {
        flatbuffers_string_ref_t method_name = flatbuffers_string_create_str(&builder, methods[i]);
        flatbuffers_string_ref_t req_type = flatbuffers_string_create_str(&builder, "Request");
        flatbuffers_string_ref_t resp_type = flatbuffers_string_create_str(&builder, "Response");
        uvrpc_Method_vec_push(&builder, uvrpc_Method_create(&builder, method_name, req_type, resp_type));
    }
    flatbuffers_uoffset_t methods_vec = uvrpc_Method_vec_end(&builder);
    
    /* Create service table */
    uvrpc_Service_create_as_root(&builder, name_ref, methods_vec);
    
    size_t size = flatcc_builder_get_buffer_size(&builder);
    *out_size = size;
    char* buffer = (char*)malloc(size);
    if (buffer) {
        void* buf = flatcc_builder_get_direct_buffer(&builder, &size);
        if (buf) memcpy(buffer, buf, size);
    }
    
    flatcc_builder_reset(&builder);
    return buffer;
}

/* Unpack service definition from FlatCC buffer */
int uvrpc_unpack_service_def(const char* buffer, size_t size,
                             char** name, char*** methods, int* method_count) {
    if (!buffer || !name || !methods || !method_count) return -1;
    
    uvrpc_Service_table_t service = uvrpc_Service_as_root(buffer);
    if (!service) return -2;
    
    *name = NULL;
    *methods = NULL;
    *method_count = 0;
    
    /* Get service name */
    flatbuffers_string_t n = uvrpc_Service_name(service);
    if (n) *name = strdup(n);
    
    /* Get methods */
    uvrpc_Method_vec_t mvec = uvrpc_Service_methods(service);
    if (mvec) {
        *method_count = uvrpc_Method_vec_len(mvec);
        if (*method_count > 0) {
            *methods = (char**)calloc(*method_count, sizeof(char*));
            for (int i = 0; i < *method_count; i++) {
                uvrpc_Method_table_t method = uvrpc_Method_vec_at(mvec, i);
                if (method) {
                    flatbuffers_string_t method_name = uvrpc_Method_name(method);
                    if (method_name) (*methods)[i] = strdup(method_name);
                }
            }
        }
    }
    
    return 0;
}

/* Free unpacked service definition */
void uvrpc_free_service_def(char* name, char** methods, int method_count) {
    if (name) free(name);
    if (methods) {
        for (int i = 0; i < method_count; i++) {
            if (methods[i]) free(methods[i]);
        }
        free(methods);
    }
}