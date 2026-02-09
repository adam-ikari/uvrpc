#include "uvrpc_internal.h"
#include "uvrpc_generated.h"  /* flatbuffers 生成的头文件 */
#include <string.h>
#include <stdlib.h>

/* uvzmq 接收回调 */
static void on_zmq_recv(uvzmq_socket_t* socket, zmq_msg_t* msg, void* arg) {
    uvrpc_client_t* client = (uvrpc_client_t*)arg;

    if (!client) {
        return;
    }

    /* 获取消息数据 */
    void* data = zmq_msg_data(msg);
    size_t size = zmq_msg_size(msg);

    UVRPC_LOG_DEBUG("Client received %zu bytes", size);

    /* 解析 flatbuffers 响应 */
    flatbuffers_t* fb = flatbuffers_init(data, size);
    if (!fb) {
        UVRPC_LOG_ERROR("Failed to init flatbuffers");
        return;
    }

    uvrpc_RpcResponse_table_t response;
    if (!uvrpc_RpcResponse_as_root(fb)) {
        UVRPC_LOG_ERROR("Invalid RpcResponse message");
        flatbuffers_clear(fb);
        return;
    }
    uvrpc_RpcResponse_init(&response, fb);

    /* 获取响应状态和数据 */
    int32_t status = uvrpc_RpcResponse_status(&response);
    flatbuffers_uint8_vec_t response_vec = uvrpc_RpcResponse_response_data(&response);
    const uint8_t* response_data = response_vec.data;
    size_t response_size = response_vec.len;

    UVRPC_LOG_DEBUG("Received response with status: %d", status);

    /* 调用回调函数（这里简化处理，实际应该使用 request_id 匹配） */
    /* TODO: 实现基于 request_id 的请求匹配 */
    /* 暂时使用第一个 pending request */
    uvrpc_client_request_t* req = client->pending_requests;
    if (req) {
        HASH_DEL(client->pending_requests, req);
        if (req->callback) {
            req->callback(req->ctx, status, response_data, response_size);
        }
        UVRPC_FREE(req);
    }

    flatbuffers_clear(fb);
}

/* 创建客户端 */
uvrpc_client_t* uvrpc_client_new(uv_loop_t* loop, const char* server_addr, int zmq_type) {
    if (!server_addr) {
        UVRPC_LOG_ERROR("server_addr cannot be NULL");
        return NULL;
    }

    /* 默认使用 ZMQ_REQ */
    if (zmq_type == 0) {
        zmq_type = ZMQ_REQ;
    }

    uvrpc_client_t* client = (uvrpc_client_t*)UVRPC_CALLOC(1, sizeof(uvrpc_client_t));
    if (!client) {
        UVRPC_LOG_ERROR("Failed to allocate client");
        return NULL;
    }

    client->loop = loop;
    client->owns_loop = (loop == NULL);
    client->zmq_type = zmq_type;

    /* 如果没有提供 loop，创建默认 loop */
    if (!client->loop) {
        client->loop = (uv_loop_t*)UVRPC_MALLOC(sizeof(uv_loop_t));
        if (!client->loop) {
            UVRPC_LOG_ERROR("Failed to allocate loop");
            UVRPC_FREE(client);
            return NULL;
        }
        uv_loop_init(client->loop);
    }

    /* 复制服务器地址 */
    client->server_addr = strdup(server_addr);
    if (!client->server_addr) {
        UVRPC_LOG_ERROR("Failed to duplicate server_addr");
        if (client->owns_loop) {
            uv_loop_close(client->loop);
            UVRPC_FREE(client->loop);
        }
        UVRPC_FREE(client);
        return NULL;
    }

    /* 创建 uvzmq socket */
    client->socket = uvzmq_socket_new(client->loop, zmq_type);
    if (!client->socket) {
        UVRPC_LOG_ERROR("Failed to create uvzmq socket (type: %d)", zmq_type);
        UVRPC_FREE(client->server_addr);
        if (client->owns_loop) {
            uv_loop_close(client->loop);
            UVRPC_FREE(client->loop);
        }
        UVRPC_FREE(client);
        return NULL;
    }

    /* 设置消息回调（除了 PUSH 和 PUB 类型不需要接收） */
    if (zmq_type != ZMQ_PUSH && zmq_type != ZMQ_PUB) {
        uvzmq_socket_set_recv_callback(client->socket, on_zmq_recv, client);
    }

    /* 连接到服务器（除了 PUB 和 PUSH 类型不需要连接） */
    if (zmq_type != ZMQ_PUB && zmq_type != ZMQ_PUSH) {
        int rc = uvzmq_socket_connect(client->socket, server_addr);
        if (rc != 0) {
            UVRPC_LOG_ERROR("Failed to connect to server: %s", server_addr);
            uvzmq_socket_free(client->socket);
            UVRPC_FREE(client->server_addr);
            if (client->owns_loop) {
                uv_loop_close(client->loop);
                UVRPC_FREE(client->loop);
            }
            UVRPC_FREE(client);
            return NULL;
        }
    }

    client->pending_requests = NULL;
    client->next_request_id = 1;
    client->is_connected = 1;

    UVRPC_LOG_INFO("Client created and connected to: %s (ZMQ type: %d)", 
                   server_addr, zmq_type);

    return client;
}

/* 调用 RPC 服务 */
int uvrpc_client_call(uvrpc_client_t* client,
                       const char* service_name,
                       const uint8_t* request_data,
                       size_t request_size,
                       uvrpc_response_callback_t callback,
                       void* ctx) {
    if (!client || !service_name || !callback) {
        return UVRPC_ERROR_INVALID_PARAM;
    }

    if (!client->is_connected) {
        UVRPC_LOG_ERROR("Client is not connected");
        return UVRPC_ERROR;
    }

    /* 创建请求条目 */
    uvrpc_client_request_t* req = (uvrpc_client_request_t*)UVRPC_CALLOC(1, sizeof(uvrpc_client_request_t));
    if (!req) {
        UVRPC_LOG_ERROR("Failed to allocate request");
        return UVRPC_ERROR_NO_MEMORY;
    }

    req->callback = callback;
    req->ctx = ctx;

    /* 添加到 pending_requests */
    /* TODO: 使用 request_id 进行匹配 */
    HASH_ADD_INT(client->pending_requests, /* request_id field */, req);

    /* 构造请求消息 */
    flatbuffers_builder_t* builder = flatbuffers_builder_init(1024);
    if (!builder) {
        UVRPC_LOG_ERROR("Failed to create flatbuffers builder");
        HASH_DEL(client->pending_requests, req);
        UVRPC_FREE(req);
        return UVRPC_ERROR_NO_MEMORY;
    }

    /* 创建 service_id 字符串 */
    flatbuffers_string_ref_t service_ref = flatbuffers_string_create(builder, service_name);

    /* 创建 request_data 向量 */
    flatbuffers_uint8_vec_ref_t request_vec_ref = 0;
    if (request_data && request_size > 0) {
        request_vec_ref = flatbuffers_uint8_vec_create(builder, request_data, request_size);
    }

    /* 创建 RpcRequest */
    uvrpc_RpcRequest_start(builder);
    uvrpc_RpcRequest_service_id_add(builder, service_ref);
    if (request_vec_ref) {
        uvrpc_RpcRequest_request_data_add(builder, request_vec_ref);
    }
    flatbuffers_uint8_vec_ref_t request_ref = uvrpc_RpcRequest_end(builder);

    /* 创建 RpcMessage union */
    uvrpc_RpcMessage_start_as_RpcRequest(builder);
    uvrpc_RpcMessage_RpcRequest_add(builder, request_ref);
    flatbuffers_union_ref_t msg_ref = uvrpc_RpcMessage_end_as_RpcRequest(builder);

    uvrpc_RpcMessage_create_as_root(builder, msg_ref);

    /* 获取序列化数据 */
    size_t msg_size;
    const uint8_t* msg_data = flatbuffers_builder_get_data(builder, &msg_size);

    /* 发送请求 */
    zmq_msg_t request_msg;
    zmq_msg_init_size(&request_msg, msg_size);
    memcpy(zmq_msg_data(&request_msg), msg_data, msg_size);

    int rc = uvzmq_socket_send(client->socket, &request_msg);
    if (rc != 0) {
        UVRPC_LOG_ERROR("Failed to send request");
        zmq_msg_close(&request_msg);
        flatbuffers_builder_clear(builder);
        HASH_DEL(client->pending_requests, req);
        UVRPC_FREE(req);
        return UVRPC_ERROR;
    }

    zmq_msg_close(&request_msg);
    flatbuffers_builder_clear(builder);

    UVRPC_LOG_DEBUG("Sent request for service: %s (%zu bytes)", service_name, msg_size);

    return UVRPC_OK;
}

/* 释放客户端 */
void uvrpc_client_free(uvrpc_client_t* client) {
    if (!client) {
        return;
    }

    /* 释放所有待处理请求 */
    uvrpc_client_request_t* req, *tmp;
    HASH_ITER(hh, client->pending_requests, req, tmp) {
        HASH_DEL(client->pending_requests, req);
        UVRPC_FREE(req);
    }

    /* 释放 socket */
    if (client->socket) {
        uvzmq_socket_close(client->socket);
        uvzmq_socket_free(client->socket);
    }

    /* 释放地址字符串 */
    if (client->server_addr) {
        UVRPC_FREE(client->server_addr);
    }

    /* 关闭并释放 loop（如果拥有） */
    if (client->owns_loop && client->loop) {
        uv_loop_close(client->loop);
        UVRPC_FREE(client->loop);
    }

    UVRPC_FREE(client);

    UVRPC_LOG_INFO("Client freed");
}