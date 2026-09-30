# INPROC Transport Guide

## What is INPROC?

INPROC (In-Process) is a high-performance transport layer for UVRPC that enables zero-copy communication between server and client **within the same process**.

## Key Characteristics

- **Zero-copy**: No data serialization or network overhead
- **Highest performance**: 125,000+ ops/s with 0.03ms latency
- **In-process only**: Server and client must run in the same process
- **Shared event loop**: Both server and client use the same libuv event loop

## Use Cases

1. **Modular architecture**: Communication between modules within a single application
2. **Testing and debugging**: Easy to test RPC logic without network setup
3. **High-performance in-process services**: When you need RPC semantics but don't need network

## Important Limitations

⚠️ **INPROC only works when server and client are in the SAME process.**

- You cannot run `simple_server` and `simple_client` as separate processes with INPROC
- The endpoint hash table is stored in process memory and not shared across processes
- For inter-process communication, use TCP, IPC, or UDP instead

## How to Use

See `examples/simple_inproc.c` for a complete example:

```c
// Create event loop (shared by both server and client)
uv_loop_t loop;
uv_loop_init(&loop);

// The registry is how INPROC peers find each other. Create one and hand it to
// every config that has to see the others.
uvbus_loop_registry_t* registry = uvbus_loop_registry_new();

// Create server
uvrpc_config_t* server_config = uvrpc_config_new();
uvrpc_config_set_loop(server_config, &loop);
uvrpc_config_set_loop_registry(server_config, registry);
uvrpc_config_set_address(server_config, "inproc://my_service");
uvrpc_config_set_transport(server_config, UVRPC_TRANSPORT_INPROC);

uvrpc_server_t* server = uvrpc_server_create(server_config);
uvrpc_server_register(server, "add", add_handler, &server_ctx);
uvrpc_server_start(server);

// Create client (in the same process)
uvrpc_config_t* client_config = uvrpc_config_new();
uvrpc_config_set_loop(client_config, &loop);
uvrpc_config_set_loop_registry(client_config, registry);   // the same one
uvrpc_config_set_address(client_config, "inproc://my_service");
uvrpc_config_set_transport(client_config, UVRPC_TRANSPORT_INPROC);

uvrpc_client_t* client = uvrpc_client_create(client_config);
uvrpc_client_connect_with_callback(client, on_connect, &client_ctx);

// Run event loop
uv_run(&loop, UV_RUN_DEFAULT);
```

## Running the Example

```bash
# Build
make simple_inproc

# Run (server and client in same process)
./dist/bin/simple_inproc

# With custom endpoint
./dist/bin/simple_inproc inproc://custom_endpoint
```

## Common Errors

### Error: "Endpoint not found"

```
[INPROC] ERROR: Endpoint 'my_service' not found.
[INPROC] INPROC transport is for in-process communication only.
[INPROC] Make sure the server is running in the same process as the client.
```

**Solution**: Ensure both server and client are running in the same process. Use `simple_inproc.c` as a reference.

### Error: Connection failed

If you try to connect from a different process, you will get `UVBUS_ERROR_NOT_FOUND` (-8) or `UVBUS_ERROR_IO` (-6).

**Solution**: Use TCP, IPC, or UDP for inter-process communication.

## Comparison with Other Transports

| Transport | Process Scope | Round-trip latency | Use Case |
|-----------|--------------|--------------------|----------|
| INPROC | Single process | ~1.00 µs | In-process modules |
| SAMELOOP | Single loop | ~0.98 µs | Same-loop fast path |
| IPC | Cross-process (same host) | ~10.98 µs | Local IPC |
| UDP | Cross-process | ~20.22 µs | Broadcast/Unreliable |
| TCP | Cross-process | ~19.92 µs | Network communication |

Measured by the `Benchmark` workflow on a GitHub Actions runner (run
`36658530099`) — sequential ping-pong, one request in flight, so these are
latency figures and not concurrent throughput. See
[Benchmark](/guide/benchmark).

## Best Practices

1. **Use INPROC for modular design**: Break your application into modules that communicate via RPC
2. **Share the event loop**: Always use the same `uv_loop_t` for both server and client
3. **Clean up properly**: Free both server and client, then
   `uvbus_loop_registry_free(reg)`, then close the event loop
4. **Testing**: Great for unit tests and integration tests

## See Also

- `examples/simple_inproc.c` - Complete working example
- `docs/api/index.md` - API documentation
- `README.md` - Project overview