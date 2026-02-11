#include "uvrpc_internal.h"
#include "msgpack_wrapper.h"
#include <string.h>
#include <stdlib.h>

/* ZMQ 消息释放回调 */
static void zmq_free_wrapper(void* data, void* hint) {
    (void)hint;  /* 未使用 */
    free(data);
}

/* uvzmq 接收回调 */
static void on_zmq_recv(uvzmq_socket_t* socket, zmq_msg_t* msg, void* arg) {
    (void)socket;  /* 未使用参数 */
    uvrpc_server_t* server = (uvrpc_server_t*)arg;

    if (!server || !server->is_running || !server->zmq_sock) {
        return;
    }

    /* ROUTER 模式：接收多部分消息（路由帧 + 空帧 + 数据帧） */
    if (server->zmq_type == ZMQ_ROUTER) {
        size_t frame_size = zmq_msg_size(msg);
        void* frame_data = zmq_msg_data(msg);
        
if (!server->has_routing_id) {
            /* 第一帧：路由帧 */
            memcpy(server->routing_id, frame_data, frame_size);
            server->routing_id_size = frame_size;
            server->has_routing_id = 1;
            return;
        } else if (server->router_state == 0) {
            /* 第二帧：空帧（DEALER 添加的分隔符） */
            if (frame_size != 0) {
                UVRPC_LOG_ERROR("ROUTER: Expected empty frame, got %zu bytes", frame_size);
                server->router_state = 0;
                server->has_routing_id = 0;
                return;
            }
            server->router_state = 1;
            return;
        } else {
            /* 第三帧：数据帧 */
            fprintf(stderr, "[SERVER] Processing data frame (size: %zu)\n", frame_size);
            
            /* 解析 msgpack 请求 */
            uvrpc_request_t request;
            if (uvrpc_deserialize_request_msgpack(frame_data, frame_size, &request) != 0) {
                UVRPC_LOG_ERROR("Failed to deserialize request");
                server->has_routing_id = 0;
                server->router_state = 0;
                return;
            }
        
            UVRPC_LOG_DEBUG("ROUTER: Received request (routing_id_size=%zu), service: %s, method: %s", 
                           server->routing_id_size, request.service_id, request.method_id);
            fprintf(stderr, "[SERVER] Request: service=%s, method=%s\n", request.service_id, request.method_id);
            
            /* 查找服务处理器 */
            uvrpc_service_entry_t* entry = NULL;
            HASH_FIND_STR(server->services, request.service_id, entry);
            
            if (!entry) {
                /* 发送服务未找到错误响应 */
                uvrpc_response_t response;
                response.request_id = request.request_id;
                response.status = UVRPC_ERROR_SERVICE_NOT_FOUND;
                response.error_message = (char*)"Service not found";
                response.response_data = NULL;
                response.response_data_size = 0;
                
                uint8_t* serialized_data = NULL;
                size_t serialized_size = 0;
                if (uvrpc_serialize_response_msgpack(&response, &serialized_data, &serialized_size) != 0) {
                    UVRPC_LOG_ERROR("Failed to serialize error response");
                    uvrpc_free_request(&request);
                    server->has_routing_id = 0;
                    server->router_state = 0;
                    return;
                }
                
                /* ROUTER 模式：发送路由帧 + 空帧 + 数据帧 */
                zmq_msg_t routing_msg;
                zmq_msg_init_data(&routing_msg, server->routing_id, server->routing_id_size, NULL, NULL);
                zmq_msg_send(&routing_msg, server->zmq_sock, ZMQ_SNDMORE);
                
                zmq_msg_t empty_msg;
                zmq_msg_init(&empty_msg);
                zmq_msg_send(&empty_msg, server->zmq_sock, ZMQ_SNDMORE);
                zmq_msg_close(&empty_msg);
                
                zmq_msg_t response_msg;
                zmq_msg_init_data(&response_msg, serialized_data, serialized_size, 
                                 zmq_free_wrapper, NULL);
                zmq_msg_send(&response_msg, server->zmq_sock, 0);
                
                uvrpc_free_request(&request);
                server->has_routing_id = 0;
                server->router_state = 0;
                return;
            }
            
            /* 调用服务处理器 */
            uint8_t* resp_data = NULL;
            size_t resp_size = 0;
            int status = entry->handler(entry->ctx,
                                        request.request_data,
                                        request.request_data_size,
                                        &resp_data,
                                        &resp_size);
            
            /* 创建响应 */
            uvrpc_response_t response;
            response.request_id = request.request_id;
            response.status = status;
            response.error_message = (status == 0) ? NULL : (char*)"Service handler error";
            response.response_data = resp_data;
            response.response_data_size = resp_size;
            
            /* 序列化响应 */
            uint8_t* serialized_data = NULL;
            size_t serialized_size = 0;
            if (uvrpc_serialize_response_msgpack(&response, &serialized_data, &serialized_size) != 0) {
                UVRPC_LOG_ERROR("Failed to serialize response");
                
                if (resp_data) free(resp_data);
                uvrpc_free_request(&request);
                server->has_routing_id = 0;
                server->router_state = 0;
                return;
            }
            
            /* ROUTER 模式：发送路由帧 + 空帧 + 数据帧 */
            zmq_msg_t routing_msg;
            zmq_msg_init_data(&routing_msg, server->routing_id, server->routing_id_size, NULL, NULL);
            zmq_msg_send(&routing_msg, server->zmq_sock, ZMQ_SNDMORE);
            
            zmq_msg_t empty_msg;
            zmq_msg_init(&empty_msg);
            zmq_msg_send(&empty_msg, server->zmq_sock, ZMQ_SNDMORE);
            zmq_msg_close(&empty_msg);
            
            zmq_msg_t response_msg;
            zmq_msg_init_data(&response_msg, serialized_data, serialized_size, 
                             zmq_free_wrapper, NULL);
            zmq_msg_send(&response_msg, server->zmq_sock, 0);
            
            /* 清理 */
            uvrpc_free_request(&request);
            server->has_routing_id = 0;
            server->router_state = 0;
            
            /* 释放响应数据 */
            if (resp_data) {
                free(resp_data);
            }
            
            return;
        }
    }
    
    /* 非 ROUTER 模式（REQ/REP 等）：直接处理消息 */
    /* 获取消息数据 */
    void* data = zmq_msg_data(msg);
    size_t size = zmq_msg_size(msg);

    /* 解析 msgpack 请求 */
    uvrpc_request_t request;
    if (uvrpc_deserialize_request_msgpack(data, size, &request) != 0) {
        UVRPC_LOG_ERROR("Failed to deserialize request");
        return;
    }

    UVRPC_LOG_DEBUG("Received request for service: %s, method: %s", request.service_id, request.method_id);

    /* 查找服务处理器 */
    uvrpc_service_entry_t* entry = NULL;
    HASH_FIND_STR(server->services, request.service_id, entry);

    if (!entry) {
        UVRPC_LOG_ERROR("Service not found: %s", request.service_id);

        /* 发送服务未找到错误响应 */
        uvrpc_response_t response;
        response.request_id = request.request_id;
        response.status = UVRPC_ERROR_SERVICE_NOT_FOUND;
        response.error_message = (char*)"Service not found";
        response.response_data = NULL;
        response.response_data_size = 0;

        uint8_t* resp_data = NULL;
        size_t resp_size = 0;
        if (uvrpc_serialize_response_msgpack(&response, &resp_data, &resp_size) != 0) {
            UVRPC_LOG_ERROR("Failed to serialize error response");
            uvrpc_free_request(&request);
            return;
        }

        /* 发送响应 - 使用零拷贝优化 */
        zmq_msg_t response_msg;
        zmq_msg_init_data(&response_msg, resp_data, resp_size, zmq_free_wrapper, NULL);

        int rc = zmq_msg_send(&response_msg, server->zmq_sock, 0);
        if (rc < 0) {
            UVRPC_LOG_ERROR("Failed to send error response");
            /* 发送失败时手动释放 */
            uvrpc_free_serialized_data(resp_data);
        }

        zmq_msg_close(&response_msg);
        uvrpc_free_request(&request);
        return;
    }

    /* 调用服务处理器 */
    uint8_t* resp_data = NULL;
    size_t resp_size = 0;
    int status = entry->handler(entry->ctx,
                                request.request_data,
                                request.request_data_size,
                                &resp_data,
                                &resp_size);

    /* 创建响应 */
    uvrpc_response_t response;
    response.request_id = request.request_id;
    response.status = status;
    response.error_message = (status == 0) ? NULL : (char*)"Service handler error";
    response.response_data = resp_data;
    response.response_data_size = resp_size;

    /* 序列化响应 */
    uint8_t* serialized_data = NULL;
    size_t serialized_size = 0;
    if (uvrpc_serialize_response_msgpack(&response, &serialized_data, &serialized_size) != 0) {
        UVRPC_LOG_ERROR("Failed to serialize response");
        UVRPC_FREE(resp_data);
        uvrpc_free_request(&request);
        return;
    }

    /* 发送响应 - 使用零拷贝优化 */
    zmq_msg_t response_msg;
    zmq_msg_init_data(&response_msg, serialized_data, serialized_size, zmq_free_wrapper, NULL);

    int rc = zmq_msg_send(&response_msg, server->zmq_sock, 0);
    if (rc < 0) {
        UVRPC_LOG_ERROR("Failed to send response (errno: %d)", zmq_errno());
        /* 发送失败时手动释放 */
        uvrpc_free_serialized_data(serialized_data);
    }

    zmq_msg_close(&response_msg);
    
    /* 释放服务处理器返回的数据 */
    if (resp_data) {
        UVRPC_FREE(resp_data);
    }
    uvrpc_free_request(&request);
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

    /* 分配服务器结构 */
    uvrpc_server_t* server = (uvrpc_server_t*)UVRPC_CALLOC(1, sizeof(uvrpc_server_t));
    if (!server) {
        UVRPC_LOG_ERROR("Failed to allocate server");
        return NULL;
    }

    /* 设置或创建 loop */
    if (loop) {
        server->loop = loop;
        server->owns_loop = 0;
    } else {
        server->loop = (uv_loop_t*)UVRPC_MALLOC(sizeof(uv_loop_t));
        if (!server->loop) {
            UVRPC_LOG_ERROR("Failed to allocate loop");
            UVRPC_FREE(server);
            return NULL;
        }
        uv_loop_init(server->loop);
        server->owns_loop = 1;
    }

    /* 复制绑定地址 */
    server->bind_addr = UVRPC_MALLOC(strlen(bind_addr) + 1);
    if (!server->bind_addr) {
        UVRPC_LOG_ERROR("Failed to allocate bind_addr");
        if (server->owns_loop) {
            uv_loop_close(server->loop);
            UVRPC_FREE(server->loop);
        }
        UVRPC_FREE(server);
        return NULL;
    }
    strcpy(server->bind_addr, bind_addr);

    server->zmq_type = zmq_type;

    /* 创建 ZMQ context */
    server->zmq_ctx = zmq_ctx_new();
    if (!server->zmq_ctx) {
        UVRPC_LOG_ERROR("Failed to create ZMQ context");
        UVRPC_FREE(server->bind_addr);
        if (server->owns_loop) {
            uv_loop_close(server->loop);
            UVRPC_FREE(server->loop);
        }
        UVRPC_FREE(server);
        return NULL;
    }

    /* 性能优化：I/O 线程数设置 */
    /* 单线程服务器使用 2 个 I/O 线程以平衡性能和兼容性 */
    int io_threads = 2;
    zmq_ctx_set(server->zmq_ctx, ZMQ_IO_THREADS, io_threads);

    /* 创建 ZMQ socket */
    server->zmq_sock = zmq_socket(server->zmq_ctx, zmq_type);
    if (!server->zmq_sock) {
        UVRPC_LOG_ERROR("Failed to create ZMQ socket (type: %d)", zmq_type);
        UVRPC_FREE(server->bind_addr);
        zmq_ctx_term(server->zmq_ctx);
        if (server->owns_loop) {
            uv_loop_close(server->loop);
            UVRPC_FREE(server->loop);
        }
        UVRPC_FREE(server);
        return NULL;
    }

    /* 设置 socket 选项 */
    int linger = 0;
    zmq_setsockopt(server->zmq_sock, ZMQ_LINGER, &linger, sizeof(linger));
    
    /* 性能优化：增加高水位标记以支持更高的吞吐量 */
    int sndhwm = 10000;  /* 发送队列高水位标记 */
    int rcvhwm = 10000;  /* 接收队列高水位标记 */
    zmq_setsockopt(server->zmq_sock, ZMQ_SNDHWM, &sndhwm, sizeof(sndhwm));
    zmq_setsockopt(server->zmq_sock, ZMQ_RCVHWM, &rcvhwm, sizeof(rcvhwm));
    
    /* ROUTER 模式特定选项 */
    if (zmq_type == ZMQ_ROUTER) {
        /* 设置路由器手动模式（避免自动连接） */
        int router_mandatory = 0;
        zmq_setsockopt(server->zmq_sock, ZMQ_ROUTER_MANDATORY, &router_mandatory, sizeof(router_mandatory));
    }

    /* 创建 uvzmq socket（除了 PUB 和 PUSH 类型不需要接收） */
    if (zmq_type != ZMQ_PUB && zmq_type != ZMQ_PUSH) {
        if (uvzmq_socket_new(server->loop, server->zmq_sock, on_zmq_recv, server, &server->socket) != 0) {
            UVRPC_LOG_ERROR("Failed to create uvzmq socket (type: %d)", zmq_type);
            UVRPC_FREE(server->bind_addr);
            zmq_close(server->zmq_sock);
            zmq_ctx_term(server->zmq_ctx);
            if (server->owns_loop) {
                uv_loop_close(server->loop);
                UVRPC_FREE(server->loop);
            }
            UVRPC_FREE(server);
            return NULL;
        }
    }

    server->services = NULL;
    server->is_running = 0;
    
    /* 初始化 ROUTER 模式状态 */
    server->has_routing_id = 0;
    server->router_state = 0;
    server->routing_id_size = 0;
    memset(server->routing_id, 0, sizeof(server->routing_id));

    UVRPC_LOG_INFO("Server created with bind address: %s (ZMQ type: %d)",
                   bind_addr, zmq_type);

    return server;
}

/* 注册服务 */
int uvrpc_server_register_service(uvrpc_server_t* server,
                                   const char* service_name,
                                   uvrpc_service_handler_t handler,
                                   void* ctx) {
    if (!server || !service_name || !*service_name || !handler) {
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
    entry = (uvrpc_service_entry_t*)UVRPC_MALLOC(sizeof(uvrpc_service_entry_t));
    if (!entry) {
        UVRPC_LOG_ERROR("Failed to allocate service entry");
        return UVRPC_ERROR_NO_MEMORY;
    }

    entry->name = UVRPC_MALLOC(strlen(service_name) + 1);
    if (!entry->name) {
        UVRPC_FREE(entry);
        return UVRPC_ERROR_NO_MEMORY;
    }
    strcpy(entry->name, service_name);
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
    int rc = zmq_bind(server->zmq_sock, server->bind_addr);
    if (rc != 0) {
        UVRPC_LOG_ERROR("Failed to bind socket: %s (errno: %d)", server->bind_addr, zmq_errno());
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
        UVRPC_FREE(entry->name);
        UVRPC_FREE(entry);
    }

    /* 释放 uvzmq socket */
    if (server->socket) {
        uvzmq_socket_free(server->socket);
    }

    /* 关闭 ZMQ socket */
    if (server->zmq_sock) {
        zmq_close(server->zmq_sock);
    }

    /* 终止 ZMQ context */
    if (server->zmq_ctx) {
        zmq_ctx_term(server->zmq_ctx);
    }

    /* 释放绑定地址 */
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