/**
 * mpack 序列化/反序列化实现
 * 使用 mpack 库进行二进制序列化
 */

#include "msgpack_wrapper.h"
#include "uvrpc_internal.h"
#include <mpack.h>
#include <string.h>

/* 默认缓冲区大小 */
#define UVRPC_DEFAULT_BUFFER_SIZE 4096
/* 最大 map 字段数 */
#define UVRPC_MAX_MAP_FIELDS 10
/* 最大 key 长度 */
#define UVRPC_MAX_KEY_LENGTH 127

/* 序列化 RPC 请求为 mpack */
int uvrpc_serialize_request_msgpack(const uvrpc_request_t* request,
                                      uint8_t** output, size_t* output_size) {
    if (!request || !output || !output_size) {
        return UVRPC_ERROR_INVALID_PARAM;
    }

    /* 创建 mpack writer */
    char* buffer = (char*)UVRPC_MALLOC(UVRPC_DEFAULT_BUFFER_SIZE);
    if (!buffer) {
        return UVRPC_ERROR_NO_MEMORY;
    }

    mpack_writer_t writer;
    mpack_writer_init(&writer, buffer, UVRPC_DEFAULT_BUFFER_SIZE);

    /* 使用数组格式代替 map 格式以减少开销 */
    /* 数组格式: [request_id, service_id, method_id, request_data] */
    mpack_start_array(&writer, 4);

    /* 0: request_id */
    mpack_write_uint(&writer, request->request_id);

    /* 1: service_id */
    if (request->service_id) {
        mpack_write_cstr(&writer, request->service_id);
    } else {
        mpack_write_nil(&writer);
    }

    /* 2: method_id */
    if (request->method_id) {
        mpack_write_cstr(&writer, request->method_id);
    } else {
        mpack_write_nil(&writer);
    }

    /* 3: request_data */
    if (request->request_data && request->request_data_size > 0) {
        mpack_write_bin(&writer, (const char*)request->request_data, request->request_data_size);
    } else {
        mpack_write_nil(&writer);
    }

    mpack_finish_array(&writer);

    /* 检查错误 */
    if (mpack_writer_error(&writer) != mpack_ok) {
        UVRPC_FREE(buffer);
        return UVRPC_ERROR;
    }

    /* 获取数据 */
    size_t size = mpack_writer_buffer_used(&writer);
    char* data = (char*)UVRPC_MALLOC(size);
    if (!data) {
        UVRPC_FREE(buffer);
        return UVRPC_ERROR_NO_MEMORY;
    }
    memcpy(data, buffer, size);
    UVRPC_FREE(buffer);

    *output = (uint8_t*)data;
    *output_size = size;

    return UVRPC_OK;
}

/* 反序列化 mpack 为 RPC 请求 */
int uvrpc_deserialize_request_msgpack(const uint8_t* data, size_t size,
                                       uvrpc_request_t* request) {
    if (!data || !request) {
        return UVRPC_ERROR_INVALID_PARAM;
    }

    memset(request, 0, sizeof(uvrpc_request_t));

    /* 解析 mpack */
    mpack_reader_t reader;
    mpack_reader_init_data(&reader, (const char*)data, size);

    /* 读取数组格式: [request_id, service_id, method_id, request_data] */
    uint32_t count = mpack_expect_array(&reader);
    if (count < 4) {
        UVRPC_LOG_ERROR("Invalid array format: expected 4 elements, got %u", count);
        mpack_reader_destroy(&reader);
        return UVRPC_ERROR;
    }

    /* 0: request_id */
    request->request_id = (uint32_t)mpack_expect_uint(&reader);

    /* 1: service_id */
    if (mpack_peek_tag(&reader).type == mpack_type_nil) {
        mpack_discard(&reader);  /* 跳过 nil 值 */
    } else {
        uint32_t len = mpack_expect_str(&reader);
        if (mpack_reader_error(&reader) == mpack_ok && len > 0) {
            const char* str = mpack_read_bytes_inplace(&reader, len);
            if (mpack_reader_error(&reader) == mpack_ok && str) {
                request->service_id = strndup(str, len);
            }
            mpack_done_str(&reader);
        } else {
            mpack_discard(&reader);
        }
    }

    /* 2: method_id */
    if (mpack_peek_tag(&reader).type == mpack_type_nil) {
        mpack_discard(&reader);  /* 跳过 nil 值 */
    } else {
        uint32_t len = mpack_expect_str(&reader);
        if (mpack_reader_error(&reader) == mpack_ok && len > 0) {
            const char* str = mpack_read_bytes_inplace(&reader, len);
            if (mpack_reader_error(&reader) == mpack_ok && str) {
                request->method_id = strndup(str, len);
            }
            mpack_done_str(&reader);
        } else {
            mpack_discard(&reader);
        }
    }

    /* 3: request_data */
    if (mpack_peek_tag(&reader).type == mpack_type_nil) {
        mpack_discard(&reader);  /* 跳过 nil 值 */
    } else {
        uint32_t bin_size = mpack_expect_bin(&reader);
        if (mpack_reader_error(&reader) == mpack_ok && bin_size > 0) {
            const char* bin_data = mpack_read_bytes_inplace(&reader, bin_size);
            if (mpack_reader_error(&reader) == mpack_ok && bin_data) {
                request->request_data_size = bin_size;
                request->request_data = (uint8_t*)UVRPC_MALLOC(bin_size);
                if (!request->request_data) {
                    UVRPC_LOG_ERROR("Failed to allocate request_data: %u bytes", bin_size);
                    mpack_reader_destroy(&reader);
                    uvrpc_free_request(request);
                    return UVRPC_ERROR_NO_MEMORY;
                }
                memcpy(request->request_data, bin_data, bin_size);
            }
            mpack_done_bin(&reader);
        } else {
            mpack_discard(&reader);
        }
    }

    /* 丢弃剩余元素（如果有） */
    while (count-- > 4) {
        mpack_discard(&reader);
    }

    mpack_done_array(&reader);

    if (mpack_reader_error(&reader) != mpack_ok) {
        uvrpc_free_request(request);
        mpack_reader_destroy(&reader);
        return UVRPC_ERROR;
    }

    mpack_reader_destroy(&reader);

    return UVRPC_OK;
}

/* 序列化 RPC 响应为 mpack */
int uvrpc_serialize_response_msgpack(const uvrpc_response_t* response,
                                      uint8_t** output, size_t* output_size) {
    if (!response || !output || !output_size) {
        return UVRPC_ERROR_INVALID_PARAM;
    }

    /* 创建 mpack writer */
    char* buffer = (char*)UVRPC_MALLOC(UVRPC_DEFAULT_BUFFER_SIZE);
    if (!buffer) {
        return UVRPC_ERROR_NO_MEMORY;
    }

    mpack_writer_t writer;
    mpack_writer_init(&writer, buffer, UVRPC_DEFAULT_BUFFER_SIZE);

    /* 使用数组格式代替 map 格式以减少开销 */
    /* 数组格式: [request_id, status, error_message, response_data] */
    mpack_start_array(&writer, 4);

    /* 0: request_id */
    mpack_write_uint(&writer, response->request_id);

    /* 1: status */
    mpack_write_int(&writer, response->status);

    /* 2: error_message */
    if (response->error_message) {
        mpack_write_cstr(&writer, response->error_message);
    } else {
        mpack_write_nil(&writer);
    }

    /* 3: response_data */
    if (response->response_data && response->response_data_size > 0) {
        mpack_write_bin(&writer, (const char*)response->response_data, response->response_data_size);
    } else {
        mpack_write_nil(&writer);
    }

    mpack_finish_array(&writer);

    /* 检查错误 */
    if (mpack_writer_error(&writer) != mpack_ok) {
        UVRPC_FREE(buffer);
        return UVRPC_ERROR;
    }

    /* 获取数据 */
    size_t size = mpack_writer_buffer_used(&writer);
    char* data = (char*)UVRPC_MALLOC(size);
    if (!data) {
        UVRPC_FREE(buffer);
        return UVRPC_ERROR_NO_MEMORY;
    }
    memcpy(data, buffer, size);
    UVRPC_FREE(buffer);

    *output = (uint8_t*)data;
    *output_size = size;

    return UVRPC_OK;
}

/* 反序列化 mpack 为 RPC 响应 */
int uvrpc_deserialize_response_msgpack(const uint8_t* data, size_t size,
                                        uvrpc_response_t* response) {
    if (!data || !response) {
        return UVRPC_ERROR_INVALID_PARAM;
    }

    memset(response, 0, sizeof(uvrpc_response_t));

    /* 解析 mpack */
    mpack_reader_t reader;
    mpack_reader_init_data(&reader, (const char*)data, size);

    /* 读取数组格式: [request_id, status, error_message, response_data] */
    uint32_t count = mpack_expect_array(&reader);
    if (count < 4) {
        fprintf(stderr, "[RESPONSE_DESERIALIZE] Invalid array format: expected 4 elements, got %u\n", count);
        mpack_reader_destroy(&reader);
        return UVRPC_ERROR;
    }

    /* 0: request_id */
    response->request_id = (uint32_t)mpack_expect_uint(&reader);

    /* 1: status */
    response->status = (int32_t)mpack_expect_int(&reader);

    /* 2: error_message */
    if (mpack_peek_tag(&reader).type == mpack_type_nil) {
        mpack_discard(&reader);  /* 跳过 nil 值 */
    } else {
        uint32_t len = mpack_expect_str(&reader);
        if (mpack_reader_error(&reader) == mpack_ok && len > 0) {
            const char* str = mpack_read_bytes_inplace(&reader, len);
            if (str) {
                response->error_message = strndup(str, len);
            }
            mpack_done_str(&reader);
        } else {
            mpack_discard(&reader);
        }
    }

    /* 3: response_data */
    if (mpack_peek_tag(&reader).type == mpack_type_nil) {
        mpack_discard(&reader);  /* 跳过 nil 值 */
    } else {
        uint32_t bin_size = mpack_expect_bin(&reader);
        if (mpack_reader_error(&reader) == mpack_ok && bin_size > 0) {
            const char* bin_data = mpack_read_bytes_inplace(&reader, bin_size);
            if (bin_data) {
                response->response_data_size = bin_size;
                response->response_data = (uint8_t*)UVRPC_MALLOC(bin_size);
                if (!response->response_data) {
                    UVRPC_LOG_ERROR("Failed to allocate response_data: %u bytes", bin_size);
                    mpack_reader_destroy(&reader);
                    uvrpc_free_response(response);
                    return UVRPC_ERROR_NO_MEMORY;
                }
                memcpy(response->response_data, bin_data, bin_size);
            }
            mpack_done_bin(&reader);
        } else {
            mpack_discard(&reader);
        }
    }

    /* 丢弃剩余元素（如果有） */
    while (count-- > 4) {
        mpack_discard(&reader);
    }

    mpack_done_array(&reader);

    if (mpack_reader_error(&reader) != mpack_ok) {
        fprintf(stderr, "[RESPONSE_DESERIALIZE] Failed, error=%d\n", mpack_reader_error(&reader));
        uvrpc_free_response(response);
        mpack_reader_destroy(&reader);
        return UVRPC_ERROR;
    }

    mpack_reader_destroy(&reader);

    return UVRPC_OK;
}

/* 释放函数 */
void uvrpc_free_serialized_data(uint8_t* data) {
    if (data) {
        free(data);
    }
}

void uvrpc_free_request(uvrpc_request_t* request) {
    if (request) {
        if (request->service_id) free(request->service_id);
        if (request->method_id) free(request->method_id);
        if (request->request_data) free(request->request_data);
        memset(request, 0, sizeof(uvrpc_request_t));
    }
}

void uvrpc_free_response(uvrpc_response_t* response) {
    if (response) {
        if (response->error_message) free(response->error_message);
        if (response->response_data) free(response->response_data);
        memset(response, 0, sizeof(uvrpc_response_t));
    }
}