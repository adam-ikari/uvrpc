/**
 * @file uvrpc_config.c
 * @brief UVRPC Configuration Management
 * 
 * Provides functions for creating, configuring, and freeing UVRPC
 * configuration structures.
 * 
 * @author UVRPC Team
 * @date 2026
 * @version 1.0
 */

#include "../include/uvrpc.h"
#include "../include/uvrpc_allocator.h"
#include <stdlib.h>
#include <string.h>

uvrpc_config_t* uvrpc_config_new(void) {
    uvrpc_config_t* config = (uvrpc_config_t*)uvrpc_calloc(1, sizeof(uvrpc_config_t));
    if (!config) return NULL;

    config->loop = NULL;
    config->address = NULL;
    config->transport = UVRPC_TRANSPORT_TCP;  /* Default to TCP */
    config->performance_mode = UVRPC_PERF_LOW_LATENCY;  /* Default to low latency */
    config->pool_size = UVRPC_DEFAULT_POOL_SIZE;
    config->max_concurrent = UVRPC_MAX_CONCURRENT_REQUESTS;
    config->max_pending_callbacks = UVRPC_MAX_PENDING_CALLBACKS;
    config->timeout_ms = 0;
    config->msgid_offset = 0;  /* Default: 0 = auto-assign */

    return config;
}

void uvrpc_config_free(uvrpc_config_t* config) {
    if (!config) return;

    if (config->address) {
        uvrpc_free(config->address);
    }

    uvrpc_free(config);
}

uvrpc_config_t* uvrpc_config_set_loop(uvrpc_config_t* config, uv_loop_t* loop) {
    if (!config) return NULL;
    config->loop = loop;
    return config;
}

uvrpc_config_t* uvrpc_config_set_address(uvrpc_config_t* config, const char* address) {
    if (!config || !address) return NULL;

    if (config->address) {
        uvrpc_free(config->address);
    }

    config->address = uvrpc_strdup(address);
    if (!config->address) {
        return NULL;
    }

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

uvrpc_config_t* uvrpc_config_set_performance_mode(uvrpc_config_t* config, uvrpc_perf_mode_t mode) {
    if (!config) return NULL;
    config->performance_mode = mode;
    return config;
}

uvrpc_config_t* uvrpc_config_set_pool_size(uvrpc_config_t* config, int pool_size) {
    if (!config) return NULL;
    config->pool_size = (pool_size > 0) ? pool_size : UVRPC_DEFAULT_POOL_SIZE;
    return config;
}

uvrpc_config_t* uvrpc_config_set_max_concurrent(uvrpc_config_t* config, int max_concurrent) {
    if (!config) return NULL;
    config->max_concurrent = (max_concurrent > 0) ? max_concurrent : UVRPC_MAX_CONCURRENT_REQUESTS;
    return config;
}

uvrpc_config_t* uvrpc_config_set_max_pending_callbacks(uvrpc_config_t* config, int max_pending) {
    if (!config) return NULL;
    /* Validate that max_pending is a power of 2 */
    if (max_pending > 0 && (max_pending & (max_pending - 1)) == 0) {
        config->max_pending_callbacks = max_pending;
    } else {
        /* Use default value if invalid */
        config->max_pending_callbacks = UVRPC_MAX_PENDING_CALLBACKS;
    }
    return config;
}

uvrpc_config_t* uvrpc_config_set_timeout(uvrpc_config_t* config, uint64_t timeout_ms) {
    if (!config) return NULL;
    config->timeout_ms = timeout_ms;
    return config;
}

uvrpc_config_t* uvrpc_config_set_msgid_offset(uvrpc_config_t* config, uint32_t msgid_offset) {
    if (!config) return NULL;
    config->msgid_offset = msgid_offset;
    return config;
}