/**
 * JSON 序列化/反序列化实现
 * 简单的 JSON 实现，用于 RPC 消息的序列化和反序列化
 */

#include "json_serializer.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* 简单的 JSON 字符串转义 */
static void json_escape_string(const char* input, char* output, size_t output_size) {
    size_t j = 0;
    for (size_t i = 0; input[i] != '\0' && j < output_size - 1; i++) {
        switch (input[i]) {
            case '"':  if (j < output_size - 2) { output[j++] = '\\'; output[j++] = '"'; } break;
            case '\\': if (j < output_size - 2) { output[j++] = '\\'; output[j++] = '\\'; } break;
            case '\b': if (j < output_size - 2) { output[j++] = '\\'; output[j++] = 'b'; } break;
            case '\f': if (j < output_size - 2) { output[j++] = '\\'; output[j++] = 'f'; } break;
            case '\n': if (j < output_size - 2) { output[j++] = '\\'; output[j++] = 'n'; } break;
            case '\r': if (j < output_size - 2) { output[j++] = '\\'; output[j++] = 'r'; } break;
            case '\t': if (j < output_size - 2) { output[j++] = '\\'; output[j++] = 't'; } break;
            default:   if (j < output_size - 1) output[j++] = input[i]; break;
        }
    }
    output[j] = '\0';
}

/* 序列化 RPC 请求为 JSON */
int uvrpc_serialize_request_json(const uvrpc_request_t* request,
                                   uint8_t** output, size_t* output_size) {
    if (!request || !output || !output_size) {
        return -1;
    }

    /* 计算所需的缓冲区大小 */
    size_t size = 256;  /* 基本开销 */
    if (request->service_id) size += strlen(request->service_id) * 2;
    if (request->method_id) size += strlen(request->method_id) * 2;
    size += request->request_data_size * 4;  /* Base64 编码开销 */

    char* buffer = (char*)malloc(size);
    if (!buffer) {
        return -1;
    }

    /* 转义字符串 */
    char service_escaped[512] = {0};
    char method_escaped[512] = {0};
    if (request->service_id) {
        json_escape_string(request->service_id, service_escaped, sizeof(service_escaped));
    }
    if (request->method_id) {
        json_escape_string(request->method_id, method_escaped, sizeof(method_escaped));
    }

    /* 构建 JSON */
    int len = snprintf(buffer, size,
        "{\"request_id\":%u,\"service_id\":\"%s\",\"method_id\":\"%s\",\"request_data_size\":%zu}",
        request->request_id,
        service_escaped,
        method_escaped,
        request->request_data_size);

    if (len < 0 || (size_t)len >= size) {
        free(buffer);
        return -1;
    }

    /* 添加二进制数据（这里简单地将数据追加到 JSON 后面，实际应用中应该使用 Base64 编码） */
    if (request->request_data && request->request_data_size > 0) {
        char* data_pos = buffer + len;
        size_t remaining = size - len;
        int data_len = snprintf(data_pos, remaining, ",\"request_data\":\"");
        if (data_len < 0 || (size_t)data_len >= remaining) {
            free(buffer);
            return -1;
        }
        len += data_len;
        data_pos += data_len;
        remaining -= data_len;

        /* 简单的十六进制编码 */
        for (size_t i = 0; i < request->request_data_size && remaining > 2; i++) {
            int hex_len = snprintf(data_pos, remaining, "%02x", request->request_data[i]);
            if (hex_len < 0 || (size_t)hex_len >= remaining) break;
            len += hex_len;
            data_pos += hex_len;
            remaining -= hex_len;
        }

        int quote_len = snprintf(data_pos, remaining, "\"}");
        if (quote_len < 0 || (size_t)quote_len >= remaining) {
            free(buffer);
            return -1;
        }
        len += quote_len;
    } else {
        /* 关闭 JSON */
        int close_len = snprintf(buffer + len, size - len, "}");
        if (close_len < 0 || (size_t)close_len >= size - len) {
            free(buffer);
            return -1;
        }
        len += close_len;
    }

    *output = (uint8_t*)buffer;
    *output_size = len;
    return 0;
}

/* 简单的 JSON 解析器 */
static int json_parse_string(const char* json, const char* key, char* output, size_t output_size) {
    char search_key[256];
    snprintf(search_key, sizeof(search_key), "\"%s\":\"", key);

    const char* start = strstr(json, search_key);
    if (!start) {
        return -1;
    }

    start += strlen(search_key);
    const char* end = strchr(start, '"');
    if (!end) {
        return -1;
    }

    size_t len = end - start;
    if (len >= output_size) {
        len = output_size - 1;
    }

    /* 复制并处理转义字符 */
    size_t j = 0;
    for (size_t i = 0; i < len && j < output_size - 1; i++) {
        if (start[i] == '\\' && i + 1 < len) {
            switch (start[i + 1]) {
                case '"':  output[j++] = '"'; i++; break;
                case '\\': output[j++] = '\\'; i++; break;
                case 'b':  output[j++] = '\b'; i++; break;
                case 'f':  output[j++] = '\f'; i++; break;
                case 'n':  output[j++] = '\n'; i++; break;
                case 'r':  output[j++] = '\r'; i++; break;
                case 't':  output[j++] = '\t'; i++; break;
                default:   output[j++] = start[i]; break;
            }
        } else {
            output[j++] = start[i];
        }
    }
    output[j] = '\0';

    return 0;
}

static int json_parse_uint32(const char* json, const char* key, uint32_t* output) {
    char search_key[256];
    snprintf(search_key, sizeof(search_key), "\"%s\":", key);

    const char* start = strstr(json, search_key);
    if (!start) {
        return -1;
    }

    start += strlen(search_key);
    while (*start == ' ') start++;

    return sscanf(start, "%u", output) == 1 ? 0 : -1;
}

static int json_parse_int32(const char* json, const char* key, int32_t* output) {
    char search_key[256];
    snprintf(search_key, sizeof(search_key), "\"%s\":", key);

    const char* start = strstr(json, search_key);
    if (!start) {
        return -1;
    }

    start += strlen(search_key);
    while (*start == ' ') start++;

    return sscanf(start, "%d", output) == 1 ? 0 : -1;
}

static int json_parse_size_t(const char* json, const char* key, size_t* output) {
    char search_key[256];
    snprintf(search_key, sizeof(search_key), "\"%s\":", key);

    const char* start = strstr(json, search_key);
    if (!start) {
        return -1;
    }

    start += strlen(search_key);
    while (*start == ' ') start++;

    unsigned long val;
    if (sscanf(start, "%lu", &val) != 1) {
        return -1;
    }
    *output = (size_t)val;
    return 0;
}

/* 反序列化 JSON 为 RPC 请求 */
int uvrpc_deserialize_request_json(const uint8_t* data, size_t size,
                                    uvrpc_request_t* request) {
    (void)size;  /* 避免未使用参数警告 */
    if (!data || !request) {
        return -1;
    }

    memset(request, 0, sizeof(uvrpc_request_t));

    const char* json = (const char*)data;

    /* 解析 request_id */
    if (json_parse_uint32(json, "request_id", &request->request_id) != 0) {
        return -1;
    }

    /* 解析 service_id */
    request->service_id = (char*)malloc(512);
    if (!request->service_id) {
        return -1;
    }
    if (json_parse_string(json, "service_id", request->service_id, 512) != 0) {
        free(request->service_id);
        request->service_id = NULL;
        return -1;
    }

    /* 解析 method_id */
    request->method_id = (char*)malloc(512);
    if (!request->method_id) {
        free(request->service_id);
        request->service_id = NULL;
        return -1;
    }
    if (json_parse_string(json, "method_id", request->method_id, 512) != 0) {
        free(request->service_id);
        free(request->method_id);
        request->service_id = NULL;
        request->method_id = NULL;
        return -1;
    }

    /* 解析 request_data_size */
    if (json_parse_size_t(json, "request_data_size", &request->request_data_size) != 0) {
        free(request->service_id);
        free(request->method_id);
        request->service_id = NULL;
        request->method_id = NULL;
        return -1;
    }

    /* 解析 request_data (十六进制字符串) */
    if (request->request_data_size > 0) {
        char search_key[256];
        snprintf(search_key, sizeof(search_key), "\"request_data\":\"");

        const char* start = strstr(json, search_key);
        if (start) {
            start += strlen(search_key);
            const char* end = strchr(start, '"');
            if (end) {
                size_t hex_len = end - start;
                request->request_data = (uint8_t*)malloc(request->request_data_size);
                if (request->request_data) {
                    for (size_t i = 0; i < request->request_data_size && i * 2 < hex_len; i++) {
                        unsigned int val;
                        if (sscanf(start + i * 2, "%2x", &val) == 1) {
                            request->request_data[i] = (uint8_t)val;
                        }
                    }
                }
            }
        }
    }

    return 0;
}

/* 序列化 RPC 响应为 JSON */
int uvrpc_serialize_response_json(const uvrpc_response_t* response,
                                   uint8_t** output, size_t* output_size) {
    if (!response || !output || !output_size) {
        return -1;
    }

    size_t size = 256;
    if (response->error_message) size += strlen(response->error_message) * 2;
    size += response->response_data_size * 4;

    char* buffer = (char*)malloc(size);
    if (!buffer) {
        return -1;
    }

    char error_escaped[512] = {0};
    if (response->error_message) {
        json_escape_string(response->error_message, error_escaped, sizeof(error_escaped));
    }

    int len = snprintf(buffer, size,
        "{\"request_id\":%u,\"status\":%d,\"error_message\":\"%s\",\"response_data_size\":%zu",
        response->request_id,
        response->status,
        error_escaped,
        response->response_data_size);

    if (len < 0 || (size_t)len >= size) {
        free(buffer);
        return -1;
    }

    if (response->response_data && response->response_data_size > 0) {
        char* data_pos = buffer + len;
        size_t remaining = size - len;
        int data_len = snprintf(data_pos, remaining, ",\"response_data\":\"");
        if (data_len < 0 || (size_t)data_len >= remaining) {
            free(buffer);
            return -1;
        }
        len += data_len;
        data_pos += data_len;
        remaining -= data_len;

        for (size_t i = 0; i < response->response_data_size && remaining > 2; i++) {
            int hex_len = snprintf(data_pos, remaining, "%02x", response->response_data[i]);
            if (hex_len < 0 || (size_t)hex_len >= remaining) break;
            len += hex_len;
            data_pos += hex_len;
            remaining -= hex_len;
        }

        int quote_len = snprintf(data_pos, remaining, "\"}");
        if (quote_len < 0 || (size_t)quote_len >= remaining) {
            free(buffer);
            return -1;
        }
        len += quote_len;
    } else {
        int close_len = snprintf(buffer + len, size - len, "}");
        if (close_len < 0 || (size_t)close_len >= size - len) {
            free(buffer);
            return -1;
        }
        len += close_len;
    }

    *output = (uint8_t*)buffer;
    *output_size = len;
    return 0;
}

/* 反序列化 JSON 为 RPC 响应 */
int uvrpc_deserialize_response_json(const uint8_t* data, size_t size,
                                     uvrpc_response_t* response) {
    (void)size;  /* 避免未使用参数警告 */
    if (!data || !response) {
        return -1;
    }

    memset(response, 0, sizeof(uvrpc_response_t));

    const char* json = (const char*)data;

    if (json_parse_uint32(json, "request_id", &response->request_id) != 0) {
        return -1;
    }

    if (json_parse_int32(json, "status", &response->status) != 0) {
        return -1;
    }

    response->error_message = (char*)malloc(512);
    if (response->error_message) {
        if (json_parse_string(json, "error_message", response->error_message, 512) != 0) {
            free(response->error_message);
            response->error_message = NULL;
        }
    }

    if (json_parse_size_t(json, "response_data_size", &response->response_data_size) != 0) {
        return -1;
    }

    if (response->response_data_size > 0) {
        char search_key[256];
        snprintf(search_key, sizeof(search_key), "\"response_data\":\"");

        const char* start = strstr(json, search_key);
        if (start) {
            start += strlen(search_key);
            const char* end = strchr(start, '"');
            if (end) {
                size_t hex_len = end - start;
                response->response_data = (uint8_t*)malloc(response->response_data_size);
                if (response->response_data) {
                    for (size_t i = 0; i < response->response_data_size && i * 2 < hex_len; i++) {
                        unsigned int val;
                        if (sscanf(start + i * 2, "%2x", &val) == 1) {
                            response->response_data[i] = (uint8_t)val;
                        }
                    }
                }
            }
        }
    }

    return 0;
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
