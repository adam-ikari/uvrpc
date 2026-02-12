/**
 * UVRPC Service Definition (placeholder for future use)
 */

#include "uvrpc_service.h"
#include <stdlib.h>
#include <string.h>

char* uvrpc_pack_service_def(const char* name, const char** methods,
                              int method_count, size_t* out_size) {
    /* Placeholder - Service definition not currently implemented */
    (void)name;
    (void)methods;
    (void)method_count;
    (void)out_size;
    return NULL;
}

int uvrpc_unpack_service_def(const char* buffer, size_t size,
                              char** name, char*** methods,
                              int* method_count) {
    /* Placeholder - Service definition not currently implemented */
    (void)buffer;
    (void)size;
    (void)name;
    (void)methods;
    (void)method_count;
    return -1;
}