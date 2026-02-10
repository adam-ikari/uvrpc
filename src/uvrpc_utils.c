#include "uvrpc_internal.h"

/**
 * 获取错误描述
 * @param error_code 错误码
 * @return 错误描述字符串
 */
const char* uvrpc_strerror(int error_code) {
    switch (error_code) {
        case UVRPC_OK:
            return "Success";
        case UVRPC_ERROR:
            return "General error";
        case UVRPC_ERROR_INVALID_PARAM:
            return "Invalid parameter";
        case UVRPC_ERROR_NO_MEMORY:
            return "Out of memory";
        case UVRPC_ERROR_SERVICE_NOT_FOUND:
            return "Service not found";
        case UVRPC_ERROR_TIMEOUT:
            return "Operation timeout";
        case UVRPC_ERROR_NOT_FOUND:
            return "Not found";
        default:
            return "Unknown error";
    }
}

/**
 * 获取模式名称
 * @param mode RPC 模式
 * @return 模式名称字符串
 */
const char* uvrpc_mode_name(uvrpc_mode_t mode) {
    switch (mode) {
        case UVRPC_MODE_REQ_REP:
            return "REQ_REP";
        case UVRPC_MODE_ROUTER_DEALER:
            return "ROUTER_DEALER";
        case UVRPC_MODE_PUB_SUB:
            return "PUB_SUB";
        case UVRPC_MODE_PUSH_PULL:
            return "PUSH_PULL";
        default:
            return "UNKNOWN";
    }
}