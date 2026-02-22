/* Simple Log Service Demo - Minimal oneway logging example */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>
#include <time.h>
#include "log_logservice_api.h"
#include "include/uvrpc.h"

/* User-implemented handler for log requests */
uvrpc_error_t uvrpc_logservice_handle_request(const char* method_name,
                                                const void* request,
                                                uvrpc_request_t* req) {
    if (strcmp(method_name, "Log") == 0) {
        log_LogEntry_t* entry = log_LogEntry_as_root(request);

        const char* level_str = "UNKNOWN";
        switch (entry->level) {
            case 0: level_str = "DEBUG"; break;
            case 1: level_str = "INFO"; break;
            case 2: level_str = "WARNING"; break;
            case 3: level_str = "ERROR"; break;
            case 4: level_str = "FATAL"; break;
        }

        time_t ts = entry->timestamp;
        char time_str[64];
        strftime(time_str, sizeof(time_str), "%H:%M:%S", localtime(&ts));

        printf("%s [%s] %s: %s\n",
               time_str,
               level_str,
               entry->source ? entry->source : "app",
               entry->message ? entry->message : "");

        /* Send empty response (oneway) */
        flatcc_builder_t builder;
        flatcc_builder_init(&builder);
        log_EmptyResponse_start_as_root(&builder);
        log_EmptyResponse_end_as_root(&builder);

        size_t size;
        void* buf = flatcc_builder_finalize_buffer(&builder, &size);
        uvrpc_error_t ret = uvrpc_request_send_response(req, UVRPC_OK, buf, size);
        free(buf);
        flatcc_builder_clear(&builder);

        return ret;
    }

    return UVRPC_ERROR_NOT_FOUND;
}

/* Client connection callback */
void on_client_connect(uvrpc_client_t* client, uvrpc_error_t status, void* ctx) {
    if (status == UVRPC_OK) {
        printf("\n--- Sending Log Messages ---\n");

        /* Send three oneway log messages using struct */
        log_LogEntry_t entry1 = {
            .level = 1,  /* INFO */
            .message = "Application started",
            .timestamp = time(NULL),
            .source = "main",
            .thread_id = 0,
            .component = "app"
        };
        uvrpc_logservice_Log(client, &entry1);

        log_LogEntry_t entry2 = {
            .level = 2,  /* WARNING */
            .message = "Configuration warning",
            .timestamp = time(NULL),
            .source = "config",
            .thread_id = 0,
            .component = "app"
        };
        uvrpc_logservice_Log(client, &entry2);

        log_LogEntry_t entry3 = {
            .level = 3,  /* ERROR */
            .message = "Connection error",
            .timestamp = time(NULL),
            .source = "network",
            .thread_id = 0,
            .component = "app"
        };
        uvrpc_logservice_Log(client, &entry3);

        printf("All oneway logs sent (fire-and-forget)\n");
        uv_stop(uvrpc_client_get_loop(client));
    }
}

int main() {
    uv_loop_t loop;
    uv_loop_init(&loop);

    /* Create server */
    printf("Starting Log Service...\n");
    uvrpc_server_t* server = uvrpc_logservice_create_server(&loop, "tcp://127.0.0.1:6666");
    uvrpc_logservice_start_server(server);

    /* Create client */
    uvrpc_client_t* client = uvrpc_logservice_create_client(
        &loop,
        "tcp://127.0.0.1:6666",
        on_client_connect,
        NULL
    );

    /* Run */
    uv_run(&loop, UV_RUN_DEFAULT);

    /* Cleanup */
    uvrpc_logservice_free_client(client);
    uvrpc_logservice_stop_server(server);
    uvrpc_logservice_free_server(server);
    uv_loop_close(&loop);

    return 0;
}