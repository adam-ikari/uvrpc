/**
 * msgpack 序列化/反序列化实现
 */

#include "msgpack_serializer.h"
#include <msgpack.h>
#include <string.h>
#include <stdlib.h>

#define UVRPC_MALLOC(size)      malloc(size)
#define UVRPC_CALLOC(n, size)   calloc(n, size)
#define UVRPC_REALLOC(ptr, size) realloc(ptr, size)
#define UVRPC_FREE(ptr)         do { if (ptr) { free(ptr); ptr = NULL; } } while(0)

int uvrpc_serialize_request(const uvrpc_request_t* request,
                             uint8_t** output, size_t* output_size) {
    if (!request || !output || !output_size) {
        return -1;
    }

    /* 创建 msgpack packer */
    msgpack_sbuffer sbuf;
    msgpack_packer pk;
    msgpack_sbuffer_init(&sbuf);
    msgpack_packer_init(&pk, &sbuf, msgpack_sbuffer_write);

    /* 序列化为 map: {type, request_id, service_id, method_id, request_data} */
    msgpack_pack_map(&pk, 5);

    msgpack_pack_str(&pk, 4);
    msgpack_pack_str_body(&pk, "type", 4);
    msgpack_pack_int(&pk, UVRPC_MSG_TYPE_REQUEST);

    msgpack_pack_str(&pk, 10);
    msgpack_pack_str_body(&pk, "request_id", 10);
    msgpack_pack_uint32(&pk, request->request_id);

    msgpack_pack_str(&pk, 10);
    msgpack_pack_str_body(&pk, "service_id", 10);
    msgpack_pack_str(&pk, strlen(request->service_id ? request->service_id : ""));
    msgpack_pack_str_body(&pk, request->service_id ? request->service_id : "",
                          strlen(request->service_id ? request->service_id : ""));

    msgpack_pack_str(&pk, 9);
    msgpack_pack_str_body(&pk, "method_id", 9);
    msgpack_pack_str(&pk, strlen(request->method_id ? request->method_id : ""));
    msgpack_pack_str_body(&pk, request->method_id ? request->method_id : "",
                          strlen(request->method_id ? request->method_id : ""));

    msgpack_pack_str(&pk, 12);
    msgpack_pack_str_body(&pk, "request_data", 12);
    if (request->request_data && request->request_data_size > 0) {
        msgpack_pack_bin(&pk, request->request_data_size);
        msgpack_pack_bin_body(&pk, request->request_data, request->request_data_size);
    } else {
        msgpack_pack_bin(&pk, 0);
        msgpack_pack_bin_body(&pk, NULL, 0);
    }

    /* 复制输出数据 */
    *output_size = sbuf.size;
    *output = (uint8_t*)UVRPC_MALLOC(*output_size);
    if (!*output) {
        msgpack_sbuffer_destroy(&sbuf);
        return -1;
    }
    memcpy(*output, sbuf.data, *output_size);

    msgpack_sbuffer_destroy(&sbuf);
    return 0;
}

int uvrpc_deserialize_request(const uint8_t* data, size_t size,
                               uvrpc_request_t* request) {
    if (!data || size == 0 || !request) {
        return -1;
    }

    /* 创建 msgpack unpacker */
    msgpack_unpacked result;
    msgpack_unpack_return ret;
    msgpack_unpacked_init(&result);

    ret = msgpack_unpack_next(&result, data, size, NULL);
    if (ret != MSGPACK_UNPACK_SUCCESS) {
        msgpack_unpacked_destroy(&result);
        return -1;
    }

    msgpack_object obj = result.data;
    if (obj.type != MSGPACK_OBJECT_MAP) {
        msgpack_unpacked_destroy(&result);
        return -1;
    }

    /* 初始化请求结构 */
    memset(request, 0, sizeof(uvrpc_request_t));

    /* 解析 map */
    int i;
    for (i = 0; i < obj.via.map.size; i++) {
        msgpack_object_kv* kv = &obj.via.map.ptr[i];

        if (kv->key.type != MSGPACK_OBJECT_STR) {
            continue;
        }

        const char* key = kv->key.via.str.ptr;
        size_t key_len = kv->key.via.str.size;

        if (strncmp(key, "type", key_len) == 0) {
            if (kv->val.type != MSGPACK_OBJECT_POSITIVE_INTEGER ||
                kv->val.via.u64 != UVRPC_MSG_TYPE_REQUEST) {
                msgpack_unpacked_destroy(&result);
                uvrpc_free_request(request);
                return -1;
            }
        } else if (strncmp(key, "request_id", key_len) == 0) {
            if (kv->val.type == MSGPACK_OBJECT_POSITIVE_INTEGER) {
                request->request_id = (uint32_t)kv->val.via.u64;
            }
        } else if (strncmp(key, "service_id", key_len) == 0) {
            if (kv->val.type == MSGPACK_OBJECT_STR && kv->val.via.str.size > 0) {
                request->service_id = (char*)UVRPC_MALLOC(kv->val.via.str.size + 1);
                if (request->service_id) {
                    memcpy(request->service_id, kv->val.via.str.ptr, kv->val.via.str.size);
                    request->service_id[kv->val.via.str.size] = '\0';
                }
            }
        } else if (strncmp(key, "method_id", key_len) == 0) {
            if (kv->val.type == MSGPACK_OBJECT_STR && kv->val.via.str.size > 0) {
                request->method_id = (char*)UVRPC_MALLOC(kv->val.via.str.size + 1);
                if (request->method_id) {
                    memcpy(request->method_id, kv->val.via.str.ptr, kv->val.via.str.size);
                    request->method_id[kv->val.via.str.size] = '\0';
                }
            }
        } else if (strncmp(key, "request_data", key_len) == 0) {
            if (kv->val.type == MSGPACK_OBJECT_BIN && kv->val.via.bin.size > 0) {
                request->request_data = (uint8_t*)UVRPC_MALLOC(kv->val.via.bin.size);
                if (request->request_data) {
                    memcpy(request->request_data, kv->val.via.bin.ptr, kv->val.via.bin.size);
                    request->request_data_size = kv->val.via.bin.size;
                }
            }
        }
    }

    msgpack_unpacked_destroy(&result);
    return 0;
}

int uvrpc_serialize_response(const uvrpc_response_t* response,
                              uint8_t** output, size_t* output_size) {
    if (!response || !output || !output_size) {
        return -1;
    }

    /* 创建 msgpack packer */
    msgpack_sbuffer sbuf;
    msgpack_packer pk;
    msgpack_sbuffer_init(&sbuf);
    msgpack_packer_init(&pk, &sbuf, msgpack_sbuffer_write);

    /* 序列化为 map: {type, request_id, status, error_message, response_data} */
    msgpack_pack_map(&pk, 5);

    msgpack_pack_str(&pk, 4);
    msgpack_pack_str_body(&pk, "type", 4);
    msgpack_pack_int(&pk, UVRPC_MSG_TYPE_RESPONSE);

    msgpack_pack_str(&pk, 10);
    msgpack_pack_str_body(&pk, "request_id", 10);
    msgpack_pack_uint32(&pk, response->request_id);

    msgpack_pack_str(&pk, 6);
    msgpack_pack_str_body(&pk, "status", 6);
    msgpack_pack_int(&pk, response->status);

    msgpack_pack_str(&pk, 13);
    msgpack_pack_str_body(&pk, "error_message", 13);
    msgpack_pack_str(&pk, strlen(response->error_message ? response->error_message : ""));
    msgpack_pack_str_body(&pk, response->error_message ? response->error_message : "",
                          strlen(response->error_message ? response->error_message : ""));

    msgpack_pack_str(&pk, 13);
    msgpack_pack_str_body(&pk, "response_data", 13);
    if (response->response_data && response->response_data_size > 0) {
        msgpack_pack_bin(&pk, response->response_data_size);
        msgpack_pack_bin_body(&pk, response->response_data, response->response_data_size);
    } else {
        msgpack_pack_bin(&pk, 0);
        msgpack_pack_bin_body(&pk, NULL, 0);
    }

    /* 复制输出数据 */
    *output_size = sbuf.size;
    *output = (uint8_t*)UVRPC_MALLOC(*output_size);
    if (!*output) {
        msgpack_sbuffer_destroy(&sbuf);
        return -1;
    }
    memcpy(*output, sbuf.data, *output_size);

    msgpack_sbuffer_destroy(&sbuf);
    return 0;
}

int uvrpc_deserialize_response(const uint8_t* data, size_t size,
                                uvrpc_response_t* response) {
    if (!data || size == 0 || !response) {
        return -1;
    }

    /* 创建 msgpack unpacker */
    msgpack_unpacked result;
    msgpack_unpack_return ret;
    msgpack_unpacked_init(&result);

    ret = msgpack_unpack_next(&result, data, size, NULL);
    if (ret != MSGPACK_UNPACK_SUCCESS) {
        msgpack_unpacked_destroy(&result);
        return -1;
    }

    msgpack_object obj = result.data;
    if (obj.type != MSGPACK_OBJECT_MAP) {
        msgpack_unpacked_destroy(&result);
        return -1;
    }

    /* 初始化响应结构 */
    memset(response, 0, sizeof(uvrpc_response_t));

    /* 解析 map */
    int i;
    for (i = 0; i < obj.via.map.size; i++) {
        msgpack_object_kv* kv = &obj.via.map.ptr[i];

        if (kv->key.type != MSGPACK_OBJECT_STR) {
            continue;
        }

        const char* key = kv->key.via.str.ptr;
        size_t key_len = kv->key.via.str.size;

        if (strncmp(key, "type", key_len) == 0) {
            if (kv->val.type != MSGPACK_OBJECT_POSITIVE_INTEGER ||
                kv->val.via.u64 != UVRPC_MSG_TYPE_RESPONSE) {
                msgpack_unpacked_destroy(&result);
                uvrpc_free_response(response);
                return -1;
            }
        } else if (strncmp(key, "request_id", key_len) == 0) {
            if (kv->val.type == MSGPACK_OBJECT_POSITIVE_INTEGER) {
                response->request_id = (uint32_t)kv->val.via.u64;
            }
        } else if (strncmp(key, "status", key_len) == 0) {
            if (kv->val.type == MSGPACK_OBJECT_POSITIVE_INTEGER) {
                response->status = (int32_t)kv->val.via.i64;
            }
        } else if (strncmp(key, "error_message", key_len) == 0) {
            if (kv->val.type == MSGPACK_OBJECT_STR && kv->val.via.str.size > 0) {
                response->error_message = (char*)UVRPC_MALLOC(kv->val.via.str.size + 1);
                if (response->error_message) {
                    memcpy(response->error_message, kv->val.via.str.ptr, kv->val.via.str.size);
                    response->error_message[kv->val.via.str.size] = '\0';
                }
            }
        } else if (strncmp(key, "response_data", key_len) == 0) {
            if (kv->val.type == MSGPACK_OBJECT_BIN && kv->val.via.bin.size > 0) {
                response->response_data = (uint8_t*)UVRPC_MALLOC(kv->val.via.bin.size);
                if (response->response_data) {
                    memcpy(response->response_data, kv->val.via.bin.ptr, kv->val.via.bin.size);
                    response->response_data_size = kv->val.via.bin.size;
                }
            }
        }
    }

    msgpack_unpacked_destroy(&result);
    return 0;
}

void uvrpc_free_serialized_data(uint8_t* data) {
    UVRPC_FREE(data);
}

void uvrpc_free_request(uvrpc_request_t* request) {
    if (!request) {
        return;
    }
    UVRPC_FREE(request->service_id);
    UVRPC_FREE(request->method_id);
    UVRPC_FREE(request->request_data);
}

void uvrpc_free_response(uvrpc_response_t* response) {
    if (!response) {
        return;
    }
    UVRPC_FREE(response->error_message);
    UVRPC_FREE(response->response_data);
}