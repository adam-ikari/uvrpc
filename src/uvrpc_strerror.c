/**
 * UVRPC Error String Conversion
 */

#include "../include/uvrpc.h"

const char* uvrpc_strerror(int error_code) {
    switch (error_code) {
        case UVRPC_OK:
            return "Success";
        case UVRPC_ERROR:
            return "General error";
        case UVRPC_ERROR_INVALID_PARAM:
            return "Invalid parameter provided";
        case UVRPC_ERROR_NO_MEMORY:
            return "Memory allocation failed";
        case UVRPC_ERROR_NOT_CONNECTED:
            return "Not connected to server";
        case UVRPC_ERROR_TIMEOUT:
            return "Operation timed out";
        case UVRPC_ERROR_TRANSPORT:
            return "Transport layer error";
        case UVRPC_ERROR_CALLBACK_LIMIT:
            return "Callback limit exceeded (pending buffer full)";
        case UVRPC_ERROR_CANCELLED:
            return "Operation was cancelled";
        case UVRPC_ERROR_POOL_EXHAUSTED:
            return "Connection pool exhausted";
        case UVRPC_ERROR_RATE_LIMITED:
            return "Rate limit exceeded";
        case UVRPC_ERROR_NOT_FOUND:
            return "Resource not found";
        case UVRPC_ERROR_ALREADY_EXISTS:
            return "Resource already exists";
        case UVRPC_ERROR_INVALID_STATE:
            return "Invalid state for operation";
        case UVRPC_ERROR_IO:
            return "I/O error occurred";
        default:
            return "Unknown error";
    }
}