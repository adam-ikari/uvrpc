/**
 * UVRPC msgpack Serialization Interface
 */

#ifndef UVRPC_MSGPACK_H
#define UVRPC_MSGPACK_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

char* uvrpc_pack_request(const char* service, const char* method, 
                          const uint8_t* data, size_t data_size, size_t* out_size);
char* uvrpc_pack_response(int status, const uint8_t* data, size_t data_size, size_t* out_size);
int uvrpc_unpack_request(const char* buffer, size_t size,
                         char** service, char** method,
                         const uint8_t** data, size_t* data_size);
int uvrpc_unpack_response(const char* buffer, size_t size,
                          int* status, const uint8_t** data, size_t* data_size);

#ifdef __cplusplus
}
#endif

#endif /* UVRPC_MSGPACK_H */