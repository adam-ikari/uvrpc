/**
 * UVRPC msgpack Serialization Module
 * Simple wrapper around mpack
 */

#include <mpack.h>
#include <stdlib.h>
#include <string.h>

/* Pack RPC request: {service, method, data} */
char* uvrpc_pack_request(const char* service, const char* method, 
                          const uint8_t* data, size_t data_size, size_t* out_size) {
    if (!service || !method || !out_size) return NULL;
    
    char* buffer = NULL;
    size_t size = 0;
    mpack_writer_t writer;
    mpack_writer_init_growable(&writer, &buffer, &size);
    
    mpack_start_map(&writer, 3);
    mpack_write_cstr(&writer, "service");
    mpack_write_cstr(&writer, service);
    mpack_write_cstr(&writer, "method");
    mpack_write_cstr(&writer, method);
    mpack_write_cstr(&writer, "data");
    if (data && data_size > 0) {
        mpack_write_bin(&writer, (const char*)data, data_size);
    } else {
        mpack_write_nil(&writer);
    }
    mpack_finish_map(&writer);
    
    if (mpack_writer_error(&writer) != mpack_ok) {
        mpack_writer_destroy(&writer);
        if (buffer) free(buffer);
        return NULL;
    }
    
    mpack_writer_destroy(&writer);
    *out_size = size;
    return buffer;
}

/* Pack RPC response: {status, data} */
char* uvrpc_pack_response(int status, const uint8_t* data, size_t data_size, size_t* out_size) {
    if (!out_size) return NULL;
    
    char* buffer = NULL;
    size_t size = 0;
    mpack_writer_t writer;
    mpack_writer_init_growable(&writer, &buffer, &size);
    
    mpack_start_map(&writer, 2);
    mpack_write_cstr(&writer, "status");
    mpack_write_int(&writer, status);
    mpack_write_cstr(&writer, "data");
    if (data && data_size > 0) {
        mpack_write_bin(&writer, (const char*)data, data_size);
    } else {
        mpack_write_nil(&writer);
    }
    mpack_finish_map(&writer);
    
    if (mpack_writer_error(&writer) != mpack_ok) {
        mpack_writer_destroy(&writer);
        if (buffer) free(buffer);
        return NULL;
    }
    
    mpack_writer_destroy(&writer);
    *out_size = size;
    return buffer;
}

/* Unpack RPC request */
int uvrpc_unpack_request(const char* buffer, size_t size,
                         char** service, char** method,
                         const uint8_t** data, size_t* data_size) {
    if (!buffer || !service || !method || !data || !data_size) return -1;
    
    mpack_reader_t reader;
    mpack_reader_init_data(&reader, buffer, size);
    
    *service = NULL;
    *method = NULL;
    *data = NULL;
    *data_size = 0;
    
    if (mpack_expect_map(&reader) != 3) {
        mpack_reader_destroy(&reader);
        return -2;
    }
    
    /* Read service */
    const char* key = mpack_expect_str(&reader);
    if (strcmp(key, "service") == 0) {
        const char* val = mpack_expect_str(&reader);
        *service = strdup(val);
    } else {
        mpack_discard(&reader);
    }
    
    /* Read method */
    key = mpack_expect_str(&reader);
    if (strcmp(key, "method") == 0) {
        const char* val = mpack_expect_str(&reader);
        *method = strdup(val);
    } else {
        mpack_discard(&reader);
    }
    
    /* Read data */
    key = mpack_expect_str(&reader);
    if (strcmp(key, "data") == 0) {
        const char* val = mpack_expect_bin(&reader, data_size);
        if (val && *data_size > 0) {
            *data = (const uint8_t*)val;
        }
    } else {
        mpack_discard(&reader);
    }
    
    mpack_done(&reader);
    
    if (mpack_reader_error(&reader) != mpack_ok) {
        mpack_reader_destroy(&reader);
        if (*service) free(*service);
        if (*method) free(*method);
        return -3;
    }
    
    mpack_reader_destroy(&reader);
    return 0;
}

/* Unpack RPC response */
int uvrpc_unpack_response(const char* buffer, size_t size,
                          int* status, const uint8_t** data, size_t* data_size) {
    if (!buffer || !status || !data || !data_size) return -1;
    
    mpack_reader_t reader;
    mpack_reader_init_data(&reader, buffer, size);
    
    *status = 0;
    *data = NULL;
    *data_size = 0;
    
    if (mpack_expect_map(&reader) != 2) {
        mpack_reader_destroy(&reader);
        return -2;
    }
    
    /* Read status */
    const char* key = mpack_expect_str(&reader);
    if (strcmp(key, "status") == 0) {
        *status = mpack_expect_int(&reader);
    } else {
        mpack_discard(&reader);
    }
    
    /* Read data */
    key = mpack_expect_str(&reader);
    if (strcmp(key, "data") == 0) {
        const char* val = mpack_expect_bin(&reader, data_size);
        if (val && *data_size > 0) {
            *data = (const uint8_t*)val;
        }
    } else {
        mpack_discard(&reader);
    }
    
    mpack_done(&reader);
    
    if (mpack_reader_error(&reader) != mpack_ok) {
        mpack_reader_destroy(&reader);
        return -3;
    }
    
    mpack_reader_destroy(&reader);
    return 0;
}