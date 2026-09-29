/**
 * @file uvbus_loop_registry.h
 * @brief Registry for the INPROC and SAMELOOP transports (internal).
 *
 * The public entry points live in <uvbus.h>: create a registry, put it on the
 * configs that need to see each other, free it when they are done. This header
 * exposes the layout and the reference counting that the two transports use.
 *
 * Replaces the previous process-global endpoint/server hash tables (and the
 * INPROC rwlock) with a single object the caller owns and shares. Server and
 * client transports are built from independent configs and meet through this
 * registry, so no transport needs a place to keep per-loop state of its own --
 * which keeps ZERO file-scope mutable globals and NO locks (the loop runs
 * single-threaded by the framework's design contract).
 *
 * An earlier version hung this off uv_loop_t::data. That was the only per-loop
 * slot libuv offers, and it cost more than it looked like: telling our
 * registry apart from a caller's pointer meant reading through that pointer,
 * and libuv 1.47 preserves loop->data across uv_loop_init() (it saves and
 * restores it, deps/libuv/src/unix/loop.c:30-38). A loop declared the way
 * libuv's own documentation shows -- `uv_loop_t loop; uv_loop_init(&loop);` --
 * therefore left the stack's leftovers there, and about one run in five
 * faulted on the magic read instead of reporting an error. Handing the registry
 * in removes that whole class of problem: nothing the caller does to their loop
 * can reach it now.
 *
 * Lifetime: transports retain the registry in listen()/connect() and release it
 * in free(), so the object outlives them. The caller frees its own reference
 * with uvbus_loop_registry_free() once the transports are gone.
 */

#ifndef UVBUS_LOOP_REGISTRY_H
#define UVBUS_LOOP_REGISTRY_H

#include <uv.h>
#include <stdint.h>
#include "../include/uvrpc_allocator.h"
#include "../include/uvbus.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Tag stored in the first field, so a pointer that is not ours can be rejected
 * without dereferencing anything the caller does not own. */
#define UVBUS_LOOP_REGISTRY_MAGIC 0x55524300u  /* 'URC\0' */

/* The two transport .c files carry their own endpoint/server structs with a
 * `next` pointer; this registry only stores the list heads and a refcount. */
struct uvbus_loop_registry {
    uint32_t magic;
    int refcount;
    /* INPROC endpoint list head (inproc_endpoint_t*) */
    void* inproc_endpoints;
    /* SAMELOOP server list head (sameloop_server_t*) */
    void* sameloop_servers;
};

/* True if the pointer is a registry this library created. Only called on
 * pointers the caller already owns, so unlike the old magic-on-loop->data check
 * this cannot fault on a wild address. */
static inline int uvbus_loop_registry_is_valid(const uvbus_loop_registry_t* reg) {
    return reg != NULL && reg->magic == UVBUS_LOOP_REGISTRY_MAGIC;
}

/* Take a reference. Transports call this in listen()/connect(). */
static inline uvbus_loop_registry_t* uvbus_loop_registry_retain(uvbus_loop_registry_t* reg) {
    if (!uvbus_loop_registry_is_valid(reg)) return NULL;
    reg->refcount++;
    return reg;
}

/* Drop a reference. Transports call this in free(). */
static inline void uvbus_loop_registry_release(uvbus_loop_registry_t* reg) {
    if (!uvbus_loop_registry_is_valid(reg)) return;
    if (--reg->refcount > 0) return;
    uvrpc_free(reg->inproc_endpoints);
    /* sameloop_servers is a plain linked list; the transports unlink their own
     * entries, so nothing is left to free here. */
    reg->magic = 0;
    uvrpc_free(reg);
}

#ifdef __cplusplus
}
#endif

#endif /* UVBUS_LOOP_REGISTRY_H */
