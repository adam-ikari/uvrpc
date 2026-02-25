/**
 * @file uv_transport.c
 * @brief Unified Transport Layer Implementation
 */

#include "../include/uv_transport.h"
#include "../include/uvrpc_allocator.h"
#include <string.h>

/* Forward declarations for transport-specific create functions */
extern uv_transport_t* uv_transport_tcp_create(const uv_transport_config_t* config);
extern uv_transport_t* uv_transport_udp_create(const uv_transport_config_t* config);
extern uv_transport_t* uv_transport_ipc_create(const uv_transport_config_t* config);
extern uv_transport_t* uv_transport_inproc_create(const uv_transport_config_t* config);

/* Generic create function */
uv_transport_t* uv_transport_create(uv_transport_type_t type, const uv_transport_config_t* config) {
    if (!config || !config->loop || !config->address) {
        return NULL;
    }
    
    switch (type) {
        case UV_TRANSPORT_TCP:
            return uv_transport_tcp_create(config);
        case UV_TRANSPORT_UDP:
            return uv_transport_udp_create(config);
        case UV_TRANSPORT_IPC:
            return uv_transport_ipc_create(config);
        case UV_TRANSPORT_INPROC:
            return uv_transport_inproc_create(config);
        default:
            return NULL;
    }
}

/* Generic listen function */
int uv_transport_listen(uv_transport_t* transport) {
    if (!transport || !transport->vtable) {
        return UV_TRANSPORT_ERROR_INVALID_PARAM;
    }
    
    if (!transport->vtable->listen) {
        return UV_TRANSPORT_ERROR_NOT_SUPPORTED;
    }
    
    return transport->vtable->listen(transport);
}

/* Generic connect function */
int uv_transport_connect(uv_transport_t* transport) {
    if (!transport || !transport->vtable) {
        return UV_TRANSPORT_ERROR_INVALID_PARAM;
    }
    
    if (!transport->vtable->connect) {
        return UV_TRANSPORT_ERROR_NOT_SUPPORTED;
    }
    
    return transport->vtable->connect(transport);
}

/* Generic stop function */
void uv_transport_stop(uv_transport_t* transport) {
    if (!transport || !transport->vtable) {
        return;
    }
    
    if (transport->vtable->stop) {
        transport->vtable->stop(transport);
    }
}

/* Generic disconnect function */
void uv_transport_disconnect(uv_transport_t* transport) {
    if (!transport || !transport->vtable) {
        return;
    }
    
    if (transport->vtable->disconnect) {
        transport->vtable->disconnect(transport);
    }
}

/* Generic send function */
int uv_transport_send(uv_transport_t* transport, const uint8_t* data, size_t size) {
    if (!transport || !transport->vtable) {
        return UV_TRANSPORT_ERROR_INVALID_PARAM;
    }
    
    if (!transport->vtable->send) {
        return UV_TRANSPORT_ERROR_NOT_SUPPORTED;
    }
    
    return transport->vtable->send(transport, data, size);
}

/* Generic send_to function */
int uv_transport_send_to(uv_transport_t* transport, const uint8_t* data, size_t size, void* peer) {
    if (!transport || !transport->vtable) {
        return UV_TRANSPORT_ERROR_INVALID_PARAM;
    }
    
    if (!transport->vtable->send_to) {
        return UV_TRANSPORT_ERROR_NOT_SUPPORTED;
    }
    
    return transport->vtable->send_to(transport, data, size, peer);
}

/* Generic destroy function */
void uv_transport_destroy(uv_transport_t* transport) {
    if (!transport) {
        return;
    }
    
    if (transport->vtable && transport->vtable->free) {
        transport->vtable->free(transport);
    } else {
        /* No free function, just free the structure */
        uvrpc_free(transport);
    }
}

/* Generic is_connected function */
int uv_transport_is_connected(uv_transport_t* transport) {
    /* TODO: Implement for each transport type */
    (void)transport;
    return 0;
}

/* Generic get_type function */
uv_transport_type_t uv_transport_get_type(uv_transport_t* transport) {
    if (!transport) {
        return UV_TRANSPORT_TCP;  /* Default */
    }
    
    return transport->type;
}

/* Generic get_address function */
const char* uv_transport_get_address(uv_transport_t* transport) {
    /* TODO: Implement for each transport type */
    (void)transport;
    return NULL;
}