/**
 * UVBus UDP Transport Implementation
 */

#include "../include/uvbus.h"
#include "../include/uvbus_config.h"
#include "../include/uvrpc_allocator.h"
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>

/* Forward declarations */
typedef struct uvbus_udp_client uvbus_udp_client_t;
typedef struct uvbus_udp_server uvbus_udp_server_t;

/* UDP client structure */
struct uvbus_udp_client {
    uv_udp_t udp_handle;
    struct sockaddr_in server_addr;
    int is_connected;
    
    char* host;
    int port;
    
    /* Read buffer */
    uint8_t read_buffer[65536];
    
    /* Parent transport reference */
    void* parent_transport;
};

/* UDP server structure */
struct uvbus_udp_server {
    uv_udp_t udp_handle;
    
    char* host;
    int port;
    int is_listening;
    
    /* Parent transport reference */
    void* parent_transport;
};

/* Parse address */
static int parse_udp_address(const char* address, char** host, int* port) {
    if (!address || !host || !port) {
        return -1;
    }
    
    /* Skip protocol prefix */
    const char* addr_start = address;
    if (strncmp(address, "udp://", 6) == 0) {
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

/* Server receive callback */
static void on_server_recv(uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf, 
                           const struct sockaddr* addr, unsigned flags) {
    uvbus_transport_t* transport = (uvbus_transport_t*)handle->data;
    
    if (nread < 0) {
        if (nread != UV_EOF) {
            if (transport->error_cb) {
                transport->error_cb(UVBUS_ERROR_IO, uv_strerror(nread), transport->callback_ctx);
            }
        }
        uvrpc_free(buf->base);
        return;
    }
    
    if (nread > 0 && transport->recv_cb) {
        transport->recv_cb((const uint8_t*)buf->base, nread, transport->callback_ctx);
    }
    
    uvrpc_free(buf->base);
}

/* Server alloc callback */
static void on_server_alloc(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    buf->base = (char*)uvrpc_alloc(UVBUS_MAX_BUFFER_SIZE);
    if (buf->base) {
        buf->len = UVBUS_MAX_BUFFER_SIZE;
    } else {
        buf->len = 0;
    }
}

/* Client receive callback */
static void on_client_recv(uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf, 
                           const struct sockaddr* addr, unsigned flags) {
    uvbus_transport_t* transport = (uvbus_transport_t*)handle->data;
    
    if (nread < 0) {
        if (nread != UV_EOF) {
            if (transport->error_cb) {
                transport->error_cb(UVBUS_ERROR_IO, uv_strerror(nread), transport->callback_ctx);
            }
        }
        uvrpc_free(buf->base);
        return;
    }
    
    if (nread > 0 && transport->recv_cb) {
        transport->recv_cb((const uint8_t*)buf->base, nread, transport->callback_ctx);
    }
    
    uvrpc_free(buf->base);
}

/* Client alloc callback */
static void on_client_alloc(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    buf->base = (char*)uvrpc_alloc(UVBUS_MAX_BUFFER_SIZE);
    if (buf->base) {
        buf->len = UVBUS_MAX_BUFFER_SIZE;
    } else {
        buf->len = 0;
    }
}

/* Send callback */
static void on_send(uv_udp_send_t* req, int status) {
    uvrpc_free(req->data);
    uvrpc_free(req);
}

/* UDP vtable functions */
static int udp_listen(void* impl_ptr, const char* address);
static int udp_connect(void* impl_ptr, const char* address);
static void udp_disconnect(void* impl_ptr);
static int udp_send(void* impl_ptr, const uint8_t* data, size_t size);
static int udp_send_to(void* impl_ptr, const uint8_t* data, size_t size, void* target);
static void udp_free(void* impl_ptr);

/* Global vtable for UDP */
static const uvbus_transport_vtable_t udp_vtable = {
    .listen = udp_listen,
    .connect = udp_connect,
    .disconnect = udp_disconnect,
    .send = udp_send,
    .send_to = udp_send_to,
    .free = udp_free
};

/* UDP listen implementation */
static int udp_listen(void* impl_ptr, const char* address) {
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
    if (parse_udp_address(address, &host, &port) != 0) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    
    /* Create server */
    uvbus_udp_server_t* server = (uvbus_udp_server_t*)uvrpc_alloc(sizeof(uvbus_udp_server_t));
    if (!server) {
        uvrpc_free(host);
        return UVBUS_ERROR_NO_MEMORY;
    }
    
    memset(server, 0, sizeof(uvbus_udp_server_t));
    server->host = host;
    server->port = port;
    server->parent_transport = transport;
    
    /* Initialize UDP handle */
    uv_udp_init(transport->loop, &server->udp_handle);
    server->udp_handle.data = transport;
    
    /* Bind to address */
    struct sockaddr_in addr;
    uv_ip4_addr(host, port, &addr);
    
    if (uv_udp_bind(&server->udp_handle, (const struct sockaddr*)&addr, 0) != 0) {
        uv_close((uv_handle_t*)&server->udp_handle, NULL);
        uvrpc_free(server->host);
        uvrpc_free(server);
        return UVBUS_ERROR_IO;
    }
    
    /* Start receiving */
    if (uv_udp_recv_start(&server->udp_handle, on_server_alloc, on_server_recv) != 0) {
        uv_close((uv_handle_t*)&server->udp_handle, NULL);
        uvrpc_free(server->host);
        uvrpc_free(server);
        return UVBUS_ERROR_IO;
    }
    
    server->is_listening = 1;
    transport->is_connected = 1;
    transport->impl.udp_server = (void*)server;
    
    return UVBUS_OK;
}

/* UDP connect implementation */
static int udp_connect(void* impl_ptr, const char* address) {
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
    if (parse_udp_address(address, &host, &port) != 0) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    
    /* Create client */
    uvbus_udp_client_t* client = (uvbus_udp_client_t*)uvrpc_alloc(sizeof(uvbus_udp_client_t));
    if (!client) {
        uvrpc_free(host);
        return UVBUS_ERROR_NO_MEMORY;
    }
    
    memset(client, 0, sizeof(uvbus_udp_client_t));
    client->host = host;
    client->port = port;
    client->parent_transport = transport;
    
    /* Initialize UDP handle */
    uv_udp_init(transport->loop, &client->udp_handle);
    client->udp_handle.data = transport;
    
    /* Set up server address */
    uv_ip4_addr(host, port, &client->server_addr);
    
    /* Bind to local address (random port) */
    struct sockaddr_in local_addr;
    uv_ip4_addr("0.0.0.0", 0, &local_addr);
    if (uv_udp_bind(&client->udp_handle, (const struct sockaddr*)&local_addr, 0) != 0) {
        uv_close((uv_handle_t*)&client->udp_handle, NULL);
        uvrpc_free(client->host);
        uvrpc_free(client);
        return UVBUS_ERROR_IO;
    }
    
    /* Start receiving */
    if (uv_udp_recv_start(&client->udp_handle, on_client_alloc, on_client_recv) != 0) {
        uv_close((uv_handle_t*)&client->udp_handle, NULL);
        uvrpc_free(client->host);
        uvrpc_free(client);
        return UVBUS_ERROR_IO;
    }
    
    client->is_connected = 1;
    transport->is_connected = 1;
    transport->impl.udp_client = (void*)client;
    
    if (transport->connect_cb) {
        transport->connect_cb(UVBUS_OK, transport->callback_ctx);
    }
    
    return UVBUS_OK;
}

/* UDP disconnect implementation */
static void udp_disconnect(void* impl_ptr) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    if (!transport) {
        return;
    }
    
    if (transport->is_server && transport->impl.udp_server) {
        uvbus_udp_server_t* server = (uvbus_udp_server_t*)transport->impl.udp_server;
        /* Close UDP handle */
        uv_close((uv_handle_t*)&server->udp_handle, NULL);
        
        uvrpc_free(server->host);
        uvrpc_free(server);
        transport->impl.udp_server = NULL;
    } else if (!transport->is_server && transport->impl.udp_client) {
        uvbus_udp_client_t* client = (uvbus_udp_client_t*)transport->impl.udp_client;
        /* Close UDP handle */
        uv_close((uv_handle_t*)&client->udp_handle, NULL);
        
        uvrpc_free(client->host);
        uvrpc_free(client);
        transport->impl.udp_client = NULL;
    }
    
    transport->is_connected = 0;
}

/* UDP send implementation */
static int udp_send(void* impl_ptr, const uint8_t* data, size_t size) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    if (!transport) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    
    if (!transport->is_connected) {
        return UVBUS_ERROR_NOT_CONNECTED;
    }
    
    if (transport->is_server) {
        /* UDP server doesn't broadcast like TCP - needs target */
        return UVBUS_ERROR_INVALID_PARAM;
    } else {
        uvbus_udp_client_t* client = (uvbus_udp_client_t*)transport->impl.udp_client;
        /* Client send to server */
        uv_udp_send_t* req = (uv_udp_send_t*)uvrpc_alloc(sizeof(uv_udp_send_t));
        if (!req) {
            return UVBUS_ERROR_NO_MEMORY;
        }
        
        uint8_t* data_copy = (uint8_t*)uvrpc_alloc(size);
        if (!data_copy) {
            uvrpc_free(req);
            return UVBUS_ERROR_NO_MEMORY;
        }
        memcpy(data_copy, data, size);
        
        uv_buf_t buf = uv_buf_init((char*)data_copy, size);
        req->data = data_copy;
        
        if (uv_udp_send(req, &client->udp_handle, &buf, 1, 
                        (const struct sockaddr*)&client->server_addr, on_send) != 0) {
            uvrpc_free(data_copy);
            uvrpc_free(req);
            return UVBUS_ERROR_IO;
        }
    }
    
    return UVBUS_OK;
}

/* UDP send to specific target implementation */
static int udp_send_to(void* impl_ptr, const uint8_t* data, size_t size, void* target) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    if (!transport) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    
    if (!transport->is_server) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    
    uvbus_udp_server_t* server = (uvbus_udp_server_t*)transport->impl.udp_server;
    struct sockaddr_in* addr = (struct sockaddr_in*)target;
    if (!addr) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    
    uv_udp_send_t* req = (uv_udp_send_t*)uvrpc_alloc(sizeof(uv_udp_send_t));
    if (!req) {
        return UVBUS_ERROR_NO_MEMORY;
    }
    
    uint8_t* data_copy = (uint8_t*)uvrpc_alloc(size);
    if (!data_copy) {
        uvrpc_free(req);
        return UVBUS_ERROR_NO_MEMORY;
    }
    memcpy(data_copy, data, size);
    
    uv_buf_t buf = uv_buf_init((char*)data_copy, size);
    req->data = data_copy;
    
    if (uv_udp_send(req, &server->udp_handle, &buf, 1, 
                    (const struct sockaddr*)addr, on_send) != 0) {
        uvrpc_free(data_copy);
        uvrpc_free(req);
        return UVBUS_ERROR_IO;
    }
    
    return UVBUS_OK;
}

/* UDP free implementation */
static void udp_free(void* impl_ptr) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    if (!transport) {
        return;
    }
    
    udp_disconnect(transport);
    
    if (transport->address) {
        uvrpc_free(transport->address);
    }
    
    uvrpc_free(transport);
}

/* Export function to create UDP transport */
uvbus_transport_t* create_udp_transport(uvbus_transport_type_t type, uv_loop_t* loop) {
    uvbus_transport_t* transport = (uvbus_transport_t*)uvrpc_alloc(sizeof(uvbus_transport_t));
    if (!transport) {
        return NULL;
    }
    
    memset(transport, 0, sizeof(uvbus_transport_t));
    transport->type = type;
    transport->loop = loop;
    transport->vtable = &udp_vtable;
    
    return transport;
}