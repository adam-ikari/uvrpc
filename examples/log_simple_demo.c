/* Simple Log Service Demo - Minimal oneway logging example */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>
#include <time.h>
#include "log_logservice_api.h"
#include "log_service_reader.h"
#include <uvrpc.h>

/* The request payload is a serialized FlatBuffers root, so it is read through
 * the generated reader accessors -- log_LogEntry_t is the client-side view the
 * serializer consumes, not what arrives on the wire. */
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

/* User-implemented handler for log requests */
uvrpc_error_t uvrpc_logservice_handle_request(const char* method_name,
                                       const void* request,
                                       uvrpc_request_t* req) {
    (void)req;

    /* Log is a oneway method: the generated stub sends nothing when this
     * returns UVRPC_OK, and a reply to a oneway is dropped client-side for
     * lack of a callback slot. */
    if (strcmp(method_name, "Log") == 0) {
        log_LogEntry_table_t entry = log_LogEntry_as_root(request);

        time_t ts = (time_t)log_LogEntry_timestamp(entry);
        struct tm* tm_info = localtime(&ts);
        char time_str[64] = "?";
        if (tm_info) {
            strftime(time_str, sizeof(time_str), "%H:%M:%S", tm_info);
        }

        const char* source = log_LogEntry_source(entry);
        const char* message = log_LogEntry_message(entry);
        printf("%s [%s] %s: %s\n",
               time_str,
               level_name(log_LogEntry_level(entry)),
               source ? source : "app",
               message ? message : "");

        return UVRPC_OK;
    }

    return UVRPC_ERROR_NOT_FOUND;
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

static void send_logs(uvrpc_client_t* client) {
    printf("\n--- Sending Log Messages ---\n");

    log_LogEntry_t entry1 = {
        .level = log_LogLevel_INFO,
        .message = "Application started",
        .timestamp = time(NULL),
        .source = "main",
        .thread_id = 0,
        .component = "app"
    };
    uvrpc_logservice_Log(client, &entry1);

    log_LogEntry_t entry2 = {
        .level = log_LogLevel_WARNING,
        .message = "Configuration warning",
        .timestamp = time(NULL),
        .source = "config",
        .thread_id = 0,
        .component = "app"
    };
    uvrpc_logservice_Log(client, &entry2);

    log_LogEntry_t entry3 = {
        .level = log_LogLevel_ERROR,
        .message = "Connection error",
        .timestamp = time(NULL),
        .source = "network",
        .thread_id = 0,
        .component = "app"
    };
    uvrpc_logservice_Log(client, &entry3);

    printf("All oneway logs sent (fire-and-forget)\n");
}

/* Client connection callback */
void on_client_connect(int status, void* ctx) {
    demo_ctx_t* d = (demo_ctx_t*)ctx;
    if (status == UVRPC_OK) {
        send_logs(d->client);
    } else {
        fprintf(stderr, "Client connection failed: %d\n", status);
    }
    /* Oneway sends return as soon as the frames are queued; stopping here would
     * discard them before the server read them. Stop from a timer instead. */
    uv_timer_init(uvrpc_client_get_loop(d->client), d->stop_timer);
    uv_timer_start(d->stop_timer, stop_later, 200, 0);
}

int main(void) {
    uv_loop_t loop = {0};
    uv_loop_init(&loop);

    /* Create server */
    printf("Starting Log Service...\n");
    uvrpc_server_t* server = uvrpc_logservice_create_server(&loop, "tcp://127.0.0.1:6666", NULL);
    if (!server) {
        fprintf(stderr, "Failed to create server\n");
        return 1;
    }
    if (uvrpc_logservice_start_server(server) != UVRPC_OK) {
        fprintf(stderr, "Failed to start server\n");
        return 1;
    }

    /* Create client. The connect callback needs the handle to send, and it
     * fires during uv_run -- after this assignment -- so a stack context is
     * enough; no file-scope state is involved. */
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

    /* Run */
    uv_run(&loop, UV_RUN_DEFAULT);

    /* Cleanup */
    uvrpc_logservice_free_client(client);
    uvrpc_logservice_stop_server(server);
    uvrpc_logservice_free_server(server);
    uv_loop_close(&loop);

    return 0;
}
