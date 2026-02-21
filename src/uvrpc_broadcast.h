/**
 * UVRPC Broadcast Message Encoding/Decoding using FlatBuffers
 */

#ifndef UVRPC_BROADCAST_H
#define UVRPC_BROADCAST_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Encode broadcast message */
int uvrpc_broadcast_encode(const char* topic, const uint8_t* data, size_t size,
                           uint8_t** out_msg, size_t* out_msg_size);

/* Decode broadcast message */
int uvrpc_broadcast_decode(const uint8_t* msg, size_t msg_size,
                           char** out_topic, const uint8_t** out_data, size_t* out_data_size);

/* Free decoded topic */
void uvrpc_broadcast_free_decoded(char* topic);

#ifdef __cplusplus
}
#endif

#endif /* UVRPC_BROADCAST_H */