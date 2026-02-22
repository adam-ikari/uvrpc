/* Log Service Demo - Demonstrates oneway logging RPC */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>
#include <time.h>
#include <pthread.h>
#include "log_logservice_api.h"
#include "include/uvrpc.h"

/* User-implemented handler for all log requests */
uvrpc_error_t uvrpc_logservice_handle_request(const char* method_name,
                                                const void* request,
                                                uvrpc_request_t* req) {
    if (strcmp(method_name, "Log") == 0) {
        /* Handle single log entry */
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
        struct tm* tm_info = localtime(&ts);
        char time_str[64];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);

        printf("[%s] [%s] [%s] [Thread:%lu] %s: %s\n",
               time_str,
               level_str,
               entry->source ? entry->source : "unknown",
               (unsigned long)entry->thread_id,
               entry->component ? entry->component : "app",
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

    } else if (strcmp(method_name, "LogBatch") == 0) {
        /* Handle batch log entries */
        log_LogBatchRequest_t* batch = log_LogBatchRequest_as_root(request);
        flatbuffers_log_entry_vec_t entries = log_LogBatchRequest_entries(batch);

        size_t count = flatbuffers_log_entry_vec_len(entries);
        printf("=== Batch Log: %zu entries ===\n", count);

        for (size_t i = 0; i < count; i++) {
            log_LogEntry_t* entry = log_LogEntry_vec_at(entries, i);

            const char* level_str = "UNKNOWN";
            switch (entry->level) {
                case 0: level_str = "DEBUG"; break;
                case 1: level_str = "INFO"; break;
                case 2: level_str = "WARNING"; break;
                case 3: level_str = "ERROR"; break;
                case 4: level_str = "FATAL"; break;
            }

            printf("  [%s] [%s] %s\n",
                   level_str,
                   entry->source ? entry->source : "unknown",
                   entry->message ? entry->message : "");
        }

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

    } else if (strcmp(method_name, "QuickLog") == 0) {
        /* Handle quick log */
        log_QuickLogRequest_t* quick = log_QuickLogRequest_as_root(request);

        const char* level_str = "UNKNOWN";
        switch (quick->level) {
            case 0: level_str = "DEBUG"; break;
            case 1: level_str = "INFO"; break;
            case 2: level_str = "WARNING"; break;
            case 3: level_str = "ERROR"; break;
            case 4: level_str = "FATAL"; break;
        }

        printf("[QUICK] [%s] %s\n", level_str, quick->message ? quick->message : "");

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

/* Demo: Client sends various log messages */
void send_log_messages(uvrpc_client_t* client) {
    printf("\n=== Demo 1: Single Log Messages ===\n");

    /* Send INFO log */
    log_LogEntry_t entry1 = {
        .level = 1,  /* INFO */
        .message = "Application started successfully",
        .timestamp = time(NULL),
        .source = "main",
        .thread_id = (unsigned long)pthread_self(),
        .component = "app"
    };
    uvrpc_logservice_Log(client, &entry1);

    /* Send WARNING log */
    log_LogEntry_t entry2 = {
        .level = 2,  /* WARNING */
        .message = "Configuration file not found, using defaults",
        .timestamp = time(NULL),
        .source = "config",
        .thread_id = (unsigned long)pthread_self(),
        .component = "config-loader"
    };
    uvrpc_logservice_Log(client, &entry2);

    /* Send ERROR log */
    log_LogEntry_t entry3 = {
        .level = 3,  /* ERROR */
        .message = "Failed to connect to database",
        .timestamp = time(NULL),
        .source = "db",
        .thread_id = (unsigned long)pthread_self(),
        .component = "db-connector"
    };
    uvrpc_logservice_Log(client, &entry3);

    /* Send DEBUG log */
    log_LogEntry_t entry4 = {
        .level = 0,  /* DEBUG */
        .message = "Entering function process_request()",
        .timestamp = time(NULL),
        .source = "main",
        .thread_id = (unsigned long)pthread_self(),
        .component = "request-handler"
    };
    uvrpc_logservice_Log(client, &entry4);

    printf("\n=== Demo 2: Quick Log Messages ===\n");

    /* Send quick logs */
    log_QuickLogRequest_t quick1 = {
        .level = 1,
        .message = "Quick info message"
    };
    uvrpc_logservice_QuickLog(client, &quick1);

    log_QuickLogRequest_t quick2 = {
        .level = 2,
        .message = "Quick warning message"
    };
    uvrpc_logservice_QuickLog(client, &quick2);

    log_QuickLogRequest_t quick3 = {
        .level = 3,
        .message = "Quick error message"
    };
    uvrpc_logservice_QuickLog(client, &quick3);

    printf("\n=== Demo 3: Batch Log Messages ===\n");

    /* Create batch log entries */
    log_LogEntry_t entries[3];
    log_LogEntry_t batch_entry1 = {
        .level = 1,
        .message = "Batch entry 1: User logged in",
        .timestamp = time(NULL),
        .source = "auth",
        .thread_id = (unsigned long)pthread_self(),
        .component = "auth-service"
    };
    entries[0] = batch_entry1;

    log_LogEntry_t batch_entry2 = {
        .level = 1,
        .message = "Batch entry 2: Transaction processed",
        .timestamp = time(NULL),
        .source = "payment",
        .thread_id = (unsigned long)pthread_self(),
        .component = "payment-service"
    };
    entries[1] = batch_entry2;

    log_LogEntry_t batch_entry3 = {
        .level = 2,
        .message = "Batch entry 3: Low memory warning",
        .timestamp = time(NULL),
        .source = "system",
        .thread_id = (unsigned long)pthread_self(),
        .component = "monitor"
    };
    entries[2] = batch_entry3;

    /* Create batch request */
    log_LogBatchRequest_t batch_req = {
        .entries = entries,
        .entries_size = 3
    };
    uvrpc_logservice_LogBatch(client, &batch_req);

    printf("\n=== Demo 4: High Volume Logging ===\n");

    /* Send 10 quick logs in a loop */
    for (int i = 0; i < 10; i++) {
        char msg[64];
        snprintf(msg, sizeof(msg), "High volume log message #%d", i);
        log_QuickLogRequest_t quick = {
            .level = 1,
            .message = msg
        };
        uvrpc_logservice_QuickLog(client, &quick);
    }
    printf("Sent 10 oneway log messages (fire-and-forget)\n");
}

/* Client connection callback */
void on_client_connect(uvrpc_client_t* client, uvrpc_error_t status, void* ctx) {
    if (status == UVRPC_OK) {
        printf("Client connected to Log Service successfully\n");
        send_log_messages(client);
        uv_stop(uvrpc_client_get_loop(client));
    } else {
        printf("Client connection failed: %d\n", status);
        uv_stop(uvrpc_client_get_loop(client));
    }
}

int main() {
    uv_loop_t loop;
    uv_loop_init(&loop);

    /* Create log service server */
    printf("=== Starting Log Service Server ===\n");
    uvrpc_server_t* server = uvrpc_logservice_create_server(&loop, "tcp://127.0.0.1:6666");
    if (!server) {
        fprintf(stderr, "Failed to create server\n");
        return 1;
    }

    uvrpc_error_t ret = uvrpc_logservice_start_server(server);
    if (ret != UVRPC_OK) {
        fprintf(stderr, "Failed to start server: %d\n", ret);
        return 1;
    }
    printf("Log Service started on tcp://127.0.0.1:6666\n");

    /* Create client */
    printf("\n=== Creating Log Service Client ===\n");
    uvrpc_client_t* client = uvrpc_logservice_create_client(
        &loop,
        "tcp://127.0.0.1:6666",
        on_client_connect,
        NULL
    );

    if (!client) {
        fprintf(stderr, "Failed to create client\n");
        return 1;
    }

    /* Run event loop */
    printf("\n=== Running Event Loop ===\n");
    uv_run(&loop, UV_RUN_DEFAULT);

    /* Cleanup */
    printf("\n=== Cleanup ===\n");
    uvrpc_logservice_free_client(client);
    uvrpc_logservice_stop_server(server);
    uvrpc_logservice_free_server(server);
    uv_loop_close(&loop);

    printf("\n=== Demo Complete ===\n");
    return 0;
}