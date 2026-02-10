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

    /* 创建 map: request_id, service_id, method_id, request_data */
    mpack_start_map(&writer, 4);

    /* request_id */
    mpack_write_cstr(&writer, "request_id");
    mpack_write_uint(&writer, request->request_id);

    /* service_id */
    mpack_write_cstr(&writer, "service_id");
    if (request->service_id) {
        mpack_write_cstr(&writer, request->service_id);
    } else {
        mpack_write_nil(&writer);
    }

    /* method_id */
    mpack_write_cstr(&writer, "method_id");
    if (request->method_id) {
        mpack_write_cstr(&writer, request->method_id);
    } else {
        mpack_write_nil(&writer);
    }

    /* request_data */
    mpack_write_cstr(&writer, "request_data");
    if (request->request_data && request->request_data_size > 0) {
        mpack_write_bin(&writer, (const char*)request->request_data, request->request_data_size);
    } else {
        mpack_write_nil(&writer);
    }

    mpack_finish_map(&writer);

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

    /* 读取 map */
    uint32_t count = mpack_expect_map_max(&reader, UVRPC_MAX_MAP_FIELDS);

    /* 逐个读取字段 */
    for (uint32_t i = count; i > 0 && mpack_reader_error(&reader) == mpack_ok; --i) {
        char key[UVRPC_MAX_KEY_LENGTH + 1];
        mpack_expect_cstr(&reader, key, sizeof(key));
        if (strcmp(key, "request_id") == 0) {
            request->request_id = (uint32_t)mpack_expect_uint(&reader);
        } else if (strcmp(key, "service_id") == 0) {
            if (mpack_reader_error(&reader) == mpack_ok) {
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
            } else {
                mpack_discard(&reader);
            }
        } else if (strcmp(key, "method_id") == 0) {
            if (mpack_reader_error(&reader) == mpack_ok) {
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
            } else {
                mpack_discard(&reader);
            }
        } else if (strcmp(key, "request_data") == 0) {
            if (mpack_reader_error(&reader) == mpack_ok) {
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
            } else {
                mpack_discard(&reader);
            }
        } else {
            mpack_discard(&reader);
        }
    }

    mpack_done_map(&reader);

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

    /* 创建 map: request_id, status, error_message, response_data */
    mpack_start_map(&writer, 4);

    /* request_id */
    mpack_write_cstr(&writer, "request_id");
    mpack_write_uint(&writer, response->request_id);

    /* status */
    mpack_write_cstr(&writer, "status");
    mpack_write_int(&writer, response->status);

    /* error_message */
    mpack_write_cstr(&writer, "error_message");
    if (response->error_message) {
        mpack_write_cstr(&writer, response->error_message);
    } else {
        mpack_write_nil(&writer);
    }

    /* response_data */
    mpack_write_cstr(&writer, "response_data");
    if (response->response_data && response->response_data_size > 0) {
        mpack_write_bin(&writer, (const char*)response->response_data, response->response_data_size);
    } else {
        mpack_write_nil(&writer);
    }

    mpack_finish_map(&writer);

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

    /* 读取 map */
    uint32_t count = mpack_expect_map(&reader);

    /* 逐个读取字段 */
    for (uint32_t i = count; i > 0 && mpack_reader_error(&reader) == mpack_ok; --i) {
        char key[UVRPC_MAX_KEY_LENGTH + 1];
        mpack_expect_cstr(&reader, key, sizeof(key));

        if (strcmp(key, "request_id") == 0) {
            response->request_id = (uint32_t)mpack_expect_uint(&reader);
        } else if (strcmp(key, "status") == 0) {
            response->status = (int32_t)mpack_expect_int(&reader);
        } else if (strcmp(key, "error_message") == 0) {
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
        } else if (strcmp(key, "response_data") == 0) {
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
        } else {
            mpack_discard(&reader);
        }
    }

    mpack_done_map(&reader);

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