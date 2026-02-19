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

/* Debug logging macro - compiles out in release builds */
#ifdef UVRPC_DEBUG
#define UVRPC_LOG(fmt, ...) fprintf(stderr, "[DEBUG] " fmt "\n", ##__VA_ARGS__)
#else
#define UVRPC_LOG(fmt, ...) ((void)0)
#endif

/* Error logging - always enabled */
#define UVRPC_ERROR(fmt, ...) fprintf(stderr, "[ERROR] " fmt "\n", ##__VA_ARGS__)

typedef struct uvbus_tcp_client uvbus_tcp_client_t;
typedef struct uvbus_tcp_server uvbus_tcp_server_t;

/* TCP client structure */
struct uvbus_tcp_client {
    uv_tcp_t tcp_handle;
    uv_connect_t connect_req;
    uv_write_t write_req;
    
    char* host;
    int port;
    int is_connected;
    int ref_count;  /* Reference counting for async cleanup */
    
    /* UVRPC compatibility: pointer to client_connection */
    void* client_connection;
    
    /* Read buffer - optimized for performance vs memory trade-off */
    uint8_t read_buffer[32768];  /* Reduced from 64KB to 32KB */
    size_t read_pos;
    
    /* Parent transport reference */
    void* parent_transport;
};

/* TCP server structure */
struct uvbus_tcp_server {
    uv_tcp_t listen_handle;
    
    char* host;
    int port;
    int is_listening;
    int ref_count;  /* Reference counting for async cleanup */
    
    /* Client connections */
    uvbus_tcp_client_t** clients;
    int client_count;
    int client_capacity;
    
    /* Parent transport reference */
    void* parent_transport;
};

/* Reference counting helpers */
static void ref_init(int* ref_count) {
    *ref_count = 1;
}

static int ref_inc(int* ref_count) {
    return __sync_add_and_fetch(ref_count, 1);
}

static int ref_dec(int* ref_count) {
    return __sync_sub_and_fetch(ref_count, 1);
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

    uvbus_tcp_client_t* client = (uvbus_tcp_client_t*)stream->data;
    if (!client) {
        uvrpc_free(buf->base);
        return;
    }

    uvbus_transport_t* transport = (uvbus_transport_t*)client->parent_transport;
    if (!transport) {
        uvrpc_free(buf->base);
        return;
    }

    if (nread < 0) {
        if (nread != UV_EOF) {
            if (transport->error_cb) {
                transport->error_cb(UVBUS_ERROR_IO, uv_strerror(nread), transport->callback_ctx);
            }
        }
        uvrpc_free(buf->base);
        return;
    }

    if (nread > 0) {
        /* Add data to client's read buffer */
        if (client->read_pos + nread <= sizeof(client->read_buffer)) {
            memcpy(client->read_buffer + client->read_pos, buf->base, nread);
            client->read_pos += nread;
        } else {
            /* Buffer overflow - reset and log error */
            UVRPC_ERROR("Buffer overflow: read_pos=%zu, nread=%zd, buffer_size=%zu",
                    client->read_pos, nread, sizeof(client->read_buffer));
            client->read_pos = 0;
            uvrpc_free(buf->base);
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
            if (frame_size == 0 || frame_size > 64*1024) {  /* 64KB max */
                /* Invalid frame size, reset buffer */
                UVRPC_ERROR("Invalid frame size (%u), resetting buffer", frame_size);
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
                    UVRPC_ERROR("Failed to allocate %u bytes for frame", frame_size);
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
                    transport->recv_cb(frame_copy, frame_size, client, transport->callback_ctx);
                } else {
                    /* Client mode: pass NULL for client context (not needed) */
                    transport->recv_cb(frame_copy, frame_size, NULL, transport->callback_ctx);
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
    buf->base = (char*)uvrpc_alloc(suggested_size);
    buf->len = suggested_size;
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
    
    /* Create new client connection */
    uvbus_tcp_client_t* client = (uvbus_tcp_client_t*)uvrpc_alloc(sizeof(uvbus_tcp_client_t));
    if (!client) {
        return;
    }
    
    memset(client, 0, sizeof(uvbus_tcp_client_t));
    
    /* Initialize TCP handle */
    if (!transport->loop) {
        uvrpc_free(client);
        return;
    }
    
    uv_tcp_init(transport->loop, &client->tcp_handle);
    client->tcp_handle.data = client;
    
    /* Optimize socket buffers for accepted connections */
    uv_tcp_nodelay(&client->tcp_handle, 1);  /* Disable Nagle algorithm */
    /* Note: Default socket buffer sizes are used, auto-tuned by the OS */
    client->tcp_handle.data = client;
    
    /* Accept connection */
    if (uv_accept(server, (uv_stream_t*)&client->tcp_handle) == 0) {
        /* Add to client list */
        if (tcp_server->client_count >= tcp_server->client_capacity) {
            tcp_server->client_capacity *= 2;
            tcp_server->clients = (uvbus_tcp_client_t**)uvrpc_realloc(
                tcp_server->clients, 
                sizeof(uvbus_tcp_client_t*) * tcp_server->client_capacity
            );
        }
        
        tcp_server->clients[tcp_server->client_count++] = client;
        
        /* Set parent reference */
        client->parent_transport = transport;
        
        /* Start reading */
        uv_read_start((uv_stream_t*)&client->tcp_handle, on_client_alloc, on_client_read);
    } else {
        uv_close((uv_handle_t*)&client->tcp_handle, NULL);
        uvrpc_free(client);
    }
}

/* Cleanup callbacks */
static void on_client_close(uv_handle_t* handle) {
    uvbus_tcp_client_t* client = (uvbus_tcp_client_t*)handle->data;
    if (!client) return;
    
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
        UVRPC_ERROR("Connection failed: %s", uv_strerror(status));
        if (transport->connect_cb) {
            transport->connect_cb(UVBUS_ERROR_IO, transport->callback_ctx);
        }
    }
}

/* Write callback */
static void on_write(uv_write_t* req, int status) {
    uvrpc_free(req->data);
    uvrpc_free(req);
}

/* TCP vtable functions */
static int tcp_listen(void* impl_ptr, const char* address);
static int tcp_connect(void* impl_ptr, const char* address);
static void tcp_disconnect(void* impl_ptr);
static int tcp_send(void* impl_ptr, const uint8_t* data, size_t size);
static int tcp_send_to(void* impl_ptr, const uint8_t* data, size_t size, void* target);
static void tcp_free(void* impl_ptr);

/* Global vtable for TCP */
static const uvbus_transport_vtable_t tcp_vtable = {
    .listen = tcp_listen,
    .connect = tcp_connect,
    .disconnect = tcp_disconnect,
    .send = tcp_send,
    .send_to = tcp_send_to,
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
                ref_dec(&server->clients[i]->ref_count);
                uv_close((uv_handle_t*)&server->clients[i]->tcp_handle, on_client_close);
            }
        }
        uvrpc_free(server->clients);
        server->clients = NULL;
        server->client_count = 0;
        
        /* Close listen handle */
        ref_dec(&server->ref_count);
        uv_close((uv_handle_t*)&server->listen_handle, on_server_close);
        
        transport->impl.tcp_server = NULL;
    } else if (!transport->is_server && transport->impl.tcp_client) {
        uvbus_tcp_client_t* client = (uvbus_tcp_client_t*)transport->impl.tcp_client;
        /* Clear the pointer first to prevent double disconnect */
        transport->impl.tcp_client = NULL;
        /* Close client connection */
        ref_dec(&client->ref_count);
        uv_close((uv_handle_t*)&client->tcp_handle, on_client_close);
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
        req->data = frame_data;

        if (uv_write(req, (uv_stream_t*)&client->tcp_handle, &buf, 1, on_write) != 0) {
            uvrpc_free(frame_data);
            uvrpc_free(req);
            return UVBUS_ERROR_IO;
        }
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

    if (uv_write(req, (uv_stream_t*)&client->tcp_handle, &buf, 1, on_write) != 0) {
        uvrpc_free(frame_data);
        uvrpc_free(req);
        return UVBUS_ERROR_IO;
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
    
    uvrpc_free(transport);
}

/* Export function to create TCP transport */
uvbus_transport_t* create_tcp_transport(uvbus_transport_type_t type, uv_loop_t* loop) {
    uvbus_transport_t* transport = (uvbus_transport_t*)uvrpc_alloc(sizeof(uvbus_transport_t));
    if (!transport) {
        return NULL;
    }
    
    memset(transport, 0, sizeof(uvbus_transport_t));
    transport->type = type;
    transport->loop = loop;
    transport->vtable = &tcp_vtable;
    
    return transport;
}