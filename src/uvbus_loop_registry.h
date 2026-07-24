/**
 * @file uvbus_loop_registry.h
 * @brief Per-loop registry for INPROC and SAMELOOP transports.
 *
 * Replaces the previous process-global endpoint/server hash tables (and the
 * INPROC rwlock) with per-uv_loop_t state attached to loop->data.
 *
 * Rationale: server and client transports for INPROC/SAMELOOP are created from
 * independent configs that share exactly one value — the uv_loop_t*. Hanging a
 * per-loop name->endpoint registry off loop->data lets server and client find
 * each other with ZERO file-scope mutable globals and NO locks (the loop runs
 * single-threaded by the framework's design contract).
 *
 * loop->data is a public libuv field; the framework, tests, and examples never
 * set it. A magic tag distinguishes our registry from a user-set pointer: if
 * loop->data is non-NULL and not our tag, the registry is unavailable for that
 * loop and the transport reports an error (it will not overwrite user data).
 *
 * Lifetime: retain() on listen/connect, release() on transport free. When the
 * refcount reaches zero the registry is freed and loop->data is cleared.
 *
 * Internal header (not part of the public API).
 */

#ifndef UVBUS_LOOP_REGISTRY_H
#define UVBUS_LOOP_REGISTRY_H

#include <uv.h>
#include "../include/uvrpc_allocator.h"
#include "../include/uvbus.h"  /* for UVBUS_LOG_ERROR */

#ifdef __cplusplus
extern "C" {
#endif

/* Magic tag stored in the first field so we can recognize our registry among
 * arbitrary user-set loop->data pointers. */
#define UVBUS_LOOP_REGISTRY_MAGIC 0x55524300u  /* 'URC\0' */

/* Opaque registry type. The two transport .c files carry their own endpoint/
 * server structs with a `next` pointer; this registry only stores the list
 * heads and a refcount. */
typedef struct uvbus_loop_registry {
    uint32_t magic;
    int refcount;
    /* INPROC endpoint list head (inproc_endpoint_t*) */
    void* inproc_endpoints;
    /* SAMELOOP server list head (sameloop_server_t*) */
    void* sameloop_servers;
} uvbus_loop_registry_t;

/* Obtain the per-loop registry, creating it if loop->data is NULL.
 * Returns NULL (and logs) if loop->data is set to something that is not our
 * registry — in that case the caller must fail cleanly rather than overwrite
 * user state. Also increments the refcount. */
static inline uvbus_loop_registry_t* uvbus_loop_registry_retain(uv_loop_t* loop) {
    if (!loop) return NULL;
    uvbus_loop_registry_t* reg = (uvbus_loop_registry_t*)loop->data;
    if (reg) {
        if (reg->magic != UVBUS_LOOP_REGISTRY_MAGIC) {
            UVBUS_LOG_ERROR("loop->data is set to a non-uvrpc pointer; "
                            "INPROC/SAMELOOP transports cannot use this loop");
            return NULL;
        }
        reg->refcount++;
        return reg;
    }
    reg = (uvbus_loop_registry_t*)uvrpc_alloc(sizeof(uvbus_loop_registry_t));
    if (!reg) {
        UVBUS_LOG_ERROR("failed to allocate loop registry");
        return NULL;
    }
    reg->magic = UVBUS_LOOP_REGISTRY_MAGIC;
    reg->refcount = 1;
    reg->inproc_endpoints = NULL;
    reg->sameloop_servers = NULL;
    loop->data = reg;
    return reg;
}

/* Decrement the refcount; when it reaches zero, free the registry and clear
 * loop->data. Safe to call with NULL (e.g. if retain failed). */
static inline void uvbus_loop_registry_release(uv_loop_t* loop) {
    if (!loop) return;
    uvbus_loop_registry_t* reg = (uvbus_loop_registry_t*)loop->data;
    if (!reg || reg->magic != UVBUS_LOOP_REGISTRY_MAGIC) return;
    if (--reg->refcount > 0) return;
    /* All transports on this loop are gone; free the bucket arrays (the
     * endpoint/server lists themselves are already empty — each transport
     * removes its entries on disconnect/free) and clear the slot. */
    uvrpc_free(reg->inproc_endpoints);
    /* sameloop_servers is a plain linked list (no bucket array); nothing to
     * free here beyond the registry itself. */
    loop->data = NULL;
    uvrpc_free(reg);
}

#ifdef __cplusplus
}
#endif

#endif /* UVBUS_LOOP_REGISTRY_H */
