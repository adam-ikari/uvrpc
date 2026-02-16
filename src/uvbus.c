#include "../include/uvbus.h"
#include "../include/uvrpc_allocator.h"
#include <string.h>
#include <stdlib.h>

/* Transport creation functions */
extern uvbus_transport_t* create_tcp_transport(uvbus_transport_type_t type, uv_loop_t* loop);
extern uvbus_transport_t* create_ipc_transport(uvbus_transport_type_t type, uv_loop_t* loop);
extern uvbus_transport_t* create_inproc_transport(uvbus_transport_type_t type, uv_loop_t* loop);
extern uvbus_transport_t* create_udp_transport(uvbus_transport_type_t type, uv_loop_t* loop);

/* Implementation */

uvbus_config_t* uvbus_config_new(void) {
    uvbus_config_t* config = (uvbus_config_t*)uvrpc_alloc(sizeof(uvbus_config_t));
    if (!config) {
        return NULL;
    }
    
    memset(config, 0, sizeof(uvbus_config_t));
    config->timeout_ms = 30000;  /* Default 30 seconds */
    config->enable_timeout = 0;
    
    return config;
}

void uvbus_config_free(uvbus_config_t* config) {
    if (config) {
        uvrpc_free(config);
    }
}

void uvbus_config_set_loop(uvbus_config_t* config, uv_loop_t* loop) {
    if (config) {
        config->loop = loop;
    }
}

void uvbus_config_set_transport(uvbus_config_t* config, uvbus_transport_type_t transport) {
    if (config) {
        config->transport = transport;
    }
}

void uvbus_config_set_address(uvbus_config_t* config, const char* address) {
    if (config) {
        config->address = address;
    }
}

void uvbus_config_set_recv_callback(uvbus_config_t* config, uvbus_recv_callback_t recv_cb, void* ctx) {
    if (config) {
        config->recv_cb = recv_cb;
        config->callback_ctx = ctx;
    }
}

void uvbus_config_set_connect_callback(uvbus_config_t* config, uvbus_connect_callback_t connect_cb, void* ctx) {
    if (config) {
        config->connect_cb = connect_cb;
        config->callback_ctx = ctx;
    }
}

void uvbus_config_set_close_callback(uvbus_config_t* config, uvbus_close_callback_t close_cb, void* ctx) {
    if (config) {
        config->close_cb = close_cb;
        config->callback_ctx = ctx;
    }
}

void uvbus_config_set_error_callback(uvbus_config_t* config, uvbus_error_callback_t error_cb, void* ctx) {
    if (config) {
        config->error_cb = error_cb;
        config->callback_ctx = ctx;
    }
}

void uvbus_config_set_timeout(uvbus_config_t* config, uint64_t timeout_ms) {
    if (config) {
        config->timeout_ms = timeout_ms;
    }
}

void uvbus_config_set_timeout_enabled(uvbus_config_t* config, int enabled) {
    if (config) {
        config->enable_timeout = enabled;
    }
}

static uvbus_transport_t* create_transport(uvbus_transport_type_t type, uv_loop_t* loop) {
    switch (type) {
        case UVBUS_TRANSPORT_TCP:
            return create_tcp_transport(type, loop);
        case UVBUS_TRANSPORT_UDP:
            return create_udp_transport(type, loop);
        case UVBUS_TRANSPORT_IPC:
            return create_ipc_transport(type, loop);
        case UVBUS_TRANSPORT_INPROC:
            return create_inproc_transport(type, loop);
        default:
            return NULL;
    }
}

uvbus_t* uvbus_server_new(uvbus_config_t* config) {
    if (!config || !config->loop) {
        return NULL;
    }
    
    uvbus_t* bus = (uvbus_t*)uvrpc_alloc(sizeof(uvbus_t));
    if (!bus) {
        return NULL;
    }
    
    memset(bus, 0, sizeof(uvbus_t));
    bus->config = *config;
    bus->is_active = 0;
    
    /* Create transport */
    bus->transport = create_transport(config->transport, config->loop);
    if (!bus->transport) {
        uvrpc_free(bus);
        return NULL;
    }

    bus->transport->parent_bus = bus;
    bus->transport->is_server = 1;
    bus->transport->type = config->transport;
    bus->transport->loop = config->loop;
    
    if (config->address) {
        bus->transport->address = uvrpc_strdup(config->address);
    }
    
    /* Set callbacks */
    bus->transport->recv_cb = config->recv_cb;
    bus->transport->connect_cb = config->connect_cb;
    bus->transport->close_cb = config->close_cb;
    bus->transport->error_cb = config->error_cb;
    bus->transport->callback_ctx = config->callback_ctx;
    
    return bus;
}

uvbus_t* uvbus_client_new(uvbus_config_t* config) {
    if (!config || !config->loop) {
        return NULL;
    }
    
    uvbus_t* bus = (uvbus_t*)uvrpc_alloc(sizeof(uvbus_t));
    if (!bus) {
        return NULL;
    }
    
    memset(bus, 0, sizeof(uvbus_t));
    bus->config = *config;
    bus->is_active = 0;
    
    /* Create transport */
    bus->transport = create_transport(config->transport, config->loop);
    if (!bus->transport) {
        uvrpc_free(bus);
        return NULL;
    }

    bus->transport->parent_bus = bus;
    bus->transport->is_server = 0;
    bus->transport->type = config->transport;
    bus->transport->loop = config->loop;
    
    if (config->address) {
        bus->transport->address = uvrpc_strdup(config->address);
    }
    
    /* Set callbacks */
    bus->transport->recv_cb = config->recv_cb;
    bus->transport->connect_cb = config->connect_cb;
    bus->transport->close_cb = config->close_cb;
    bus->transport->error_cb = config->error_cb;
    bus->transport->callback_ctx = config->callback_ctx;
    
    return bus;
}

uvbus_error_t uvbus_listen(uvbus_t* bus) {
    if (!bus || !bus->transport || !bus->transport->vtable) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    
    if (!bus->transport->is_server) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    
    if (bus->transport->vtable->listen) {
        uvbus_error_t result = bus->transport->vtable->listen(bus->transport, bus->transport->address);
        if (result == UVBUS_OK) {
            bus->is_active = 1;
        }
        return result;
    }
    
    return UVBUS_ERROR;
}

void uvbus_stop(uvbus_t* bus) {
    if (!bus) {
        return;
    }
    
    bus->is_active = 0;
}

uvbus_error_t uvbus_send(uvbus_t* bus, const uint8_t* data, size_t size) {
    if (!bus || !bus->transport || !bus->transport->vtable) {
        return UVBUS_ERROR_INVALID_PARAM;
    }

    if (!bus->is_active) {
        return UVBUS_ERROR_NOT_CONNECTED;
    }

    if (bus->transport->vtable->send) {
        return bus->transport->vtable->send(bus->transport, data, size);
    }

    return UVBUS_ERROR;
}

uvbus_error_t uvbus_send_to(uvbus_t* bus, const uint8_t* data, size_t size, void* client) {
    if (!bus || !bus->transport || !bus->transport->vtable) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    
    if (!bus->is_active) {
        return UVBUS_ERROR_NOT_CONNECTED;
    }
    
    if (bus->transport->vtable->send_to) {
        return bus->transport->vtable->send_to(bus->transport, data, size, client);
    }
    
    return UVBUS_ERROR;
}

uvbus_error_t uvbus_connect(uvbus_t* bus) {
    if (!bus || !bus->transport || !bus->transport->vtable) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    
    if (bus->transport->is_server) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    
    if (bus->transport->vtable->connect) {
        /* Note: Connection is asynchronous, is_connected will be set in the callback */
        return bus->transport->vtable->connect(bus->transport, bus->transport->address);
    }
    
    return UVBUS_ERROR;
}

uvbus_error_t uvbus_connect_with_callback(uvbus_t* bus, uvbus_connect_callback_t callback, void* ctx) {
    if (!bus || !bus->transport || !bus->transport->vtable) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    
    if (bus->transport->is_server) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    
    /* Override callback */
    bus->transport->connect_cb = callback;
    bus->transport->callback_ctx = ctx;
    
    if (bus->transport->vtable->connect) {
        /* Note: Connection is asynchronous, is_active will be set in the callback */
        return bus->transport->vtable->connect(bus->transport, bus->transport->address);
    }
    
    return UVBUS_ERROR;
}

void uvbus_disconnect(uvbus_t* bus) {
    if (!bus || !bus->transport) {
        return;
    }
    
    bus->is_active = 0;
    bus->transport->is_connected = 0;
    
    if (bus->transport->vtable && bus->transport->vtable->disconnect) {
        bus->transport->vtable->disconnect(bus->transport);
    }
}

uvbus_error_t uvbus_client_send(uvbus_t* bus, const uint8_t* data, size_t size) {
    if (!bus || !bus->transport || !bus->transport->vtable) {
        return UVBUS_ERROR_INVALID_PARAM;
    }
    
    if (!bus->transport->is_connected) {
        return UVBUS_ERROR_NOT_CONNECTED;
    }
    
    if (bus->transport->vtable->send) {
        return bus->transport->vtable->send(bus->transport, data, size);
    }
    
    return UVBUS_ERROR;
}

uv_loop_t* uvbus_get_loop(uvbus_t* bus) {
    return bus ? bus->transport->loop : NULL;
}

uvbus_transport_type_t uvbus_get_transport_type(uvbus_t* bus) {
    return bus ? bus->transport->type : UVBUS_TRANSPORT_TCP;
}

const char* uvbus_get_address(uvbus_t* bus) {
    return bus ? bus->transport->address : NULL;
}

int uvbus_is_connected(uvbus_t* bus) {
    return bus ? bus->transport->is_connected : 0;
}

int uvbus_is_server(uvbus_t* bus) {
    return bus ? bus->transport->is_server : 0;
}

void uvbus_free(uvbus_t* bus) {
    if (!bus) {
        return;
    }
    
    /* Stop if active */
    if (bus->is_active) {
        if (bus->transport && bus->transport->is_server) {
            uvbus_stop(bus);
        } else {
            uvbus_disconnect(bus);
        }
    }
    
    /* Free transport */
    if (bus->transport) {
        if (bus->transport->vtable && bus->transport->vtable->free) {
            bus->transport->vtable->free(bus->transport);
        } else {
            if (bus->transport->address) {
                uvrpc_free(bus->transport->address);
            }
            uvrpc_free(bus->transport);
        }
    }
    
    uvrpc_free(bus);
}