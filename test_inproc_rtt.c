#include "include/uvrpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static volatile int response_received = 0;

void echo_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    uvrpc_request_send_response(req, UVRPC_OK, req->params, req->params_size);
}

void on_connect(int status, void* ctx) {
    printf("Connected: %d\n", status);
}

void on_response(uvrpc_response_t* resp, void* ctx) {
    response_received = 1;
}

int main() {
    uv_loop_t loop;
    uv_loop_init(&loop);

    printf("=== INPROC Round-Trip Time Test ===\n");

    /* Create server */
    uvrpc_config_t* server_config = uvrpc_config_new();
    uvrpc_config_set_loop(server_config, &loop);
    uvrpc_config_set_address(server_config, "inproc://rtt_test");
    uvrpc_config_set_comm_type(server_config, UVRPC_COMM_SERVER_CLIENT);

    uvrpc_server_t* server = uvrpc_server_create(server_config);
    uvrpc_server_register(server, "echo", echo_handler, NULL);
    uvrpc_server_start(server);

    /* Create client */
    uvrpc_config_t* client_config = uvrpc_config_new();
    uvrpc_config_set_loop(client_config, &loop);
    uvrpc_config_set_address(client_config, "inproc://rtt_test");
    uvrpc_config_set_comm_type(client_config, UVRPC_COMM_SERVER_CLIENT);

    uvrpc_client_t* client = uvrpc_client_create(client_config);
    uvrpc_client_connect_with_callback(client, on_connect, NULL);

    /* Wait for connection */
    for (int i = 0; i < 50; i++) {
        uv_run(&loop, UV_RUN_ONCE);
    }

    /* Measure RTT for 100 requests */
    const int ITERATIONS = 100;
    const char* test_data = "Hello";
    double total_rtt = 0;
    int successful = 0;

    for (int i = 0; i < ITERATIONS; i++) {
        response_received = 0;

        struct timespec start, end;
        clock_gettime(CLOCK_MONOTONIC, &start);

        uvrpc_client_call(client, "echo",
                         (uint8_t*)test_data, strlen(test_data),
                         on_response, NULL);

        /* Wait for response */
        for (int j = 0; j < 100 && !response_received; j++) {
            uv_run(&loop, UV_RUN_ONCE);
        }

        clock_gettime(CLOCK_MONOTONIC, &end);

        if (response_received) {
            double rtt = (end.tv_sec - start.tv_sec) +
                        (end.tv_nsec - start.tv_nsec) / 1e9;
            total_rtt += rtt;
            successful++;
        }

        /* Small delay between requests */
        usleep(1000);
    }

    if (successful > 0) {
        double avg_rtt = total_rtt / successful;
        printf("\n=== Results ===\n");
        printf("Successful: %d/%d\n", successful, ITERATIONS);
        printf("Average RTT: %.6f ms\n", avg_rtt * 1000);
        printf("Estimated throughput: %.0f ops/s\n", 1.0 / avg_rtt);
    }

    /* Cleanup */
    uvrpc_client_free(client);
    uvrpc_server_free(server);
    uvrpc_config_free(client_config);
    uvrpc_config_free(server_config);
    uv_loop_close(&loop);

    return 0;
}