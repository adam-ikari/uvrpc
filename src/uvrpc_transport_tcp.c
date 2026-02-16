/**
 * UVRPC TCP Transport Implementation
 * High-performance TCP transport with connection pooling
 */

#include "uvrpc_transport_internal.h"
#include "../include/uvrpc.h"
#include "../include/uvrpc_allocator.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* TCP transport implementation */
typedef struct uvrpc_tcp_transport {
    uv_loop_t* loop;
    int is_server;

    /* Handles */
    uv_tcp_t handle;         /* Client connection handle */
    uv_tcp_t listen_handle;  /* Server listening handle */
    uv_connect_t connect_req;

    /* Client connections (server only) */
    client_connection_t* client_connections;

    /* Read buffer */
    uint8_t read_buffer[8192];
    size_t read_pos;

    /* Callbacks */
    uvrpc_recv_callback_t recv_cb;
    uvrpc_connect_callback_t connect_cb;
    uvrpc_error_callback_t error_cb;
    void* ctx;

    /* Timeout */
    uv_timer_t timeout_timer;
    uint64_t timeout_ms;
    int timeout_enabled;

    /* Status */
    int is_connected;
} uvrpc_tcp_transport_t;

/* Forward declarations */
static void alloc_callback(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);
static void read_callback(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf);
static void write_callback(uv_write_t* req, int status);
static void connect_callback(uv_connect_t* req, int status);
static void connection_callback(uv_stream_t* server, int status);
static void timeout_callback(uv_timer_t* handle);
static void client_alloc_callback(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);
static void client_read_callback(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf);
static void async_callback(uv_async_t* handle);

/* Parse address string */
static int parse_tcp_address(const char* addr_str, struct sockaddr_storage* addr) {
    char host[256];
    int port = 5555;

    const char* addr_to_parse = addr_str;
    if (strncmp(addr_str, "tcp://", 6) == 0) {
        addr_to_parse = addr_str + 6;
    }

    if (sscanf(addr_to_parse, "%255[^:]:%d", host, &port) == 2) {
        if (uv_ip4_addr(host, port, (struct sockaddr_in*)addr) == 0 ||
            uv_ip6_addr(host, port, (struct sockaddr_in6*)addr) == 0) {
            return 0;
        }
    }

    return -1;
}

/* Process complete frames */
static void process_frames(uvrpc_tcp_transport_t* tcp) {
    while (tcp->read_pos >= 4) {
        size_t frame_size = 0;
        int rv = parse_frame_length(tcp->read_buffer, tcp->read_pos, &frame_size);

        if (rv <= 0) break;  /* Incomplete or invalid frame */

        size_t total_size = 4 + frame_size;
        if (tcp->read_pos < total_size) break;  /* Not enough data */

        /* Extract frame data */
        uint8_t* frame_data = uvrpc_alloc(frame_size);
        if (frame_data) {
            memcpy(frame_data, tcp->read_buffer + 4, frame_size);

            if (tcp->recv_cb) {
                tcp->recv_cb(frame_data, frame_size, tcp->ctx);
            }

            uvrpc_free(frame_data);
        }

        /* Remove processed frame */
        memmove(tcp->read_buffer, tcp->read_buffer + total_size,
                tcp->read_pos - total_size);
        tcp->read_pos -= total_size;
    }
}

/* Alloc callback */
static void alloc_callback(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    uvrpc_tcp_transport_t* tcp = (uvrpc_tcp_transport_t*)handle->data;
    buf->base = (char*)(tcp->read_buffer + tcp->read_pos);
    buf->len = sizeof(tcp->read_buffer) - tcp->read_pos;
}

/* Read callback (client) */
static void read_callback(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    uvrpc_tcp_transport_t* tcp = (uvrpc_tcp_transport_t*)stream->data;

    if (nread < 0) {
        if (nread != UV_EOF && tcp->error_cb) {
            tcp->error_cb(nread, uv_strerror(nread), tcp->ctx);
        }
        uv_read_stop(stream);
        return;
    }

    if (nread == 0) return;

    tcp->read_pos += nread;
    process_frames(tcp);
}

/* Client alloc callback */
static void client_alloc_callback(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    client_connection_t* conn = (client_connection_t*)handle->data;
    buf->base = (char*)(conn->read_buffer + conn->read_pos);
    buf->len = sizeof(conn->read_buffer) - conn->read_pos;
}

/* Client read callback */
static void client_read_callback(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    client_connection_t* conn = (client_connection_t*)stream->data;

    if (nread < 0) {
        if (nread != UV_EOF) {
            fprintf(stderr, "Client connection error: %s\n", uv_strerror(nread));
        }
        uv_read_stop(stream);
        return;
    }

    if (nread == 0) return;

    conn->read_pos += nread;

    /* Process frames */
    while (conn->read_pos >= 4) {
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
        if (conn->read_pos < total_size) break;

        /* Call receive callback */
        if (conn->recv_cb) {
            uint8_t* frame_data = uvrpc_alloc(frame_size);
            if (frame_data) {
                memcpy(frame_data, conn->read_buffer + 4, frame_size);
                conn->recv_cb(frame_data, frame_size, stream);
                uvrpc_free(frame_data);
            }
        }

        /* Remove processed frame */
        memmove(conn->read_buffer, conn->read_buffer + total_size,
                conn->read_pos - total_size);
        conn->read_pos -= total_size;
    }
}

/* Write callback */
static void write_callback(uv_write_t* req, int status) {
    if (status < 0) {
        fprintf(stderr, "Write error: %s\n", uv_strerror(status));
    }
    uvrpc_free(req->data);
    uvrpc_free(req);
}

/* Connection callback (server) */
static void connection_callback(uv_stream_t* server, int status) {
    if (status < 0) {
        uvrpc_tcp_transport_t* tcp = (uvrpc_tcp_transport_t*)server->data;
        if (tcp->error_cb) {
            tcp->error_cb(status, uv_strerror(status), tcp->ctx);
        }
        return;
    }

    uvrpc_tcp_transport_t* tcp = (uvrpc_tcp_transport_t*)server->data;

    /* Accept connection */
    client_connection_t* client_conn = uvrpc_calloc(1, sizeof(client_connection_t));
    if (!client_conn) {
        fprintf(stderr, "Failed to allocate client connection\n");
        return;
    }

    client_conn->is_tcp = 1;
    uv_tcp_init(tcp->loop, &client_conn->handle.tcp_handle);
    client_conn->handle.tcp_handle.data = client_conn;
    client_conn->recv_cb = tcp->recv_cb;
    client_conn->recv_ctx = tcp->ctx;
    client_conn->server = tcp->ctx;
    client_conn->read_pos = 0;

    if (uv_accept(server, (uv_stream_t*)&client_conn->handle.tcp_handle) == 0) {
        uv_read_start((uv_stream_t*)&client_conn->handle.tcp_handle,
                      client_alloc_callback, client_read_callback);

        /* Add to client list */
        client_conn->next = tcp->client_connections;
        tcp->client_connections = client_conn;
    } else {
        uv_close((uv_handle_t*)&client_conn->handle.tcp_handle, NULL);
        uvrpc_free(client_conn);
    }
}

/* Connect callback (client) */
static void connect_callback(uv_connect_t* req, int status) {
    uvrpc_tcp_transport_t* tcp = (uvrpc_tcp_transport_t*)req->handle->data;

    /* Stop timeout timer */
    if (tcp->timeout_enabled) {
        uv_timer_stop(&tcp->timeout_timer);
    }

    if (status < 0) {
        if (tcp->error_cb) {
            tcp->error_cb(status, uv_strerror(status), tcp->ctx);
        }
        if (tcp->connect_cb) {
            tcp->connect_cb(status, tcp->ctx);
        }
        return;
    }

    tcp->is_connected = 1;
    uv_read_start((uv_stream_t*)&tcp->handle, alloc_callback, read_callback);

    if (tcp->connect_cb) {
        uvrpc_connect_callback_t cb = tcp->connect_cb;
        tcp->connect_cb = NULL;
        cb(0, tcp->ctx);
    }
}

/* Timeout callback */
static void timeout_callback(uv_timer_t* handle) {
    uvrpc_tcp_transport_t* tcp = (uvrpc_tcp_transport_t*)handle->data;

    uv_timer_stop(&tcp->timeout_timer);

    if (tcp->is_connected) {
        /* Disconnect */
        uv_read_stop((uv_stream_t*)&tcp->handle);
        tcp->is_connected = 0;
    }

    if (tcp->error_cb) {
        tcp->error_cb(UVRPC_ERROR_TIMEOUT, "Connection timeout", tcp->ctx);
    }

    if (tcp->connect_cb) {
        tcp->connect_cb(UVRPC_ERROR_TIMEOUT, tcp->ctx);
        tcp->connect_cb = NULL;
    }
}

/* Async callback */
static void async_callback(uv_async_t* handle) {
    uvrpc_tcp_transport_t* tcp = (uvrpc_tcp_transport_t*)handle->data;
    if (tcp->connect_cb) {
        tcp->connect_cb(0, tcp->ctx);
        tcp->connect_cb = NULL;
    }
}

/* Listen implementation */
static int tcp_listen(void* impl, const char* address,
                      uvrpc_recv_callback_t recv_cb, void* ctx) {
    uvrpc_tcp_transport_t* tcp = (uvrpc_tcp_transport_t*)impl;

    tcp->recv_cb = recv_cb;
    tcp->ctx = ctx;

    struct sockaddr_storage addr;
    if (parse_tcp_address(address, &addr) != 0) {
        return UVRPC_ERROR_INVALID_PARAM;
    }

    printf("[TCP] Binding to %s\n", address);

    if (uv_tcp_bind(&tcp->listen_handle, (const struct sockaddr*)&addr, 0) != 0) {
        return UVRPC_ERROR;
    }

    if (uv_listen((uv_stream_t*)&tcp->listen_handle, 128, connection_callback) != 0) {
        return UVRPC_ERROR;
    }

    printf("[TCP] Listening on %s\n", address);
    return UVRPC_OK;
}

/* Connect implementation */
static int tcp_connect(void* impl, const char* address,
                       uvrpc_connect_callback_t connect_cb,
                       uvrpc_recv_callback_t recv_cb, void* ctx) {
    uvrpc_tcp_transport_t* tcp = (uvrpc_tcp_transport_t*)impl;

    tcp->connect_cb = connect_cb;
    tcp->recv_cb = recv_cb;
    tcp->ctx = ctx;

    struct sockaddr_storage addr;
    if (parse_tcp_address(address, &addr) != 0) {
        return UVRPC_ERROR_INVALID_PARAM;
    }

    int rv = uv_tcp_connect(&tcp->connect_req, &tcp->handle,
                          (const struct sockaddr*)&addr, connect_callback);

    /* Start timeout timer if enabled */
    if (rv == 0 && tcp->timeout_enabled && tcp->timeout_ms > 0) {
        uv_timer_start(&tcp->timeout_timer, timeout_callback, tcp->timeout_ms, 0);
    }

    return rv;
}

/* Disconnect implementation */
static void tcp_disconnect(void* impl) {
    uvrpc_tcp_transport_t* tcp = (uvrpc_tcp_transport_t*)impl;

    if (tcp->is_connected) {
        uv_read_stop((uv_stream_t*)&tcp->handle);
        tcp->is_connected = 0;
    }
}

/* Send implementation (client only) */
static void tcp_send(void* impl, const uint8_t* data, size_t size) {
    uvrpc_tcp_transport_t* tcp = (uvrpc_tcp_transport_t*)impl;

    if (!tcp->is_connected) {
        fprintf(stderr, "TCP: Cannot send, not connected\n");
        return;
    }

    size_t total_size;
    uint8_t* buffer = create_length_prefixed_buffer(data, size, &total_size);
    if (!buffer) return;

    uv_write_t* req = uvrpc_alloc(sizeof(uv_write_t));
    if (!req) {
        uvrpc_free(buffer);
        return;
    }

    uv_buf_t buf = uv_buf_init((char*)buffer, total_size);
    req->data = buffer;
    uv_write(req, (uv_stream_t*)&tcp->handle, &buf, 1, write_callback);
}

/* Send to specific client (server only) */
static void tcp_send_to(void* impl, const uint8_t* data, size_t size, void* target) {
    uvrpc_tcp_transport_t* tcp = (uvrpc_tcp_transport_t*)impl;
    uv_stream_t* client_stream = (uv_stream_t*)target;

    if (!client_stream) {
        fprintf(stderr, "TCP: Cannot send, no target\n");
        return;
    }

    size_t total_size;
    uint8_t* buffer = create_length_prefixed_buffer(data, size, &total_size);
    if (!buffer) return;

    uv_write_t* req = uvrpc_alloc(sizeof(uv_write_t));
    if (!req) {
        uvrpc_free(buffer);
        return;
    }

    uv_buf_t buf = uv_buf_init((char*)buffer, total_size);
    req->data = buffer;
    uv_write(req, client_stream, &buf, 1, write_callback);
}

/* Free implementation */
static void tcp_free(void* impl) {
    uvrpc_tcp_transport_t* tcp = (uvrpc_tcp_transport_t*)impl;

    if (!tcp) return;

    /* Clean up client connections */
    client_connection_t* conn = tcp->client_connections;
    while (conn) {
        client_connection_t* next = conn->next;
        uv_read_stop((uv_stream_t*)&conn->handle.tcp_handle);
        if (!uv_is_closing((uv_handle_t*)&conn->handle.tcp_handle)) {
            uv_close((uv_handle_t*)&conn->handle.tcp_handle, NULL);
        }
        uvrpc_free(conn);
        conn = next;
    }

    /* Close handles */
    if (tcp->is_server) {
        if (!uv_is_closing((uv_handle_t*)&tcp->listen_handle)) {
            uv_close((uv_handle_t*)&tcp->listen_handle, NULL);
        }
    } else {
        if (!uv_is_closing((uv_handle_t*)&tcp->handle)) {
            uv_close((uv_handle_t*)&tcp->handle, NULL);
        }
    }

    /* Close timeout timer */
    if (tcp->timeout_enabled && !uv_is_closing((uv_handle_t*)&tcp->timeout_timer)) {
        uv_close((uv_handle_t*)&tcp->timeout_timer, NULL);
    }

    uvrpc_free(tcp);
}

/* Set timeout implementation */
static int tcp_set_timeout(void* impl, uint64_t timeout_ms) {
    uvrpc_tcp_transport_t* tcp = (uvrpc_tcp_transport_t*)impl;
    tcp->timeout_ms = timeout_ms;
    tcp->timeout_enabled = (timeout_ms > 0);
    return UVRPC_OK;
}

/* Virtual function table */
static const uvrpc_transport_vtable_t tcp_vtable = {
    .listen = tcp_listen,
    .connect = tcp_connect,
    .disconnect = tcp_disconnect,
    .send = tcp_send,
    .send_to = tcp_send_to,
    .free = tcp_free,
    .set_timeout = tcp_set_timeout
};

/* Create TCP transport */
uvrpc_transport_t* uvrpc_transport_tcp_new(uv_loop_t* loop, int is_server) {
    uvrpc_tcp_transport_t* tcp = uvrpc_calloc(1, sizeof(uvrpc_tcp_transport_t));
    if (!tcp) return NULL;

    tcp->loop = loop;
    tcp->is_server = is_server;
    tcp->is_connected = 0;
    tcp->read_pos = 0;
    tcp->client_connections = NULL;
    tcp->timeout_ms = 0;
    tcp->timeout_enabled = 0;

    /* Initialize async handle */
    uv_async_init(loop, (uv_async_t*)&tcp->handle, async_callback);
    tcp->handle.data = tcp;

    /* Initialize timeout timer */
    uv_timer_init(loop, &tcp->timeout_timer);
    tcp->timeout_timer.data = tcp;

    /* Initialize handles */
    if (is_server) {
        uv_tcp_init(loop, &tcp->listen_handle);
        tcp->listen_handle.data = tcp;
    } else {
        uv_tcp_init(loop, &tcp->handle);
        tcp->handle.data = tcp;
    }

    /* Create transport wrapper */
    uvrpc_transport_t* transport = uvrpc_calloc(1, sizeof(uvrpc_transport_t));
    if (!transport) {
        tcp_free(tcp);
        return NULL;
    }

    transport->loop = loop;
    transport->type = UVRPC_TRANSPORT_TCP;
    transport->impl = tcp;
    transport->vtable = &tcp_vtable;
    transport->is_server = is_server;

    return transport;
}