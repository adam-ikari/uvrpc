#ifndef ECHO_HANDLERS_H
#define ECHO_HANDLERS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "echo_service.h"

/**
 * echo方法handler声明
 */
int echo_service_echo_handler(
    const char* request_str,
    char** response_str
);

/**
 * process方法handler声明
 */
int echo_service_process_handler(
    const echo_service_process_request_t* request,
    echo_service_process_response_t* response
);

/**
 * 注册所有handlers到服务
 * 
 * @param server 服务实例
 */
void echo_service_register_all_handlers(uvrpc_server_t* server);

#ifdef __cplusplus
}
#endif

#endif /* ECHO_HANDLERS_H */
