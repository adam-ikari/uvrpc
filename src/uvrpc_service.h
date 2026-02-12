/**
 * UVRPC Service Registry Interface
 */

#ifndef UVRPC_SERVICE_H
#define UVRPC_SERVICE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Pack service definition to FlatCC buffer */
char* uvrpc_pack_service_def(const char* name, const char** methods, 
                              int method_count, size_t* out_size);

/* Unpack service definition from FlatCC buffer */
int uvrpc_unpack_service_def(const char* buffer, size_t size,
                             char** name, char*** methods, int* method_count);

/* Free unpacked service definition */
void uvrpc_free_service_def(char* name, char** methods, int method_count);

#ifdef __cplusplus
}
#endif

#endif /* UVRPC_SERVICE_H */