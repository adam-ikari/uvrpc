/**
 * UVRPC msgpack Serialization Interface
 * Uses unified RpcFrame format: type, msgid, method, params
 */

#ifndef UVRPC_MSGPACK_H
#define UVRPC_MSGPACK_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

char* uvrpc_pack_request(uint32_t msgid, const char* method, 
                          const uint8_t* data, size_t data_size, size_t* out_size);
char* uvrpc_pack_response(uint32_t msgid, int32_t error_code, const uint8_t* data, size_t data_size, size_t* out_size);
int uvrpc_unpack_request(const char* buffer, uint32_t* msgid, char** method,
                          uint8_t** data, size_t* data_size);
int uvrpc_unpack_response(const char* buffer, uint32_t* msgid, int32_t* error_code, 
                           uint8_t** data, size_t* data_size);

#ifdef __cplusplus
}
#endif

#endif /* UVRPC_MSGPACK_H */