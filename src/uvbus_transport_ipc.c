/**
 * UVBus IPC Transport Implementation (Unix Domain Sockets)
 */

#include "../include/uvbus.h"
#include "../include/uvrpc_allocator.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

/* Forward declarations */
typedef struct uvbus_ipc_client uvbus_ipc_client_t;
typedef struct uvbus_ipc_server uvbus_ipc_server_t;

/* IPC client structure */
struct uvbus_ipc_client {
    uv_pipe_t pipe_handle;
    uv_connect_t connect_req;
    int is_connected;
    
    char* socket_path;
    
    /* Read buffer */
    uint8_t read_buffer[65536];
    size_t read_pos;
    
    /* Parent transport reference */
    void* parent_transport;
};

/* IPC server structure */
struct uvbus_ipc_server {
    uv_pipe_t listen_pipe;
    
    char* socket_path;
    int is_listening;
    
    /* Client connections */
    uvbus_ipc_client_t** clients;
    int client_count;
    int client_capacity;
    
    /* Parent transport reference */
    void* parent_transport;
};

/* Parse address */
static int parse_ipc_address(const char* address, char** socket_path) {
    if (!address || !socket_path) {
        return -1;
    }
    
    /* Skip protocol prefix */
    const char* path_start = address;
    if (strncmp(address, "ipc://", 6) == 0) {
        path_start += 6;
    }
    
    *socket_path = (char*)uvrpc_alloc(strlen(path_start) + 1);
    if (!*socket_path) {
        return -1;
    }
    strcpy(*socket_path, path_start);
    
    return 0;
}

/* Client read callback */
static void on_client_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    uvbus_transport_t* transport = (uvbus_transport_t*)stream->data;
    
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
    uvbus_ipc_client_t* client = (uvbus_ipc_client_t*)handle->data;
    
    if (client->read_pos + suggested_size > sizeof(client->read_buffer)) {
        suggested_size = sizeof(client->read_buffer) - client->read_pos;
    }
    
    buf->base = (char*)client->read_buffer + client->read_pos;
    buf->len = suggested_size;
}

/* Client connect callback */
static void on_client_connect(uv_connect_t* req, int status) {
    uvbus_transport_t* transport = (uvbus_transport_t*)req->data;
    
    if (status == 0) {
        transport->is_connected = 1;
        
        /* Start reading */
        uvbus_ipc_client_t* client = (uvbus_ipc_client_t*)transport->impl.ipc_client;
        uv_read_start((uv_stream_t*)&client->pipe_handle, on_client_alloc, on_client_read);
        
        if (transport->connect_cb) {
            transport->connect_cb(UVBUS_OK, transport->callback_ctx);
        }
    } else {
        if (transport->connect_cb) {
            transport->connect_cb(UVBUS_ERROR_IO, transport->callback_ctx);
        }
    }
}

/* Server connection callback */
static void on_server_connection(uv_stream_t* server, int status) {
    uvbus_transport_t* transport = (uvbus_transport_t*)server->data;
    
    if (status < 0) {
        if (transport->error_cb) {
            transport->error_cb(UVBUS_ERROR_IO, uv_strerror(status), transport->callback_ctx);
        }
        return;
    }
    
    uvbus_ipc_server_t* ipc_server = (uvbus_ipc_server_t*)transport->impl.ipc_server;
    
    /* Create new client connection */
    uvbus_ipc_client_t* client = (uvbus_ipc_client_t*)uvrpc_alloc(sizeof(uvbus_ipc_client_t));
    if (!client) {
        return;
    }
    
    memset(client, 0, sizeof(uvbus_ipc_client_t));
    
    /* Initialize pipe handle */
    uv_pipe_init(transport->loop, &client->pipe_handle, 0);
    client->pipe_handle.data = transport;
    
    /* Accept connection */
    if (uv_accept(server, (uv_stream_t*)&client->pipe_handle) == 0) {
        /* Add to client list */
        if (ipc_server->client_count >= ipc_server->client_capacity) {
            ipc_server->client_capacity *= 2;
            ipc_server->clients = (uvbus_ipc_client_t**)uvrpc_realloc(
                ipc_server->clients, 
                sizeof(uvbus_ipc_client_t*) * ipc_server->client_capacity
            );
        }
        
        ipc_server->clients[ipc_server->client_count++] = client;
        
        /* Set parent reference */
        client->parent_transport = transport;
        
        /* Start reading */
        uv_read_start((uv_stream_t*)&client->pipe_handle, on_client_alloc, on_client_read);
    } else {
        uv_close((uv_handle_t*)&client->pipe_handle, NULL);
        uvrpc_free(client);
    }
}

/* Write callback */
static void on_write(uv_write_t* req, int status) {
    uvrpc_free(req->data);
    uvrpc_free(req);
}

/* IPC vtable functions */
static int ipc_listen(void* impl_ptr, const char* address);
static int ipc_connect(void* impl_ptr, const char* address);
static void ipc_disconnect(void* impl_ptr);
static int ipc_send(void* impl_ptr, const uint8_t* data, size_t size);
static int ipc_send_to(void* impl_ptr, const uint8_t* data, size_t size, void* target);
static void ipc_free(void* impl_ptr);

/* Global vtable for IPC */
static const uvbus_transport_vtable_t ipc_vtable = {
    .listen = ipc_listen,
    .connect = ipc_connect,
    .disconnect = ipc_disconnect,
    .send = ipc_send,
    .send_to = ipc_send_to,
    .free = ipc_free
};

/* IPC listen implementation */
static int ipc_listen(void* impl_ptr, const char* address) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    if (!transport) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    
    if (!transport->is_server) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    
    /* Parse address */
    char* socket_path = NULL;
    if (parse_ipc_address(address, &socket_path) != 0) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    
    /* Create server */
    uvbus_ipc_server_t* server = (uvbus_ipc_server_t*)uvrpc_alloc(sizeof(uvbus_ipc_server_t));
    if (!server) {
        uvrpc_free(socket_path);
        return UVBUS_ERROR_NO_MEMORY;
    }
    
    memset(server, 0, sizeof(uvbus_ipc_server_t));
    server->socket_path = socket_path;
    server->parent_transport = transport;
    server->clients = (uvbus_ipc_client_t**)uvrpc_alloc(sizeof(uvbus_ipc_client_t*) * 10);
    server->client_capacity = 10;
    server->client_count = 0;
    
    /* Initialize pipe handle */
    uv_pipe_init(transport->loop, &server->listen_pipe, 0);
    server->listen_pipe.data = transport;
    
    /* Bind to socket path */
    if (uv_pipe_bind(&server->listen_pipe, socket_path) != 0) {
        uv_close((uv_handle_t*)&server->listen_pipe, NULL);
        uvrpc_free(server->socket_path);
        uvrpc_free(server->clients);
        uvrpc_free(server);
        return UVBUS_ERROR_IO;
    }
    
    /* Listen */
    if (uv_listen((uv_stream_t*)&server->listen_pipe, 128, on_server_connection) != 0) {
        uv_close((uv_handle_t*)&server->listen_pipe, NULL);
        uvrpc_free(server->socket_path);
        uvrpc_free(server->clients);
        uvrpc_free(server);
        return UVBUS_ERROR_IO;
    }
    
    server->is_listening = 1;
    transport->is_connected = 1;
    transport->impl.ipc_server = (void*)server;
    
    return UVBUS_OK;
}

/* IPC connect implementation */
static int ipc_connect(void* impl_ptr, const char* address) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    if (!transport) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    
    if (transport->is_server) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    
    /* Parse address */
    char* socket_path = NULL;
    if (parse_ipc_address(address, &socket_path) != 0) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    
    /* Create client */
    uvbus_ipc_client_t* client = (uvbus_ipc_client_t*)uvrpc_alloc(sizeof(uvbus_ipc_client_t));
    if (!client) {
        uvrpc_free(socket_path);
        return UVBUS_ERROR_NO_MEMORY;
    }
    
    memset(client, 0, sizeof(uvbus_ipc_client_t));
    client->socket_path = socket_path;
    client->parent_transport = transport;
    
    /* Initialize pipe handle */
    uv_pipe_init(transport->loop, &client->pipe_handle, 0);
    client->pipe_handle.data = transport;
    
    /* Connect */
    transport->impl.ipc_client = (void*)client;
    client->connect_req.data = transport;
    
    uv_pipe_connect(&client->connect_req, &client->pipe_handle, 
                    socket_path, on_client_connect);
    
    return UVBUS_OK;
}

/* IPC disconnect implementation */
static void ipc_disconnect(void* impl_ptr) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    if (!transport) {
        return;
    }
    
    if (transport->is_server && transport->impl.ipc_server) {
        uvbus_ipc_server_t* server = (uvbus_ipc_server_t*)transport->impl.ipc_server;
        /* Close all client connections */
        for (int i = 0; i < server->client_count; i++) {
            if (server->clients[i]) {
                uv_close((uv_handle_t*)&server->clients[i]->pipe_handle, NULL);
                uvrpc_free(server->clients[i]);
            }
        }
        uvrpc_free(server->clients);
        
        /* Close listen handle */
        uv_close((uv_handle_t*)&server->listen_pipe, NULL);
        
        /* Remove socket file */
        unlink(server->socket_path);
        
        uvrpc_free(server->socket_path);
        uvrpc_free(server);
        transport->impl.ipc_server = NULL;
    } else if (!transport->is_server && transport->impl.ipc_client) {
        uvbus_ipc_client_t* client = (uvbus_ipc_client_t*)transport->impl.ipc_client;
        /* Close client connection */
        uv_close((uv_handle_t*)&client->pipe_handle, NULL);
        
        uvrpc_free(client->socket_path);
        uvrpc_free(client);
        transport->impl.ipc_client = NULL;
    }
    
    transport->is_connected = 0;
}

/* IPC send implementation */
static int ipc_send(void* impl_ptr, const uint8_t* data, size_t size) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    if (!transport) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    
    if (!transport->is_connected) {
        return UVBUS_ERROR_NOT_CONNECTED;
    }
    
    if (transport->is_server) {
        uvbus_ipc_server_t* server = (uvbus_ipc_server_t*)transport->impl.ipc_server;
        /* Broadcast to all clients */
        for (int i = 0; i < server->client_count; i++) {
            uv_write_t* req = (uv_write_t*)uvrpc_alloc(sizeof(uv_write_t));
            if (!req) {
                continue;
            }
            
            uint8_t* data_copy = (uint8_t*)uvrpc_alloc(size);
            if (!data_copy) {
                uvrpc_free(req);
                continue;
            }
            memcpy(data_copy, data, size);
            
            uv_buf_t buf = uv_buf_init((char*)data_copy, size);
            req->data = data_copy;
            
            uv_write(req, (uv_stream_t*)&server->clients[i]->pipe_handle, &buf, 1, on_write);
        }
    } else {
        uvbus_ipc_client_t* client = (uvbus_ipc_client_t*)transport->impl.ipc_client;
        /* Send to server */
        uv_write_t* req = (uv_write_t*)uvrpc_alloc(sizeof(uv_write_t));
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
        
        if (uv_write(req, (uv_stream_t*)&client->pipe_handle, &buf, 1, on_write) != 0) {
            uvrpc_free(data_copy);
            uvrpc_free(req);
            return UVBUS_ERROR_IO;
        }
    }
    
    return UVBUS_OK;
}

/* IPC send to specific client implementation */
static int ipc_send_to(void* impl_ptr, const uint8_t* data, size_t size, void* target) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    if (!transport) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    
    if (!transport->is_server) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    
    uvbus_ipc_client_t* client = (uvbus_ipc_client_t*)target;
    if (!client) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    
    uv_write_t* req = (uv_write_t*)uvrpc_alloc(sizeof(uv_write_t));
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
    
    if (uv_write(req, (uv_stream_t*)&client->pipe_handle, &buf, 1, on_write) != 0) {
        uvrpc_free(data_copy);
        uvrpc_free(req);
        return UVBUS_ERROR_IO;
    }
    
    return UVBUS_OK;
}

/* IPC free implementation */
static void ipc_free(void* impl_ptr) {
    uvbus_transport_t* transport = (uvbus_transport_t*)impl_ptr;
    if (!transport) {
        return;
    }
    
    ipc_disconnect(transport);
    
    if (transport->address) {
        uvrpc_free(transport->address);
    }
    
    uvrpc_free(transport);
}

/* Export function to create IPC transport */
uvbus_transport_t* create_ipc_transport(uvbus_transport_type_t type, uv_loop_t* loop) {
    uvbus_transport_t* transport = (uvbus_transport_t*)uvrpc_alloc(sizeof(uvbus_transport_t));
    if (!transport) {
        return NULL;
    }
    
    memset(transport, 0, sizeof(uvbus_transport_t));
    transport->type = type;
    transport->loop = loop;
    transport->vtable = &ipc_vtable;
    
    return transport;
}