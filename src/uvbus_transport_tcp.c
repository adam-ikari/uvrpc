/**
 * UVBus TCP Transport Implementation
 */

#include "../include/uvbus.h"
#include "../include/uvbus_config.h"
#include "../include/uvrpc_allocator.h"
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <netdb.h>

/* UVBUS_LOG / UVBUS_LOG_DEBUG / UVBUS_LOG_ERROR are provided by uvbus.h */

typedef struct uvbus_tcp_client uvbus_tcp_client_t;
typedef struct uvbus_tcp_server uvbus_tcp_server_t;

/* Forward declarations */
static void on_client_close(uv_handle_t* handle);
static void on_server_close(uv_handle_t* handle);

/* TCP client structure - optimized for cache locality */
struct uvbus_tcp_client {
    /* Frequently accessed fields - grouped together */
    int is_connected;
    int ref_count;  /* Reference counting for async cleanup */
    size_t read_pos;
    
    /* Pointer fields */
    char* host;
    void* parent_transport;
    void* client_connection;
    
    /* Integer fields */
    int port;
    
    /* LibUV handles - kept together at end */
    uv_tcp_t tcp_handle;
    uv_connect_t connect_req;
    uv_write_t write_req;
    
    /* Large buffer - placed at end to improve cache locality for small fields */
    uint8_t read_buffer[262144];  /* 256KB read buffer */
};

/* TCP server structure - optimized for cache locality */
struct uvbus_tcp_server {
    /* Frequently accessed fields - grouped together */
    int is_listening;
    int ref_count;  /* Reference counting for async cleanup */
    int client_count;
    int client_capacity;
    
    /* Pointer fields */
    char* host;
    uvbus_tcp_client_t** clients;
    void* parent_transport;
    
    /* Integer fields */
    int port;
    
    /* LibUV handle - kept at end */
    uv_tcp_t listen_handle;
};

/* Reference counting helpers */
static void ref_init(int* ref_count) {
    *ref_count = 1;
}

static int ref_inc(int* ref_count) {
    return ++(*ref_count);
}

static int ref_dec(int* ref_count) {
    return --(*ref_count);
}

/* Parse address */
static int parse_tcp_address(const char* address, char** host, int* port) {
    if (!address || !host || !port) {
        return -1;
    }
    
    /* Skip protocol prefix */
    const char* addr_start = address;
    if (strncmp(address, "tcp://", 6) == 0) {
        addr_start += 6;
    }
    
    /* Find port separator */
    const char* colon = strrchr(addr_start, ':');
    if (!colon) {
        return -1;
    }
    
    /* Extract host */
    size_t host_len = colon - addr_start;
    *host = (char*)uvrpc_alloc(host_len + 1);
    if (!*host) {
        return -1;
    }
    memcpy(*host, addr_start, host_len);
    (*host)[host_len] = '\0';
    
    /* Extract port */
    *port = atoi(colon + 1);
    
    return 0;
}

/* Client read callback */
static void on_client_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    if (!stream) return;

    UVBUS_LOG("on_client_read called, nread=%zd", nread);

    uvbus_tcp_client_t* client = (uvbus_tcp_client_t*)stream->data;
    if (!client) {
        UVBUS_LOG("on_client_read: client is NULL");
        uvrpc_free(buf->base);
        return;
    }

    /* Check if client is being closed */
    if (client->ref_count == 0) {
        UVBUS_LOG("on_client_read: client ref_count is 0");
        uvrpc_free(buf->base);
        return;
    }

    uvbus_transport_t* transport = (uvbus_transport_t*)client->parent_transport;
    if (!transport) {
        UVBUS_LOG("on_client_read: transport is NULL");
        uvrpc_free(buf->base);
        return;
    }

    if (nread < 0) {
        UVBUS_LOG("on_client_read: error nread=%zd (%s)", nread, uv_strerror(nread));
        if (nread != UV_EOF) {
            if (transport->error_cb) {
                transport->error_cb(UVBUS_ERROR_IO, uv_strerror(nread), transport->callback_ctx);
            }
        }
        uvrpc_free(buf->base);
        return;
    }

    if (nread > 0) {
        UVBUS_LOG("on_client_read: received %zd bytes, read_pos=%zu", nread, client->read_pos);
        
        /* Add data to client's read buffer with strict bounds checking */
        if (nread > 0 && client->read_pos < sizeof(client->read_buffer) &&
            (size_t)nread <= sizeof(client->read_buffer) - client->read_pos) {
            memcpy(client->read_buffer + client->read_pos, buf->base, nread);
            client->read_pos += nread;
        } else {
            /* Buffer overflow detected - close connection to prevent attacks */
            UVBUS_LOG_ERROR("Buffer overflow detected: read_pos=%zu, nread=%zd, buffer_size=%zu",  /* cppcheck-suppress invalidPrintfArgType_sint */
                    client->read_pos, nread, sizeof(client->read_buffer));
            uvrpc_free(buf->base);
            /* Close the connection instead of just resetting */
            if (!uv_is_closing((uv_handle_t*)&client->tcp_handle)) {
                uv_close((uv_handle_t*)&client->tcp_handle, on_client_close);
            }
            return;
        }

        /* Process complete frames */
        while (client->read_pos >= 4) {
            /* Parse frame length (big-endian) */
            uint32_t frame_size = (uint32_t)client->read_buffer[0] << 24 |
                                  (uint32_t)client->read_buffer[1] << 16 |
                                  (uint32_t)client->read_buffer[2] << 8 |
                                  (uint32_t)client->read_buffer[3];

            /* Validate frame size - stricter limit for stability */
            if (frame_size == 0 || frame_size > UVBUS_DEFAULT_MAX_FRAME_SIZE) {
                /* Invalid frame size, reset buffer */
                UVBUS_LOG_ERROR("Invalid frame size (%u), resetting buffer", frame_size);
                client->read_pos = 0;
                break;
            }

            size_t total_size = 4 + frame_size;
            if (client->read_pos < total_size) {
                /* Not enough data yet */
                break;
            }

            /* Extract frame data (skip 4-byte length prefix) */
            if (transport->recv_cb) {
                /* Copy frame data to heap so callback can safely access it */
                uint8_t* frame_copy = (uint8_t*)uvrpc_alloc(frame_size);
                if (!frame_copy) {
                    UVBUS_LOG_ERROR("Failed to allocate %u bytes for frame", frame_size);
                    if (transport->error_cb) {
                        transport->error_cb(UVBUS_ERROR_NO_MEMORY, "Frame allocation failed", transport->callback_ctx);
                    }
                    client->read_pos = 0;
                    break;
                }
                memcpy(frame_copy, client->read_buffer + 4, frame_size);

                /* Determine if this is server mode or client mode */
                if (transport->is_server) {
                    /* Server mode: pass client context and server context */
                    transport->recv_cb(frame_copy, frame_size, client, transport->recv_ctx);
                } else {
                    /* Client mode: pass recv context */
                    transport->recv_cb(frame_copy, frame_size, transport->recv_ctx, transport->recv_ctx);
                }

                /* Always free the frame copy after callback returns
                 * This ensures cleanup even if callback forgets to free it */
                uvrpc_free(frame_copy);
            }

            /* Remove processed frame from buffer */
            size_t remaining = client->read_pos - total_size;
            if (remaining > 0 && remaining < sizeof(client->read_buffer)) {
                memmove(client->read_buffer, client->read_buffer + total_size, remaining);
            }
            client->read_pos = remaining;
        }
    }

    uvrpc_free(buf->base);
}


/* Client alloc callback */
static void on_client_alloc(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    (void)handle;  /* Buffer allocation is independent of the specific handle */
    /* Allocate buffer based on suggested size */
    size_t alloc_size = suggested_size;
    if (alloc_size < 65536) {
        alloc_size = 65536;  /* 64KB minimum for high concurrency */
    }
    buf->base = (char*)uvrpc_alloc(alloc_size);
    buf->len = alloc_size;
}

/* Server connection callback */
static void on_server_connection(uv_stream_t* server, int status) {
    if (!server) {
        return;
    }
    
    uvbus_transport_t* transport = (uvbus_transport_t*)server->data;
    if (!transport) {
        return;
    }
    
    if (status < 0) {
        if (transport->error_cb) {
            transport->error_cb(UVBUS_ERROR_IO, uv_strerror(status), transport->callback_ctx);
        }
        return;
    }
    
    uvbus_tcp_server_t* tcp_server = (uvbus_tcp_server_t*)transport->impl.tcp_server;
    if (!tcp_server) {
        return;
    }
    
    /* Check client limit */
    if (tcp_server->client_count >= UVBUS_MAX_CLIENTS) {
        if (transport->error_cb) {
            transport->error_cb(UVBUS_ERROR_MAX_CLIENTS, "Maximum clients reached (1024)", transport->callback_ctx);
        }
        return;
    }
    
    /* Create new client connection */
    uvbus_tcp_client_t* client = (uvbus_tcp_client_t*)uvrpc_alloc(sizeof(uvbus_tcp_client_t));
    if (!client) {
        return;
    }
    
    memset(client, 0, sizeof(uvbus_tcp_client_t));
    
    /* Initialize reference count */
    ref_init(&client->ref_count);
    
    /* Initialize TCP handle */
    if (!transport->loop) {
        uvrpc_free(client);
        return;
    }
    
    uv_tcp_init(transport->loop, &client->tcp_handle);
    client->tcp_handle.data = client;
    
    /* Optimize socket buffers for accepted connections */
    uv_tcp_nodelay(&client->tcp_handle, 1);  /* Disable Nagle algorithm */
    
    /* Accept connection */
    if (uv_accept(server, (uv_stream_t*)&client->tcp_handle) == 0) {
        /* Expand client list if needed */
        if (tcp_server->client_count >= tcp_server->client_capacity) {
            int new_capacity = tcp_server->client_capacity * 2;
            if (new_capacity > UVBUS_MAX_CLIENTS) {
                new_capacity = UVBUS_MAX_CLIENTS;
            }
            uvbus_tcp_client_t** new_clients = (uvbus_tcp_client_t**)uvrpc_realloc(
                tcp_server->clients,
                sizeof(uvbus_tcp_client_t*) * new_capacity
            );
            if (!new_clients) {
                uv_close((uv_handle_t*)&client->tcp_handle, NULL);
                uvrpc_free(client);
                return;
            }
            tcp_server->clients = new_clients;
            tcp_server->client_capacity = new_capacity;
        }
        
        /* Add to client list */
        tcp_server->clients[tcp_server->client_count++] = client;
        
        /* Set parent reference */
        client->parent_transport = transport;
        
        /* Start reading */
        if (uv_read_start((uv_stream_t*)&client->tcp_handle, on_client_alloc, on_client_read) != 0) {
            /* Failed to start reading, remove from list and close */
            tcp_server->client_count--;
            uv_close((uv_handle_t*)&client->tcp_handle, NULL);
            uvrpc_free(client);
        }
    } else {
        uv_close((uv_handle_t*)&client->tcp_handle, NULL);
        uvrpc_free(client);
    }
}

/* Cleanup callbacks */
static void on_client_close(uv_handle_t* handle) {
    uvbus_tcp_client_t* client = (uvbus_tcp_client_t*)handle->data;
    if (!client) return;
    
    /* Clear the handle data to prevent double free */
    handle->data = NULL;
    
    if (ref_dec(&client->ref_count) == 0) {
        /* Free resources when ref count reaches 0 */
        if (client->host) {
            uvrpc_free(client->host);
        }
        uvrpc_free(client);
    }
}

static void on_server_close(uv_handle_t* handle) {
    uvbus_tcp_server_t* server = (uvbus_tcp_server_t*)handle->data;
    if (!server) return;
    
    if (ref_dec(&server->ref_count) == 0) {
        /* Free resources when ref count reaches 0 */
        if (server->host) {
            uvrpc_free(server->host);
        }
        if (server->clients) {
            uvrpc_free(server->clients);
        }
        uvrpc_free(server);
    }
}

/* Client connect callback */
static void on_client_connect(uv_connect_t* req, int status) {
    if (!req) {
        return;
    }

    uvbus_transport_t* transport = (uvbus_transport_t*)req->data;
    if (!transport) {
        return;
    }

    /* Drop the reference tcp_connect took. If the application released the
     * transport while the attempt was in flight, this is the last one and the
     * struct is reclaimed here rather than in tcp_free. */
    int last_ref = (ref_dec(&transport->ref_count) == 0);

    if (status == 0) {
        transport->is_connected = 1;

        /* Start reading */
        uvbus_tcp_client_t* client = (uvbus_tcp_client_t*)transport->impl.tcp_client;
        if (client) {
            uv_read_start((uv_stream_t*)&client->tcp_handle, on_client_alloc, on_client_read);
        }

        /* Set bus is_active flag - this is critical for sending to work */
        if (transport->parent_bus) {
            transport->parent_bus->is_active = 1;
        }

        if (transport->connect_cb) {
            transport->connect_cb(UVBUS_OK, transport->callback_ctx);
        }
    } else {
        UVBUS_LOG_ERROR("Connection failed: %s", uv_strerror(status));
        if (transport->connect_cb) {
            transport->connect_cb(UVBUS_ERROR_IO, transport->callback_ctx);
        }
    }

    if (last_ref) {
        uvrpc_free(transport);
    }
}

/* Shared broadcast buffer with refcount */
typedef struct {
    uint8_t* data;      /* Frame data (4-byte length prefix + payload) */
    size_t size;        /* Total frame size */
    int ref_count;          /* Refcount for shared broadcast buffer, starts at eligible_count */
} tcp_broadcast_buf_t;

/* Broadcast write callback - decrements shared refcount */
static void on_broadcast_write(uv_write_t* req, int status) {
    if (status != 0) {
        UVBUS_LOG("Broadcast write failed: %s", uv_strerror(status));
    }

    tcp_broadcast_buf_t* shared = (tcp_broadcast_buf_t*)req->data;
    if (shared && ref_dec(&shared->ref_count) == 0) {
        /* Last write completed - free shared buffer and frame data */
        uvrpc_free(shared->data);
        uvrpc_free(shared);
    }
    uvrpc_free(req);
}

/* Write callback */
static void on_write(uv_write_t* req, int status) {
    UVBUS_LOG("Write completed, status=%d", status);
    
    if (status != 0) {
        UVBUS_LOG("Write failed: %s", uv_strerror(status));
    }
    
    uvrpc_free(req->data);
    uvrpc_free(req);
}

/* TCP vtable functions */
static int tcp_listen(void* impl_ptr, const char* address);
static int tcp_connect(void* impl_ptr, const char* address);
static void tcp_disconnect(void* impl_ptr);
static int tcp_send(void* impl_ptr, const uint8_t* data, size_t size);
static int tcp_send_to(void* impl_ptr, const uint8_t* data, size_t size, void* target);
static int tcp_broadcast(void* impl_ptr, const uint8_t* data, size_t size);
static void tcp_free(void* impl_ptr);

/* Global vtable for TCP */
static const uvbus_transport_vtable_t tcp_vtable = {
    .listen = tcp_listen,
    .connect = tcp_connect,
    .disconnect = tcp_disconnect,
    .send = tcp_send,
    .send_to = tcp_send_to,
    .broadcast = tcp_broadcast,
    .free = tcp_free
};

/* TCP listen implementation */
static int tcp_listen(void* impl_ptr, const char* address) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    if (!transport) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    
    if (!transport->is_server) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    
    /* Parse address */
    char* host = NULL;
    int port = 0;
    if (parse_tcp_address(address, &host, &port) != 0) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    
    /* Create server */
    uvbus_tcp_server_t* server = (uvbus_tcp_server_t*)uvrpc_alloc(sizeof(uvbus_tcp_server_t));
    if (!server) {
        uvrpc_free(host);
        return UVBUS_ERROR_NO_MEMORY;
    }
    
    memset(server, 0, sizeof(uvbus_tcp_server_t));
    server->host = host;
    server->port = port;
    server->parent_transport = transport;
    ref_init(&server->ref_count);
    server->clients = (uvbus_tcp_client_t**)uvrpc_alloc(sizeof(uvbus_tcp_client_t*) * UVBUS_INITIAL_CLIENT_CAPACITY);
    server->client_capacity = UVBUS_INITIAL_CLIENT_CAPACITY;
    server->client_count = 0;
    
    /* Initialize TCP handle */
    uv_tcp_init(transport->loop, &server->listen_handle);
    server->listen_handle.data = transport;
    
    /* Bind to address */
    struct sockaddr_in addr;
    uv_ip4_addr(host, port, &addr);
    
    if (uv_tcp_bind(&server->listen_handle, (const struct sockaddr*)&addr, 0) != 0) {
        uv_close((uv_handle_t*)&server->listen_handle, NULL);
        uvrpc_free(server->host);
        uvrpc_free(server->clients);
        uvrpc_free(server);
        return UVBUS_ERROR_IO;
    }
    
    /* Listen */
    if (uv_listen((uv_stream_t*)&server->listen_handle, UVBUS_BACKLOG, on_server_connection) != 0) {
        uv_close((uv_handle_t*)&server->listen_handle, NULL);
        uvrpc_free(server->host);
        uvrpc_free(server->clients);
        uvrpc_free(server);
        return UVBUS_ERROR_IO;
    }
    
    server->is_listening = 1;
    transport->is_connected = 1;
    transport->impl.tcp_server = (void*)server;
    
    /* Set bus as active - server is ready to accept connections */
    if (transport->parent_bus) {
        transport->parent_bus->is_active = 1;
    }
    
    return UVBUS_OK;
}

/* TCP connect implementation */
static int tcp_connect(void* impl_ptr, const char* address) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    if (!transport) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    
    if (transport->is_server) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    
    /* Parse address */
    char* host = NULL;
    int port = 0;
    if (parse_tcp_address(address, &host, &port) != 0) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    
    /* Create client */
    uvbus_tcp_client_t* client = (uvbus_tcp_client_t*)uvrpc_alloc(sizeof(uvbus_tcp_client_t));
    if (!client) {
        uvrpc_free(host);
        return UVBUS_ERROR_NO_MEMORY;
    }
    
    memset(client, 0, sizeof(uvbus_tcp_client_t));
    client->host = host;
    client->port = port;
    client->parent_transport = transport;
    ref_init(&client->ref_count);
    
    /* Initialize TCP handle */
    uv_tcp_init(transport->loop, &client->tcp_handle);
    client->tcp_handle.data = client;  /* Set to client so on_client_read can find it */
    
    /* Optimize socket buffers for better memory usage */
    uv_tcp_nodelay(&client->tcp_handle, 1);  /* Disable Nagle algorithm for low latency */
    /* Note: We use default socket buffer sizes which are auto-tuned by the OS */
    
    /* Set up server address */
    struct sockaddr_in addr;
    uv_ip4_addr(host, port, &addr);
    
    /* Connect */
    transport->impl.tcp_client = (void*)client;
    client->connect_req.data = transport;
    
    /* Hold the transport for as long as the request can still fire. The
     * callback reads req->data, so releasing it before the attempt completes
     * leaves the callback dereferencing freed memory -- which is exactly what
     * "connect, give up, free the client" does. */
    ref_inc(&transport->ref_count);

    uv_tcp_connect(&client->connect_req, &client->tcp_handle, 
                   (const struct sockaddr*)&addr, on_client_connect);
    
    return UVBUS_OK;
}

/* TCP disconnect implementation */
static void tcp_disconnect(void* impl_ptr) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    if (!transport) {
        return;
    }
    
    if (transport->is_server && transport->impl.tcp_server) {
        uvbus_tcp_server_t* server = (uvbus_tcp_server_t*)transport->impl.tcp_server;
        /* Close all client connections */
        for (int i = 0; i < server->client_count; i++) {
            if (server->clients[i]) {
                uvbus_tcp_client_t* client = server->clients[i];
                /* Remove from list first to prevent double close */
                server->clients[i] = NULL;
                /* Clear parent reference to prevent access during cleanup */
                client->parent_transport = NULL;
                /* Close TCP handle */
                if (!uv_is_closing((uv_handle_t*)&client->tcp_handle)) {
                    /* No ref_inc: ref_init left the count at 1 and this
                     * callback is what releases it. Adding one here made the
                     * count land on 1, so on_client_close never reached 0 and
                     * the struct -- 256 KB, it embeds the read buffer -- was
                     * never freed. */
                    uv_close((uv_handle_t*)&client->tcp_handle, on_client_close);
                } else {
                    /* Handle already closing, free immediately */
                    if (client->host) {
                        uvrpc_free(client->host);
                    }
                    uvrpc_free(client);
                }
            }
        }
        uvrpc_free(server->clients);
        server->clients = NULL;
        server->client_count = 0;
        
        /* Close listen handle. No ref_dec here: ref_init left the count at 1
         * and on_server_close is what releases it, so decrementing on both
         * sides drove it to -1 and the server struct and its host string were
         * never freed. This is the same shape as the client path below. */
        /* The listen handle's data is the transport while listening --
         * on_server_connection needs it -- but on_server_close expects the
         * server, and by the time that callback runs tcp_free has already
         * released the transport. Re-point it before closing, exactly as the
         * UDP transport does, so each of the two callbacks sees the type it
         * expects. */
        server->listen_handle.data = server;
        if (!uv_is_closing((uv_handle_t*)&server->listen_handle)) {
            uv_close((uv_handle_t*)&server->listen_handle, on_server_close);
        }
        
        transport->impl.tcp_server = NULL;
    } else if (!transport->is_server && transport->impl.tcp_client) {
        uvbus_tcp_client_t* client = (uvbus_tcp_client_t*)transport->impl.tcp_client;
        /* Clear the pointer first to prevent double disconnect */
        transport->impl.tcp_client = NULL;
        /* Clear parent reference */
        client->parent_transport = NULL;
        /* Close TCP handle */
        if (!uv_is_closing((uv_handle_t*)&client->tcp_handle)) {
            uv_close((uv_handle_t*)&client->tcp_handle, on_client_close);
        } else {
            /* Handle already closing, free immediately */
            if (client->host) {
                uvrpc_free(client->host);
            }
            uvrpc_free(client);
        }
    }
    
    transport->is_connected = 0;
}

/* TCP send implementation */
static int tcp_send(void* impl_ptr, const uint8_t* data, size_t size) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    if (!transport) {
        return UVBUS_ERROR_INVALID_PARAM;
    }

    if (!transport->is_connected) {
        return UVBUS_ERROR_NOT_CONNECTED;
    }

    /* Allocate buffer with 4-byte frame length prefix */
    size_t total_size = 4 + size;
    uint8_t* frame_data = (uint8_t*)uvrpc_alloc(total_size);
    if (!frame_data) {
        return UVBUS_ERROR_NO_MEMORY;
    }

    /* Write frame length in big-endian format */
    frame_data[0] = (size >> 24) & 0xFF;
    frame_data[1] = (size >> 16) & 0xFF;
    frame_data[2] = (size >> 8) & 0xFF;
    frame_data[3] = size & 0xFF;

    /* Copy payload data */
    memcpy(frame_data + 4, data, size);

    if (transport->is_server) {
        uvbus_tcp_server_t* server = (uvbus_tcp_server_t*)transport->impl.tcp_server;
        /* Broadcast to all clients */
        for (int i = 0; i < server->client_count; i++) {
            uv_write_t* req = (uv_write_t*)uvrpc_alloc(sizeof(uv_write_t));
            if (!req) {
                continue;
            }

            /* Copy frame data for this write */
            uint8_t* data_copy = (uint8_t*)uvrpc_alloc(total_size);
            if (!data_copy) {
                uvrpc_free(req);
                continue;
            }
            memcpy(data_copy, frame_data, total_size);

            uv_buf_t buf = uv_buf_init((char*)data_copy, total_size);
            req->data = data_copy;

            uv_write(req, (uv_stream_t*)&server->clients[i]->tcp_handle, &buf, 1, on_write);
        }
    } else {
        uvbus_tcp_client_t* client = (uvbus_tcp_client_t*)transport->impl.tcp_client;
        /* Send to server */
        uv_write_t* req = (uv_write_t*)uvrpc_alloc(sizeof(uv_write_t));
        if (!req) {
            uvrpc_free(frame_data);
            return UVBUS_ERROR_NO_MEMORY;
        }

        uv_buf_t buf = uv_buf_init((char*)frame_data, total_size);
        req->data = frame_data;  /* Set req->data for cleanup in on_write callback */

        /* Write the data */
        int write_result = uv_write(req, (uv_stream_t*)&client->tcp_handle, &buf, 1, on_write);
        if (write_result != 0) {
            uvrpc_free(frame_data);
            uvrpc_free(req);
            
            /* UV_ENOBUFS indicates buffer is completely full */
            if (write_result == UV_ENOBUFS) {
                /* Return error to application layer - let caller handle retry/backpressure */
                return UVBUS_ERROR_BUFFER_FULL;
            }
            return UVBUS_ERROR_IO;
        }
        
        /* Note: frame_data is now owned by the write request, don't free it here */
        return UVBUS_OK;
    }

    /* Note: frame_data is now owned by the write request, don't free it here */
    return UVBUS_OK;
}

/* TCP send to specific client implementation */
static int tcp_send_to(void* impl_ptr, const uint8_t* data, size_t size, void* target) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    if (!transport) {
        return UVBUS_ERROR_INVALID_PARAM;
    }

    if (!transport->is_server) {
        return UVBUS_ERROR_INVALID_PARAM;
    }

    uvbus_tcp_client_t* client = (uvbus_tcp_client_t*)target;
    if (!client) {
        return UVBUS_ERROR_INVALID_PARAM;
    }

    /* Check if TCP handle is closing or closed */
    if (uv_is_closing((uv_handle_t*)&client->tcp_handle)) {
        return UVBUS_ERROR_NOT_CONNECTED;
    }

    /* Allocate buffer with 4-byte frame length prefix */
    size_t total_size = 4 + size;
    uint8_t* frame_data = (uint8_t*)uvrpc_alloc(total_size);
    if (!frame_data) {
        return UVBUS_ERROR_NO_MEMORY;
    }

    /* Write frame length in big-endian format */
    frame_data[0] = (size >> 24) & 0xFF;
    frame_data[1] = (size >> 16) & 0xFF;
    frame_data[2] = (size >> 8) & 0xFF;
    frame_data[3] = size & 0xFF;

    /* Copy payload data */
    memcpy(frame_data + 4, data, size);

    uv_write_t* req = (uv_write_t*)uvrpc_alloc(sizeof(uv_write_t));
    if (!req) {
        uvrpc_free(frame_data);
        return UVBUS_ERROR_NO_MEMORY;
    }

    uv_buf_t buf = uv_buf_init((char*)frame_data, total_size);
    req->data = frame_data;

    int write_result = uv_write(req, (uv_stream_t*)&client->tcp_handle, &buf, 1, on_write);
    if (write_result != 0) {
        uvrpc_free(frame_data);
        uvrpc_free(req);
        if (write_result == UV_ENOBUFS) {
            return UVBUS_ERROR_BUFFER_FULL;
        }
        return UVBUS_ERROR_IO;
    }

    return UVBUS_OK;
}

/* TCP broadcast implementation - shared buffer with refcount */
static int tcp_broadcast(void* impl_ptr, const uint8_t* data, size_t size) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    if (!transport) {
        return UVBUS_ERROR_INVALID_PARAM;
    }

    if (!transport->is_server) {
        return UVBUS_ERROR_INVALID_PARAM;
    }

    if (!transport->is_connected) {
        return UVBUS_ERROR_NOT_CONNECTED;
    }

    uvbus_tcp_server_t* server = (uvbus_tcp_server_t*)transport->impl.tcp_server;
    if (!server) {
        return UVBUS_ERROR_INVALID_PARAM;
    }

    /* No clients - nothing to do */
    if (server->client_count == 0) {
        return UVBUS_OK;
    }

    /* Allocate shared broadcast buffer */
    tcp_broadcast_buf_t* shared = (tcp_broadcast_buf_t*)uvrpc_alloc(sizeof(tcp_broadcast_buf_t));
    if (!shared) {
        return UVBUS_ERROR_NO_MEMORY;
    }

    /* Allocate and fill frame data once (4-byte big-endian length prefix + payload) */
    size_t total_size = 4 + size;
    uint8_t* frame_data = (uint8_t*)uvrpc_alloc(total_size);
    if (!frame_data) {
        uvrpc_free(shared);
        return UVBUS_ERROR_NO_MEMORY;
    }

    frame_data[0] = (size >> 24) & 0xFF;
    frame_data[1] = (size >> 16) & 0xFF;
    frame_data[2] = (size >> 8) & 0xFF;
    frame_data[3] = size & 0xFF;
    memcpy(frame_data + 4, data, size);

    shared->data = frame_data;
    shared->size = total_size;

    /* Count eligible clients (non-NULL, not closing) for refcount */
    int eligible_count = 0;
    for (int i = 0; i < server->client_count; i++) {
        uvbus_tcp_client_t* client = server->clients[i];
        if (client && !uv_is_closing((uv_handle_t*)&client->tcp_handle)) {
            eligible_count++;
        }
    }

    /* No eligible clients - clean up and return */
    if (eligible_count == 0) {
        uvrpc_free(frame_data);
        uvrpc_free(shared);
        return UVBUS_OK;
    }

    /* Set refcount to number of eligible clients that will share this buffer */
    shared->ref_count = eligible_count;

    /* Send the same shared buffer to every eligible client */
    for (int i = 0; i < server->client_count; i++) {
        uvbus_tcp_client_t* client = server->clients[i];
        if (!client || uv_is_closing((uv_handle_t*)&client->tcp_handle)) {
            continue;
        }

        uv_write_t* req = (uv_write_t*)uvrpc_alloc(sizeof(uv_write_t));
        if (!req) {
            /* Alloc failed - decrement refcount for this skipped client */
            if (ref_dec(&shared->ref_count) == 0) {
                uvrpc_free(shared->data);
                uvrpc_free(shared);
            }
            continue;
        }

        uv_buf_t buf = uv_buf_init((char*)shared->data, shared->size);
        req->data = shared;

        int write_result = uv_write(req, (uv_stream_t*)&client->tcp_handle, &buf, 1, on_broadcast_write);
        if (write_result != 0) {
            uvrpc_free(req);
            /* Decrement refcount for this failed write so the buffer is eventually freed */
            if (ref_dec(&shared->ref_count) == 0) {
                uvrpc_free(shared->data);
                uvrpc_free(shared);
            }
            continue;
        }
    }

    return UVBUS_OK;
}

/* TCP free implementation */
static void tcp_free(void* impl_ptr) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    if (!transport) {
        return;
    }

    tcp_disconnect(transport);

    if (transport->address) {
        uvrpc_free(transport->address);
        transport->address = NULL;
    }

    /* Disconnecting closes the handles; the client and server structs they
     * point at are reclaimed by their own close callbacks, not here.
     * Drop the owner's reference; the struct goes when the last one does. A
     * connect still in flight holds a reference of its own, released by
     * on_client_connect, so freeing during a connect attempt defers the
     * destruction instead of pulling it out from under the callback. */
    if (ref_dec(&transport->ref_count) == 0) {
        uvrpc_free(transport);
    }
}

/* Export function to create TCP transport */
uvbus_transport_t* create_tcp_transport(uvbus_transport_type_t type, uv_loop_t* loop) {
    uvbus_transport_t* transport = (uvbus_transport_t*)uvrpc_alloc(sizeof(uvbus_transport_t));
    if (!transport) {
        return NULL;
    }
    
    memset(transport, 0, sizeof(uvbus_transport_t));
    transport->ref_count = 1;
    transport->type = type;
    transport->loop = loop;
    transport->vtable = &tcp_vtable;
    
    /* Set fast path function pointers */
    transport->fast_send = tcp_send;
    transport->fast_send_to = tcp_send_to;
    
    return transport;
}
