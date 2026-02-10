/**
 * msgpack 序列化/反序列化接口
 * 提供纯 C99 接口，用于 RPC 消息的序列化和反序列化
 */

#ifndef MSGPACK_SERIALIZER_H
#define MSGPACK_SERIALIZER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

/* RPC 消息类型 */
typedef enum {
    UVRPC_MSG_TYPE_REQUEST = 0,
    UVRPC_MSG_TYPE_RESPONSE = 1,
    UVRPC_MSG_TYPE_ERROR = 2
} uvrpc_msg_type_t;

/* RPC 请求结构 */
typedef struct uvrpc_request {
    uint32_t request_id;
    char* service_id;
    char* method_id;
    uint8_t* request_data;
    size_t request_data_size;
} uvrpc_request_t;

/* RPC 响应结构 */
typedef struct uvrpc_response {
    uint32_t request_id;
    int32_t status;
    char* error_message;
    uint8_t* response_data;
    size_t response_data_size;
} uvrpc_response_t;

/**
 * 序列化 RPC 请求
 * @param request 请求结构
 * @param output 输出数据指针（调用者负责使用 uvrpc_free_serialized_data 释放）
 * @param output_size 输出数据大小
 * @return 0 成功，-1 失败
 */
int uvrpc_serialize_request(const uvrpc_request_t* request,
                             uint8_t** output, size_t* output_size);

/**
 * 反序列化 RPC 请求
 * @param data 输入数据
 * @param size 数据大小
 * @param request 输出请求结构（调用者负责使用 uvrpc_free_request 释放）
 * @return 0 成功，-1 失败
 */
int uvrpc_deserialize_request(const uint8_t* data, size_t size,
                               uvrpc_request_t* request);

/**
 * 序列化 RPC 响应
 * @param response 响应结构
 * @param output 输出数据指针（调用者负责使用 uvrpc_free_serialized_data 释放）
 * @param output_size 输出数据大小
 * @return 0 成功，-1 失败
 */
int uvrpc_serialize_response(const uvrpc_response_t* response,
                              uint8_t** output, size_t* output_size);

/**
 * 反序列化 RPC 响应
 * @param data 输入数据
 * @param size 数据大小
 * @param response 输出响应结构（调用者负责使用 uvrpc_free_response 释放）
 * @return 0 成功，-1 失败
 */
int uvrpc_deserialize_response(const uint8_t* data, size_t size,
                                uvrpc_response_t* response);

/**
 * 释放序列化分配的内存
 * @param data 数据指针
 */
void uvrpc_free_serialized_data(uint8_t* data);

/**
 * 释放请求结构分配的内存
 * @param request 请求结构指针
 */
void uvrpc_free_request(uvrpc_request_t* request);

/**
 * 释放响应结构分配的内存
 * @param response 响应结构指针
 */
void uvrpc_free_response(uvrpc_response_t* response);

#ifdef __cplusplus
}
#endif

#endif /* MSGPACK_SERIALIZER_H */