#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <uv.h>
#include "uvrpc.h"
#include "uvbus.h"
#include "rpc_api.h"
#include "rpc_benchmark_builder.h"
#include "rpc_benchmark_reader.h"
#include "rpc_rpc_common.h"
#include "flatcc/flatcc_flatbuffers.h"
#include "flatcc/flatcc_builder.h"

#define MAX_PROCESSES 100
#define MAX_CLIENTS_PER_LOOP 10
#define MAX_REQUESTS 1000000
#define PID_FILE "/tmp/uvrpc_benchmark.pid"
#define SHM_SIZE (sizeof(unsigned long long) * MAX_PROCESSES * MAX_CLIENTS_PER_LOOP * 3)

/* Transport types */
typedef enum {
    TRANSPORT_TCP = 0,
    TRANSPORT_UDP,
    TRANSPORT_IPC,
    TRANSPORT_INPROC
} transport_type_t;

/* Shared memory structure for statistics */
typedef struct {
    unsigned long long ops;
    unsigned long long errors;
    unsigned long long latency_us;
} client_stats_t;

/* Global state */
static volatile sig_atomic_t running = 1;
static transport_type_t transport_type = TRANSPORT_TCP;
static int num_loops = 4;
static int clients_per_loop = 5;
static int num_requests = 10000;
static int warmup_requests = 100;
static char server_address[256] = "tcp://127.0.0.1:5555";
static pid_t server_pid = -1;
static pid_t client_pids[MAX_PROCESSES];
static int num_client_pids = 0;
static int use_inproc_threads = 0;
static int shm_fd = -1;
static client_stats_t* shared_stats = NULL;
static const char* shm_name = "/uvrpc_benchmark_shm";

/* Signal handler */
static void signal_handler(int signum) {
    (void)signum;
    running = 0;
}

/* Setup signal handlers */
static void setup_signals(void) {
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGCHLD, &sa, NULL);
}

/* Cleanup shared memory */
static void cleanup_shared_memory(void) {
    if (shared_stats && munmap(shared_stats, SHM_SIZE) == -1) {
        perror("munmap");
    }
    shared_stats = NULL;
    
    if (shm_fd != -1) {
        close(shm_fd);
        shm_fd = -1;
    }
    
    shm_unlink(shm_name);
}

/* Initialize shared memory */
static int init_shared_memory(void) {
    shm_fd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open");
        return -1;
    }
    
    if (ftruncate(shm_fd, SHM_SIZE) == -1) {
        perror("ftruncate");
        close(shm_fd);
        return -1;
    }
    
    shared_stats = (client_stats_t*)mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared_stats == MAP_FAILED) {
        perror("mmap");
        close(shm_fd);
        return -1;
    }
    
    memset(shared_stats, 0, SHM_SIZE);
    return 0;
}

/* PID file management */
static int write_pid_file(pid_t pid) {
    FILE* f = fopen(PID_FILE, "w");
    if (!f) {
        perror("fopen");
        return -1;
    }
    fprintf(f, "%d\n", pid);
    fclose(f);
    return 0;
}

static void cleanup_pid_file(void) {
    unlink(PID_FILE);
}

static void kill_existing_processes(void) {
    FILE* f = fopen(PID_FILE, "r");
    if (!f) return;
    
    pid_t old_pid;
    if (fscanf(f, "%d", &old_pid) == 1) {
        if (old_pid > 0 && kill(old_pid, 0) == 0) {
            printf("[MAIN] Killing existing benchmark process %d\n", old_pid);
            kill(old_pid, SIGTERM);
            sleep(1);
            if (kill(old_pid, 0) == 0) {
                kill(old_pid, SIGKILL);
            }
        }
    }
    fclose(f);
    cleanup_pid_file();
}

/* Server handler implementation - must match rpc_user_impl.c signature */
int rpc_handle_request(const char* method_name, const void* request, uvrpc_request_t* req) {
    if (strcmp(method_name, "Add") == 0) {
        rpc_BenchmarkAddRequest_table_t add_req = (rpc_BenchmarkAddRequest_table_t)request;
        
        int32_t a = rpc_BenchmarkAddRequest_a(add_req);
        int32_t b = rpc_BenchmarkAddRequest_b(add_req);
        int32_t result = a + b;
        
        flatcc_builder_t builder;
        flatcc_builder_init(&builder);
        rpc_BenchmarkAddResponse_start_as_root(&builder);
        rpc_BenchmarkAddResponse_result_add(&builder, result);
        rpc_BenchmarkAddResponse_end_as_root(&builder);
        
        size_t size;
        void* buf = flatcc_builder_finalize_buffer(&builder, &size);
        
        uvrpc_request_send_response(req, UVRPC_OK, buf, size);
        
        flatcc_builder_reset(&builder);
        return 0;
    } else if (strcmp(method_name, "Echo") == 0) {
        rpc_BenchmarkEchoRequest_table_t echo_req = (rpc_BenchmarkEchoRequest_table_t)request;
        
        flatcc_builder_t builder;
        flatcc_builder_init(&builder);
        rpc_BenchmarkEchoResponse_start_as_root(&builder);
        
        if (rpc_BenchmarkEchoRequest_data_is_present(echo_req)) {
            flatbuffers_uint8_vec_t data_vec = rpc_BenchmarkEchoRequest_data(echo_req);
            size_t data_len = flatbuffers_vec_len(data_vec);
            if (data_len > 0) {
                const uint8_t* data_ptr = (const uint8_t*)data_vec;
                flatbuffers_uint8_vec_ref_t vec_ref = flatbuffers_uint8_vec_create(&builder, data_ptr, data_len);
                rpc_BenchmarkEchoResponse_data_add(&builder, vec_ref);
            }
        }
        rpc_BenchmarkEchoResponse_end_as_root(&builder);
        
        size_t size;
        void* buf = flatcc_builder_finalize_buffer(&builder, &size);
        
        uvrpc_request_send_response(req, UVRPC_OK, buf, size);
        
        flatcc_builder_reset(&builder);
        return 0;
    }
    
    return -1;
}

/* Server process */
static void run_server(void) {
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, &loop);
    uvrpc_config_set_address(config, server_address);
    uvrpc_config_set_performance_mode(config, UVRPC_PERF_LOW_LATENCY);
    
    uvrpc_server_t* server = uvrpc_server_create(config);
    if (!server) {
        fprintf(stderr, "[SERVER] Failed to create server\n");
        exit(1);
    }
    
    rpc_register_all(server);
    
    if (uvrpc_server_start(server) != UVRPC_OK) {
        fprintf(stderr, "[SERVER] Failed to start server\n");
        exit(1);
    }
    
    printf("[SERVER] Started on %s\n", server_address);
    fflush(stdout);
    
    while (running) {
        uv_run(&loop, UV_RUN_ONCE);
    }
    
    uvrpc_server_stop(server);
    uvrpc_server_free(server);
    uvrpc_config_free(config);
    uv_loop_close(&loop);
    
    printf("[SERVER] Stopped\n");
    exit(0);
}

/* Server in same process for INPROC */
static uvrpc_server_t* g_inproc_server = NULL;
static uv_loop_t g_server_loop;

static void run_server_in_process(void) {
    uv_loop_init(&g_server_loop);
    
    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, &g_server_loop);
    uvrpc_config_set_address(config, server_address);
    uvrpc_config_set_performance_mode(config, UVRPC_PERF_LOW_LATENCY);
    
    g_inproc_server = uvrpc_server_create(config);
    if (!g_inproc_server) {
        fprintf(stderr, "[SERVER] Failed to create server\n");
        exit(1);
    }
    
    rpc_register_all(g_inproc_server);
    
    if (uvrpc_server_start(g_inproc_server) != UVRPC_OK) {
        fprintf(stderr, "[SERVER] Failed to start server\n");
        exit(1);
    }
    
    printf("[SERVER] Started on %s\n", server_address);
    fflush(stdout);
}

/* Client statistics */
typedef struct {
    int loop_idx;
    int client_idx;
    int total_requests;
    int completed_requests;
    int error_count;
    unsigned long long total_latency_us;
    struct timeval start_time;
} client_context_t;

/* Client callback */
static void client_callback(uvrpc_response_t* response, void* user_data) {
    client_context_t* ctx = (client_context_t*)user_data;
    
    if (response->status == UVRPC_OK) {
        ctx->completed_requests++;
        
        struct timeval now;
        gettimeofday(&now, NULL);
        unsigned long long latency_us = (now.tv_sec - ctx->start_time.tv_sec) * 1000000LL +
                                        (now.tv_usec - ctx->start_time.tv_usec);
        ctx->total_latency_us += latency_us;
    } else {
        ctx->error_count++;
    }
    
    /* Update shared memory */
    if (shared_stats) {
        int idx = ctx->loop_idx * MAX_CLIENTS_PER_LOOP + ctx->client_idx;
        shared_stats[idx].ops = ctx->completed_requests;
        shared_stats[idx].errors = ctx->error_count;
        shared_stats[idx].latency_us = ctx->total_latency_us;
    }
    
    uvrpc_response_free(response);
}

/* Send requests continuously */
static void send_requests(uvrpc_client_t* client, client_context_t* ctx, uv_loop_t* loop) {
    while (running && ctx->completed_requests < ctx->total_requests) {
        gettimeofday(&ctx->start_time, NULL);
        
        int ret = BenchmarkService_Add(client, client_callback, ctx, 42, 58);
        
        if (ret != UVRPC_OK) {
            ctx->error_count++;
            usleep(1000);
        }
        
        uv_run(loop, UV_RUN_ONCE);
        
        if (!running) break;
    }
}

/* Client process (for fork) */
static void run_client_process(int loop_idx, int client_idx) {
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    uvrpc_client_t* client = rpc_client_create(&loop, server_address, NULL, NULL);
    if (!client) {
        fprintf(stderr, "[CLIENT %d-%d] Failed to create client\n", loop_idx, client_idx);
        exit(1);
    }
    
    /* Wait for connection */
    int wait_count = 0;
    while (wait_count < 100) {
        uv_run(&loop, UV_RUN_ONCE);
        wait_count++;
        usleep(10000);
    }
    
    /* Warmup */
    for (int i = 0; i < warmup_requests && running; i++) {
        rpc_BenchmarkAddResponse_table_t response;
        int ret = BenchmarkService_Add_sync(client, &response, 10, 20, 5000);
        (void)ret;
        uv_run(&loop, UV_RUN_ONCE);
    }
    
    if (!running) {
        rpc_client_free(client);
        uv_loop_close(&loop);
        exit(0);
    }
    
    /* Actual benchmark */
    client_context_t ctx = {loop_idx, client_idx, num_requests, 0, 0, 0, {0, 0}};
    struct timeval start_time, end_time;
    gettimeofday(&start_time, NULL);
    
    send_requests(client, &ctx, &loop);
    
    gettimeofday(&end_time, NULL);
    
    /* Wait for remaining responses */
    int timeout = 5;
    time_t deadline = time(NULL) + timeout;
    while (ctx.completed_requests < ctx.total_requests && time(NULL) < deadline && running) {
        uv_run(&loop, UV_RUN_ONCE);
        usleep(10000);
    }
    
    rpc_client_free(client);
    uv_loop_close(&loop);
    
    /* Update final stats */
    if (shared_stats) {
        int idx = loop_idx * MAX_CLIENTS_PER_LOOP + client_idx;
        shared_stats[idx].ops = ctx.completed_requests;
        shared_stats[idx].errors = ctx.error_count;
        shared_stats[idx].latency_us = ctx.total_latency_us;
    }
    
    double elapsed = (end_time.tv_sec - start_time.tv_sec) + 
                     (end_time.tv_usec - start_time.tv_usec) / 1000000.0;
    
    printf("[CLIENT %d-%d] Completed: %d/%d, Errors: %d, Time: %.2fs\n",
           loop_idx, client_idx, ctx.completed_requests, num_requests, ctx.error_count, elapsed);
    fflush(stdout);
    
    exit(0);
}

/* Thread function for INPROC clients */
typedef struct {
    int loop_idx;
    int client_idx;
} thread_arg_t;

static void* client_thread_func(void* arg) {
    thread_arg_t* t_arg = (thread_arg_t*)arg;
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    uvrpc_client_t* client = rpc_client_create(&loop, server_address, NULL, NULL);
    if (!client) {
        fprintf(stderr, "[CLIENT %d-%d] Failed to create client\n", t_arg->loop_idx, t_arg->client_idx);
        free(arg);
        return NULL;
    }
    
    /* Wait for connection */
    int wait_count = 0;
    while (wait_count < 100) {
        uv_run(&loop, UV_RUN_ONCE);
        wait_count++;
        usleep(10000);
    }
    
    /* Warmup */
    for (int i = 0; i < warmup_requests && running; i++) {
        rpc_BenchmarkAddResponse_table_t response;
        int ret = BenchmarkService_Add_sync(client, &response, 10, 20, 5000);
        (void)ret;
        uv_run(&loop, UV_RUN_ONCE);
    }
    
    if (!running) {
        rpc_client_free(client);
        uv_loop_close(&loop);
        free(arg);
        return NULL;
    }
    
    /* Actual benchmark */
    client_context_t ctx = {t_arg->loop_idx, t_arg->client_idx, num_requests, 0, 0, 0, {0, 0}};
    struct timeval start_time, end_time;
    gettimeofday(&start_time, NULL);
    
    send_requests(client, &ctx, &loop);
    
    gettimeofday(&end_time, NULL);
    
    /* Wait for remaining responses */
    int timeout = 5;
    time_t deadline = time(NULL) + timeout;
    while (ctx.completed_requests < ctx.total_requests && time(NULL) < deadline && running) {
        uv_run(&loop, UV_RUN_ONCE);
        usleep(10000);
    }
    
    rpc_client_free(client);
    uv_loop_close(&loop);
    
    /* Update final stats */
    if (shared_stats) {
        int idx = t_arg->loop_idx * MAX_CLIENTS_PER_LOOP + t_arg->client_idx;
        shared_stats[idx].ops = ctx.completed_requests;
        shared_stats[idx].errors = ctx.error_count;
        shared_stats[idx].latency_us = ctx.total_latency_us;
    }
    
    double elapsed = (end_time.tv_sec - start_time.tv_sec) + 
                     (end_time.tv_usec - start_time.tv_usec) / 1000000.0;
    
    printf("[CLIENT %d-%d] Completed: %d/%d, Errors: %d, Time: %.2fs\n",
           t_arg->loop_idx, t_arg->client_idx, ctx.completed_requests, num_requests, ctx.error_count, elapsed);
    fflush(stdout);
    
    free(arg);
    return NULL;
}

/* Collect and print statistics */
static void collect_statistics(void) {
    sleep(1);
    
    unsigned long long total_ops = 0;
    unsigned long long total_errors = 0;
    unsigned long long total_latency_us = 0;
    int total_clients = num_loops * clients_per_loop;
    
    for (int i = 0; i < total_clients; i++) {
        total_ops += shared_stats[i].ops;
        total_errors += shared_stats[i].errors;
        total_latency_us += shared_stats[i].latency_us;
    }
    
    printf("\n=== Benchmark Results ===\n");
    printf("Transport: %s\n", 
           transport_type == TRANSPORT_TCP ? "TCP" :
           transport_type == TRANSPORT_UDP ? "UDP" :
           transport_type == TRANSPORT_IPC ? "IPC" : "INPROC");
    printf("Loops: %d\n", num_loops);
    printf("Clients per loop: %d\n", clients_per_loop);
    printf("Total clients: %d\n", total_clients);
    printf("Requests per client: %d\n", num_requests);
    printf("Warmup requests: %d\n", warmup_requests);
    printf("\n");
    printf("Total operations: %llu\n", total_ops);
    printf("Total errors: %llu\n", total_errors);
    unsigned long long total_attempts = total_ops + total_errors;
    if (total_attempts > 0) {
        printf("Success rate: %.2f%%\n", 100.0 * total_ops / total_attempts);
    }
    printf("\n");
    
    if (total_ops > 0) {
        double avg_latency_us = (double)total_latency_us / total_ops;
        printf("Average latency: %.2f us\n", avg_latency_us);
        double total_time_sec = 1.0; /* Approximate */
        printf("Throughput: %.2f ops/s\n", total_ops / total_time_sec);
    }
    
    printf("========================\n");
}

/* Wait for all child processes */
static void wait_for_children(void) {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, 0)) > 0) {
        if (WIFEXITED(status)) {
            printf("[MAIN] Process %d exited with status %d\n", pid, WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            printf("[MAIN] Process %d killed by signal %d\n", pid, WTERMSIG(status));
        }
    }
}

/* Kill all child processes */
static void kill_all_children(void) {
    for (int i = 0; i < num_client_pids; i++) {
        if (client_pids[i] > 0) {
            printf("[MAIN] Killing client process %d\n", client_pids[i]);
            kill(client_pids[i], SIGTERM);
        }
    }
    
    if (server_pid > 0) {
        printf("[MAIN] Killing server process %d\n", server_pid);
        kill(server_pid, SIGTERM);
    }
    
    sleep(1);
    
    for (int i = 0; i < num_client_pids; i++) {
        if (client_pids[i] > 0 && kill(client_pids[i], 0) == 0) {
            kill(client_pids[i], SIGKILL);
        }
    }
    
    if (server_pid > 0 && kill(server_pid, 0) == 0) {
        kill(server_pid, SIGKILL);
    }
}

/* Print usage */
static void print_usage(const char* prog) {
    printf("Usage: %s [OPTIONS]\n", prog);
    printf("\nOptions:\n");
    printf("  -t TRANSPORT  Transport type: tcp, udp, ipc, inproc (default: tcp)\n");
    printf("  -l LOOPS      Number of loops/processes (default: 4)\n");
    printf("  -c CLIENTS    Number of clients per loop (default: 5)\n");
    printf("  -n REQUESTS   Number of requests per client (default: 10000)\n");
    printf("  -w WARMUP     Number of warmup requests (default: 100)\n");
    printf("  -a ADDRESS    Server address (default: tcp://127.0.0.1:5555)\n");
    printf("  -h            Show this help\n");
    printf("\nExamples:\n");
    printf("  %s -t tcp -l 4 -c 5 -n 10000\n", prog);
    printf("  %s -t inproc -l 2 -c 10 -n 5000\n", prog);
}

int main(int argc, char* argv[]) {
    int opt;
    
    while ((opt = getopt(argc, argv, "t:l:c:n:w:a:h")) != -1) {
        switch (opt) {
            case 't':
                if (strcmp(optarg, "tcp") == 0) {
                    transport_type = TRANSPORT_TCP;
                } else if (strcmp(optarg, "udp") == 0) {
                    transport_type = TRANSPORT_UDP;
                } else if (strcmp(optarg, "ipc") == 0) {
                    transport_type = TRANSPORT_IPC;
                } else if (strcmp(optarg, "inproc") == 0) {
                    transport_type = TRANSPORT_INPROC;
                } else {
                    fprintf(stderr, "Invalid transport type: %s\n", optarg);
                    print_usage(argv[0]);
                    return 1;
                }
                break;
            case 'l':
                num_loops = atoi(optarg);
                if (num_loops < 1 || num_loops > MAX_PROCESSES) {
                    fprintf(stderr, "Invalid number of loops: %s\n", optarg);
                    return 1;
                }
                break;
            case 'c':
                clients_per_loop = atoi(optarg);
                if (clients_per_loop < 1 || clients_per_loop > MAX_CLIENTS_PER_LOOP) {
                    fprintf(stderr, "Invalid number of clients per loop: %s\n", optarg);
                    return 1;
                }
                break;
            case 'n':
                num_requests = atoi(optarg);
                if (num_requests < 1 || num_requests > MAX_REQUESTS) {
                    fprintf(stderr, "Invalid number of requests: %s\n", optarg);
                    return 1;
                }
                break;
            case 'w':
                warmup_requests = atoi(optarg);
                if (warmup_requests < 0) {
                    fprintf(stderr, "Invalid number of warmup requests: %s\n", optarg);
                    return 1;
                }
                break;
            case 'a':
                strncpy(server_address, optarg, sizeof(server_address) - 1);
                server_address[sizeof(server_address) - 1] = '\0';
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }
    
    if (transport_type == TRANSPORT_TCP) {
        strcpy(server_address, "tcp://127.0.0.1:5555");
    } else if (transport_type == TRANSPORT_UDP) {
        strcpy(server_address, "udp://127.0.0.1:5555");
    } else if (transport_type == TRANSPORT_IPC) {
        strcpy(server_address, "ipc:///tmp/uvrpc_benchmark.ipc");
    } else if (transport_type == TRANSPORT_INPROC) {
        strcpy(server_address, "inproc://benchmark");
    }
    
    setup_signals();
    kill_existing_processes();
    
    if (write_pid_file(getpid()) != 0) {
        return 1;
    }
    
    if (init_shared_memory() != 0) {
        fprintf(stderr, "Failed to initialize shared memory\n");
        cleanup_pid_file();
        return 1;
    }
    
    printf("=== UVRPC Unified Benchmark ===\n");
    printf("Transport: %s\n", 
           transport_type == TRANSPORT_TCP ? "TCP" :
           transport_type == TRANSPORT_UDP ? "UDP" :
           transport_type == TRANSPORT_IPC ? "IPC" : "INPROC");
    printf("Loops: %d\n", num_loops);
    printf("Clients per loop: %d\n", clients_per_loop);
    printf("Total clients: %d\n", num_loops * clients_per_loop);
    printf("Requests per client: %d\n", num_requests);
    printf("Warmup requests: %d\n", warmup_requests);
    printf("Server address: %s\n", server_address);
    printf("===============================\n\n");
    
    if (transport_type == TRANSPORT_INPROC) {
    /* For INPROC, server runs in main process, clients run in threads */
    server_pid = getpid(); /* Server runs in main process */
    run_server_in_process();
} else {
    /* For other transports, fork server process */
    server_pid = fork();
    if (server_pid < 0) {
        perror("fork server");
        cleanup_shared_memory();
        cleanup_pid_file();
        return 1;
    } else if (server_pid == 0) {
        run_server();
        exit(0);
    }
    
    sleep(1);
}
    
if (transport_type == TRANSPORT_INPROC) {
        pthread_t threads[num_loops * clients_per_loop];
        
        for (int i = 0; i < num_loops; i++) {
            for (int j = 0; j < clients_per_loop; j++) {
                thread_arg_t* arg = malloc(sizeof(thread_arg_t));
                arg->loop_idx = i;
                arg->client_idx = j;
                
                if (pthread_create(&threads[i * clients_per_loop + j], NULL, client_thread_func, arg) != 0) {
                    perror("pthread_create");
                    free(arg);
                }
            }
        }
        
        /* Run server loop while clients are running */
        int clients_done = 0;
        time_t start_time = time(NULL);
        while (running && clients_done < num_loops * clients_per_loop) {
            uv_run(&g_server_loop, UV_RUN_ONCE);
            usleep(1000);
            
            /* Check if all clients are done (timeout after 60 seconds) */
            if (time(NULL) - start_time > 60) {
                printf("[MAIN] Timeout waiting for clients\n");
                break;
            }
        }
        
        for (int i = 0; i < num_loops * clients_per_loop; i++) {
            pthread_join(threads[i], NULL);
        }
    } else {
        for (int i = 0; i < num_loops; i++) {
            for (int j = 0; j < clients_per_loop; j++) {
                pid_t pid = fork();
                if (pid < 0) {
                    perror("fork client");
                    continue;
                } else if (pid == 0) {
                    run_client_process(i, j);
                    exit(0);
                } else {
                    client_pids[num_client_pids++] = pid;
                }
            }
        }
        
        wait_for_children();
    }
    
    if (transport_type == TRANSPORT_INPROC) {
    /* Stop in-process server */
    printf("[MAIN] Stopping in-process server...\n");
    if (g_inproc_server) {
        uvrpc_server_stop(g_inproc_server);
        uvrpc_server_free(g_inproc_server);
        g_inproc_server = NULL;
    }
    uv_loop_close(&g_server_loop);
} else if (server_pid > 0) {
    /* Stop forked server */
    printf("[MAIN] Stopping server...\n");
    kill(server_pid, SIGTERM);
    
    int status;
    int timeout = 5;
    time_t deadline = time(NULL) + timeout;
    while (waitpid(server_pid, &status, WNOHANG) == 0 && time(NULL) < deadline) {
        usleep(100000);
    }
    
    if (waitpid(server_pid, &status, WNOHANG) == 0) {
        printf("[MAIN] Force killing server...\n");
        kill(server_pid, SIGKILL);
        waitpid(server_pid, &status, 0);
    }
}
    
    collect_statistics();
    
    cleanup_shared_memory();
    cleanup_pid_file();
    
    printf("[MAIN] Benchmark completed\n");
    fflush(stdout);
    return 0;
}