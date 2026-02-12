/**
 * UVRPC Configuration Management
 */

#include "../include/uvrpc.h"
#include <stdlib.h>
#include <string.h>

uvrpc_config_t* uvrpc_config_new(void) {
    uvrpc_config_t* config = (uvrpc_config_t*)calloc(1, sizeof(uvrpc_config_t));
    if (!config) return NULL;
    
    config->loop = NULL;
    config->address = NULL;
    config->transport = UVRPC_TRANSPORT_TCP;  /* Default to TCP */
    
    return config;
}

void uvrpc_config_free(uvrpc_config_t* config) {
    if (!config) return;
    
    if (config->address) {
        free(config->address);
    }
    
    free(config);
}

uvrpc_config_t* uvrpc_config_set_loop(uvrpc_config_t* config, uv_loop_t* loop) {
    if (!config) return NULL;
    config->loop = loop;
    return config;
}

uvrpc_config_t* uvrpc_config_set_address(uvrpc_config_t* config, const char* address) {
    if (!config || !address) return NULL;
    
    if (config->address) {
        free(config->address);
    }
    
    config->address = strdup(address);
    
    /* Auto-detect transport type from address prefix */
    if (strncmp(address, "inproc://", 9) == 0) {
        config->transport = UVRPC_TRANSPORT_INPROC;
    } else if (strncmp(address, "ipc://", 6) == 0) {
        config->transport = UVRPC_TRANSPORT_IPC;
    } else if (strncmp(address, "tcp://", 6) == 0) {
        config->transport = UVRPC_TRANSPORT_TCP;
    } else if (strncmp(address, "udp://", 6) == 0) {
        config->transport = UVRPC_TRANSPORT_UDP;
    }
    /* For addresses without prefix, keep the current/transport default */
    
    return config;
}

uvrpc_config_t* uvrpc_config_set_transport(uvrpc_config_t* config, uvrpc_transport_type transport) {
    if (!config) return NULL;
    config->transport = transport;
    return config;
}

uvrpc_config_t* uvrpc_config_set_comm_type(uvrpc_config_t* config, uvrpc_comm_type_t comm_type) {
    if (!config) return NULL;
    config->comm_type = comm_type;
    return config;
}