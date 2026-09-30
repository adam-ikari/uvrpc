/* Log Service Demo - Demonstrates oneway logging RPC */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>
#include <time.h>
#include <pthread.h>
#include "log_logservice_api.h"
#include "log_service_reader.h"
#include <uvrpc.h>

/* The client sends a serialized FlatBuffers root, so the handler reads it
 * through the generated reader accessors -- not by casting the payload to
 * log_LogEntry_t and touching fields. That struct is the *client-side* view
 * the serializer consumes; on the wire the buffer is a log_LogEntry_table_t. */
static const char* level_name(log_LogLevel_enum_t level) {
    switch (level) {
        case log_LogLevel_DEBUG:   return "DEBUG";
        case log_LogLevel_INFO:    return "INFO";
        case log_LogLevel_WARNING: return "WARNING";
        case log_LogLevel_ERROR:   return "ERROR";
        case log_LogLevel_FATAL:   return "FATAL";
    }
    return "UNKNOWN";
}

static void print_entry(log_LogEntry_table_t entry) {
    time_t ts = (time_t)log_LogEntry_timestamp(entry);
    struct tm* tm_info = localtime(&ts);
    char time_str[64] = "?";
    if (tm_info) {
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
    }

    const char* source = log_LogEntry_source(entry);
    const char* component = log_LogEntry_component(entry);
    const char* message = log_LogEntry_message(entry);
    printf("[%s] [%s] [%s] [Thread:%lu] %s: %s\n",
           time_str,
           level_name(log_LogEntry_level(entry)),
           source ? source : "unknown",
           (unsigned long)log_LogEntry_thread_id(entry),
           component ? component : "app",
           message ? message : "");
}

/* User-implemented handler for all log requests. */
uvrpc_error_t uvrpc_logservice_handle_request(const char* method_name,
                                       const void* request,
                                       uvrpc_request_t* req) {
    (void)req;

    /* All three methods are oneway: the generated stub sends nothing when this
     * returns UVRPC_OK, and a reply to a oneway is dropped client-side for
     * lack of a callback slot. So there is nothing to send here. */
    if (strcmp(method_name, "Log") == 0) {
        print_entry(log_LogEntry_as_root(request));
        return UVRPC_OK;

    } else if (strcmp(method_name, "LogBatch") == 0) {
        log_LogEntry_vec_t entries = log_LogBatchRequest_entries(
            log_LogBatchRequest_as_root(request));
        size_t count = log_LogEntry_vec_len(entries);
        printf("=== Batch Log: %zu entries ===\n", count);

        for (size_t i = 0; i < count; i++) {
            print_entry(log_LogEntry_vec_at(entries, i));
        }
        return UVRPC_OK;

    } else if (strcmp(method_name, "QuickLog") == 0) {
        log_QuickLogRequest_table_t quick = log_QuickLogRequest_as_root(request);
        const char* message = log_QuickLogRequest_message(quick);
        printf("[QUICK] [%s] %s\n",
               level_name(log_QuickLogRequest_level(quick)),
               message ? message : "");
        return UVRPC_OK;
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

/* The connect callback receives only a status and the ctx it was registered
 * with -- no client handle. main passes a pointer to this struct and fills in
 * the handle once create_client returns; over TCP the callback fires during
 * uv_run, well after that. The stop timer is a stack handle in main too, and
 * the timer callback recovers the loop from the handle itself. */
typedef struct {
    uvrpc_client_t* client;
    uv_timer_t* stop_timer;
} demo_ctx_t;

static void stop_later(uv_timer_t* timer) {
    uv_timer_stop(timer);
    uv_stop(uv_handle_get_loop((uv_handle_t*)timer));
}

void on_client_connect(int status, void* ctx) {
    demo_ctx_t* d = (demo_ctx_t*)ctx;
    if (status == UVRPC_OK) {
        printf("Client connected to Log Service successfully\n");
        send_log_messages(d->client);
    } else {
        fprintf(stderr, "Client connection failed: %d\n", status);
    }
    /* Oneway means fire-and-forget: nothing tracks these on the client, so the
     * sends return as soon as the frames are queued. Stopping the loop right
     * here would discard them before the server ever reads them -- the demo
     * exits cleanly and prints nothing. Give the loop a moment to deliver,
     * then stop from a timer. */
    uv_timer_init(uvrpc_client_get_loop(d->client), d->stop_timer);
    uv_timer_start(d->stop_timer, stop_later, 200, 0);
}

int main() {
    uv_loop_t loop;
    uv_loop_init(&loop);

    /* Create log service server */
    printf("=== Starting Log Service Server ===\n");
    uvrpc_server_t* server = uvrpc_logservice_create_server(&loop, "tcp://127.0.0.1:6666", NULL);
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

    /* Create client. The connect callback needs the handle to send, and it
     * fires during uv_run -- after this assignment -- so a stack context is
     * enough; no file-scope state is involved. */
    printf("\n=== Creating Log Service Client ===\n");
    demo_ctx_t d = {0};
    uv_timer_t stop_timer;
    d.stop_timer = &stop_timer;
    uvrpc_client_t* client = uvrpc_logservice_create_client(
        &loop,
        "tcp://127.0.0.1:6666",
        NULL,  /* registry: only INPROC/SAMELOOP need one, TCP does not */
        on_client_connect,
        &d
    );

    if (!client) {
        fprintf(stderr, "Failed to create client\n");
        uvrpc_logservice_stop_server(server);
        uvrpc_logservice_free_server(server);
        uv_loop_close(&loop);
        return 1;
    }
    d.client = client;

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