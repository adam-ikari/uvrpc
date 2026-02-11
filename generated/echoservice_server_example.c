/**
 * Auto-generated server example for EchoService
 * Generated from /home/zhaodi-chen/project/uvrpc/examples/echo_service.yaml
 */

#include "../include/uvrpc.h"
#include "../src/uvrpc_internal.h"
#include "echoservice_gen.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <mpack.h>

/* echo handler */
int EchoService_echo_Handler(void* ctx,
                 const uint8_t* request_data,
                 size_t request_size,
                 uint8_t** response_data,
                 size_t* response_size) {
    (void)ctx;

    /* Deserialize request */
    EchoService_echo_Request_t request;
    if (EchoService_echo_DeserializeRequest(request_data, request_size, &request) != 0) {
        fprintf(stderr, "Failed to deserialize echo request\n");
        return UVRPC_ERROR;
    }

    printf("[Server] Received echo request\n");

    /* Process request (TODO: implement your business logic here) */
    EchoService_echo_Response_t response;
    memset(&response, 0, sizeof(EchoService_echo_Response_t));

    response.echo = strdup("sample_echo");
    response.timestamp = 42;

    /* Serialize response */
    if (EchoService_echo_SerializeResponse(&response, response_data, response_size) != 0) {
        fprintf(stderr, "Failed to serialize echo response\n");
        EchoService_echo_FreeRequest(&request);
        EchoService_echo_FreeResponse(&response);
        return UVRPC_ERROR;
    }

    printf("[Server] Sent echo response\n");

    /* Cleanup */
    EchoService_echo_FreeRequest(&request);
    EchoService_echo_FreeResponse(&response);

    return UVRPC_OK;
}

/* add handler */
int EchoService_add_Handler(void* ctx,
                 const uint8_t* request_data,
                 size_t request_size,
                 uint8_t** response_data,
                 size_t* response_size) {
    (void)ctx;

    /* Deserialize request */
    EchoService_add_Request_t request;
    if (EchoService_add_DeserializeRequest(request_data, request_size, &request) != 0) {
        fprintf(stderr, "Failed to deserialize add request\n");
        return UVRPC_ERROR;
    }

    printf("[Server] Received add request\n");

    /* Process request (TODO: implement your business logic here) */
    EchoService_add_Response_t response;
    memset(&response, 0, sizeof(EchoService_add_Response_t));

    response.result = 3.14;

    /* Serialize response */
    if (EchoService_add_SerializeResponse(&response, response_data, response_size) != 0) {
        fprintf(stderr, "Failed to serialize add response\n");
        EchoService_add_FreeRequest(&request);
        EchoService_add_FreeResponse(&response);
        return UVRPC_ERROR;
    }

    printf("[Server] Sent add response\n");

    /* Cleanup */
    EchoService_add_FreeRequest(&request);
    EchoService_add_FreeResponse(&response);

    return UVRPC_OK;
}

/* get_info handler */
int EchoService_getInfo_Handler(void* ctx,
                 const uint8_t* request_data,
                 size_t request_size,
                 uint8_t** response_data,
                 size_t* response_size) {
    (void)ctx;

    /* Deserialize request */
    EchoService_getInfo_Request_t request;
    if (EchoService_getInfo_DeserializeRequest(request_data, request_size, &request) != 0) {
        fprintf(stderr, "Failed to deserialize get_info request\n");
        return UVRPC_ERROR;
    }

    printf("[Server] Received get_info request\n");

    /* Process request (TODO: implement your business logic here) */
    EchoService_getInfo_Response_t response;
    memset(&response, 0, sizeof(EchoService_getInfo_Response_t));

    response.service = strdup("sample_service");
    response.version = strdup("sample_version");
    response.uptime = 42;

    /* Serialize response */
    if (EchoService_getInfo_SerializeResponse(&response, response_data, response_size) != 0) {
        fprintf(stderr, "Failed to serialize get_info response\n");
        EchoService_getInfo_FreeRequest(&request);
        EchoService_getInfo_FreeResponse(&response);
        return UVRPC_ERROR;
    }

    printf("[Server] Sent get_info response\n");

    /* Cleanup */
    EchoService_getInfo_FreeRequest(&request);
    EchoService_getInfo_FreeResponse(&response);

    return UVRPC_OK;
}

int main(int argc, char** argv) {
    const char* bind_addr = (argc > 1) ? argv[1] : "tcp://127.0.0.1:5555";

    printf("Starting EchoService Server on %s\n", bind_addr);

    /* Create libuv event loop */
    uv_loop_t* loop = uv_default_loop();

    /* Create ZMQ context */
    void* zmq_ctx = zmq_ctx_new();

    /* Create RPC server config */
    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, loop);
    uvrpc_config_set_address(config, bind_addr);
    uvrpc_config_set_transport(config, UVRPC_TRANSPORT_TCP);
    uvrpc_config_set_mode(config, UVRPC_SERVER_CLIENT);
    uvrpc_config_set_zmq_ctx(config, zmq_ctx);
    uvrpc_config_set_hwm(config, 10000, 10000);

    /* Create RPC server */
    uvrpc_server_t* server = uvrpc_server_create(config);
    if (!server) {
        fprintf(stderr, "Failed to create server\n");
        uvrpc_config_free(config);
        zmq_ctx_term(zmq_ctx);
        return 1;
    }

/* Register services */
    /* Register methods directly with full qualified names to avoid dispatcher overhead */
    if (uvrpc_server_register_service(server, "EchoService.echo", EchoService_echo_Handler, NULL) != UVRPC_OK) {
        fprintf(stderr, "Failed to register EchoService.echo service\n");
        uvrpc_server_free(server);
        uvrpc_config_free(config);
        zmq_ctx_term(zmq_ctx);
        return 1;
    }
    if (uvrpc_server_register_service(server, "EchoService.add", EchoService_add_Handler, NULL) != UVRPC_OK) {
        fprintf(stderr, "Failed to register EchoService.add service\n");
        uvrpc_server_free(server);
        uvrpc_config_free(config);
        zmq_ctx_term(zmq_ctx);
        return 1;
    }
    if (uvrpc_server_register_service(server, "EchoService.get_info", EchoService_getInfo_Handler, NULL) != UVRPC_OK) {
        fprintf(stderr, "Failed to register EchoService.get_info service\n");
        uvrpc_server_free(server);
        uvrpc_config_free(config);
        zmq_ctx_term(zmq_ctx);
        return 1;
    }

    /* Start server */

    printf("EchoService Server is running...\n");
    printf("Press Ctrl+C to stop\n");

    /* Run event loop */
    uv_run(loop, UV_RUN_DEFAULT);

    /* Cleanup */
    uvrpc_server_free(server);
    uvrpc_config_free(config);
    zmq_ctx_term(zmq_ctx);
    uv_loop_close(loop);

    printf("EchoService Server stopped\n");

    return 0;
}
