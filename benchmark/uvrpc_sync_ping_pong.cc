// UVRPC side measured with the gRPC harness's call style: a blocking unary
// call, one request in flight, instead of perf_benchmark's explicit
// UV_RUN_ONCE pump. uvrpc_client_call_async() drives the shared loop inline in
// the caller's stack frame until the response arrives -- the UVRPC equivalent
// of grpc_ping_pong's synchronous stub->Echo() call.
//
// Kept separate from perf_benchmark.c on purpose: changing that binary's pump
// would invalidate the transport baseline and the CI gate built on it.
// Same methodology as grpc_ping_pong.cc: 8-byte echo, 1000 warmup requests
// excluded, sequential one-in-flight, latency = elapsed / received.
//
// Usage: ./uvrpc_sync_ping_pong [requests]
//   requests: default 100000; TCP port 16558 (perf_benchmark uses 16555)

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <uv.h>

#include "uvrpc.h"
#include "uvrpc_async.h"

namespace {

constexpr long long kWarmupRequests = 1000;
constexpr long long kDefaultRequests = 100000;
constexpr int kPort = 16558;

long long now_ms() {
  timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<long long>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}

void echo_handler(uvrpc_request_t* req, void*) {
  uvrpc_request_send_response(req, UVRPC_OK, req->params, req->params_size);
  uvrpc_request_free(req);
}

}  // namespace

int main(int argc, char** argv) {
  long long num_requests = kDefaultRequests;
  if (argc >= 2) num_requests = std::strtoll(argv[1], nullptr, 10);
  if (num_requests <= 0) {
    std::fprintf(stderr, "usage: uvrpc_sync_ping_pong [requests>0]\n");
    return 2;
  }

  std::fprintf(stderr,
               "note: blocking await runs the shared loop inline in the "
               "caller; one request in flight, 8-byte echo over loopback TCP. "
               "Same call style as grpc_ping_pong's synchronous unary client.\n");

  char address[128];
  std::snprintf(address, sizeof(address), "tcp://127.0.0.1:%d", kPort);

  uv_loop_t loop{};
  if (uv_loop_init(&loop) != 0) {
    std::fprintf(stderr, "uv_loop_init failed\n");
    return 1;
  }

  uvbus_loop_registry_t* registry = uvbus_loop_registry_new();
  uvrpc_config_t* sconfig = uvrpc_config_new();
  sconfig = uvrpc_config_set_loop(sconfig, &loop);
  sconfig = uvrpc_config_set_loop_registry(sconfig, registry);
  sconfig = uvrpc_config_set_address(sconfig, address);
  sconfig = uvrpc_config_set_transport(sconfig, UVBUS_TRANSPORT_TCP);
  uvrpc_server_t* server = uvrpc_server_create(sconfig);
  if (!server) {
    std::fprintf(stderr, "server_create failed\n");
    return 1;
  }
  uvrpc_server_register(server, "echo", echo_handler, nullptr);
  if (uvrpc_server_start(server) != 0) {
    std::fprintf(stderr, "server_start failed\n");
    return 1;
  }
  for (int i = 0; i < 10; i++) uv_run(&loop, UV_RUN_NOWAIT);

  uvrpc_config_t* cconfig = uvrpc_config_new();
  cconfig = uvrpc_config_set_loop(cconfig, &loop);
  cconfig = uvrpc_config_set_loop_registry(cconfig, registry);
  cconfig = uvrpc_config_set_address(cconfig, address);
  cconfig = uvrpc_config_set_transport(cconfig, UVBUS_TRANSPORT_TCP);
  uvrpc_client_t* client = uvrpc_client_create(cconfig);
  if (!client || uvrpc_client_connect(client) != 0) {
    std::fprintf(stderr, "client create/connect failed\n");
    return 1;
  }
  for (int i = 0; i < 50; i++) uv_run(&loop, UV_RUN_NOWAIT);

  uvrpc_async_ctx_t* ctx = uvrpc_async_ctx_new(&loop);
  if (!ctx) {
    std::fprintf(stderr, "async_ctx_new failed\n");
    return 1;
  }
  // The async block's timer covers the whole block, not one call. Derive its
  // deadline from this host's warmup rate instead of assuming a per-call cost;
  // a fixed 5 s block expires partway through a 100k-request run.
  if (uvrpc_async_exec(ctx, 30000) != UVRPC_OK) {
    std::fprintf(stderr, "async_exec failed\n");
    return 1;
  }

  const uint8_t payload[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  auto call = [&]() {
    uvrpc_async_result_t* result = nullptr;
    int status = uvrpc_client_call_async(ctx, client, "echo", payload,
                                         sizeof(payload), &result);
    if (status != UVRPC_OK) {
      std::fprintf(stderr, "blocking call failed: %d\n", status);
      return -1;
    }
    if (!result || result->result_size != sizeof(payload)) {
      std::fprintf(stderr, "echo result size mismatch\n");
      return -1;
    }
    uvrpc_async_result_free(result);
    return 0;
  };

  long long warmup_start = now_ms();
  for (long long i = 0; i < kWarmupRequests; ++i) {
    if (call() != 0) return 1;
  }
  long long warmup_ms = now_ms() - warmup_start;
  if (warmup_ms == 0) warmup_ms = 1;
  long long warmup_us_per_req = warmup_ms * 1000 / kWarmupRequests + 1;
  long long block_timeout_ms = num_requests * warmup_us_per_req * 4 / 1000 + 10000;
  uvrpc_async_cancel_all(ctx);
  if (uvrpc_async_exec(ctx, static_cast<uint64_t>(block_timeout_ms)) != UVRPC_OK) {
    std::fprintf(stderr, "async_exec (measured) failed\n");
    return 1;
  }

  long long received = 0;
  const long long start = now_ms();
  for (long long i = 0; i < num_requests; ++i) {
    if (call() != 0) return 1;
    ++received;
  }
  long long elapsed_ms = now_ms() - start;
  if (elapsed_ms == 0) elapsed_ms = 1;

  const double us_per_req = static_cast<double>(elapsed_ms) * 1000.0 / received;
  const double rps = static_cast<double>(received) / (elapsed_ms / 1000.0);
  std::printf("transport=uvrpc_sync_tcp requests=%lld  received=%lld  "
              "elapsed=%lld ms  (sequential ping-pong)\n",
              num_requests, received, elapsed_ms);
  std::printf("  round-trip latency: %.2f us/req\n", us_per_req);
  std::printf("  throughput (= 1/latency, 1 in-flight): %.0f req/s\n", rps);

  uvrpc_async_cancel_all(ctx);
  uvrpc_async_ctx_free(ctx);
  uvrpc_client_disconnect(client);
  uvrpc_client_free(client);
  uvrpc_server_stop(server);
  uvrpc_server_free(server);
  uvrpc_config_free(sconfig);
  uvrpc_config_free(cconfig);
  uvbus_loop_registry_free(registry);
  uv_run(&loop, UV_RUN_DEFAULT);
  uv_loop_close(&loop);
  return 0;
}
