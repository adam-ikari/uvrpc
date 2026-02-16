/**
 * UVRPC Transport Layer - libuv Multi-Protocol
 * Supports TCP, UDP, IPC, INPROC
 */

#include "uvrpc_transport.h"
#include "uvrpc_transport_internal.h"
#include "../include/uvrpc.h"
#include "../include/uvrpc_allocator.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <uthash.h>

#define UVRPC_MAX_FRAME_SIZE (10 * 1024 * 1024) /* 10MB max frame */
#define UVRPC_MAX_UDP_PEERS 1000 /* Maximum UDP peers to prevent DoS */

/* Forward declaration for INPROC implementation */
typedef struct uvrpc_inproc_transport uvrpc_inproc_transport_t;
typedef struct inproc_endpoint inproc_endpoint_t;

/* Process complete frames */
static void process_frames(uvrpc_transport_t* transport) {
    while (1) {
        size_t frame_size = 0;
        int rv = parse_frame_length(transport->read_buffer, transport->read_pos, &frame_size);

        if (rv <= 0) break; /* Incomplete frame or error */

        /* Check for integer overflow in total_size calculation */
        if (frame_size > SIZE_MAX - 4) {
            fprintf(stderr, "Frame size too large, potential overflow\n");
            transport->read_pos = 0; /* Reset buffer to prevent corruption */
            break;
        }

        size_t total_size = 4 + frame_size;
        if (transport->read_pos < total_size) break; /* Not enough data */

        /* Additional bounds check before memcpy */
        if (total_size > sizeof(transport->read_buffer)) {
            fprintf(stderr, "Frame exceeds buffer size\n");
            transport->read_pos = 0; /* Reset buffer to prevent corruption */
            break;
        }

        /* Extract frame data (skip 4-byte length prefix) */
        uint8_t* frame_data = uvrpc_alloc(frame_size);
        if (frame_data) {
            memcpy(frame_data, transport->read_buffer + 4, frame_size);

            if (transport->recv_cb) {
                transport->recv_cb(frame_data, frame_size, transport->ctx);
            }

            uvrpc_free(frame_data);
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

/* Alloc callback for client connections */
static void client_alloc_callback(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    client_connection_t* conn = (client_connection_t*)handle->data;
    (void)suggested_size;
    
    buf->base = (char*)(conn->read_buffer + conn->read_pos);
    buf->len = sizeof(conn->read_buffer) - conn->read_pos;
}

/* Read callback (TCP/IPC) */
static void read_callback(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    uvrpc_transport_t* transport = (uvrpc_transport_t*)stream->data;

    if (nread < 0) {
        if (nread != UV_EOF && transport->error_cb) {
            transport->error_cb(nread, uv_strerror(nread), transport->ctx);
        }
        uv_read_stop(stream);
        return;
    }

    if (nread == 0) return;
    
    transport->read_pos += nread;
    process_frames(transport);
}

/* Read callback for client connections (TCP/IPC server) */
static void client_read_callback(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    client_connection_t* conn = (client_connection_t*)stream->data;
    
    if (nread < 0) {
        /* Connection closed or error */
        if (nread != UV_EOF) {
            fprintf(stderr, "Client connection error: %s\n", uv_strerror(nread));
        }
        uv_read_stop(stream);
        return;
    }
    
    if (nread == 0) return;
    
    conn->read_pos += nread;
    
    /* Process frames in client buffer */
    while (conn->read_pos >= 4) {
        /* Get frame size */
        uint32_t frame_size = (uint32_t)conn->read_buffer[0] << 24 |
                             (uint32_t)conn->read_buffer[1] << 16 |
                             (uint32_t)conn->read_buffer[2] << 8 |
                             (uint32_t)conn->read_buffer[3];
        
        if (frame_size == 0 || frame_size > 1024*1024) {
            fprintf(stderr, "Invalid frame size: %u\n", frame_size);
            uv_read_stop(stream);
            return;
        }
        
        size_t total_size = 4 + frame_size;
        if (conn->read_pos < total_size) {
            /* Not enough data yet */
            break;
        }
        
        /* Call server receive callback with stream context */
        if (conn->recv_cb) {
            uint8_t* frame_data = uvrpc_alloc(frame_size);
            if (frame_data) {
                memcpy(frame_data, conn->read_buffer + 4, frame_size);
                /* Pass stream as context for response */
                conn->recv_cb(frame_data, frame_size, stream);
                uvrpc_free(frame_data);
            }
        } else {
        }
        
        /* Remove processed frame from buffer */
        memmove(conn->read_buffer, conn->read_buffer + total_size,
                conn->read_pos - total_size);
        conn->read_pos -= total_size;
    }
}

/* Connection callback for TCP server */
static void tcp_connection_callback(uv_stream_t* server, int status) {
    if (status < 0) {
        uvrpc_transport_t* transport = (uvrpc_transport_t*)server->data;
        if (transport->error_cb) {
            transport->error_cb(status, uv_strerror(status), transport->ctx);
        }
        return;
    }

    uvrpc_transport_t* transport = (uvrpc_transport_t*)server->data;

    /* Accept connection */
    client_connection_t* client_conn = uvrpc_calloc(1, sizeof(client_connection_t));
    if (!client_conn) {
        fprintf(stderr, "Failed to allocate client connection\n");
        return;
    }

    client_conn->is_tcp = 1;
    uv_tcp_init(transport->loop, &client_conn->handle.tcp_handle);
    client_conn->handle.tcp_handle.data = client_conn;
    client_conn->recv_cb = transport->recv_cb;
    client_conn->recv_ctx = transport->ctx;
    client_conn->server = transport->ctx;  /* Store server pointer */
    client_conn->read_pos = 0;

    if (uv_accept(server, (uv_stream_t*)&client_conn->handle.tcp_handle) == 0) {
        uv_read_start((uv_stream_t*)&client_conn->handle.tcp_handle, client_alloc_callback, client_read_callback);

        /* Add to client connections list */
        client_conn->next = transport->client_connections;
        transport->client_connections = client_conn;
    } else {
        uv_close((uv_handle_t*)&client_conn->handle.tcp_handle, NULL);
        uvrpc_free(client_conn);
    }
}

/* Connection callback for IPC server */
static void ipc_connection_callback(uv_stream_t* server, int status) {
    if (status < 0) {
        uvrpc_transport_t* transport = (uvrpc_transport_t*)server->data;
        if (transport->error_cb) {
            transport->error_cb(status, uv_strerror(status), transport->ctx);
        }
        return;
    }

    uvrpc_transport_t* transport = (uvrpc_transport_t*)server->data;

    /* Accept connection */
    client_connection_t* client_conn = uvrpc_calloc(1, sizeof(client_connection_t));
    if (!client_conn) {
        fprintf(stderr, "Failed to allocate IPC client connection\n");
        return;
    }

    client_conn->is_tcp = 0;
    uv_pipe_init(transport->loop, &client_conn->handle.pipe_handle, 0);
    client_conn->handle.pipe_handle.data = client_conn;
    client_conn->recv_cb = transport->recv_cb;
    client_conn->recv_ctx = transport->ctx;
    client_conn->server = transport->ctx;  /* Store server pointer */
    client_conn->read_pos = 0;

    if (uv_accept(server, (uv_stream_t*)&client_conn->handle.pipe_handle) == 0) {
        uv_read_start((uv_stream_t*)&client_conn->handle.pipe_handle, client_alloc_callback, client_read_callback);

        /* Add to client connections list */
        client_conn->next = transport->client_connections;
        transport->client_connections = client_conn;
    } else {
        uv_close((uv_handle_t*)&client_conn->handle.pipe_handle, NULL);
        uvrpc_free(client_conn);
    }
}

/* Async callback handler */
static void async_callback(uv_async_t* handle) {
    uvrpc_transport_t* transport = (uvrpc_transport_t*)handle->data;
    if (transport->connect_cb) {
        transport->connect_cb(0, transport->ctx);
        transport->connect_cb = NULL;
    }
}

/* Timeout timer callback */
static void timeout_callback(uv_timer_t* handle) {
    uvrpc_transport_t* transport = (uvrpc_transport_t*)handle->data;

    /* Stop timer */
    uv_timer_stop(&transport->timeout_timer);

    /* Disconnect if connected */
    if (transport->is_connected) {
        uvrpc_transport_disconnect(transport);
    }

    /* Call error callback with timeout error */
    if (transport->error_cb) {
        transport->error_cb(UVRPC_ERROR_TIMEOUT, "Connection timeout", transport->ctx);
    }

    /* Call connect callback with timeout error */
    if (transport->connect_cb) {
        transport->connect_cb(UVRPC_ERROR_TIMEOUT, transport->ctx);
        transport->connect_cb = NULL;
    }
}

/* Connect callback for TCP client */
static void tcp_connect_callback(uv_connect_t* req, int status) {
    uvrpc_transport_t* transport = (uvrpc_transport_t*)req->handle->data;

    /* Stop timeout timer on connect or error */
    if (transport->timeout_enabled) {
        uv_timer_stop(&transport->timeout_timer);
    }

    if (status < 0) {
        if (transport->error_cb) {
            transport->error_cb(status, uv_strerror(status), transport->ctx);
        }
        if (transport->connect_cb) {
            transport->connect_cb(status, transport->ctx);
        }
        return;
    }

    transport->is_connected = 1;
    uv_read_start((uv_stream_t*)&transport->tcp_handle, alloc_callback, read_callback);

    if (transport->connect_cb) {
        uvrpc_connect_callback_t cb = transport->connect_cb;
        transport->connect_cb = NULL;
        cb(0, transport->ctx);
    }
}

/* Connect callback for IPC client */
static void ipc_connect_callback(uv_connect_t* req, int status) {
    uvrpc_transport_t* transport = (uvrpc_transport_t*)req->handle->data;

    /* Stop timeout timer on connect or error */
    if (transport->timeout_enabled) {
        uv_timer_stop(&transport->timeout_timer);
    }

    if (status < 0) {
        if (transport->error_cb) {
            transport->error_cb(status, uv_strerror(status), transport->ctx);
        }
        if (transport->connect_cb) {
            uvrpc_connect_callback_t cb = transport->connect_cb;
            transport->connect_cb = NULL;
            cb(status, transport->ctx);
        }
        return;
    }

    transport->is_connected = 1;
    uv_read_start((uv_stream_t*)&transport->pipe_handle, alloc_callback, read_callback);

    if (transport->connect_cb) {
        uvrpc_connect_callback_t cb = transport->connect_cb;
        transport->connect_cb = NULL;
        cb(0, transport->ctx);
    }
}
/* UDP receive callback */
static void udp_recv_callback(uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf,
                              const struct sockaddr* addr, unsigned flags) {
    uvrpc_transport_t* transport = (uvrpc_transport_t*)handle->data;

    if (nread < 0) {
        if (transport->error_cb) {
            transport->error_cb(nread, uv_strerror(nread), transport->ctx);
        }
        return;
    }

    if (nread == 0) return;

    /* For client, save peer address for sending responses */
    if (!transport->is_server && addr) {
        /* Check if peer already exists in list */
        udp_peer_t* peer = transport->udp_peers;
        int found = 0;
        while (peer) {
            if (memcmp(&peer->addr, addr, sizeof(struct sockaddr_storage)) == 0) {
                found = 1;
                break;
            }
            peer = peer->next;
        }

        /* Add new peer if not found */
        if (!found) {
            udp_peer_t* new_peer = uvrpc_calloc(1, sizeof(udp_peer_t));
            if (new_peer) {
                memcpy(&new_peer->addr, addr, sizeof(struct sockaddr_storage));
                new_peer->next = transport->udp_peers;
                transport->udp_peers = new_peer;
            }
        }
    }

    /* Process frames directly from buffer */
    transport->read_pos = nread;
    if (nread <= sizeof(transport->read_buffer)) {
        memcpy(transport->read_buffer, buf->base, nread);
    } else {
        /* Data exceeds buffer size, truncate to prevent overflow */
        fprintf(stderr, "UDP packet exceeds buffer size, truncating\n");
        memcpy(transport->read_buffer, buf->base, sizeof(transport->read_buffer));
        transport->read_pos = sizeof(transport->read_buffer);
    }
    process_frames(transport);
}

/* Write callback */
static void write_callback(uv_write_t* req, int status) {
    if (status < 0) {
        /* Handle send error - could add error callback here if needed */
        fprintf(stderr, "Write error: %s\n", uv_strerror(status));
    }
    uvrpc_free(req->data);
    uvrpc_free(req);
}

/* UDP send callback */
static void udp_send_callback(uv_udp_send_t* req, int status) {
    if (status < 0) {
        /* Handle send error - could add error callback here if needed */
        fprintf(stderr, "UDP send error: %s\n", uv_strerror(status));
    }
    if (req->data) {
        uvrpc_free(req->data);
    }
    uvrpc_free(req);
}

/* Create server transport */
uvrpc_transport_t* uvrpc_transport_server_new(uv_loop_t* loop, int transport_type) {
    uvrpc_transport_t* transport = uvrpc_calloc(1, sizeof(uvrpc_transport_t));
    if (!transport) return NULL;
    
    transport->loop = loop;
    transport->type = transport_type;
    transport->is_server = 1;
    transport->is_connected = 0;
    transport->read_pos = 0;
    transport->udp_peers = NULL;
    transport->address = NULL;
    transport->timeout_ms = 0;
    transport->timeout_enabled = 0;

    /* Initialize async handle for callbacks */
    uv_async_init(loop, &transport->async_handle, async_callback);
    transport->async_handle.data = transport;

    /* Initialize timeout timer */
    int timer_init_result = uv_timer_init(loop, &transport->timeout_timer);
    if (timer_init_result != 0) {
        fprintf(stderr, "Failed to init timer: %s\n", uv_strerror(timer_init_result));
    }
    transport->timeout_timer.data = transport;
    
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
            /* INPROC uses inproc transport implementation */
            {
                /* Don't create endpoint yet - will be created in listen */
                void* impl = NULL;
                int rc = uvrpc_transport_inproc_server_new(loop, NULL, NULL, NULL, NULL, NULL, &impl);
                if (rc != UVRPC_OK || !impl) {
                    uvrpc_free(transport);
                    return NULL;
                }
                /* Set up transport to use INPROC implementation */
                transport->impl = impl;
                transport->vtable = &inproc_vtable;
                transport->type = UVRPC_TRANSPORT_INPROC;
                transport->is_server = 1;
                transport->is_connected = 0;
                transport->address = NULL;
                return transport;
            }
            break;
    }
    
    return transport;
}

/* Create client transport */
uvrpc_transport_t* uvrpc_transport_client_new(uv_loop_t* loop, int transport_type) {
    uvrpc_transport_t* transport = uvrpc_calloc(1, sizeof(uvrpc_transport_t));
    if (!transport) return NULL;
    
    transport->loop = loop;
    transport->type = transport_type;
    transport->is_server = 0;
    transport->is_connected = 0;
    transport->read_pos = 0;
    transport->udp_peers = NULL;
    transport->address = NULL;
    
    /* Initialize async handle for callbacks */
    uv_async_init(loop, &transport->async_handle, async_callback);
    transport->async_handle.data = transport;
    
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
            /* INPROC uses inproc transport implementation */
            {
                /* Don't connect yet - will connect in uvrpc_transport_connect */
                void* impl = NULL;
                int rc = uvrpc_transport_inproc_client_new(loop, NULL, NULL, NULL, NULL, NULL, &impl);
                if (rc != UVRPC_OK || !impl) {
                    uvrpc_free(transport);
                    return NULL;
                }
                /* Set up transport to use INPROC implementation */
                transport->impl = impl;
                transport->vtable = &inproc_vtable;
                transport->type = UVRPC_TRANSPORT_INPROC;
                transport->is_server = 0;
                transport->is_connected = 0;
                transport->address = NULL;
                return transport;
            }
            break;
    }
    
    return transport;
}

/* Free transport */
void uvrpc_transport_free(uvrpc_transport_t* transport) {
    if (!transport) return;
    
    uv_loop_t* loop = transport->loop;
    
    /* Clean up all client connections for TCP/IPC servers */
    client_connection_t* conn = transport->client_connections;
    while (conn) {
        client_connection_t* next = conn->next;
        if (conn->is_tcp) {
            uv_read_stop((uv_stream_t*)&conn->handle.tcp_handle);
            if (!uv_is_closing((uv_handle_t*)&conn->handle.tcp_handle)) {
                uv_close((uv_handle_t*)&conn->handle.tcp_handle, NULL);
            }
        } else {
            uv_read_stop((uv_stream_t*)&conn->handle.pipe_handle);
            if (!uv_is_closing((uv_handle_t*)&conn->handle.pipe_handle)) {
                uv_close((uv_handle_t*)&conn->handle.pipe_handle, NULL);
            }
        }
        uvrpc_free(conn);
        conn = next;
    }
    transport->client_connections = NULL;

    /* Clean up UDP peer addresses */
    udp_peer_t* peer = transport->udp_peers;
    while (peer) {
        udp_peer_t* next = peer->next;
        uvrpc_free(peer);
        peer = next;
    }
    transport->udp_peers = NULL;

    if (transport->address) {
        uvrpc_free(transport->address);
    }

    /* Close async handle */
    if (!uv_is_closing((uv_handle_t*)&transport->async_handle)) {
        uv_close((uv_handle_t*)&transport->async_handle, NULL);
    }

    /* Close timeout timer - only if it was ever started */
    if (transport->timeout_enabled && !uv_is_closing((uv_handle_t*)&transport->timeout_timer)) {
        uv_close((uv_handle_t*)&transport->timeout_timer, NULL);
    }

    switch (transport->type) {
        case UVRPC_TRANSPORT_TCP:
            if (transport->is_server) {
                if (!uv_is_closing((uv_handle_t*)&transport->listen_handle)) {
                    uv_close((uv_handle_t*)&transport->listen_handle, NULL);
                }
            } else {
                if (!uv_is_closing((uv_handle_t*)&transport->tcp_handle)) {
                    uv_close((uv_handle_t*)&transport->tcp_handle, NULL);
                }
            }
            break;
        case UVRPC_TRANSPORT_UDP:
            if (!uv_is_closing((uv_handle_t*)&transport->udp_handle)) {
                uv_close((uv_handle_t*)&transport->udp_handle, NULL);
            }
            break;
        case UVRPC_TRANSPORT_IPC:
            if (transport->is_server) {
                if (!uv_is_closing((uv_handle_t*)&transport->listen_pipe)) {
                    uv_close((uv_handle_t*)&transport->listen_pipe, NULL);
                }
            } else {
                if (!uv_is_closing((uv_handle_t*)&transport->pipe_handle)) {
                    uv_close((uv_handle_t*)&transport->pipe_handle, NULL);
                }
            }
            break;
        case UVRPC_TRANSPORT_INPROC:
            /* Cleanup INPROC registry when server transport is freed */
            if (transport->is_server && loop) {
                /* INPROC doesn't need cleanup - global list is managed internally */
            }
            break;
    }

    /* Note: Close callbacks will be processed by the event loop owner */
    uvrpc_free(transport);
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
                    fprintf(stderr, "Failed to parse address: %s:%d\n", host, port);
                    return UVRPC_ERROR_INVALID_PARAM;
                }
            } else {
                fprintf(stderr, "Invalid address format: %s\n", addr_to_parse);
                return UVRPC_ERROR_INVALID_PARAM;
            }
            
            printf("[LISTEN] Binding TCP socket to %s:%d\n", host, port);
            fflush(stdout);
            int bind_err = uv_tcp_bind(&transport->listen_handle, (const struct sockaddr*)&addr, 0);
            if (bind_err != 0) {
                return UVRPC_ERROR;
            }
            printf("[LISTEN] Bind successful\n");
            fflush(stdout);
            
            int listen_err = uv_listen((uv_stream_t*)&transport->listen_handle, 128, tcp_connection_callback);
            if (listen_err != 0) {
                return UVRPC_ERROR;
            }
            printf("[LISTEN] Listen successful\n");
            fflush(stdout);
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
            /* INPROC listen - create endpoint with the actual address */
            /* Strip inproc:// prefix */
            const char* name = address;
            if (address && strncmp(address, "inproc://", 9) == 0) {
                name = address + 9;
            }
            
            if (!name || strlen(name) == 0) {
                return UVRPC_ERROR_INVALID_PARAM;
            }

            /* Find or create endpoint */
            inproc_endpoint_t* endpoint = inproc_find_endpoint(name);
            if (!endpoint) {
                endpoint = uvrpc_inproc_create_endpoint(name);
                if (!endpoint) {
                    return UVRPC_ERROR_NO_MEMORY;
                }
            }
            
            /* Set server transport */
            if (endpoint->server_transport) {
                return UVRPC_ERROR; /* Server already exists */
            }
            endpoint->server_transport = transport;

            transport->address = uvrpc_strdup(address);
            if (!transport->address) {
                return UVRPC_ERROR_NO_MEMORY;
            }
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
            
            int rv = uv_tcp_connect(&transport->connect_req, &transport->tcp_handle,
                                  (const struct sockaddr*)&addr, tcp_connect_callback);

    /* Start timeout timer if enabled */
    if (rv == 0 && transport->timeout_enabled && transport->timeout_ms > 0) {
        uv_timer_start(&transport->timeout_timer, timeout_callback, transport->timeout_ms, 0);
    }

    return rv;
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

            /* Add peer address to list */
            udp_peer_t* new_peer = calloc(1, sizeof(udp_peer_t));
            if (new_peer) {
                memcpy(&new_peer->addr, &addr, sizeof(addr));
                new_peer->next = transport->udp_peers;
                transport->udp_peers = new_peer;
            }

            transport->is_connected = 1;

            uv_udp_recv_start(&transport->udp_handle, alloc_callback, udp_recv_callback);

            /* Async callback through event loop */
            if (connect_cb) {
                transport->connect_cb = connect_cb;
                transport->ctx = ctx;
                uv_async_send(&transport->async_handle);
            }
            return UVRPC_OK;
        }
        case UVRPC_TRANSPORT_IPC: {
            /* Strip ipc:// prefix if present */
            const char* addr_to_parse = address;
            if (strncmp(address, "ipc://", 6) == 0) {
                addr_to_parse = address + 6;
            }
            
            uv_pipe_connect(&transport->connect_req, &transport->pipe_handle, addr_to_parse, ipc_connect_callback);

            /* Start timeout timer if enabled */
            if (transport->timeout_enabled && transport->timeout_ms > 0) {
                uv_timer_start(&transport->timeout_timer, timeout_callback, transport->timeout_ms, 0);
            }

            return UVRPC_OK;
        }
        case UVRPC_TRANSPORT_INPROC: {
            /* INPROC connect - find endpoint and register client */
            const char* name = address;
            if (address && strncmp(address, "inproc://", 9) == 0) {
                name = address + 9;
            }
            
            if (!name || strlen(name) == 0) {
                return UVRPC_ERROR_INVALID_PARAM;
            }

            transport->address = uvrpc_strdup(name);
            if (!transport->address) {
                return UVRPC_ERROR_NO_MEMORY;
            }

            inproc_endpoint_t* endpoint = inproc_find_endpoint(name);
            printf("[DEBUG INPROC] Looking for endpoint: '%s', found: %p\n", name, (void*)endpoint);
            fflush(stdout);
            if (!endpoint || !endpoint->server_transport) {
                printf("[DEBUG INPROC] Endpoint not found or no server transport\n");
                fflush(stdout);
                uvrpc_free(transport->address);
                transport->address = NULL;
                return UVRPC_ERROR_NOT_CONNECTED;
            }

            /* Copy callbacks to INPROC implementation */
            if (transport->impl) {
                uvrpc_inproc_transport_t* inproc_impl = (uvrpc_inproc_transport_t*)transport->impl;
                inproc_impl->recv_cb = transport->recv_cb;
                inproc_impl->connect_cb = transport->connect_cb;
                inproc_impl->error_cb = transport->error_cb;
                inproc_impl->ctx = transport->ctx;
                
                /* Add INPROC implementation to endpoint's client list */
                inproc_add_client(endpoint, inproc_impl);
            }
            
            transport->is_connected = 1;
            
            /* Call connect callback */
            if (transport->connect_cb) {
                transport->connect_cb(UVRPC_OK, transport->ctx);
            }
            
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
                break;
            case UVRPC_TRANSPORT_UDP:
                uv_udp_recv_stop(&transport->udp_handle);
                break;
            case UVRPC_TRANSPORT_IPC:
                uv_read_stop((uv_stream_t*)&transport->pipe_handle);
                break;
            case UVRPC_TRANSPORT_INPROC: {
                /* Remove client from endpoint list */
                if (transport->address) {
                    inproc_endpoint_t* endpoint = inproc_find_endpoint(transport->address);
                    if (endpoint) {

                        if (endpoint) {
                            /* Find and remove this client from the clients array */
                            int found = 0;
                            for (int i = 0; i < endpoint->client_count; i++) {
                                if (endpoint->clients[i] == transport) {
                                    found = 1;
                                    /* Shift remaining clients down */
                                    for (int j = i; j < endpoint->client_count - 1; j++) {
                                        endpoint->clients[j] = endpoint->clients[j + 1];
                                    }
                                    endpoint->client_count--;
                                    break;
                                }
                            }
                        }
                    }
                }
                break;
            }
        }
        transport->is_connected = 0;
    }
}

/* Send data (4-byte length prefix + data) - immediate send for backward compatibility */
void uvrpc_transport_send(uvrpc_transport_t* transport, const uint8_t* data, size_t size) {
    uvrpc_transport_send_with_flush(transport, data, size, 1);  /* flush=true for immediate send */
}

/* Send data with flush control (4-byte length prefix + data) */
void uvrpc_transport_send_with_flush(uvrpc_transport_t* transport, const uint8_t* data, size_t size, int flush) {
    if (!transport || !data || size == 0) return;

    /* Check connection status for TCP and IPC */
    if ((transport->type == UVRPC_TRANSPORT_TCP || transport->type == UVRPC_TRANSPORT_IPC) &&
        !transport->is_connected) {
        fprintf(stderr, "Cannot send: not connected\n");
        return;
    }

    /* For now, flush flag is ignored but provides future optimization point.
     * In high throughput mode, this could be used to batch multiple writes together.
     * For low latency mode, flush=true ensures immediate sending.
     */

    /* Allocate buffer with 4-byte length prefix */
    size_t total_size = 4 + size;
    uint8_t* buffer = uvrpc_alloc(total_size);
    if (!buffer) {
        fprintf(stderr, "Failed to allocate send buffer (%zu bytes)\n", total_size);
        return;
    }

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
            if (transport->is_server) {
                /* Server: send to all connected clients */
                client_connection_t* client = transport->client_connections;
                int client_count = 0;

                /* Count clients first */
                while (client) {
                    client_count++;
                    client = client->next;
                }

                if (client_count > 0) {
                    /* Create a separate buffer and write request for each client */
                    client = transport->client_connections;
                    while (client) {
                        uint8_t* client_buffer = uvrpc_alloc(total_size);
                        if (client_buffer) {
                            memcpy(client_buffer, buffer, total_size);

                            uv_write_t* req = uvrpc_alloc(sizeof(uv_write_t));
                            if (req) {
                                uv_buf_t client_buf = uv_buf_init((char*)client_buffer, total_size);
                                req->data = client_buffer;
                                uv_write(req, (uv_stream_t*)&client->handle.tcp_handle, &client_buf, 1, write_callback);
                            } else {
                                uvrpc_free(client_buffer);
                            }
                        }
                        client = client->next;
                    }
                    uvrpc_free(buffer); /* Free original buffer, we've made copies */
                } else {
                    uvrpc_free(buffer); /* No clients, just free buffer */
                }
            } else {
                /* Client: send to server */
                uv_write_t* req = uvrpc_alloc(sizeof(uv_write_t));
                if (!req) {
                    uvrpc_free(buffer);
                    return;
                }

                uv_buf_t buf = uv_buf_init((char*)buffer, total_size);
                req->data = buffer;
                uv_write(req, (uv_stream_t*)&transport->tcp_handle, &buf, 1, write_callback);
            }
            break;
        }

        case UVRPC_TRANSPORT_UDP: {
            uv_buf_t buf = uv_buf_init((char*)buffer, total_size);

            /* Send to all peer addresses */
            udp_peer_t* peer = transport->udp_peers;
            int peer_count = 0;
            while (peer && peer_count < UVRPC_MAX_UDP_PEERS) {
                peer_count++;
                peer = peer->next;
            }

            if (peer_count > 0 && peer_count <= UVRPC_MAX_UDP_PEERS) {
                /* For single peer, send directly */
                if (peer_count == 1) {
                    uv_udp_send_t* req = uvrpc_alloc(sizeof(uv_udp_send_t));
                    if (req) {
                        req->data = buffer;
                        uv_udp_send(req, &transport->udp_handle, &buf, 1,
                                    (struct sockaddr*)&transport->udp_peers->addr, udp_send_callback);
                    } else {
                        uvrpc_free(buffer);
                    }
                } else {
                    /* For multiple peers, send to each (create separate buffers) */
                    peer = transport->udp_peers;
                    while (peer) {
                        /* Create separate buffer for each peer */
                        uint8_t* peer_buffer = uvrpc_alloc(total_size);
                        if (peer_buffer) {
                            memcpy(peer_buffer, buffer, total_size);
                            uv_udp_send_t* req = uvrpc_alloc(sizeof(uv_udp_send_t));
                            if (req) {
                                req->data = peer_buffer;
                                uv_buf_t peer_buf = uv_buf_init((char*)peer_buffer, total_size);
                                uv_udp_send(req, &transport->udp_handle, &peer_buf, 1,
                                            (struct sockaddr*)&peer->addr, udp_send_callback);
                            } else {
                                uvrpc_free(peer_buffer);
                            }
                        }
                        peer = peer->next;
                    }
                    uvrpc_free(buffer);
                }
            } else {
                uvrpc_free(buffer);
            }
            break;
        }
        case UVRPC_TRANSPORT_IPC: {
            if (transport->is_server) {
                /* Server: send to all connected clients */
                client_connection_t* client = transport->client_connections;
                int client_count = 0;

                /* Count clients first */
                while (client) {
                    client_count++;
                    client = client->next;
                }

                if (client_count > 0) {
                    /* Create a separate buffer and write request for each client */
                    client = transport->client_connections;
                    while (client) {
                        uint8_t* client_buffer = uvrpc_alloc(total_size);
                        if (client_buffer) {
                            memcpy(client_buffer, buffer, total_size);

                            uv_write_t* req = uvrpc_alloc(sizeof(uv_write_t));
                            if (req) {
                                uv_buf_t client_buf = uv_buf_init((char*)client_buffer, total_size);
                                req->data = client_buffer;
                                uv_write(req, (uv_stream_t*)&client->handle.pipe_handle, &client_buf, 1, write_callback);
                            } else {
                                uvrpc_free(client_buffer);
                            }
                        }
                        client = client->next;
                    }
                    uvrpc_free(buffer); /* Free original buffer, we've made copies */
                } else {
                    uvrpc_free(buffer); /* No clients, just free buffer */
                }
            } else {
                /* Client: send to server */
                uv_write_t* req = uvrpc_alloc(sizeof(uv_write_t));
                if (!req) {
                    uvrpc_free(buffer);
                    return;
                }

                uv_buf_t buf = uv_buf_init((char*)buffer, total_size);
                req->data = buffer;
                uv_write(req, (uv_stream_t*)&transport->pipe_handle, &buf, 1, write_callback);
            }
            break;
        }
        case UVRPC_TRANSPORT_INPROC: {
                    inproc_endpoint_t* endpoint = NULL;
                    if (transport->address) {
                        endpoint = inproc_find_endpoint(transport->address);
                    }

            if (endpoint) {
                inproc_send_to_all(transport, endpoint, data, size);
            }

            uvrpc_free(buffer);
            break;
        }
    }
}

/* Set error callback */
void uvrpc_transport_set_error_callback(uvrpc_transport_t* transport, uvrpc_error_callback_t error_cb) {
    if (!transport) return;
    transport->error_cb = error_cb;
}

/* Set connection timeout */
void uvrpc_transport_set_timeout(uvrpc_transport_t* transport, uint64_t timeout_ms) {
    if (!transport) return;
    transport->timeout_ms = timeout_ms;
    transport->timeout_enabled = (timeout_ms > 0);
}