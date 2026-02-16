/**
 * UVRPC Message ID Encoding (32-bit)
 *
 * Design principles:
 * 1. 32-bit integer, compact and efficient
 * 2. Simple increment, no complex encoding needed
 * 3. Uniqueness guaranteed in single-client environment
 * 4. Lock-free design, zero global variables
 */

#ifndef UVRPC_MSGID_H
#define UVRPC_MSGID_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Message ID context */
typedef struct {
    uint32_t next_seq;     /* Next sequence number */
} uvrpc_msgid_ctx_t;

/* Create message ID context */
uvrpc_msgid_ctx_t* uvrpc_msgid_ctx_new(void);
void uvrpc_msgid_ctx_free(uvrpc_msgid_ctx_t* ctx);

/* Set initial sequence number (for multi-client scenarios) */
void uvrpc_msgid_ctx_set_start(uvrpc_msgid_ctx_t* ctx, uint32_t start_seq);

/* Generate next message ID (32-bit) */
uint32_t uvrpc_msgid_next(uvrpc_msgid_ctx_t* ctx);

#ifdef __cplusplus
}
#endif

#endif /* UVRPC_MSGID_H */