#include "uvrpc_internal.h"
#include "msgpack_wrapper.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

/* 前向声明 */
uvrpc_client_t* uvrpc_client_new_zmq(uv_loop_t* loop, const char* server_addr, int zmq_type);

/* 零拷贝释放回调 - 用于 DEALER 模式 */
static void uvrpc_free_serialized_data_wrapper(void* data, void* hint) {
    (void)hint;  /* 未使用参数 */
    if (data) {
        uvrpc_free_serialized_data(data);
    }
}

/* uvzmq 接收回调 */
static void on_zmq_recv(uvzmq_socket_t* socket, zmq_msg_t* msg, void* arg) {
    (void)socket;  /* 未使用参数 */
    uvrpc_client_t* client = (uvrpc_client_t*)arg;

    if (!client || !client->is_connected) {
        return;
    }

    /* 获取消息数据 */
    size_t size = zmq_msg_size(msg);
    void* data = zmq_msg_data(msg);
    
    /* DEALER 模式：跳过空帧（ZMQ 应该自动剥离，但保险起见检查） */
    if (client->zmq_type == ZMQ_DEALER && size == 0) {
        return;
    }

    UVRPC_LOG_DEBUG("Client received %zu bytes", size);

    UVRPC_LOG_DEBUG("Client received %zu bytes", size);

    /* 解析 msgpack 响应 */
    uvrpc_response_t response;
    if (uvrpc_deserialize_response_msgpack(data, size, &response) != 0) {
        UVRPC_LOG_ERROR("Failed to deserialize response");
        return;
    }

    /* 查找待处理请求 */
    uvrpc_client_request_t* entry = NULL;
    HASH_FIND_INT(client->pending_requests, &response.request_id, entry);

    if (!entry) {
        UVRPC_LOG_ERROR("No pending request found for request_id: %u", response.request_id);
        uvrpc_free_response(&response);
        return;
    }

    /* 调用回调 */
    if (entry->callback) {
        entry->callback(entry->ctx, response.status, response.response_data, response.response_data_size);
    }

    /* 从待处理列表中移除 */
    HASH_DEL(client->pending_requests, entry);
    UVRPC_FREE(entry);

    /* 释放响应 */
    uvrpc_free_response(&response);
}

/* 创建客户端（使用模式枚举） */
uvrpc_client_t* uvrpc_client_new(uv_loop_t* loop, const char* server_addr, uvrpc_mode_t mode) {
    int zmq_type = ZMQ_REQ;  /* 默认 */

    switch (mode) {
        case UVRPC_MODE_REQ_REP:
            zmq_type = ZMQ_REQ;
            break;
        case UVRPC_MODE_ROUTER_DEALER:
            zmq_type = ZMQ_DEALER;
            break;
        case UVRPC_MODE_PUB_SUB:
            zmq_type = ZMQ_SUB;
            break;
        case UVRPC_MODE_PUSH_PULL:
            zmq_type = ZMQ_PULL;
            break;
        default:
            UVRPC_LOG_ERROR("Invalid RPC mode: %d", mode);
            return NULL;
    }

    return uvrpc_client_new_zmq(loop, server_addr, zmq_type);
}

/* 创建客户端（直接指定 ZMQ 类型） */
uvrpc_client_t* uvrpc_client_new_zmq(uv_loop_t* loop, const char* server_addr, int zmq_type) {
    if (!server_addr) {
        UVRPC_LOG_ERROR("server_addr cannot be NULL");
        return NULL;
    }

    /* 默认使用 ZMQ_REQ */
    if (zmq_type == 0) {
        zmq_type = ZMQ_REQ;
    }

    /* 分配客户端结构 */
    uvrpc_client_t* client = (uvrpc_client_t*)UVRPC_CALLOC(1, sizeof(uvrpc_client_t));
    if (!client) {
        UVRPC_LOG_ERROR("Failed to allocate client");
        return NULL;
    }

    /* 设置或创建 loop */
    if (loop) {
        client->loop = loop;
        client->owns_loop = 0;
    } else {
        client->loop = (uv_loop_t*)UVRPC_MALLOC(sizeof(uv_loop_t));
        if (!client->loop) {
            UVRPC_LOG_ERROR("Failed to allocate loop");
            UVRPC_FREE(client);
            return NULL;
        }
        uv_loop_init(client->loop);
        client->owns_loop = 1;
    }

    /* 复制服务器地址 */
    client->server_addr = UVRPC_MALLOC(strlen(server_addr) + 1);
    if (!client->server_addr) {
        UVRPC_LOG_ERROR("Failed to allocate server_addr");
        if (client->owns_loop) {
            uv_loop_close(client->loop);
            UVRPC_FREE(client->loop);
        }
        UVRPC_FREE(client);
        return NULL;
    }
    strcpy(client->server_addr, server_addr);

    client->zmq_type = zmq_type;
    client->next_request_id = 1;

    /* 创建 ZMQ context */
    client->zmq_ctx = zmq_ctx_new();
    if (!client->zmq_ctx) {
        UVRPC_LOG_ERROR("Failed to create ZMQ context");
        UVRPC_FREE(client->server_addr);
        if (client->owns_loop) {
            uv_loop_close(client->loop);
            UVRPC_FREE(client->loop);
        }
        UVRPC_FREE(client);
        return NULL;
    }

    /* 性能优化：I/O 线程数设置 */
    /* 注意：客户端通常只需要 1 个 I/O 线程 */
    /* 服务器可以设置多个 I/O 线程来处理多个并发连接 */
    int io_threads = 1;
    zmq_ctx_set(client->zmq_ctx, ZMQ_IO_THREADS, io_threads);

    /* 创建 ZMQ socket */
    client->zmq_sock = zmq_socket(client->zmq_ctx, zmq_type);
    if (!client->zmq_sock) {
        UVRPC_LOG_ERROR("Failed to create ZMQ socket (type: %d)", zmq_type);
        UVRPC_FREE(client->server_addr);
        zmq_ctx_term(client->zmq_ctx);
        if (client->owns_loop) {
            uv_loop_close(client->loop);
            UVRPC_FREE(client->loop);
        }
        UVRPC_FREE(client);
        return NULL;
    }

    /* 设置 socket 选项 */
    int linger = 0;
    zmq_setsockopt(client->zmq_sock, ZMQ_LINGER, &linger, sizeof(linger));
    
    /* 性能优化：增加高水位标记以支持更高的吞吐量 */
    int sndhwm = 10000;  /* 发送队列高水位标记 */
    int rcvhwm = 10000;  /* 接收队列高水位标记 */
    zmq_setsockopt(client->zmq_sock, ZMQ_SNDHWM, &sndhwm, sizeof(sndhwm));
    zmq_setsockopt(client->zmq_sock, ZMQ_RCVHWM, &rcvhwm, sizeof(rcvhwm));
    
    /* 性能优化：增加 TCP 缓冲区大小 */
    int tcp_sndbuf = 256 * 1024;  /* 256KB 发送缓冲区 */
    int tcp_rcvbuf = 256 * 1024;  /* 256KB 接收缓冲区 */
    zmq_setsockopt(client->zmq_sock, ZMQ_SNDBUF, &tcp_sndbuf, sizeof(tcp_sndbuf));
    zmq_setsockopt(client->zmq_sock, ZMQ_RCVBUF, &tcp_rcvbuf, sizeof(tcp_rcvbuf));
    
    /* 性能优化：设置立即连接，减少连接延迟 */
    int immediate = 1;
    zmq_setsockopt(client->zmq_sock, ZMQ_IMMEDIATE, &immediate, sizeof(immediate));
    
    /* ROUTER/DEALER 模式：设置客户端标识 */
    if (zmq_type == ZMQ_DEALER) {
        char identity[32];
        snprintf(identity, sizeof(identity), "client-%p", (void*)client);
        zmq_setsockopt(client->zmq_sock, ZMQ_IDENTITY, identity, strlen(identity));
        
        /* 禁用路由器探测（DEALER 默认不需要） */
        int probe_router = 0;
        zmq_setsockopt(client->zmq_sock, ZMQ_PROBE_ROUTER, &probe_router, sizeof(probe_router));
    }

    /* 创建 uvzmq socket（除了 PUB 和 PUSH 类型不需要接收） */
    if (zmq_type != ZMQ_PUB && zmq_type != ZMQ_PUSH) {
        if (uvzmq_socket_new(client->loop, client->zmq_sock, on_zmq_recv, client, &client->socket) != 0) {
            UVRPC_LOG_ERROR("Failed to create uvzmq socket (type: %d)", zmq_type);
            UVRPC_FREE(client->server_addr);
            zmq_close(client->zmq_sock);
            zmq_ctx_term(client->zmq_ctx);
            if (client->owns_loop) {
                uv_loop_close(client->loop);
                UVRPC_FREE(client->loop);
            }
            UVRPC_FREE(client);
            return NULL;
        }
    }

    client->pending_requests = NULL;
    client->is_connected = 0;

    UVRPC_LOG_INFO("Client created with server address: %s (ZMQ type: %d)",
                   server_addr, zmq_type);

    return client;
}

/* 连接到服务器 */
int uvrpc_client_connect(uvrpc_client_t* client) {
    if (!client) {
        return UVRPC_ERROR_INVALID_PARAM;
    }

    if (client->is_connected) {
        UVRPC_LOG_ERROR("Client is already connected");
        return UVRPC_ERROR;
    }

    /* 连接 socket */
    int rc = zmq_connect(client->zmq_sock, client->server_addr);
    if (rc != 0) {
        UVRPC_LOG_ERROR("Failed to connect to server: %s (errno: %d)", client->server_addr, zmq_errno());
        return UVRPC_ERROR;
    }

    client->is_connected = 1;
    UVRPC_LOG_INFO("Client connected to: %s", client->server_addr);

    return UVRPC_OK;
}

/* 断开连接 */
void uvrpc_client_disconnect(uvrpc_client_t* client) {
    if (!client || !client->is_connected) {
        return;
    }

    client->is_connected = 0;
    UVRPC_LOG_INFO("Client disconnected");
}

/* 发送 RPC 请求（异步） */
int uvrpc_client_call(uvrpc_client_t* client,
                       const char* service_id,
                       const char* method_id,
                       const uint8_t* request_data,
                       size_t request_size,
                       uvrpc_response_callback_t callback,
                       void* ctx) {
    if (!client || !service_id || !*service_id || !method_id || !*method_id) {
        return UVRPC_ERROR_INVALID_PARAM;
    }

    if (!client->is_connected) {
        UVRPC_LOG_ERROR("Client is not connected");
        return UVRPC_ERROR;
    }

    /* 创建请求条目 */
    uvrpc_client_request_t* entry = (uvrpc_client_request_t*)UVRPC_MALLOC(sizeof(uvrpc_client_request_t));
    if (!entry) {
        UVRPC_LOG_ERROR("Failed to allocate request entry");
        return UVRPC_ERROR_NO_MEMORY;
    }

    entry->request_id = client->next_request_id++;
    entry->callback = callback;
    entry->ctx = ctx;

    /* 添加到待处理列表 */
    HASH_ADD_INT(client->pending_requests, request_id, entry);

    /* 创建请求 */
    uvrpc_request_t request;
    request.request_id = entry->request_id;
    request.service_id = (char*)service_id;
    request.method_id = (char*)method_id;
    request.request_data = (uint8_t*)request_data;
    request.request_data_size = request_size;

/* 序列化请求 */
    uint8_t* serialized_data = NULL;
    size_t serialized_size = 0;
    if (uvrpc_serialize_request_msgpack(&request, &serialized_data, &serialized_size) != 0) {
        UVRPC_LOG_ERROR("Failed to serialize request");
        HASH_DEL(client->pending_requests, entry);
        UVRPC_FREE(entry);
        return UVRPC_ERROR;
    }

/* DEALER 模式：发送空帧 + 数据帧 - 优化为单次批量发送 */
    int rc = 0;
    if (client->zmq_type == ZMQ_DEALER) {
        /* DEALER 模式：使用 zmq_msg 批量发送空帧 + 数据帧 */
        zmq_msg_t parts[2];
        
        /* 第一帧：空帧 */
        zmq_msg_init(&parts[0]);
        
        /* 第二帧：数据帧 - 使用零拷贝 */
        zmq_msg_init_data(&parts[1], serialized_data, serialized_size, uvrpc_free_serialized_data_wrapper, NULL);
        
        /* 批量发送 - 减少系统调用开销 */
        rc = zmq_msg_send(&parts[0], client->zmq_sock, ZMQ_SNDMORE);
        if (rc >= 0) {
            rc = zmq_msg_send(&parts[1], client->zmq_sock, 0);
        }
        
        zmq_msg_close(&parts[0]);
        zmq_msg_close(&parts[1]);
    } else {
        /* 非 DEALER 模式：直接发送数据帧 */
        rc = zmq_send(client->zmq_sock, serialized_data, serialized_size, 0);
        /* 释放序列化数据 */
        uvrpc_free_serialized_data(serialized_data);
    }
    
    if (rc < 0) {
        UVRPC_LOG_ERROR("Failed to send request (errno: %d, msg: %s)", zmq_errno(), zmq_strerror(zmq_errno()));
        HASH_DEL(client->pending_requests, entry);
        UVRPC_FREE(entry);
        return UVRPC_ERROR;
    }

    UVRPC_LOG_DEBUG("Sent request (id: %u, service: %s, method: %s)",
                    entry->request_id, service_id, method_id);

    return UVRPC_OK;
}

/* 释放客户端 */
void uvrpc_client_free(uvrpc_client_t* client) {
    if (!client) {
        return;
    }

    /* 断开连接 */
    uvrpc_client_disconnect(client);

    /* 释放所有待处理请求 */
    uvrpc_client_request_t* entry, *tmp;
    HASH_ITER(hh, client->pending_requests, entry, tmp) {
        HASH_DEL(client->pending_requests, entry);
        UVRPC_FREE(entry);
    }

    /* 释放 uvzmq socket (异步释放，需要运行事件循环来完成清理) */
    if (client->socket) {
        uvzmq_socket_free(client->socket);
        /* 运行事件循环以确保异步清理完成 */
        if (client->loop) {
            uv_run(client->loop, UV_RUN_NOWAIT);
        }
    }

    /* 关闭 ZMQ socket */
    if (client->zmq_sock) {
        zmq_close(client->zmq_sock);
    }

    /* 终止 ZMQ context */
    if (client->zmq_ctx) {
        zmq_ctx_term(client->zmq_ctx);
    }

    /* 释放服务器地址 */
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

void* uvrpc_client_get_zmq_socket(uvrpc_client_t* client) {
    if (!client) {
        return NULL;
    }
    return client->zmq_sock;
}