#include "uvrpc.h"
#include "msgpack_wrapper.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zmq.h>
#include <uv.h>
#include <uvzmq.h>
#include <uthash.h>

/* 服务处理器条目 */
typedef struct uvrpc_service_entry {
    char* name;                         /* 服务名称 */
    uvrpc_service_handler_t handler;    /* 处理函数 */
    void* ctx;                          /* 用户上下文 */
    UT_hash_handle hh;                  /* uthash 句柄 */
} uvrpc_service_entry_t;

/* 服务器结构 */
typedef struct uvrpc_server {
    uv_loop_t* loop;
    char* bind_addr;
    void* zmq_ctx;
    void* zmq_sock;
    uvzmq_socket_t* socket;
    int zmq_type;
    uvrpc_service_entry_t* services;
    int owns_loop;
    int owns_zmq_ctx;
    int is_running;
    uint8_t routing_id[256];
    size_t routing_id_size;
    int has_routing_id;
    int router_state;
} uvrpc_server_t;

/* 客户端请求上下文 */
typedef struct uvrpc_client_request {
    uint32_t request_id;
    uvrpc_response_callback_t callback;
    void* ctx;
    UT_hash_handle hh;
} uvrpc_client_request_t;

/* 客户端结构 */
typedef struct uvrpc_client {
    uv_loop_t* loop;
    char* server_addr;
    void* zmq_ctx;
    void* zmq_sock;
    uvzmq_socket_t* socket;
    int zmq_type;
    uvrpc_client_request_t* pending_requests;
    int owns_loop;
    int owns_zmq_ctx;
    uint32_t next_request_id;
    int is_connected;
    int batch_size;
} uvrpc_client_t;

/* Async 结构 */
typedef struct uvrpc_async {
    uv_loop_t* loop;
    uint32_t request_id;
    int completed;
    int consumed;              /* 标记结果是否已被消费 */
    int status;
    uint8_t* response_data;
    size_t response_size;
    uint64_t timeout_ms;        /* 超时时间（毫秒） */
    uint64_t start_time_ms;     /* 开始时间（毫秒） */
    uv_timer_t timeout_timer;   /* libuv 超时定时器 */
    uvrpc_async_result_t result;  /* 存储结果（避免使用静态全局变量） */
} uvrpc_async_t;

/* ==================== 辅助函数 ==================== */

static int zmq_type_from_mode(uvrpc_mode_t mode, uvrpc_transport_t transport, int is_server) {
    (void)transport;  /* 暂时未使用 */
    switch (mode) {
        case UVRPC_SERVER_CLIENT:
            return is_server ? ZMQ_ROUTER : ZMQ_DEALER;
        case UVRPC_BROADCAST:
            return is_server ? ZMQ_PUB : ZMQ_SUB;
        default:
            return ZMQ_ROUTER;
    }
}

/* ZMQ free 函数适配器 */
static void zmq_free_wrapper(void* data, void* hint) {
    (void)hint;
    if (data) {
        free(data);
    }
}

static void on_server_recv(uvzmq_socket_t* socket, zmq_msg_t* msg, void* arg) {
    (void)socket;
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
            if (frame_size > sizeof(server->routing_id)) {
                frame_size = sizeof(server->routing_id);
            }
            memcpy(server->routing_id, frame_data, frame_size);
            server->routing_id_size = frame_size;
            server->has_routing_id = 1;
            return;
        } else if (server->router_state == 0) {
            /* 第二帧：空帧（DEALER 添加的分隔符） */
            if (frame_size != 0) {
                server->router_state = 0;
                server->has_routing_id = 0;
                return;
            }
            server->router_state = 1;
            return;
        } else {
            /* 第三帧：数据帧 */
            uvrpc_request_t request;
            if (uvrpc_deserialize_request_msgpack(frame_data, frame_size, &request) != 0) {
                server->has_routing_id = 0;
                server->router_state = 0;
                return;
            }

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
                if (uvrpc_serialize_response_msgpack(&response, &serialized_data, &serialized_size) == 0) {
                    /* ROUTER 模式：发送路由帧 + 空帧 + 数据帧 */
                    zmq_msg_t routing_msg;
                    zmq_msg_init_data(&routing_msg, server->routing_id, server->routing_id_size, NULL, NULL);
                    zmq_msg_send(&routing_msg, server->zmq_sock, ZMQ_SNDMORE);

                    zmq_msg_t empty_msg;
                    zmq_msg_init(&empty_msg);
                    zmq_msg_send(&empty_msg, server->zmq_sock, ZMQ_SNDMORE);
                    zmq_msg_close(&empty_msg);

                    zmq_msg_t response_msg;
                    zmq_msg_init_data(&response_msg, serialized_data, serialized_size, zmq_free_wrapper, NULL);
                    zmq_msg_send(&response_msg, server->zmq_sock, 0);
                }

                uvrpc_free_request(&request);
                server->has_routing_id = 0;
                server->router_state = 0;
                return;
            }

            /* 调用服务处理器 */
            uvrpc_response_t response;
            response.request_id = request.request_id;
            response.status = UVRPC_OK;
            response.error_message = NULL;
            response.response_data = NULL;
            response.response_data_size = 0;

            int handler_status = entry->handler(entry->ctx, request.request_data, request.request_data_size,
                                               &response.response_data, &response.response_data_size);
            response.status = (handler_status == UVRPC_OK) ? UVRPC_OK : handler_status;

            /* 序列化响应 */
            uint8_t* serialized_data = NULL;
            size_t serialized_size = 0;
            if (uvrpc_serialize_response_msgpack(&response, &serialized_data, &serialized_size) == 0) {
                /* ROUTER 模式：发送路由帧 + 空帧 + 数据帧 */
                zmq_msg_t routing_msg;
                zmq_msg_init_data(&routing_msg, server->routing_id, server->routing_id_size, NULL, NULL);
                zmq_msg_send(&routing_msg, server->zmq_sock, ZMQ_SNDMORE);

                zmq_msg_t empty_msg;
                zmq_msg_init(&empty_msg);
                zmq_msg_send(&empty_msg, server->zmq_sock, ZMQ_SNDMORE);
                zmq_msg_close(&empty_msg);

                zmq_msg_t response_msg;
                zmq_msg_init_data(&response_msg, serialized_data, serialized_size, zmq_free_wrapper, NULL);
                zmq_msg_send(&response_msg, server->zmq_sock, 0);
            }

            uvrpc_free_request(&request);
            server->has_routing_id = 0;
            server->router_state = 0;
        }
    }
    /* PUB/SUB 模式：直接接收数据帧（服务器作为PUB端，通常只发送不接收） */
    else if (server->zmq_type == ZMQ_PUB) {
        /* PUB模式通常不接收消息，这里留空 */
        return;
    }
}

static void on_client_recv(uvzmq_socket_t* socket, zmq_msg_t* msg, void* arg) {
    (void)socket;
    uvrpc_client_t* client = (uvrpc_client_t*)arg;

    if (!client || !client->is_connected) {
        return;
    }

    /* 获取消息数据 */
    size_t size = zmq_msg_size(msg);
    void* data = zmq_msg_data(msg);

    /* DEALER 模式：跳过空帧 */
    if (client->zmq_type == ZMQ_DEALER && size == 0) {
        return;
    }

    /* 解析 msgpack 响应 */
    uvrpc_response_t response;
    if (uvrpc_deserialize_response_msgpack(data, size, &response) != 0) {
        return;
    }

    /* 查找待处理请求 */
    uvrpc_client_request_t* entry = NULL;
    HASH_FIND_INT(client->pending_requests, &response.request_id, entry);

    if (!entry) {
        uvrpc_free_response(&response);
        return;
    }

    /* 调用回调 */
    if (entry->callback) {
        entry->callback(entry->ctx, response.status, response.response_data, response.response_data_size);
    }

    /* 从待处理列表中移除 */
    HASH_DEL(client->pending_requests, entry);
    free(entry);

    /* 释放响应 */
    uvrpc_free_response(&response);
}

/* ==================== 服务器 API 实现 ==================== */

uvrpc_server_t* uvrpc_server_create(const uvrpc_config_t* config) {
    if (!config || !config->loop || !config->address) {
        return NULL;
    }
    
    /* 分配服务器结构 */
    uvrpc_server_t* server = (uvrpc_server_t*)calloc(1, sizeof(uvrpc_server_t));
    if (!server) {
        return NULL;
    }
    
    /* 基本配置 */
    server->loop = config->loop;
    server->bind_addr = strdup(config->address);
    server->owns_loop = 0;
    server->is_running = 0;
    server->has_routing_id = 0;
    server->router_state = 0;
    
    /* ZMQ Context */
    if (config->zmq_ctx) {
        server->zmq_ctx = config->zmq_ctx;
        server->owns_zmq_ctx = 0;
    } else {
        server->zmq_ctx = zmq_ctx_new();
        if (!server->zmq_ctx) {
            goto error;
        }
        server->owns_zmq_ctx = 1;
        
        /* 设置IO线程数 */
        if (config->io_threads > 0) {
            zmq_ctx_set(server->zmq_ctx, ZMQ_IO_THREADS, config->io_threads);
        }
    }
    
    /* 创建ZMQ socket */
    int zmq_type = zmq_type_from_mode(config->mode, config->transport, 1);
    server->zmq_sock = zmq_socket(server->zmq_ctx, zmq_type);
    if (!server->zmq_sock) {
        goto error;
    }
    
    server->zmq_type = zmq_type;
    
    /* 设置socket选项 */
    if (config->sndhwm > 0) {
        zmq_setsockopt(server->zmq_sock, ZMQ_SNDHWM, &config->sndhwm, sizeof(config->sndhwm));
    }
    if (config->rcvhwm > 0) {
        zmq_setsockopt(server->zmq_sock, ZMQ_RCVHWM, &config->rcvhwm, sizeof(config->rcvhwm));
    }
    if (config->tcp_sndbuf > 0) {
        zmq_setsockopt(server->zmq_sock, ZMQ_SNDBUF, &config->tcp_sndbuf, sizeof(config->tcp_sndbuf));
    }
    if (config->tcp_rcvbuf > 0) {
        zmq_setsockopt(server->zmq_sock, ZMQ_RCVBUF, &config->tcp_rcvbuf, sizeof(config->tcp_rcvbuf));
    }
    if (config->tcp_keepalive) {
        int keepalive = 1;
        zmq_setsockopt(server->zmq_sock, ZMQ_TCP_KEEPALIVE, &keepalive, sizeof(keepalive));
        zmq_setsockopt(server->zmq_sock, ZMQ_TCP_KEEPALIVE_IDLE, &config->tcp_keepalive_idle, sizeof(config->tcp_keepalive_idle));
        zmq_setsockopt(server->zmq_sock, ZMQ_TCP_KEEPALIVE_CNT, &config->tcp_keepalive_cnt, sizeof(config->tcp_keepalive_cnt));
        zmq_setsockopt(server->zmq_sock, ZMQ_TCP_KEEPALIVE_INTVL, &config->tcp_keepalive_intvl, sizeof(config->tcp_keepalive_intvl));
    }
    if (config->linger >= 0) {
        zmq_setsockopt(server->zmq_sock, ZMQ_LINGER, &config->linger, sizeof(config->linger));
    }
    
    /* UDP组播配置 */
    if (config->transport == UVRPC_TRANSPORT_UDP && config->udp_multicast) {
        /* 注意：ZMQ的组播选项可能因版本而异 */
        #ifdef ZMQ_MULTICAST_LOOP
        zmq_setsockopt(server->zmq_sock, ZMQ_MULTICAST_LOOP, &config->udp_multicast, sizeof(config->udp_multicast));
        #endif
        if (config->udp_multicast_group) {
            #ifdef ZMQ_MCAST_LOOP
            zmq_setsockopt(server->zmq_sock, ZMQ_MCAST_LOOP, &config->udp_multicast, sizeof(config->udp_multicast));
            #endif
        }
    }
    
    /* 创建uvzmq socket */
    int rc = uvzmq_socket_new(server->loop, server->zmq_sock, on_server_recv, server, &server->socket);
    if (rc != 0) {
        goto error;
    }
    
    return server;
    
error:
    if (server->zmq_sock) {
        zmq_close(server->zmq_sock);
    }
    if (server->zmq_ctx && server->owns_zmq_ctx) {
        zmq_ctx_term(server->zmq_ctx);
    }
    if (server->bind_addr) {
        free(server->bind_addr);
    }
    free(server);
    return NULL;
}

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
        return UVRPC_ERROR;
    }

    /* 创建新服务条目 */
    entry = (uvrpc_service_entry_t*)malloc(sizeof(uvrpc_service_entry_t));
    if (!entry) {
        return UVRPC_ERROR;
    }

    entry->name = strdup(service_name);
    if (!entry->name) {
        free(entry);
        return UVRPC_ERROR;
    }

    entry->handler = handler;
    entry->ctx = ctx;

    /* 添加到哈希表 */
    HASH_ADD_STR(server->services, name, entry);

    return UVRPC_OK;
}

int uvrpc_server_start(uvrpc_server_t* server) {
    if (!server) {
        return UVRPC_ERROR_INVALID_PARAM;
    }
    
    /* 绑定地址 */
    int rc = zmq_bind(server->zmq_sock, server->bind_addr);
    if (rc != 0) {
        return UVRPC_ERROR;
    }
    
    server->is_running = 1;
    return UVRPC_OK;
}

int uvrpc_server_stop(uvrpc_server_t* server) {
    if (!server) {
        return UVRPC_ERROR_INVALID_PARAM;
    }
    
    server->is_running = 0;
    return UVRPC_OK;
}

int uvrpc_server_get_stats(uvrpc_server_t* server, int* services_count) {
    if (!server || !services_count) {
        return UVRPC_ERROR_INVALID_PARAM;
    }
    
    /* TODO: 实现统计信息获取 */
    *services_count = 0;
    
    return UVRPC_OK;
}

void uvrpc_server_free(uvrpc_server_t* server) {
    if (!server) {
        return;
    }
    
    if (server->is_running) {
        uvrpc_server_stop(server);
    }
    
    if (server->socket) {
        uvzmq_socket_free(server->socket);
    }
    
    if (server->zmq_sock) {
        zmq_close(server->zmq_sock);
    }
    
    if (server->zmq_ctx && server->owns_zmq_ctx) {
        zmq_ctx_term(server->zmq_ctx);
    }
    
    if (server->bind_addr) {
        free(server->bind_addr);
    }
    
    free(server);
}

/* ==================== 客户端 API 实现 ==================== */

uvrpc_client_t* uvrpc_client_create(const uvrpc_config_t* config) {
    if (!config || !config->loop || !config->address) {
        return NULL;
    }
    
    /* 分配客户端结构 */
    uvrpc_client_t* client = (uvrpc_client_t*)calloc(1, sizeof(uvrpc_client_t));
    if (!client) {
        return NULL;
    }
    
    /* 基本配置 */
    client->loop = config->loop;
    client->server_addr = strdup(config->address);
    client->owns_loop = 0;
    client->is_connected = 0;
    client->next_request_id = 1;
    client->batch_size = config->batch_size;
    client->pending_requests = NULL;
    
    /* ZMQ Context */
    if (config->zmq_ctx) {
        client->zmq_ctx = config->zmq_ctx;
        client->owns_zmq_ctx = 0;
    } else {
        client->zmq_ctx = zmq_ctx_new();
        if (!client->zmq_ctx) {
            goto error;
        }
        client->owns_zmq_ctx = 1;
        
        /* 设置IO线程数 */
        if (config->io_threads > 0) {
            zmq_ctx_set(client->zmq_ctx, ZMQ_IO_THREADS, config->io_threads);
        }
    }
    
    /* 创建ZMQ socket */
    int zmq_type = zmq_type_from_mode(config->mode, config->transport, 0);
    client->zmq_sock = zmq_socket(client->zmq_ctx, zmq_type);
    if (!client->zmq_sock) {
        goto error;
    }
    
    client->zmq_type = zmq_type;
    
    /* 设置socket选项 */
    if (config->sndhwm > 0) {
        zmq_setsockopt(client->zmq_sock, ZMQ_SNDHWM, &config->sndhwm, sizeof(config->sndhwm));
    }
    if (config->rcvhwm > 0) {
        zmq_setsockopt(client->zmq_sock, ZMQ_RCVHWM, &config->rcvhwm, sizeof(config->rcvhwm));
    }
    if (config->tcp_sndbuf > 0) {
        zmq_setsockopt(client->zmq_sock, ZMQ_SNDBUF, &config->tcp_sndbuf, sizeof(config->tcp_sndbuf));
    }
    if (config->tcp_rcvbuf > 0) {
        zmq_setsockopt(client->zmq_sock, ZMQ_RCVBUF, &config->tcp_rcvbuf, sizeof(config->tcp_rcvbuf));
    }
    if (config->tcp_keepalive) {
        int keepalive = 1;
        zmq_setsockopt(client->zmq_sock, ZMQ_TCP_KEEPALIVE, &keepalive, sizeof(keepalive));
        zmq_setsockopt(client->zmq_sock, ZMQ_TCP_KEEPALIVE_IDLE, &config->tcp_keepalive_idle, sizeof(config->tcp_keepalive_idle));
        zmq_setsockopt(client->zmq_sock, ZMQ_TCP_KEEPALIVE_CNT, &config->tcp_keepalive_cnt, sizeof(config->tcp_keepalive_cnt));
        zmq_setsockopt(client->zmq_sock, ZMQ_TCP_KEEPALIVE_INTVL, &config->tcp_keepalive_intvl, sizeof(config->tcp_keepalive_intvl));
    }
    if (config->reconnect_ivl > 0) {
        zmq_setsockopt(client->zmq_sock, ZMQ_RECONNECT_IVL, &config->reconnect_ivl, sizeof(config->reconnect_ivl));
    }
    if (config->reconnect_ivl_max > 0) {
        zmq_setsockopt(client->zmq_sock, ZMQ_RECONNECT_IVL_MAX, &config->reconnect_ivl_max, sizeof(config->reconnect_ivl_max));
    }
    if (config->linger >= 0) {
        zmq_setsockopt(client->zmq_sock, ZMQ_LINGER, &config->linger, sizeof(config->linger));
    }
    
    /* 创建uvzmq socket */
    int rc = uvzmq_socket_new(client->loop, client->zmq_sock, on_client_recv, client, &client->socket);
    if (rc != 0) {
        goto error;
    }
    
    return client;
    
error:
    if (client->zmq_sock) {
        zmq_close(client->zmq_sock);
    }
    if (client->zmq_ctx && client->owns_zmq_ctx) {
        zmq_ctx_term(client->zmq_ctx);
    }
    if (client->server_addr) {
        free(client->server_addr);
    }
    free(client);
    return NULL;
}

int uvrpc_client_connect(uvrpc_client_t* client) {
    if (!client) {
        return UVRPC_ERROR_INVALID_PARAM;
    }
    
    /* 连接到服务器 */
    int rc = zmq_connect(client->zmq_sock, client->server_addr);
    if (rc != 0) {
        return UVRPC_ERROR;
    }
    
    client->is_connected = 1;
    return UVRPC_OK;
}

void uvrpc_client_disconnect(uvrpc_client_t* client) {
    if (!client) {
        return;
    }
    
    if (client->is_connected) {
        zmq_disconnect(client->zmq_sock, client->server_addr);
        client->is_connected = 0;
    }
}

int uvrpc_client_call(uvrpc_client_t* client,
                       const char* service_name,
                       const char* method_name,
                       const uint8_t* request_data,
                       size_t request_size,
                       uvrpc_response_callback_t callback,
                       void* ctx) {
    if (!client || !service_name || !*service_name || !method_name || !*method_name || !callback) {
        return UVRPC_ERROR_INVALID_PARAM;
    }

    if (!client->is_connected) {
        return UVRPC_ERROR;
    }

    /* 创建请求条目 */
    uvrpc_client_request_t* entry = (uvrpc_client_request_t*)malloc(sizeof(uvrpc_client_request_t));
    if (!entry) {
        return UVRPC_ERROR;
    }

    entry->request_id = client->next_request_id++;
    entry->callback = callback;
    entry->ctx = ctx;

    /* 添加到待处理列表 */
    HASH_ADD_INT(client->pending_requests, request_id, entry);

    /* 创建请求 */
    uvrpc_request_t request;
    request.request_id = entry->request_id;
    request.service_id = (char*)service_name;
    request.method_id = (char*)method_name;
    request.request_data = (uint8_t*)request_data;
    request.request_data_size = request_size;

    /* 序列化请求 */
    uint8_t* serialized_data = NULL;
    size_t serialized_size = 0;
    if (uvrpc_serialize_request_msgpack(&request, &serialized_data, &serialized_size) != 0) {
        HASH_DEL(client->pending_requests, entry);
        free(entry);
        return UVRPC_ERROR;
    }

    /* DEALER 模式：发送空帧 + 数据帧 */
    int rc = 0;
    zmq_msg_t empty_msg;
    zmq_msg_init(&empty_msg);
    rc = zmq_msg_send(&empty_msg, client->zmq_sock, ZMQ_SNDMORE);
    zmq_msg_close(&empty_msg);

    if (rc < 0) {
        uvrpc_free_serialized_data(serialized_data);
        HASH_DEL(client->pending_requests, entry);
        free(entry);
        return UVRPC_ERROR;
    }

    zmq_msg_t data_msg;
    zmq_msg_init_data(&data_msg, serialized_data, serialized_size, zmq_free_wrapper, NULL);
    rc = zmq_msg_send(&data_msg, client->zmq_sock, 0);

    if (rc < 0) {
        HASH_DEL(client->pending_requests, entry);
        free(entry);
        return UVRPC_ERROR;
    }

    return UVRPC_OK;
}

int uvrpc_client_get_stats(uvrpc_client_t* client, int* pending_requests) {
    if (!client || !pending_requests) {
        return UVRPC_ERROR_INVALID_PARAM;
    }
    
    /* TODO: 实现统计信息获取 */
    *pending_requests = 0;
    
    return UVRPC_OK;
}

void uvrpc_client_free(uvrpc_client_t* client) {
    if (!client) {
        return;
    }

    if (client->is_connected) {
        uvrpc_client_disconnect(client);
    }

    /* 清理所有pending请求 */
    uvrpc_client_request_t* entry, *tmp;
    HASH_ITER(hh, client->pending_requests, entry, tmp) {
        HASH_DEL(client->pending_requests, entry);
        free(entry);
    }

    if (client->socket) {
        uvzmq_socket_free(client->socket);
    }

    if (client->zmq_sock) {
        zmq_close(client->zmq_sock);
    }

    if (client->zmq_ctx && client->owns_zmq_ctx) {
        zmq_ctx_term(client->zmq_ctx);
    }

    if (client->server_addr) {
        free(client->server_addr);
    }

    free(client);
}

/* ==================== Async API 实现 ==================== */

uvrpc_async_t* uvrpc_async_create(uv_loop_t* loop) {
    if (!loop) {
        return NULL;
    }
    
    uvrpc_async_t* async = (uvrpc_async_t*)calloc(1, sizeof(uvrpc_async_t));
    if (!async) {
        return NULL;
    }
    
    async->loop = loop;
    async->request_id = 0;
    async->completed = 0;
    async->consumed = 0;
    async->status = 0;
    async->response_data = NULL;
    async->response_size = 0;
    
    return async;
}

void uvrpc_async_free(uvrpc_async_t* async) {
    if (!async) {
        return;
    }
    
    if (async->response_data) {
        free(async->response_data);
    }
    
    free(async);
}

/* Async回调上下文 */
typedef struct {
    uvrpc_async_t* async;
    int completed;
    int status;
    uint8_t* response_data;
    size_t response_size;
} async_callback_ctx_t;

/* Async响应回调 */
static void async_response_callback(void* ctx, int status, const uint8_t* response_data, size_t response_size) {
    async_callback_ctx_t* async_ctx = (async_callback_ctx_t*)ctx;

    if (async_ctx->async && !async_ctx->completed) {
        async_ctx->completed = 1;
        async_ctx->status = status;

        /* Copy response data to async_t (take ownership) */
        if (response_data && response_size > 0) {
            async_ctx->async->response_data = malloc(response_size);
            if (async_ctx->async->response_data) {
                memcpy(async_ctx->async->response_data, response_data, response_size);
                async_ctx->async->response_size = response_size;
            }
            /* Don't free in async_ctx since we took ownership */
        }

        /* Fill async result */
        async_ctx->async->completed = 1;
        async_ctx->async->status = async_ctx->status;
        async_ctx->async->response_data = async_ctx->async->response_data;
        async_ctx->async->response_size = async_ctx->async->response_size;
    }

    free(async_ctx);
}

int uvrpc_client_call_async(uvrpc_client_t* client,
                             const char* service_name,
                             const char* method_name,
                             const uint8_t* request_data,
                             size_t request_size,
                             uvrpc_async_t* async) {
    if (!client || !service_name || !*service_name || !method_name || !*method_name || !async) {
        return UVRPC_ERROR_INVALID_PARAM;
    }

    if (!client->is_connected) {
        return UVRPC_ERROR;
    }

    /* 创建回调上下文 */
    async_callback_ctx_t* async_ctx = (async_callback_ctx_t*)malloc(sizeof(async_callback_ctx_t));
    if (!async_ctx) {
        return UVRPC_ERROR_NO_MEMORY;
    }

    async_ctx->async = async;
    async_ctx->completed = 0;
    async_ctx->status = UVRPC_ERROR;
    async_ctx->response_data = NULL;
    async_ctx->response_size = 0;

    /* 使用uvrpc_client_call发送请求 */
    int rc = uvrpc_client_call(client, service_name, method_name,
                               request_data, request_size,
                               async_response_callback, async_ctx);

    return rc;
}

const uvrpc_async_result_t* uvrpc_async_await(uvrpc_async_t* async) {
    if (!async) {
        return NULL;
    }

    /* 使用async内部的result字段，避免静态全局变量 */
    async->result.status = async->status;
    async->result.response_data = async->response_data;
    async->result.response_size = async->response_size;

    return &async->result;
}

/* 超时定时器回调 */
static void on_async_timeout(uv_timer_t* handle) {
    uvrpc_async_t* async = (uvrpc_async_t*)handle->data;
    if (async && !async->completed) {
        async->completed = 1;
        async->status = UVRPC_ERROR_TIMEOUT;
    }
}

const uvrpc_async_result_t* uvrpc_async_await_timeout(uvrpc_async_t* async, uint64_t timeout_ms) {
    if (!async || !async->loop) {
        return NULL;
    }

    if (async->completed) {
        /* 使用async内部的result字段，避免静态全局变量 */
        async->result.status = async->status;
        async->result.response_data = async->response_data;
        async->result.response_size = async->response_size;
        return &async->result;
    }

    /* 设置超时定时器 */
    async->timeout_ms = timeout_ms;
    async->start_time_ms = uv_now(async->loop);  /* 记录开始时间 */
    async->timeout_timer.data = async;
    uv_timer_init(async->loop, &async->timeout_timer);
    uv_timer_start(&async->timeout_timer, on_async_timeout, timeout_ms, 0);

    /* 运行事件循环直到完成或超时 */
    /* 使用 UV_RUN_ONCE 模式，移除睡眠以提高性能 */
    int loops = 0;
    while (!async->completed) {
        uv_run(async->loop, UV_RUN_ONCE);
        loops++;
        
        /* 每1000次循环检查超时，避免CPU 100%占用 */
        if (loops % 1000 == 0) {
            /* 检查是否超时 */
            uint64_t elapsed = (uv_now(async->loop) - async->start_time_ms);
            if (elapsed >= async->timeout_ms) {
                async->completed = 1;
                async->status = UVRPC_ERROR_TIMEOUT;
                break;
            }
        }
    }

    /* 停止并关闭定时器 */
    uv_timer_stop(&async->timeout_timer);
    uv_close((uv_handle_t*)&async->timeout_timer, NULL);
    uv_run(async->loop, UV_RUN_ONCE);  /* 确保关闭回调执行 */

    /* 使用async内部的result字段，避免静态全局变量 */
    async->result.status = async->status;
    async->result.response_data = async->response_data;
    async->result.response_size = async->response_size;

    return &async->result;
}

int uvrpc_async_await_all(uvrpc_async_t** asyncs, int count) {
    if (!asyncs || count <= 0) {
        return UVRPC_ERROR_INVALID_PARAM;
    }

    if (count == 1) {
        uvrpc_async_await(asyncs[0]);
        return UVRPC_OK;
    }

    /* 获取第一个async的loop */
    uv_loop_t* loop = asyncs[0]->loop;
    if (!loop) {
        return UVRPC_ERROR_INVALID_PARAM;
    }

    /* 等待所有async完成 */
    int completed_count = 0;
    while (completed_count < count) {
        completed_count = 0;
        for (int i = 0; i < count; i++) {
            if (asyncs[i]->completed) {
                completed_count++;
            }
        }
        if (completed_count < count) {
            uv_run(loop, UV_RUN_ONCE);
        }
    }

    return UVRPC_OK;
}

int uvrpc_async_await_any(uvrpc_async_t** asyncs, int count) {
    if (!asyncs || count <= 0) {
        return UVRPC_ERROR_INVALID_PARAM;
    }

    if (count == 1) {
        uvrpc_async_await(asyncs[0]);
        return 0;  /* 返回第一个完成的索引 */
    }

    /* 获取第一个async的loop */
    uv_loop_t* loop = asyncs[0]->loop;
    if (!loop) {
        return UVRPC_ERROR_INVALID_PARAM;
    }

    /* 等待任意一个async完成 */
    while (1) {
        for (int i = 0; i < count; i++) {
            if (asyncs[i]->completed) {
                return i;  /* 返回完成的索引 */
            }
        }
        uv_run(loop, UV_RUN_ONCE);
    }

    return UVRPC_ERROR;
}

/* ==================== 通用客户端调用 API ==================== */

int uvrpc_client_call_async_generic(
    uvrpc_client_t* client,
    const char* service_name,
    const char* method_name,
    const void* request,
    uvrpc_serialize_func_t serialize_func,
    uvrpc_async_t* async
) {
    if (!client || !service_name || !method_name || !request || !async || !serialize_func) {
        return UVRPC_ERROR_INVALID_PARAM;
    }

    if (!client->is_connected) {
        return UVRPC_ERROR;
    }

    /* 序列化请求 */
    uint8_t* serialized = NULL;
    size_t serialized_size = 0;
    if (serialize_func(request, &serialized, &serialized_size) != 0) {
        return UVRPC_ERROR;
    }

    /* 发送异步调用 */
    int rc = uvrpc_client_call_async(client, service_name, method_name,
                                      serialized, serialized_size, async);

    /* 释放序列化数据（uvrpc_client_call_async 接管所有权） */
    if (rc != UVRPC_OK) {
        free(serialized);
    }

    return rc;
}

int uvrpc_client_call_sync(
    uvrpc_client_t* client,
    const char* service_name,
    const char* method_name,
    const void* request,
    uvrpc_serialize_func_t serialize_func,
    void* response,
    uvrpc_deserialize_func_t deserialize_func,
    uv_loop_t* loop
) {
    if (!client || !service_name || !method_name || !request || !response || !loop) {
        return UVRPC_ERROR_INVALID_PARAM;
    }

    if (!serialize_func || !deserialize_func) {
        return UVRPC_ERROR_INVALID_PARAM;
    }

    /* 创建异步上下文 */
    uvrpc_async_t* async = uvrpc_async_create(loop);
    if (!async) {
        return UVRPC_ERROR_NO_MEMORY;
    }

    /* 发送异步调用 */
    int rc = uvrpc_client_call_async_generic(client, service_name, method_name,
                                              request, serialize_func, async);
    if (rc != UVRPC_OK) {
        uvrpc_async_free(async);
        return rc;
    }

    /* 等待响应 */
    const uvrpc_async_result_t* result = uvrpc_async_await(async);
    
    if (result && result->status == UVRPC_OK) {
        rc = deserialize_func(result->response_data, result->response_size, response);
    } else if (result) {
        rc = result->status;
    } else {
        rc = UVRPC_ERROR;
    }

    /* 释放异步上下文 */
    uvrpc_async_free(async);

    return rc;
}