#include "uvrpc_internal.h"
#include "uvrpc_generated.h"  /* flatbuffers 生成的头文件 */
#include <string.h>
#include <stdlib.h>

/* uvzmq 接收回调 */
static void on_zmq_recv(uvzmq_socket_t* socket, zmq_msg_t* msg, void* arg) {
    uvrpc_server_t* server = (uvrpc_server_t*)arg;

    if (!server || !server->is_running) {
        return;
    }

    /* 获取消息数据 */
    void* data = zmq_msg_data(msg);
    size_t size = zmq_msg_size(msg);

    UVRPC_LOG_DEBUG("Server received %zu bytes", size);

    /* 解析 flatbuffers 请求 */
    flatbuffers_t* fb = flatbuffers_init(data, size);
    if (!fb) {
        UVRPC_LOG_ERROR("Failed to init flatbuffers");
        return;
    }

    uvrpc_RpcRequest_table_t request;
    if (!uvrpc_RpcRequest_as_root(fb)) {
        UVRPC_LOG_ERROR("Invalid RpcRequest message");
        flatbuffers_clear(fb);
        return;
    }
    uvrpc_RpcRequest_init(&request, fb);

    /* 获取服务 ID */
    const char* service_id = uvrpc_RpcRequest_service_id(&request);
    if (!service_id) {
        UVRPC_LOG_ERROR("No service_id in request");
        flatbuffers_clear(fb);
        return;
    }

    UVRPC_LOG_DEBUG("Received request for service: %s", service_id);

    /* 查找服务处理器 */
    uvrpc_service_entry_t* entry = NULL;
    HASH_FIND_STR(server->services, service_id, entry);

    if (!entry) {
        UVRPC_LOG_ERROR("Service not found: %s", service_id);
        
        /* 发送服务未找到错误响应 */
        flatbuffers_builder_t* builder = flatbuffers_builder_init(512);
        if (!builder) {
            UVRPC_LOG_ERROR("Failed to create flatbuffers builder for error response");
            flatbuffers_clear(fb);
            return;
        }
        
        /* 获取请求 ID */
        uint32_t request_id = uvrpc_RpcRequest_request_id(&request);
        
        /* 创建错误消息 */
        flatbuffers_string_ref_t error_msg_ref = flatbuffers_string_create(builder, "Service not found");
        
        /* 创建 RpcResponse */
        uvrpc_RpcResponse_start(builder);
        uvrpc_RpcResponse_request_id_add(builder, request_id);
        uvrpc_RpcResponse_status_add(builder, UVRPC_ERROR_SERVICE_NOT_FOUND);
        uvrpc_RpcResponse_error_message_add(builder, error_msg_ref);
        flatbuffers_uint8_vec_ref_t response_ref = uvrpc_RpcResponse_end(builder);
        
        /* 创建 RpcMessage union */
        uvrpc_RpcMessage_start_as_RpcResponse(builder);
        uvrpc_RpcMessage_RpcResponse_add(builder, response_ref);
        flatbuffers_union_ref_t msg_ref = uvrpc_RpcMessage_end_as_RpcResponse(builder);
        uvrpc_RpcMessage_create_as_root(builder, msg_ref);
        
        /* 获取序列化数据 */
        size_t resp_size;
        const uint8_t* resp_data = flatbuffers_builder_get_data(builder, &resp_size);
        
        /* 发送响应 */
        zmq_msg_t response_msg;
        zmq_msg_init_size(&response_msg, resp_size);
        memcpy(zmq_msg_data(&response_msg), resp_data, resp_size);
        
        int rc = uvzmq_socket_send(server->socket, &response_msg);
        if (rc != 0) {
            UVRPC_LOG_ERROR("Failed to send error response");
        }
        
        zmq_msg_close(&response_msg);
        flatbuffers_builder_clear(builder);
        flatbuffers_clear(fb);
        return;
    }

    /* 提取请求数据 */
    flatbuffers_uint8_vec_t request_vec = uvrpc_RpcRequest_request_data(&request);
    const uint8_t* request_data = request_vec.data;
    size_t request_size = request_vec.len;

    /* 调用服务处理器 */
    uint8_t* response_data = NULL;
    size_t response_size = 0;
    int status = entry->handler(entry->ctx, request_data, request_size,
                                 &response_data, &response_size);

    /* 构造响应消息 */
    flatbuffers_builder_t* builder = flatbuffers_builder_init(1024);
    if (!builder) {
        UVRPC_LOG_ERROR("Failed to create flatbuffers builder");
        if (response_data) UVRPC_FREE(response_data);
        flatbuffers_clear(fb);
        return;
    }

    /* 获取请求 ID */
    uint32_t request_id = uvrpc_RpcRequest_request_id(&request);

    /* 创建 response_data 向量 */
    flatbuffers_uint8_vec_ref_t response_vec_ref = 0;
    if (response_data && response_size > 0) {
        response_vec_ref = flatbuffers_uint8_vec_create(builder, response_data, response_size);
    }

    /* 创建 RpcResponse */
    uvrpc_RpcResponse_start(builder);
    uvrpc_RpcResponse_request_id_add(builder, request_id);
    uvrpc_RpcResponse_status_add(builder, status);
    if (response_vec_ref) {
        uvrpc_RpcResponse_response_data_add(builder, response_vec_ref);
    }
    flatbuffers_uint8_vec_ref_t response_ref = uvrpc_RpcResponse_end(builder);

    /* 创建 RpcMessage union */
    uvrpc_RpcMessage_start_as_RpcResponse(builder);
    uvrpc_RpcMessage_RpcResponse_add(builder, response_ref);
    flatbuffers_union_ref_t msg_ref = uvrpc_RpcMessage_end_as_RpcResponse(builder);

    uvrpc_RpcMessage_create_as_root(builder, msg_ref);

    /* 获取序列化数据 */
    size_t resp_size;
    const uint8_t* resp_data = flatbuffers_builder_get_data(builder, &resp_size);

    /* 发送响应 */
    zmq_msg_t response_msg;
    zmq_msg_init_size(&response_msg, resp_size);
    memcpy(zmq_msg_data(&response_msg), resp_data, resp_size);

    int rc = uvzmq_socket_send(server->socket, &response_msg);
    if (rc != 0) {
        UVRPC_LOG_ERROR("Failed to send response");
    }

    zmq_msg_close(&response_msg);
    flatbuffers_builder_clear(builder);
    flatbuffers_clear(fb);

    /* 释放响应数据 */
    if (response_data) UVRPC_FREE(response_data);
}

/* 创建服务器（使用模式枚举） */
uvrpc_server_t* uvrpc_server_new(uv_loop_t* loop, const char* bind_addr, uvrpc_mode_t mode) {
    int zmq_type = ZMQ_REP;  /* 默认 */
    
    switch (mode) {
        case UVRPC_MODE_REQ_REP:
            zmq_type = ZMQ_REP;
            break;
        case UVRPC_MODE_ROUTER_DEALER:
            zmq_type = ZMQ_ROUTER;
            break;
        case UVRPC_MODE_PUB_SUB:
            zmq_type = ZMQ_PUB;
            break;
        case UVRPC_MODE_PUSH_PULL:
            zmq_type = ZMQ_PUSH;
            break;
        default:
            UVRPC_LOG_ERROR("Invalid RPC mode: %d", mode);
            return NULL;
    }
    
    return uvrpc_server_new_zmq(loop, bind_addr, zmq_type);
}

/* 创建服务器（直接指定 ZMQ 类型） */
uvrpc_server_t* uvrpc_server_new_zmq(uv_loop_t* loop, const char* bind_addr, int zmq_type) {
    if (!bind_addr) {
        UVRPC_LOG_ERROR("bind_addr cannot be NULL");
        return NULL;
    }

    /* 默认使用 ZMQ_REP */
    if (zmq_type == 0) {
        zmq_type = ZMQ_REP;
    }

    uvrpc_server_t* server = (uvrpc_server_t*)UVRPC_CALLOC(1, sizeof(uvrpc_server_t));
    if (!server) {
        UVRPC_LOG_ERROR("Failed to allocate server");
        return NULL;
    }

    server->loop = loop;
    server->owns_loop = (loop == NULL);
    server->zmq_type = zmq_type;

    /* 如果没有提供 loop，创建默认 loop */
    if (!server->loop) {
        server->loop = (uv_loop_t*)UVRPC_MALLOC(sizeof(uv_loop_t));
        if (!server->loop) {
            UVRPC_LOG_ERROR("Failed to allocate loop");
            UVRPC_FREE(server);
            return NULL;
        }
        uv_loop_init(server->loop);
    }

    /* 复制绑定地址 */
    server->bind_addr = strdup(bind_addr);
    if (!server->bind_addr) {
        UVRPC_LOG_ERROR("Failed to duplicate bind_addr");
        if (server->owns_loop) {
            uv_loop_close(server->loop);
            UVRPC_FREE(server->loop);
        }
        UVRPC_FREE(server);
        return NULL;
    }

    /* 创建 uvzmq socket */
    server->socket = uvzmq_socket_new(server->loop, zmq_type);
    if (!server->socket) {
        UVRPC_LOG_ERROR("Failed to create uvzmq socket (type: %d)", zmq_type);
        UVRPC_FREE(server->bind_addr);
        if (server->owns_loop) {
            uv_loop_close(server->loop);
            UVRPC_FREE(server->loop);
        }
        UVRPC_FREE(server);
        return NULL;
    }

    /* 设置消息回调（除了 PUB 和 PUSH 类型不需要接收） */
    if (zmq_type != ZMQ_PUB && zmq_type != ZMQ_PUSH) {
        uvzmq_socket_set_recv_callback(server->socket, on_zmq_recv, server);
    }

    server->services = NULL;
    server->is_running = 0;

    UVRPC_LOG_INFO("Server created with bind address: %s (ZMQ type: %d)", 
                   bind_addr, zmq_type);

    return server;
}

/* 注册服务 */
int uvrpc_server_register_service(uvrpc_server_t* server,
                                   const char* service_name,
                                   uvrpc_service_handler_t handler,
                                   void* ctx) {
    if (!server || !service_name || !handler) {
        return UVRPC_ERROR_INVALID_PARAM;
    }

    /* 检查服务是否已存在 */
    uvrpc_service_entry_t* entry = NULL;
    HASH_FIND_STR(server->services, service_name, entry);

    if (entry) {
        UVRPC_LOG_ERROR("Service already registered: %s", service_name);
        return UVRPC_ERROR;
    }

    /* 创建新服务条目 */
    entry = (uvrpc_service_entry_t*)UVRPC_CALLOC(1, sizeof(uvrpc_service_entry_t));
    if (!entry) {
        UVRPC_LOG_ERROR("Failed to allocate service entry");
        return UVRPC_ERROR_NO_MEMORY;
    }

    entry->name = strdup(service_name);
    if (!entry->name) {
        UVRPC_LOG_ERROR("Failed to duplicate service name");
        UVRPC_FREE(entry);
        return UVRPC_ERROR_NO_MEMORY;
    }

    entry->handler = handler;
    entry->ctx = ctx;

    /* 添加到哈希表 */
    HASH_ADD_STR(server->services, name, entry);

    UVRPC_LOG_INFO("Service registered: %s", service_name);

    return UVRPC_OK;
}

/* 启动服务器 */
int uvrpc_server_start(uvrpc_server_t* server) {
    if (!server) {
        return UVRPC_ERROR_INVALID_PARAM;
    }

    if (server->is_running) {
        UVRPC_LOG_ERROR("Server is already running");
        return UVRPC_ERROR;
    }

    /* 绑定 socket */
    int rc = uvzmq_socket_bind(server->socket, server->bind_addr);
    if (rc != 0) {
        UVRPC_LOG_ERROR("Failed to bind socket: %s", server->bind_addr);
        return UVRPC_ERROR;
    }

    server->is_running = 1;
    UVRPC_LOG_INFO("Server started and listening on: %s", server->bind_addr);

    return UVRPC_OK;
}

/* 停止服务器 */
void uvrpc_server_stop(uvrpc_server_t* server) {
    if (!server || !server->is_running) {
        return;
    }

    server->is_running = 0;
    UVRPC_LOG_INFO("Server stopped");
}

/* 释放服务器 */
void uvrpc_server_free(uvrpc_server_t* server) {
    if (!server) {
        return;
    }

    /* 停止服务器 */
    uvrpc_server_stop(server);

    /* 释放所有服务条目 */
    uvrpc_service_entry_t* entry, *tmp;
    HASH_ITER(hh, server->services, entry, tmp) {
        HASH_DEL(server->services, entry);
        if (entry->name) UVRPC_FREE(entry->name);
        UVRPC_FREE(entry);
    }

    /* 释放 socket */
    if (server->socket) {
        uvzmq_socket_close(server->socket);
        uvzmq_socket_free(server->socket);
    }

    /* 释放地址字符串 */
    if (server->bind_addr) {
        UVRPC_FREE(server->bind_addr);
    }

    /* 关闭并释放 loop（如果拥有） */
    if (server->owns_loop && server->loop) {
        uv_loop_close(server->loop);
        UVRPC_FREE(server->loop);
    }

    UVRPC_FREE(server);

    UVRPC_LOG_INFO("Server freed");
}

/* 获取错误描述 */
const char* uvrpc_strerror(int error_code) {
    switch (error_code) {
        case UVRPC_OK:
            return "Success";
        case UVRPC_ERROR:
            return "Generic error";
        case UVRPC_ERROR_INVALID_PARAM:
            return "Invalid parameter";
        case UVRPC_ERROR_NO_MEMORY:
            return "Out of memory";
        case UVRPC_ERROR_SERVICE_NOT_FOUND:
            return "Service not found";
        case UVRPC_ERROR_TIMEOUT:
            return "Operation timeout";
        default:
            return "Unknown error";
    }
}

/* 获取模式名称 */
const char* uvrpc_mode_name(uvrpc_mode_t mode) {
    switch (mode) {
        case UVRPC_MODE_REQ_REP:
            return "REQ_REP";
        case UVRPC_MODE_ROUTER_DEALER:
            return "ROUTER_DEALER";
        case UVRPC_MODE_PUB_SUB:
            return "PUB_SUB";
        case UVRPC_MODE_PUSH_PULL:
            return "PUSH_PULL";
        default:
            return "UNKNOWN";
    }
}