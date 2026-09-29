/**
 * @file uvbus_loop_registry.c
 * @brief Public entry points for the INPROC/SAMELOOP registry.
 */

#include "uvbus_loop_registry.h"

uvbus_loop_registry_t* uvbus_loop_registry_new(void) {
    uvbus_loop_registry_t* reg =
        (uvbus_loop_registry_t*)uvrpc_alloc(sizeof(uvbus_loop_registry_t));
    if (!reg) {
        UVBUS_LOG_ERROR("failed to allocate loop registry");
        return NULL;
    }
    reg->magic = UVBUS_LOOP_REGISTRY_MAGIC;
    reg->refcount = 1;
    reg->inproc_endpoints = NULL;
    reg->sameloop_servers = NULL;
    return reg;
}

void uvbus_loop_registry_free(uvbus_loop_registry_t* reg) {
    if (!uvbus_loop_registry_is_valid(reg)) {
        return;
    }
    uvrpc_free(reg->inproc_endpoints);
    reg->inproc_endpoints = NULL;
    reg->sameloop_servers = NULL;
    reg->magic = 0;
    uvrpc_free(reg);
}
