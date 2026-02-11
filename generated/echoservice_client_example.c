/**
 * Auto-generated client example for EchoService
 * Generated from /home/zhaodi-chen/project/uvrpc/examples/echo_service.yaml
 */

#include "../include/uvrpc.h"
#include "echoservice_gen.h"
#include <stdio.h>
#include <string.h>
#include <mpack.h>

/* echo call */
void EchoService_echo_Call(uvrpc_client_t* client) {
    printf("[Client] Calling echo\n");

    /* Create request */
    EchoService_echo_Request_t request;
    memset(&request, 0, sizeof(EchoService_echo_Request_t));

    request.message = strdup("sample_message");

    /* Serialize request */
    uint8_t* request_data = NULL;
    size_t request_size = 0;
    if (EchoService_echo_SerializeRequest(&request, &request_data, &request_size) != 0) {
        fprintf(stderr, "Failed to serialize echo request\n");
        EchoService_echo_FreeRequest(&request);
        return;
    }

    /* Call service */
    if (uvrpc_client_call(client, "EchoService.echo", "echo",
                          request_data, request_size,
                          EchoService_echo_ResponseCallback, NULL) != UVRPC_OK) {
        fprintf(stderr, "Failed to call echo\n");
    }

    /* Cleanup */
    free(request_data);
    EchoService_echo_FreeRequest(&request);
}

/* add call */
void EchoService_add_Call(uvrpc_client_t* client) {
    printf("[Client] Calling add\n");

    /* Create request */
    EchoService_add_Request_t request;
    memset(&request, 0, sizeof(EchoService_add_Request_t));

    request.a = 3.14;
    request.b = 3.14;

    /* Serialize request */
    uint8_t* request_data = NULL;
    size_t request_size = 0;
    if (EchoService_add_SerializeRequest(&request, &request_data, &request_size) != 0) {
        fprintf(stderr, "Failed to serialize add request\n");
        EchoService_add_FreeRequest(&request);
        return;
    }

    /* Call service */
    if (uvrpc_client_call(client, "EchoService.add", "add",
                          request_data, request_size,
                          EchoService_add_ResponseCallback, NULL) != UVRPC_OK) {
        fprintf(stderr, "Failed to call add\n");
    }

    /* Cleanup */
    free(request_data);
    EchoService_add_FreeRequest(&request);
}

/* get_info call */
void EchoService_getInfo_Call(uvrpc_client_t* client) {
    printf("[Client] Calling get_info\n");

    /* Create request */
    EchoService_getInfo_Request_t request;
    memset(&request, 0, sizeof(EchoService_getInfo_Request_t));


    /* Serialize request */
    uint8_t* request_data = NULL;
    size_t request_size = 0;
    if (EchoService_getInfo_SerializeRequest(&request, &request_data, &request_size) != 0) {
        fprintf(stderr, "Failed to serialize get_info request\n");
        EchoService_getInfo_FreeRequest(&request);
        return;
    }

    /* Call service */
    if (uvrpc_client_call(client, "EchoService.get_info", "get_info",
                          request_data, request_size,
                          EchoService_getInfo_ResponseCallback, NULL) != UVRPC_OK) {
        fprintf(stderr, "Failed to call get_info\n");
    }

    /* Cleanup */
    free(request_data);
    EchoService_getInfo_FreeRequest(&request);
}

/* echo response callback */
void EchoService_echo_ResponseCallback(void* ctx, int status,
                            const uint8_t* response_data,
                            size_t response_size) {
    (void)ctx;

    if (status != UVRPC_OK) {
        fprintf(stderr, "echo call failed: %s\n", uvrpc_strerror(status));
        return;
    }

    /* Deserialize response */
    EchoService_echo_Response_t response;
    if (EchoService_echo_DeserializeResponse(response_data, response_size, &response) != 0) {
        fprintf(stderr, "Failed to deserialize echo response\n");
        return;
    }

    printf("[Client] Received echo response\n");

    /* Process response (TODO: handle response data) */

    /* Cleanup */
    EchoService_echo_FreeResponse(&response);
}

/* add response callback */
void EchoService_add_ResponseCallback(void* ctx, int status,
                            const uint8_t* response_data,
                            size_t response_size) {
    (void)ctx;

    if (status != UVRPC_OK) {
        fprintf(stderr, "add call failed: %s\n", uvrpc_strerror(status));
        return;
    }

    /* Deserialize response */
    EchoService_add_Response_t response;
    if (EchoService_add_DeserializeResponse(response_data, response_size, &response) != 0) {
        fprintf(stderr, "Failed to deserialize add response\n");
        return;
    }

    printf("[Client] Received add response\n");

    /* Process response (TODO: handle response data) */

    /* Cleanup */
    EchoService_add_FreeResponse(&response);
}

/* get_info response callback */
void EchoService_getInfo_ResponseCallback(void* ctx, int status,
                            const uint8_t* response_data,
                            size_t response_size) {
    (void)ctx;

    if (status != UVRPC_OK) {
        fprintf(stderr, "get_info call failed: %s\n", uvrpc_strerror(status));
        return;
    }

    /* Deserialize response */
    EchoService_getInfo_Response_t response;
    if (EchoService_getInfo_DeserializeResponse(response_data, response_size, &response) != 0) {
        fprintf(stderr, "Failed to deserialize get_info response\n");
        return;
    }

    printf("[Client] Received get_info response\n");

    /* Process response (TODO: handle response data) */

    /* Cleanup */
    EchoService_getInfo_FreeResponse(&response);
}

int main(int argc, char** argv) {
    const char* server_addr = (argc > 1) ? argv[1] : "tcp://127.0.0.1:5555";

    printf("Starting EchoService Client connecting to %s\n", server_addr);

    /* Create libuv event loop */
    uv_loop_t* loop = uv_default_loop();

    /* Create RPC config */
    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, loop);
    uvrpc_config_set_address(config, server_addr);
    uvrpc_config_set_transport(config, UVRPC_TRANSPORT_TCP);
    uvrpc_config_set_mode(config, UVRPC_SERVER_CLIENT);
    uvrpc_config_set_hwm(config, 10000, 10000);

    /* Create RPC client */
    uvrpc_client_t* client = uvrpc_client_create(config);
    if (!client) {
        fprintf(stderr, "Failed to create client\n");
        uvrpc_config_free(config);
        return 1;
    }

    /* Connect to server */
    if (uvrpc_client_connect(client) != UVRPC_OK) {
        fprintf(stderr, "Failed to connect to server\n");
        uvrpc_client_free(client);
        uvrpc_config_free(config);
        return 1;
    }

    printf("EchoService Client connected to server\n");

    /* Call services */
    EchoService_echo_Call(client);
    EchoService_add_Call(client);
    EchoService_getInfo_Call(client);

    printf("EchoService Client sent requests, waiting for responses...\n");

    /* Run event loop */
    uv_run(loop, UV_RUN_DEFAULT);

    /* Cleanup */
    uvrpc_client_free(client);
    uvrpc_config_free(config);
    uv_loop_close(loop);

    printf("EchoService Client stopped\n");

    return 0;
}
