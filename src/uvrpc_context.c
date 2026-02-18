/**
 * @file uvrpc_context.c
 * @brief UVRPC Context Implementation
 * 
 * Provides functions for managing user contexts attached to UVRPC
 * servers and clients, including automatic cleanup support.
 * 
 * @author UVRPC Team
 * @date 2026
 * @version 1.0
 */

#include "../include/uvrpc.h"
#include "../include/uvrpc_allocator.h"
#include <stdlib.h>
#include <string.h>

/* Create new context */
uvrpc_context_t* uvrpc_context_new(void* data) {
    uvrpc_context_t* ctx = (uvrpc_context_t*)uvrpc_calloc(1, sizeof(uvrpc_context_t));
    if (ctx) {
        ctx->data = data;
    }
    return ctx;
}

/* Create new context with cleanup callback */
uvrpc_context_t* uvrpc_context_new_with_cleanup(void* data, 
                                                  uvrpc_context_cleanup_t cleanup,
                                                  void* cleanup_data) {
    uvrpc_context_t* ctx = uvrpc_context_new(data);
    if (ctx) {
        ctx->cleanup = cleanup;
        ctx->cleanup_data = cleanup_data;
    }
    return ctx;
}

/* Free context */
void uvrpc_context_free(uvrpc_context_t* ctx) {
    if (!ctx) return;
    
    if (ctx->cleanup && ctx->data) {
        ctx->cleanup(ctx->data, ctx->cleanup_data);
    }
    
    uvrpc_free(ctx);
}

/* Get context data */
void* uvrpc_context_get_data(uvrpc_context_t* ctx) {
    return ctx ? ctx->data : NULL;
}
