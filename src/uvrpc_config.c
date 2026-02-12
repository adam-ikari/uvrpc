/**
 * UVRPC Configuration Module
 */

#include "uvrpc.h"
#include <stdlib.h>
#include <string.h>


uvrpc_config_t* uvrpc_config_new(void) {
    uvrpc_config_t* config = (uvrpc_config_t*)calloc(1, sizeof(uvrpc_config_t));
    return config;
}

void uvrpc_config_free(uvrpc_config_t* config) {
    if (!config) return;
    if (config->address) free(config->address);
    free(config);
}

uvrpc_config_t* uvrpc_config_set_loop(uvrpc_config_t* config, uv_loop_t* loop) {
    if (!config) return NULL;
    config->loop = loop;
    return config;
}

uvrpc_config_t* uvrpc_config_set_address(uvrpc_config_t* config, const char* address) {
    if (!config || !address) return NULL;
    if (config->address) free(config->address);
    config->address = strdup(address);
    return config;
}