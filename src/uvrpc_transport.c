/**
 * UVRPC Transport Layer - libuv Multi-Protocol
 * Supports TCP, UDP, IPC, INPROC
 */

#include "uvrpc_transport.h"
#include "../include/uvrpc.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <uthash.h>

#define UVRPC_MAX_FRAME_SIZE (10 * 1024 * 1024) /* 10MB max frame */

/* Transport structure */
struct uvrpc_transport {
    uv_loop_t* loop;
    int type;
    
    /* Address (for INPROC) */
    char* address;
    
    /* TCP handles */
    uv_tcp_t tcp_handle;
    uv_tcp_t listen_handle;
    uv_connect_t connect_req;
    
    /* UDP handles */
    uv_udp_t udp_handle;
    
    /* IPC handles */
    uv_pipe_t pipe_handle;
    uv_pipe_t listen_pipe;
    
    /* Read buffer */
    uint8_t read_buffer[8192];
    size_t read_pos;
    
    /* Callbacks */
    uvrpc_recv_callback_t recv_cb;
    uvrpc_connect_callback_t connect_cb;
    uvrpc_close_callback_t close_cb;
    void* ctx;
    
    /* Flags */
    int is_server;
    int is_connected;
    
    /* UDP client peer address */
    struct sockaddr_storage udp_peer;
    int has_udp_peer;
};

/* INPROC endpoint registry */
typedef struct inproc_endpoint {
    char* name;
    uvrpc_transport_t* server_transport;
    uvrpc_transport_t** clients;
    int client_count;
    int client_capacity;
    UT_hash_handle hh;
} inproc_endpoint_t;

/* Global INPROC registry */
static inproc_endpoint_t* g_inproc_registry = NULL;

/* Find or create INPROC endpoint */
static inproc_endpoint_t* inproc_get_endpoint(const char* name) {
    inproc_endpoint_t* endpoint = NULL;
    HASH_FIND_STR(g_inproc_registry, name, endpoint);
    
    if (!endpoint) {
        endpoint = calloc(1, sizeof(inproc_endpoint_t));
        endpoint->name = strdup(name);
        endpoint->server_transport = NULL;
        endpoint->clients = NULL;
        endpoint->client_count = 0;
        endpoint->client_capacity = 0;
        HASH_ADD_STR(g_inproc_registry, name, endpoint);
    }
    
    return endpoint;
}

/* Add client to INPROC endpoint */
static void inproc_add_client(inproc_endpoint_t* endpoint, uvrpc_transport_t* client) {
    if (endpoint->client_count >= endpoint->client_capacity) {
        endpoint->client_capacity = endpoint->client_capacity == 0 ? 4 : endpoint->client_capacity * 2;
        endpoint->clients = realloc(endpoint->clients, endpoint->client_capacity * sizeof(uvrpc_transport_t*));
    }
    endpoint->clients[endpoint->client_count++] = client;
}

/* Send data from client to server or server to client */
static void inproc_send_to_all(uvrpc_transport_t* sender, inproc_endpoint_t* endpoint, const uint8_t* data, size_t size) {
    for (int i = 0; i < endpoint->client_count; i++) {
        if (endpoint->clients[i] != sender && endpoint->clients[i]->recv_cb) {
            uint8_t* copy = malloc(size);
            if (copy) {
                memcpy(copy, data, size);
                endpoint->clients[i]->recv_cb(copy, size, endpoint->clients[i]->ctx);
            }
        }
    }
    
    /* Also send to server */
    if (sender != endpoint->server_transport && endpoint->server_transport && endpoint->server_transport->recv_cb) {
        uint8_t* copy = malloc(size);
        if (copy) {
            memcpy(copy, data, size);
            endpoint->server_transport->recv_cb(copy, size, endpoint->server_transport->ctx);
        }
    }
}

/* Parse 4-byte length prefix */
static int parse_frame_length(uvrpc_transport_t* transport, size_t* frame_size) {
    if (transport->read_pos < 4) return 0;
    
    uint32_t len = (transport->read_buffer[0] << 24) |
                   (transport->read_buffer[1] << 16) |
                   (transport->read_buffer[2] << 8) |
                   transport->read_buffer[3];
    
    if (len > UVRPC_MAX_FRAME_SIZE) return -1;
    
    *frame_size = len;
    return 1;
}

/* Process complete frames */
static void process_frames(uvrpc_transport_t* transport) {
    while (1) {
        size_t frame_size = 0;
        int rv = parse_frame_length(transport, &frame_size);
        
        if (rv <= 0) break; /* Incomplete frame or error */
        
        size_t total_size = 4 + frame_size;
        if (transport->read_pos < total_size) break; /* Not enough data */
        
        /* Extract frame data (skip 4-byte length prefix) */
        uint8_t* frame_data = malloc(frame_size);
        if (frame_data) {
            memcpy(frame_data, transport->read_buffer + 4, frame_size);
            
            if (transport->recv_cb) {
                transport->recv_cb(frame_data, frame_size, transport->ctx);
            }
            
            free(frame_data);
        }
        
        /* Remove processed frame from buffer */
        memmove(transport->read_buffer, transport->read_buffer + total_size,
                transport->read_pos - total_size);
        transport->read_pos -= total_size;
    }
}

/* Alloc callback for libuv */
static void alloc_callback(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    uvrpc_transport_t* transport = (uvrpc_transport_t*)handle->data;
    (void)suggested_size;
    
    buf->base = (char*)(transport->read_buffer + transport->read_pos);
    buf->len = sizeof(transport->read_buffer) - transport->read_pos;
}

/* Read callback (TCP/IPC) */
static void read_callback(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    uvrpc_transport_t* transport = (uvrpc_transport_t*)stream->data;
    
    if (nread < 0) {
        if (nread != UV_EOF) {
            fprintf(stderr, "Read error: %s\n", uv_strerror(nread));
        }
        uv_read_stop(stream);
        return;
    }
    
    if (nread == 0) return;
    
    transport->read_pos += nread;
    process_frames(transport);
}

/* Connection callback for TCP server */
static void tcp_connection_callback(uv_stream_t* server, int status) {
    if (status < 0) {
        fprintf(stderr, "Connection error: %s\n", uv_strerror(status));
        return;
    }
    
    uvrpc_transport_t* transport = (uvrpc_transport_t*)server->data;
    
    /* Accept connection */
    uv_tcp_t* client = (uv_tcp_t*)malloc(sizeof(uv_tcp_t));
    if (!client) return;
    
    uv_tcp_init(transport->loop, client);
    client->data = transport;
    
    if (uv_accept(server, (uv_stream_t*)client) == 0) {
        uv_read_start((uv_stream_t*)client, alloc_callback, read_callback);
    } else {
        uv_close((uv_handle_t*)client, NULL);
        free(client);
    }
}

/* Connection callback for IPC server */
static void ipc_connection_callback(uv_stream_t* server, int status) {
    if (status < 0) {
        fprintf(stderr, "IPC connection error: %s\n", uv_strerror(status));
        return;
    }
    
    uvrpc_transport_t* transport = (uvrpc_transport_t*)server->data;
    
    /* Accept connection */
    uv_pipe_t* client = (uv_pipe_t*)malloc(sizeof(uv_pipe_t));
    if (!client) return;
    
    uv_pipe_init(transport->loop, client, 0);
    client->data = transport;
    
    if (uv_accept(server, (uv_stream_t*)client) == 0) {
        uv_read_start((uv_stream_t*)client, alloc_callback, read_callback);
    } else {
        uv_close((uv_handle_t*)client, NULL);
        free(client);
    }
}

/* Connect callback for TCP client */
static void tcp_connect_callback(uv_connect_t* req, int status) {
    uvrpc_transport_t* transport = (uvrpc_transport_t*)req->handle->data;
    
    if (status < 0) {
        fprintf(stderr, "TCP connect error: %s\n", uv_strerror(status));
        if (transport->connect_cb) {
            transport->connect_cb(status, transport->ctx);
        }
        return;
    }
    
    transport->is_connected = 1;
    uv_read_start((uv_stream_t*)&transport->tcp_handle, alloc_callback, read_callback);
    
    if (transport->connect_cb) {
        transport->connect_cb(0, transport->ctx);
    }
}

/* Connect callback for IPC client */
static void ipc_connect_callback(uv_connect_t* req, int status) {
    uvrpc_transport_t* transport = (uvrpc_transport_t*)req->handle->data;
    
    if (status < 0) {
        fprintf(stderr, "IPC connect error: %s\n", uv_strerror(status));
        if (transport->connect_cb) {
            transport->connect_cb(status, transport->ctx);
        }
        return;
    }
    
    transport->is_connected = 1;
    uv_read_start((uv_stream_t*)&transport->pipe_handle, alloc_callback, read_callback);
    
    if (transport->connect_cb) {
        transport->connect_cb(0, transport->ctx);
    }
}

/* UDP receive callback */
static void udp_recv_callback(uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf,
                              const struct sockaddr* addr, unsigned flags) {
    uvrpc_transport_t* transport = (uvrpc_transport_t*)handle->data;
    
    if (nread < 0) {
        fprintf(stderr, "UDP recv error: %s\n", uv_strerror(nread));
        return;
    }
    
    if (nread == 0) return;
    
    /* For client, save peer address for sending responses */
    if (!transport->is_server && addr) {
        memcpy(&transport->udp_peer, addr, sizeof(struct sockaddr_storage));
        transport->has_udp_peer = 1;
    }
    
    /* Process frames directly from buffer */
    transport->read_pos = nread;
    if (nread < sizeof(transport->read_buffer)) {
        memcpy(transport->read_buffer, buf->base, nread);
    }
    process_frames(transport);
}

/* Write callback */
static void write_callback(uv_write_t* req, int status) {
    free(req->data);
    free(req);
    (void)status;
}

/* UDP send callback */
static void udp_send_callback(uv_udp_send_t* req, int status) {
    free(req->data);
    free(req);
    (void)status;
}

/* Create server transport */
uvrpc_transport_t* uvrpc_transport_server_new(uv_loop_t* loop, int transport_type) {
    uvrpc_transport_t* transport = calloc(1, sizeof(uvrpc_transport_t));
    if (!transport) return NULL;
    
    transport->loop = loop;
    transport->type = transport_type;
    transport->is_server = 1;
    transport->is_connected = 0;
    transport->read_pos = 0;
    transport->has_udp_peer = 0;
    transport->address = NULL;
    
    /* Initialize handles based on type */
    switch (transport_type) {
        case UVRPC_TRANSPORT_TCP:
            uv_tcp_init(loop, &transport->listen_handle);
            transport->listen_handle.data = transport;
            break;
        case UVRPC_TRANSPORT_UDP:
            uv_udp_init(loop, &transport->udp_handle);
            transport->udp_handle.data = transport;
            break;
        case UVRPC_TRANSPORT_IPC:
            uv_pipe_init(loop, &transport->listen_pipe, 0);
            transport->listen_pipe.data = transport;
            break;
        case UVRPC_TRANSPORT_INPROC:
            /* INPROC uses in-memory registry */
            break;
    }
    
    return transport;
}

/* Create client transport */
uvrpc_transport_t* uvrpc_transport_client_new(uv_loop_t* loop, int transport_type) {
    uvrpc_transport_t* transport = calloc(1, sizeof(uvrpc_transport_t));
    if (!transport) return NULL;
    
    transport->loop = loop;
    transport->type = transport_type;
    transport->is_server = 0;
    transport->is_connected = 0;
    transport->read_pos = 0;
    transport->has_udp_peer = 0;
    transport->address = NULL;
    
    /* Initialize handles based on type */
    switch (transport_type) {
        case UVRPC_TRANSPORT_TCP:
            uv_tcp_init(loop, &transport->tcp_handle);
            transport->tcp_handle.data = transport;
            break;
        case UVRPC_TRANSPORT_UDP:
            uv_udp_init(loop, &transport->udp_handle);
            transport->udp_handle.data = transport;
            break;
        case UVRPC_TRANSPORT_IPC:
            uv_pipe_init(loop, &transport->pipe_handle, 0);
            transport->pipe_handle.data = transport;
            break;
        case UVRPC_TRANSPORT_INPROC:
            /* INPROC uses in-memory registry */
            break;
    }
    
    return transport;
}

/* Free transport */
void uvrpc_transport_free(uvrpc_transport_t* transport) {
    if (!transport) return;
    
    if (transport->address) {
        free(transport->address);
    }
    
    switch (transport->type) {
        case UVRPC_TRANSPORT_TCP:
            if (transport->is_server) {
                uv_close((uv_handle_t*)&transport->listen_handle, NULL);
            } else {
                uv_close((uv_handle_t*)&transport->tcp_handle, NULL);
            }
            break;
        case UVRPC_TRANSPORT_UDP:
            uv_close((uv_handle_t*)&transport->udp_handle, NULL);
            break;
        case UVRPC_TRANSPORT_IPC:
            if (transport->is_server) {
                uv_close((uv_handle_t*)&transport->listen_pipe, NULL);
            } else {
                uv_close((uv_handle_t*)&transport->pipe_handle, NULL);
            }
            break;
        case UVRPC_TRANSPORT_INPROC:
            break;
    }
    
    free(transport);
}

/* Listen on address (server) */
int uvrpc_transport_listen(uvrpc_transport_t* transport, const char* address,
                            uvrpc_recv_callback_t recv_cb, void* ctx) {
    if (!transport || !address) return UVRPC_ERROR_INVALID_PARAM;
    
    transport->recv_cb = recv_cb;
    transport->ctx = ctx;
    
    switch (transport->type) {
        case UVRPC_TRANSPORT_TCP: {
            struct sockaddr_storage addr;
            int addrlen = sizeof(addr);
            
            /* Strip tcp:// prefix if present */
            const char* addr_to_parse = address;
            if (strncmp(address, "tcp://", 6) == 0) {
                addr_to_parse = address + 6;
            }
            
            char host[256];
            int port = 5555;
            if (sscanf(addr_to_parse, "%255[^:]:%d", host, &port) == 2) {
                if (uv_ip4_addr(host, port, (struct sockaddr_in*)&addr) != 0 &&
                    uv_ip6_addr(host, port, (struct sockaddr_in6*)&addr) != 0) {
                    return UVRPC_ERROR_INVALID_PARAM;
                }
            } else {
                return UVRPC_ERROR_INVALID_PARAM;
            }
            
            if (uv_tcp_bind(&transport->listen_handle, (const struct sockaddr*)&addr, 0) != 0) {
                return UVRPC_ERROR;
            }
            
            if (uv_listen((uv_stream_t*)&transport->listen_handle, 128, tcp_connection_callback) != 0) {
                return UVRPC_ERROR;
            }
            break;
        }
        case UVRPC_TRANSPORT_UDP: {
            struct sockaddr_storage addr;
            int addrlen = sizeof(addr);
            
            /* Strip udp:// prefix if present */
            const char* addr_to_parse = address;
            if (strncmp(address, "udp://", 6) == 0) {
                addr_to_parse = address + 6;
            }
            
            char host[256];
            int port = 5555;
            if (sscanf(addr_to_parse, "%255[^:]:%d", host, &port) == 2) {
                if (uv_ip4_addr(host, port, (struct sockaddr_in*)&addr) != 0 &&
                    uv_ip6_addr(host, port, (struct sockaddr_in6*)&addr) != 0) {
                    return UVRPC_ERROR_INVALID_PARAM;
                }
            } else {
                return UVRPC_ERROR_INVALID_PARAM;
            }
            
            if (uv_udp_bind(&transport->udp_handle, (const struct sockaddr*)&addr, 0) != 0) {
                return UVRPC_ERROR;
            }
            
            uv_udp_recv_start(&transport->udp_handle, alloc_callback, udp_recv_callback);
            break;
        }
        case UVRPC_TRANSPORT_IPC: {
            /* Strip ipc:// prefix if present */
            const char* addr_to_parse = address;
            if (strncmp(address, "ipc://", 6) == 0) {
                addr_to_parse = address + 6;
            }
            
            if (uv_pipe_bind(&transport->listen_pipe, addr_to_parse) != 0) {
                return UVRPC_ERROR;
            }
            
            if (uv_listen((uv_stream_t*)&transport->listen_pipe, 128, ipc_connection_callback) != 0) {
                return UVRPC_ERROR;
            }
            break;
        }
        case UVRPC_TRANSPORT_INPROC: {
            /* Strip inproc:// prefix */
            const char* name = address;
            if (strncmp(address, "inproc://", 9) == 0) {
                name = address + 9;
            }
            
            transport->address = strdup(name);
            
            inproc_endpoint_t* endpoint = inproc_get_endpoint(name);
            if (endpoint->server_transport) {
                return UVRPC_ERROR; /* Server already exists */
            }
            
            endpoint->server_transport = transport;
            transport->is_connected = 1;
            
            return UVRPC_OK;
        }
    }
    
    return UVRPC_OK;
}

/* Connect to address (client) */
int uvrpc_transport_connect(uvrpc_transport_t* transport, const char* address,
                             uvrpc_connect_callback_t connect_cb,
                             uvrpc_recv_callback_t recv_cb, void* ctx) {
    if (!transport || !address) return UVRPC_ERROR_INVALID_PARAM;
    
    transport->connect_cb = connect_cb;
    transport->recv_cb = recv_cb;
    transport->ctx = ctx;
    
    switch (transport->type) {
        case UVRPC_TRANSPORT_TCP: {
            struct sockaddr_storage addr;
            
            /* Strip tcp:// prefix if present */
            const char* addr_to_parse = address;
            if (strncmp(address, "tcp://", 6) == 0) {
                addr_to_parse = address + 6;
            }
            
            char host[256];
            int port = 5555;
            if (sscanf(addr_to_parse, "%255[^:]:%d", host, &port) == 2) {
                if (uv_ip4_addr(host, port, (struct sockaddr_in*)&addr) != 0 &&
                    uv_ip6_addr(host, port, (struct sockaddr_in6*)&addr) != 0) {
                    return UVRPC_ERROR_INVALID_PARAM;
                }
            } else {
                return UVRPC_ERROR_INVALID_PARAM;
            }
            
            return uv_tcp_connect(&transport->connect_req, &transport->tcp_handle,
                                  (const struct sockaddr*)&addr, tcp_connect_callback);
        }
        case UVRPC_TRANSPORT_UDP: {
            struct sockaddr_storage addr;
            
            /* Strip udp:// prefix if present */
            const char* addr_to_parse = address;
            if (strncmp(address, "udp://", 6) == 0) {
                addr_to_parse = address + 6;
            }
            
            char host[256];
            int port = 5555;
            if (sscanf(addr_to_parse, "%255[^:]:%d", host, &port) == 2) {
                if (uv_ip4_addr(host, port, (struct sockaddr_in*)&addr) != 0 &&
                    uv_ip6_addr(host, port, (struct sockaddr_in6*)&addr) != 0) {
                    return UVRPC_ERROR_INVALID_PARAM;
                }
            } else {
                return UVRPC_ERROR_INVALID_PARAM;
            }
            
            memcpy(&transport->udp_peer, &addr, sizeof(addr));
            transport->has_udp_peer = 1;
            transport->is_connected = 1;
            
            uv_udp_recv_start(&transport->udp_handle, alloc_callback, udp_recv_callback);
            
            if (connect_cb) connect_cb(0, ctx);
            return UVRPC_OK;
        }
        case UVRPC_TRANSPORT_IPC: {
            /* Strip ipc:// prefix if present */
            const char* addr_to_parse = address;
            if (strncmp(address, "ipc://", 6) == 0) {
                addr_to_parse = address + 6;
            }
            
            uv_pipe_connect(&transport->connect_req, &transport->pipe_handle, addr_to_parse, ipc_connect_callback);
            return UVRPC_OK;
        }
        case UVRPC_TRANSPORT_INPROC: {
            /* Strip inproc:// prefix */
            const char* name = address;
            if (strncmp(address, "inproc://", 9) == 0) {
                name = address + 9;
            }
            
            transport->address = strdup(name);
            
            inproc_endpoint_t* endpoint = inproc_get_endpoint(name);
            if (!endpoint->server_transport) {
                return UVRPC_ERROR; /* No server exists */
            }
            
            inproc_add_client(endpoint, transport);
            transport->is_connected = 1;
            
            if (connect_cb) connect_cb(0, ctx);
            return UVRPC_OK;
        }
    }
    
    return UVRPC_ERROR;
}

/* Disconnect client */
void uvrpc_transport_disconnect(uvrpc_transport_t* transport) {
    if (!transport) return;
    
    if (transport->is_connected) {
        switch (transport->type) {
            case UVRPC_TRANSPORT_TCP:
                uv_read_stop((uv_stream_t*)&transport->tcp_handle);
                uv_close((uv_handle_t*)&transport->tcp_handle, NULL);
                break;
            case UVRPC_TRANSPORT_UDP:
                uv_udp_recv_stop(&transport->udp_handle);
                break;
            case UVRPC_TRANSPORT_IPC:
                uv_read_stop((uv_stream_t*)&transport->pipe_handle);
                uv_close((uv_handle_t*)&transport->pipe_handle, NULL);
                break;
            case UVRPC_TRANSPORT_INPROC:
                break;
        }
        transport->is_connected = 0;
    }
}

/* Send data (4-byte length prefix + data) */
void uvrpc_transport_send(uvrpc_transport_t* transport, const uint8_t* data, size_t size) {
    if (!transport || !data || size == 0) return;
    
    /* Allocate buffer with 4-byte length prefix */
    size_t total_size = 4 + size;
    uint8_t* buffer = malloc(total_size);
    if (!buffer) return;
    
    /* Write length prefix (big-endian) */
    buffer[0] = (size >> 24) & 0xFF;
    buffer[1] = (size >> 16) & 0xFF;
    buffer[2] = (size >> 8) & 0xFF;
    buffer[3] = size & 0xFF;
    
    /* Write data */
    memcpy(buffer + 4, data, size);
    
    /* Send based on transport type */
    switch (transport->type) {
        case UVRPC_TRANSPORT_TCP: {
            uv_write_t* req = malloc(sizeof(uv_write_t));
            if (!req) {
                free(buffer);
                return;
            }
            
            uv_buf_t buf = uv_buf_init((char*)buffer, total_size);
            req->data = buffer;
            
            uv_stream_t* stream = transport->is_server ? 
                (uv_stream_t*)&transport->listen_handle : (uv_stream_t*)&transport->tcp_handle;
            uv_write(req, stream, &buf, 1, write_callback);
            break;
        }
        case UVRPC_TRANSPORT_UDP: {
            uv_udp_send_t* req = malloc(sizeof(uv_udp_send_t));
            if (!req) {
                free(buffer);
                return;
            }
            
            uv_buf_t buf = uv_buf_init((char*)buffer, total_size);
            req->data = buffer;
            
            struct sockaddr* addr = (struct sockaddr*)&transport->udp_peer;
            if (transport->has_udp_peer) {
                uv_udp_send(req, &transport->udp_handle, &buf, 1, addr, udp_send_callback);
            }
            break;
        }
        case UVRPC_TRANSPORT_IPC: {
            uv_write_t* req = malloc(sizeof(uv_write_t));
            if (!req) {
                free(buffer);
                return;
            }
            
            uv_buf_t buf = uv_buf_init((char*)buffer, total_size);
            req->data = buffer;
            
            uv_stream_t* stream = transport->is_server ? 
                (uv_stream_t*)&transport->listen_pipe : (uv_stream_t*)&transport->pipe_handle;
            uv_write(req, stream, &buf, 1, write_callback);
            break;
        }
        case UVRPC_TRANSPORT_INPROC: {
            inproc_endpoint_t* endpoint = NULL;
            if (transport->address) {
                HASH_FIND_STR(g_inproc_registry, transport->address, endpoint);
            }
            
            if (endpoint) {
                inproc_send_to_all(transport, endpoint, data, size);
            }
            
            free(buffer);
            break;
        }
    }
}