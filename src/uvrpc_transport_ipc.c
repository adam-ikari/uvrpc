/**
 * UVRPC IPC Transport Implementation
 * Unix Domain Socket transport for same-machine communication
 */

#include "uvrpc_transport_internal.h"
#include "../include/uvrpc.h"
#include "../include/uvrpc_allocator.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* IPC transport implementation */
typedef struct uvrpc_ipc_transport {
    uv_loop_t* loop;
    int is_server;

    /* Handles */
    uv_pipe_t handle;         /* Client connection handle */
    uv_pipe_t listen_pipe;    /* Server listening handle */
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
} uvrpc_ipc_transport_t;

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

/* Parse address string (strip ipc:// prefix) */
static const char* parse_ipc_address(const char* addr_str) {
    if (strncmp(addr_str, "ipc://", 6) == 0) {
        return addr_str + 6;
    }
    return addr_str;
}

/* Process complete frames */
static void process_frames(uvrpc_ipc_transport_t* ipc) {
    while (ipc->read_pos >= 4) {
        size_t frame_size = 0;
        int rv = parse_frame_length(ipc->read_buffer, ipc->read_pos, &frame_size);

        if (rv <= 0) break;  /* Incomplete or invalid frame */

        size_t total_size = 4 + frame_size;
        if (ipc->read_pos < total_size) break;  /* Not enough data */

        /* Extract frame data */
        uint8_t* frame_data = uvrpc_alloc(frame_size);
        if (frame_data) {
            memcpy(frame_data, ipc->read_buffer + 4, frame_size);

            if (ipc->recv_cb) {
                ipc->recv_cb(frame_data, frame_size, ipc->ctx);
            }

            uvrpc_free(frame_data);
        }

        /* Remove processed frame */
        memmove(ipc->read_buffer, ipc->read_buffer + total_size,
                ipc->read_pos - total_size);
        ipc->read_pos -= total_size;
    }
}

/* Alloc callback */
static void alloc_callback(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    uvrpc_ipc_transport_t* ipc = (uvrpc_ipc_transport_t*)handle->data;
    buf->base = (char*)(ipc->read_buffer + ipc->read_pos);
    buf->len = sizeof(ipc->read_buffer) - ipc->read_pos;
}

/* Read callback (client) */
static void read_callback(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    uvrpc_ipc_transport_t* ipc = (uvrpc_ipc_transport_t*)stream->data;

    if (nread < 0) {
        if (nread != UV_EOF && ipc->error_cb) {
            ipc->error_cb(nread, uv_strerror(nread), ipc->ctx);
        }
        uv_read_stop(stream);
        return;
    }

    if (nread == 0) return;

    ipc->read_pos += nread;
    process_frames(ipc);
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
            fprintf(stderr, "IPC client connection error: %s\n", uv_strerror(nread));
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
        fprintf(stderr, "IPC write error: %s\n", uv_strerror(status));
    }
    uvrpc_free(req->data);
    uvrpc_free(req);
}

/* Connection callback (server) */
static void connection_callback(uv_stream_t* server, int status) {
    if (status < 0) {
        uvrpc_ipc_transport_t* ipc = (uvrpc_ipc_transport_t*)server->data;
        if (ipc->error_cb) {
            ipc->error_cb(status, uv_strerror(status), ipc->ctx);
        }
        return;
    }

    uvrpc_ipc_transport_t* ipc = (uvrpc_ipc_transport_t*)server->data;

    /* Accept connection */
    client_connection_t* client_conn = uvrpc_calloc(1, sizeof(client_connection_t));
    if (!client_conn) {
        fprintf(stderr, "Failed to allocate IPC client connection\n");
        return;
    }

    client_conn->is_tcp = 0;
    uv_pipe_init(ipc->loop, &client_conn->handle.pipe_handle, 0);
    client_conn->handle.pipe_handle.data = client_conn;
    client_conn->recv_cb = ipc->recv_cb;
    client_conn->recv_ctx = ipc->ctx;
    client_conn->server = ipc->ctx;
    client_conn->read_pos = 0;

    if (uv_accept(server, (uv_stream_t*)&client_conn->handle.pipe_handle) == 0) {
        uv_read_start((uv_stream_t*)&client_conn->handle.pipe_handle,
                      client_alloc_callback, client_read_callback);

        /* Add to client list */
        client_conn->next = ipc->client_connections;
        ipc->client_connections = client_conn;
    } else {
        uv_close((uv_handle_t*)&client_conn->handle.pipe_handle, NULL);
        uvrpc_free(client_conn);
    }
}

/* Connect callback (client) */
static void connect_callback(uv_connect_t* req, int status) {
    uvrpc_ipc_transport_t* ipc = (uvrpc_ipc_transport_t*)req->handle->data;

    /* Stop timeout timer */
    if (ipc->timeout_enabled) {
        uv_timer_stop(&ipc->timeout_timer);
    }

    if (status < 0) {
        if (ipc->error_cb) {
            ipc->error_cb(status, uv_strerror(status), ipc->ctx);
        }
        if (ipc->connect_cb) {
            uvrpc_connect_callback_t cb = ipc->connect_cb;
            ipc->connect_cb = NULL;
            cb(status, ipc->ctx);
        }
        return;
    }

    ipc->is_connected = 1;
    uv_read_start((uv_stream_t*)&ipc->handle, alloc_callback, read_callback);

    if (ipc->connect_cb) {
        uvrpc_connect_callback_t cb = ipc->connect_cb;
        ipc->connect_cb = NULL;
        cb(0, ipc->ctx);
    }
}

/* Timeout callback */
static void timeout_callback(uv_timer_t* handle) {
    uvrpc_ipc_transport_t* ipc = (uvrpc_ipc_transport_t*)handle->data;

    uv_timer_stop(&ipc->timeout_timer);

    if (ipc->is_connected) {
        /* Disconnect */
        uv_read_stop((uv_stream_t*)&ipc->handle);
        ipc->is_connected = 0;
    }

    if (ipc->error_cb) {
        ipc->error_cb(UVRPC_ERROR_TIMEOUT, "Connection timeout", ipc->ctx);
    }

    if (ipc->connect_cb) {
        ipc->connect_cb(UVRPC_ERROR_TIMEOUT, ipc->ctx);
        ipc->connect_cb = NULL;
    }
}

/* Async callback */
static void async_callback(uv_async_t* handle) {
    uvrpc_ipc_transport_t* ipc = (uvrpc_ipc_transport_t*)handle->data;
    if (ipc->connect_cb) {
        ipc->connect_cb(0, ipc->ctx);
        ipc->connect_cb = NULL;
    }
}

/* Listen implementation */
static int ipc_listen(void* impl, const char* address,
                      uvrpc_recv_callback_t recv_cb, void* ctx) {
    uvrpc_ipc_transport_t* ipc = (uvrpc_ipc_transport_t*)impl;

    ipc->recv_cb = recv_cb;
    ipc->ctx = ctx;

    const char* addr_to_parse = parse_ipc_address(address);

    printf("[IPC] Binding to %s\n", addr_to_parse);

    if (uv_pipe_bind(&ipc->listen_pipe, addr_to_parse) != 0) {
        return UVRPC_ERROR;
    }

    if (uv_listen((uv_stream_t*)&ipc->listen_pipe, 128, connection_callback) != 0) {
        return UVRPC_ERROR;
    }

    printf("[IPC] Listening on %s\n", address);
    return UVRPC_OK;
}

/* Connect implementation */
static int ipc_connect(void* impl, const char* address,
                       uvrpc_connect_callback_t connect_cb,
                       uvrpc_recv_callback_t recv_cb, void* ctx) {
    uvrpc_ipc_transport_t* ipc = (uvrpc_ipc_transport_t*)impl;

    ipc->connect_cb = connect_cb;
    ipc->recv_cb = recv_cb;
    ipc->ctx = ctx;

    const char* addr_to_parse = parse_ipc_address(address);

    uv_pipe_connect(&ipc->connect_req, &ipc->handle, addr_to_parse, connect_callback);

    /* Start timeout timer if enabled */
    if (ipc->timeout_enabled && ipc->timeout_ms > 0) {
        uv_timer_start(&ipc->timeout_timer, timeout_callback, ipc->timeout_ms, 0);
    }

    return UVRPC_OK;
}

/* Disconnect implementation */
static void ipc_disconnect(void* impl) {
    uvrpc_ipc_transport_t* ipc = (uvrpc_ipc_transport_t*)impl;

    if (ipc->is_connected) {
        uv_read_stop((uv_stream_t*)&ipc->handle);
        ipc->is_connected = 0;
    }
}

/* Send implementation (client only) */
static void ipc_send(void* impl, const uint8_t* data, size_t size) {
    uvrpc_ipc_transport_t* ipc = (uvrpc_ipc_transport_t*)impl;

    if (!ipc->is_connected) {
        fprintf(stderr, "IPC: Cannot send, not connected\n");
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
    uv_write(req, (uv_stream_t*)&ipc->handle, &buf, 1, write_callback);
}

/* Send to specific client (server only) */
static void ipc_send_to(void* impl, const uint8_t* data, size_t size, void* target) {
    uvrpc_ipc_transport_t* ipc = (uvrpc_ipc_transport_t*)impl;
    uv_stream_t* client_stream = (uv_stream_t*)target;

    if (!client_stream) {
        fprintf(stderr, "IPC: Cannot send, no target\n");
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
static void ipc_free(void* impl) {
    uvrpc_ipc_transport_t* ipc = (uvrpc_ipc_transport_t*)impl;

    if (!ipc) return;

    /* Clean up client connections */
    client_connection_t* conn = ipc->client_connections;
    while (conn) {
        client_connection_t* next = conn->next;
        uv_read_stop((uv_stream_t*)&conn->handle.pipe_handle);
        if (!uv_is_closing((uv_handle_t*)&conn->handle.pipe_handle)) {
            uv_close((uv_handle_t*)&conn->handle.pipe_handle, NULL);
        }
        uvrpc_free(conn);
        conn = next;
    }

    /* Close handles */
    if (ipc->is_server) {
        if (!uv_is_closing((uv_handle_t*)&ipc->listen_pipe)) {
            uv_close((uv_handle_t*)&ipc->listen_pipe, NULL);
        }
    } else {
        if (!uv_is_closing((uv_handle_t*)&ipc->handle)) {
            uv_close((uv_handle_t*)&ipc->handle, NULL);
        }
    }

    /* Close timeout timer */
    if (ipc->timeout_enabled && !uv_is_closing((uv_handle_t*)&ipc->timeout_timer)) {
        uv_close((uv_handle_t*)&ipc->timeout_timer, NULL);
    }

    uvrpc_free(ipc);
}

/* Set timeout implementation */
static int ipc_set_timeout(void* impl, uint64_t timeout_ms) {
    uvrpc_ipc_transport_t* ipc = (uvrpc_ipc_transport_t*)impl;
    ipc->timeout_ms = timeout_ms;
    ipc->timeout_enabled = (timeout_ms > 0);
    return UVRPC_OK;
}

/* Virtual function table */
static const uvrpc_transport_vtable_t ipc_vtable = {
    .listen = ipc_listen,
    .connect = ipc_connect,
    .disconnect = ipc_disconnect,
    .send = ipc_send,
    .send_to = ipc_send_to,
    .free = ipc_free,
    .set_timeout = ipc_set_timeout
};

/* Create IPC transport */
uvrpc_transport_t* uvrpc_transport_ipc_new(uv_loop_t* loop, int is_server) {
    uvrpc_ipc_transport_t* ipc = uvrpc_calloc(1, sizeof(uvrpc_ipc_transport_t));
    if (!ipc) return NULL;

    ipc->loop = loop;
    ipc->is_server = is_server;
    ipc->is_connected = 0;
    ipc->read_pos = 0;
    ipc->client_connections = NULL;
    ipc->timeout_ms = 0;
    ipc->timeout_enabled = 0;

    /* Initialize async handle */
    uv_async_init(loop, (uv_async_t*)&ipc->handle, async_callback);
    ipc->handle.data = ipc;

    /* Initialize timeout timer */
    uv_timer_init(loop, &ipc->timeout_timer);
    ipc->timeout_timer.data = ipc;

    /* Initialize handles */
    if (is_server) {
        uv_pipe_init(loop, &ipc->listen_pipe, 0);
        ipc->listen_pipe.data = ipc;
    } else {
        uv_pipe_init(loop, &ipc->handle, 0);
        ipc->handle.data = ipc;
    }

    /* Create transport wrapper */
    uvrpc_transport_t* transport = uvrpc_calloc(1, sizeof(uvrpc_transport_t));
    if (!transport) {
        ipc_free(ipc);
        return NULL;
    }

    transport->loop = loop;
    transport->type = UVRPC_TRANSPORT_IPC;
    transport->impl = ipc;
    transport->vtable = &ipc_vtable;
    transport->is_server = is_server;

    return transport;
}