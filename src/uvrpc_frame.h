/**
 * UVRPC Frame Encoding/Decoding using FlatCC
 */

#ifndef UVRPC_FRAME_H
#define UVRPC_FRAME_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Frame encoding functions */
int uvrpc_encode_request(uint32_t msgid, const char* method,
                         const uint8_t* params, size_t params_size,
                         uint8_t** out_data, size_t* out_size);

int uvrpc_encode_response(uint32_t msgid, int32_t error_code,
                          const uint8_t* result, size_t result_size,
                          uint8_t** out_data, size_t* out_size);

/* Frame decoding functions */
int uvrpc_decode_request(const uint8_t* data, size_t size,
                         uint32_t* out_msgid, char** out_method,
                         const uint8_t** out_params, size_t* out_params_size);

int uvrpc_decode_response(const uint8_t* data, size_t size,
                          uint32_t* out_msgid, int32_t* out_error_code,
                          const uint8_t** out_result, size_t* out_result_size);

/* Free decoded data */
void uvrpc_free_decoded(char* method_or_error);

#ifdef __cplusplus
}
#endif

#endif /* UVRPC_FRAME_H */