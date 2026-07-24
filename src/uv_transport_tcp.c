/**
 * @file uv_transport_tcp.c
 * @brief TCP Transport Implementation
 * 
 * Fully event-driven TCP transport.
 * Never blocks the event loop, never calls uv_run, never touches loop->data.
 * 
 * @author UVRPC Team
 * @date 2026
 * @version 2.0
 */

#include "../include/uv_transport.h"
#include "../include/uvrpc_allocator.h"
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>


/* Forward declarations */
static int tcp_listen(uv_transport_t* transport);
static int tcp_connect(uv_transport_t* transport);
static void tcp_stop(uv_transport_t* transport);
static void tcp_disconnect(uv_transport_t* transport);
static int tcp_send(uv_transport_t* transport, const uint8_t* data, size_t size);
static int tcp_send_to(uv_transport_t* transport, const uint8_t* data, size_t size, void* peer);
static void tcp_free(uv_transport_t* transport);

/* TCP client structure */
typedef struct {
    uv_tcp_t handle;
    int is_connected;
    void* user_ctx;
    uint8_t read_buffer[65536];
    size_t read_pos;
} uv_transport_tcp_client_t;

/* TCP transport structure */
typedef struct {
    uv_transport_t base;
    
    /* Configuration */
    uv_transport_config_t config;
    
    /* Server/Client fields */
    uv_tcp_t listen_handle;
    uv_transport_tcp_client_t* clients;
    int client_count;
    int client_capacity;
    
    /* For client mode */
    uv_connect_t connect_req;
    uv_transport_tcp_client_t client;
} uv_transport_tcp_t;

/* Parse address */
static int parse_address(const char* address, char** host, int* port) {
    if (!address || !host || !port) return -1;
    
    const char* addr_start = address;
    if (strncmp(address, "tcp://", 6) == 0) {
        addr_start += 6;
    }
    
    const char* colon = strrchr(addr_start, ':');
    if (!colon) return -1;
    
    size_t host_len = colon - addr_start;
    *host = (char*)uvrpc_alloc(host_len + 1);
    if (!*host) return -1;
    memcpy(*host, addr_start, host_len);
    (*host)[host_len] = '\0';
    
    *port = atoi(colon + 1);
    return 0;
}

/* Write callback */
static void on_write(uv_write_t* req, int status) {
    (void)status;
    
    /* Free data buffer */
    if (req->data) {
        uvrpc_free(req->data);
    }
    
    /* Free request */
    uvrpc_free(req);
    
    /* CRITICAL: Never call uv_run here */
    /* CRITICAL: Never block here */
}

/* Alloc callback */
static void on_alloc(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    (void)handle;
    (void)suggested_size;
    
    size_t alloc_size = 65536;
    buf->base = (char*)uvrpc_alloc(alloc_size);
    buf->len = alloc_size;
}

/* Read callback */
static void on_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    if (!stream) {
        if (buf->base) uvrpc_free(buf->base);
        return;
    }
    
    uv_transport_tcp_t* tcp = (uv_transport_tcp_t*)stream->data;
    if (!tcp) {
        if (buf->base) uvrpc_free(buf->base);
        return;
    }
    
    if (nread > 0) {
        /* Call user callback */
        if (tcp->config.on_recv) {
            tcp->config.on_recv(&tcp->base, 
                               (const uint8_t*)buf->base, nread, 
                               NULL,  /* peer info */
                               tcp->config.callback_ctx);
        }
    } else if (nread < 0) {
        /* Error or EOF */
        if (tcp->config.on_disconnect) {
            tcp->config.on_disconnect(&tcp->base, tcp->config.callback_ctx);
        }
    }
    
    /* Free buffer */
    if (buf->base) {
        uvrpc_free(buf->base);
    }
    
    /* CRITICAL: Never call uv_run here */
}

/* Connect callback */
static void on_connect(uv_connect_t* req, int status) {
    uv_transport_tcp_t* tcp = (uv_transport_tcp_t*)req->data;
    if (!tcp) return;
    
    if (status == 0) {
        /* Start reading */
        uv_read_start((uv_stream_t*)&tcp->client.handle, on_alloc, on_read);
    }
    
    /* Call user callback */
    if (tcp->config.on_connect) {
        tcp->config.on_connect(&tcp->base, status, tcp->config.callback_ctx);
    }
    
    /* CRITICAL: Never call uv_run here */
}

/* Connection callback (server) */
static void on_connection(uv_stream_t* server, int status) {
    if (!server) return;
    
    uv_transport_tcp_t* tcp = (uv_transport_tcp_t*)server->data;
    if (!tcp) return;
    
    if (status < 0) {
        if (tcp->config.on_error) {
            tcp->config.on_error(&tcp->base, status, uv_strerror(status), 
                                 tcp->config.callback_ctx);
        }
        return;
    }
    
    /* Accept connection */
    uv_transport_tcp_client_t* client = (uv_transport_tcp_client_t*)uvrpc_alloc(sizeof(uv_transport_tcp_client_t));
    if (!client) {
        if (tcp->config.on_error) {
            tcp->config.on_error(&tcp->base, UV_TRANSPORT_ERROR_NO_MEMORY, 
                                 "Failed to allocate client", tcp->config.callback_ctx);
        }
        return;
    }
    
    memset(client, 0, sizeof(uv_transport_tcp_client_t));
    
    uv_tcp_init(tcp->base.loop, &client->handle);
    client->handle.data = tcp;
    
    if (uv_accept(server, (uv_stream_t*)&client->handle) == 0) {
        /* Add to client list */
        if (tcp->client_count >= tcp->client_capacity) {
            int new_capacity = tcp->client_capacity * 2;
            uv_transport_tcp_client_t** new_clients = (uv_transport_tcp_client_t**)uvrpc_realloc(
                tcp->clients, sizeof(uv_transport_tcp_client_t*) * new_capacity);
            if (!new_clients) {
                uv_close((uv_handle_t*)&client->handle, NULL);
                uvrpc_free(client);
                return;
            }
            tcp->clients = new_clients;
            tcp->client_capacity = new_capacity;
        }
        
        tcp->clients[tcp->client_count++] = client;
        
        /* Start reading */
        uv_read_start((uv_stream_t*)&client->handle, on_alloc, on_read);
        
        /* Call user callback */
        if (tcp->config.on_connect) {
            tcp->config.on_connect(&tcp->base, 0, tcp->config.callback_ctx);
        }
    } else {
        uv_close((uv_handle_t*)&client->handle, NULL);
        uvrpc_free(client);
    }
}

/* TCP vtable */
static const uv_transport_vtable_t tcp_vtable = {
    .listen = tcp_listen,
    .connect = tcp_connect,
    .stop = tcp_stop,
    .disconnect = tcp_disconnect,
    .send = tcp_send,
    .send_to = tcp_send_to,
    .free = tcp_free
};

/* Implementation */

static int tcp_listen(uv_transport_t* transport) {
    uv_transport_tcp_t* tcp = (uv_transport_tcp_t*)transport;
    
    if (!tcp->config.is_server) {
        return UV_TRANSPORT_ERROR_NOT_SUPPORTED;
    }
    
    /* Parse address */
    char* host = NULL;
    int port = 0;
    if (parse_address(tcp->config.address, &host, &port) != 0) {
        return UV_TRANSPORT_ERROR_INVALID_PARAM;
    }
    
    /* Initialize listen handle */
    uv_tcp_init(tcp->base.loop, &tcp->listen_handle);
    tcp->listen_handle.data = tcp;
    
    /* Bind */
    struct sockaddr_in addr;
    uv_ip4_addr(host, port, &addr);
    
    if (uv_tcp_bind(&tcp->listen_handle, (const struct sockaddr*)&addr, 0) != 0) {
        uvrpc_free(host);
        return UV_TRANSPORT_ERROR_IO;
    }
    
    /* Listen */
    if (uv_listen((uv_stream_t*)&tcp->listen_handle, 128, on_connection) != 0) {
        uv_close((uv_handle_t*)&tcp->listen_handle, NULL);
        uvrpc_free(host);
        return UV_TRANSPORT_ERROR_IO;
    }
    
    /* Initialize client array */
    tcp->clients = (uv_transport_tcp_client_t**)uvrpc_alloc(sizeof(uv_transport_tcp_client_t*) * 10);
    tcp->client_capacity = 10;
    tcp->client_count = 0;
    
    uvrpc_free(host);
    return UV_TRANSPORT_OK;
}

static int tcp_connect(uv_transport_t* transport) {
    uv_transport_tcp_t* tcp = (uv_transport_tcp_t*)transport;
    
    if (tcp->config.is_server) {
        return UV_TRANSPORT_ERROR_NOT_SUPPORTED;
    }
    
    /* Parse address */
    char* host = NULL;
    int port = 0;
    if (parse_address(tcp->config.address, &host, &port) != 0) {
        return UV_TRANSPORT_ERROR_INVALID_PARAM;
    }
    
    /* Initialize handle */
    uv_tcp_init(tcp->base.loop, &tcp->client.handle);
    tcp->client.handle.data = tcp;
    
    /* Set up address */
    struct sockaddr_in addr;
    uv_ip4_addr(host, port, &addr);
    
    /* Connect */
    tcp->connect_req.data = tcp;
    int result = uv_tcp_connect(&tcp->connect_req, &tcp->client.handle, 
                                (const struct sockaddr*)&addr, on_connect);
    
    uvrpc_free(host);
    return result;
}

static void tcp_stop(uv_transport_t* transport) {
    uv_transport_tcp_t* tcp = (uv_transport_tcp_t*)transport;
    
    if (!tcp->config.is_server) {
        return;
    }
    
    /* Close all client connections */
    for (int i = 0; i < tcp->client_count; i++) {
        if (tcp->clients[i]) {
            uv_close((uv_handle_t*)&tcp->clients[i]->handle, NULL);
            uvrpc_free(tcp->clients[i]);
        }
    }
    
    if (tcp->clients) {
        uvrpc_free(tcp->clients);
        tcp->clients = NULL;
    }
    tcp->client_count = 0;
    
    /* Close listen handle */
    uv_close((uv_handle_t*)&tcp->listen_handle, NULL);
}

static void tcp_disconnect(uv_transport_t* transport) {
    uv_transport_tcp_t* tcp = (uv_transport_tcp_t*)transport;
    
    if (tcp->config.is_server) {
        tcp_stop(transport);
    } else {
        uv_close((uv_handle_t*)&tcp->client.handle, NULL);
    }
}

static int tcp_send(uv_transport_t* transport, const uint8_t* data, size_t size) {
    uv_transport_tcp_t* tcp = (uv_transport_tcp_t*)transport;
    
    if (!data || size == 0) {
        return UV_TRANSPORT_ERROR_INVALID_PARAM;
    }
    
    uv_write_t* req = (uv_write_t*)uvrpc_alloc(sizeof(uv_write_t));
    if (!req) return UV_TRANSPORT_ERROR_NO_MEMORY;
    
    uint8_t* buf_data = (uint8_t*)uvrpc_alloc(size);
    if (!buf_data) {
        uvrpc_free(req);
        return UV_TRANSPORT_ERROR_NO_MEMORY;
    }
    memcpy(buf_data, data, size);
    
    uv_buf_t buf = uv_buf_init((char*)buf_data, size);
    req->data = buf_data;
    
    uv_stream_t* stream;
    if (tcp->config.is_server) {
        /* Broadcast to all clients */
        for (int i = 0; i < tcp->client_count; i++) {
            if (tcp->clients[i]) {
                uv_write_t* broadcast_req = (uv_write_t*)uvrpc_alloc(sizeof(uv_write_t));
                if (!broadcast_req) continue;
                
                uint8_t* broadcast_data = (uint8_t*)uvrpc_alloc(size);
                if (!broadcast_data) {
                    uvrpc_free(broadcast_req);
                    continue;
                }
                memcpy(broadcast_data, data, size);
                
                uv_buf_t broadcast_buf = uv_buf_init((char*)broadcast_data, size);
                broadcast_req->data = broadcast_data;
                
                uv_write(broadcast_req, (uv_stream_t*)&tcp->clients[i]->handle, &broadcast_buf, 1, on_write);
            }
        }
        uvrpc_free(req);
        uvrpc_free(buf_data);
        return UV_TRANSPORT_OK;
    } else {
        stream = (uv_stream_t*)&tcp->client.handle;
    }
    
    int result = uv_write(req, stream, &buf, 1, on_write);
    if (result != 0) {
        uvrpc_free(buf_data);
        uvrpc_free(req);
        return UV_TRANSPORT_ERROR_IO;
    }
    
    return UV_TRANSPORT_OK;
}

static int tcp_send_to(uv_transport_t* transport, const uint8_t* data, size_t size, void* peer) {
    (void)peer;
    return tcp_send(transport, data, size);
}

static void tcp_free(uv_transport_t* transport) {
    uv_transport_tcp_t* tcp = (uv_transport_tcp_t*)transport;
    
    if (tcp->config.address) {
        uvrpc_free((void*)tcp->config.address);
    }
    
    uvrpc_free(transport);
}

/* Export function */
uv_transport_t* uv_transport_tcp_create(const uv_transport_config_t* config) {
    if (!config || !config->loop || !config->address) {
        return NULL;
    }
    
    uv_transport_tcp_t* tcp = (uv_transport_tcp_t*)uvrpc_alloc(sizeof(uv_transport_tcp_t));
    if (!tcp) return NULL;
    
    memset(tcp, 0, sizeof(uv_transport_tcp_t));
    
    /* Initialize base */
    tcp->base.type = UV_TRANSPORT_TCP;
    tcp->base.loop = config->loop;
    tcp->base.vtable = &tcp_vtable;
    
    /* Copy config */
    tcp->config = *config;
    tcp->config.address = uvrpc_strdup(config->address);
    
    /* CRITICAL: Never touch loop->data */
    /* CRITICAL: Never call uv_run */
    
    return &tcp->base;
}
