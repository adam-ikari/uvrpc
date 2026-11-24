// gRPC side of the UVRPC-vs-gRPC comparison benchmark.
//
// Mirrors benchmark/perf_benchmark.c's methodology on purpose:
//   - strict sequential ping-pong: one request in flight, wait for its
//     response before sending the next (synchronous unary RPC does exactly
//     this by blocking the calling thread);
//   - 8-byte echo payload;
//   - WARMUP_REQUESTS calls excluded from timing;
//   - reported throughput is the reciprocal of round-trip latency, not
//     pipelined throughput.
//
// Differences that are part of what is being measured, not hidden:
//   - gRPC core manages its own threads; UVRPC runs on one libuv loop;
//   - protobuf + gRPC/HTTP2 framing sit under the 8-byte payload.
// So this is comparable to UVRPC's tcp transport over loopback, not to
// INPROC/SAMELOOP (gRPC has no in-process transport).
//
// Usage: ./grpc_ping_pong [requests] [port]
//   requests: default 100000
//   port:     default 16557 (UVRPC perf_benchmark uses 16555)

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include <grpcpp/grpcpp.h>

#include "echo.grpc.pb.h"
#include "echo.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::Status;
using uvrpc::benchmark::Echo;
using uvrpc::benchmark::EchoRequest;
using uvrpc::benchmark::EchoResponse;

namespace {

constexpr long long kWarmupRequests = 1000;  // same as perf_benchmark.c
constexpr long long kDefaultRequests = 100000;
constexpr int kDefaultPort = 16557;
const uint8_t kPayload[8] = {0, 0, 0, 0, 0, 0, 0, 0};

long long now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

class EchoServiceImpl final : public Echo::Service {
  Status Echo(grpc::ServerContext*, const EchoRequest* req,
              EchoResponse* resp) override {
    resp->set_payload(req->payload());
    return Status::OK;
  }
};

}  // namespace

int main(int argc, char** argv) {
  long long num_requests = kDefaultRequests;
  int port = kDefaultPort;
  if (argc >= 2) num_requests = std::strtoll(argv[1], nullptr, 10);
  if (argc >= 3) port = std::atoi(argv[2]);
  if (num_requests <= 0 || port <= 0 || port > 65535) {
    std::cerr << "usage: grpc_ping_pong [requests>0] [port 1..65535]\n";
    return 2;
  }

  std::cerr << "note: synchronous unary calls over loopback TCP, one request in "
               "flight; 8-byte payload inside protobuf + gRPC/HTTP2 framing. "
               "gRPC core uses its own threads, UVRPC uses one libuv loop. "
               "Compare against UVRPC tcp, not INPROC/SAMELOOP.\n";

  EchoServiceImpl service;
  ServerBuilder builder;
  const std::string addr = "127.0.0.1:" + std::to_string(port);
  builder.AddListeningPort(addr, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server(builder.BuildAndStart());
  if (!server) {
    std::cerr << "server BuildAndStart failed on " << addr << "\n";
    return 1;
  }
  // gRPC's sync server processes requests on core-managed threads; Wait()
  // only keeps the server alive until Shutdown().
  std::thread server_thread([&] { server->Wait(); });

  std::shared_ptr<Channel> channel =
      grpc::CreateChannel(addr, grpc::InsecureChannelCredentials());
  std::unique_ptr<Echo::Stub> stub = Echo::NewStub(channel);

  EchoRequest req;
  req.set_payload(reinterpret_cast<const char*>(kPayload), sizeof(kPayload));
  EchoResponse resp;

  // One in-flight call: set a per-call deadline so a wedged server fails
  // instead of hanging the whole run. The budget is widened from the warmup
  // rate below, never assumed from a fixed per-request cost.
  auto call = [&](long long deadline_ms) {
    ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() +
                     std::chrono::milliseconds(deadline_ms));
    resp.Clear();
    return stub->Echo(&ctx, req, &resp);
  };

  // Warmup (excluded from timing), same count as perf_benchmark.c.
  const long long warmup_start = now_ms();
  for (long long i = 0; i < kWarmupRequests; ++i) {
    Status s = call(5000);
    if (!s.ok() || resp.payload().size() != sizeof(kPayload)) {
      std::cerr << "warmup call " << i << " failed: " << s.error_message()
                << "\n";
      server->Shutdown();
      server_thread.join();
      return 1;
    }
  }
  long long warmup_ms = now_ms() - warmup_start;
  if (warmup_ms == 0) warmup_ms = 1;
  const long long warmup_us_per_req = warmup_ms * 1000 / kWarmupRequests + 1;
  // Derived from the host's own warmup rate, with generous slack.
  const long long call_deadline_ms =
      std::max<long long>(5000, warmup_us_per_req * 20 / 1000);

  // Measured run: strict sequential ping-pong, exactly one request in flight.
  long long received = 0;
  const long long start = now_ms();
  for (long long i = 0; i < num_requests; ++i) {
    Status s = call(call_deadline_ms);
    if (!s.ok()) {
      std::cerr << "call " << i << " failed: " << s.error_message() << "\n";
      server->Shutdown();
      server_thread.join();
      return 1;
    }
    if (resp.payload().size() != sizeof(kPayload)) {
      std::cerr << "call " << i << " echoed " << resp.payload().size()
                << " bytes, expected " << sizeof(kPayload) << "\n";
      server->Shutdown();
      server_thread.join();
      return 1;
    }
    ++received;
  }
  long long elapsed_ms = now_ms() - start;
  if (elapsed_ms == 0) elapsed_ms = 1;

  const double us_per_req = static_cast<double>(elapsed_ms) * 1000.0 / received;
  const double rps = static_cast<double>(received) / (elapsed_ms / 1000.0);

  std::cout << "transport=grpc_tcp   requests=" << num_requests
            << "  received=" << received << "  elapsed=" << elapsed_ms
            << " ms  (sequential ping-pong)\n";
  std::cout << "  round-trip latency: " << std::fixed << std::setprecision(2)
            << us_per_req << " us/req\n";
  std::cout << "  throughput (= 1/latency, 1 in-flight): " << std::setprecision(0)
            << rps << " req/s\n";

  server->Shutdown();
  server_thread.join();
  return 0;
}
