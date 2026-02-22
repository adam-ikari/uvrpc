# uvrpcc - UVRPC DSL Code Generator

## Overview

`uvrpcc` is the UVRPC DSL code generator that parses FlatBuffers RPC schema files and generates type-safe RPC server stubs and client code. It bundles FlatCC for seamless serialization/deserialization code generation.

## Features

- ✅ Parse standard FlatBuffers `rpc_service` syntax
- ✅ Generate type-safe server stub code
- ✅ Generate type-safe client code
- ✅ Bundled FlatCC for serialization
- ✅ Jinja2-based templating
- ✅ Support for both RPC and broadcast modes
- ✅ PyInstaller packaging for standalone distribution

## Architecture

### Components

```
uvrpcc
├── RPCParser       - Parse FlatBuffers schema
├── RPCGenerator    - Generate code from templates
├── FlatCC (bundled) - Generate serialization code
└── Templates        - Jinja2 code templates
```

### Generated Files

#### RPC Mode (Server/Client)
For each service, generates:
- `{namespace}_{service}_api.h` - API header file
- `{namespace}_{service}_rpc_common.h` - Common RPC header
- `{namespace}_{service}_rpc_common.c` - Common RPC implementation
- `{namespace}_{service}_server_stub.c` - Server stub
- `{namespace}_{service}_client.c` - Client code

#### Broadcast Mode
For each broadcast service, generates:
- `{namespace}_{service}_api.h` - Broadcast API header
- `{namespace}_{service}_broadcast_publisher.c` - Publisher code
- `{namespace}_{service}_broadcast_subscriber.c` - Subscriber code

## Usage

### Basic Usage

```bash
python3 tools/uvrpcc.py schema/benchmark_server.fbs -o generated
```

### Standalone Executable (PyInstaller)

```bash
# Build standalone generator
cd tools
pyinstaller uvrpcc.spec --onefile

# Use generated executable
./dist/uvrpcc schema/benchmark_server.fbs -o generated
```

### Options

```
uvrpcc [OPTIONS] SCHEMA

Options:
  --flatcc PATH    Path to flatcc compiler (auto-detected if not specified)
  -o, --output DIR  Output directory (default: generated)
  -t, --templates DIR  Templates directory (default: tools/templates)
  SCHEMA            Schema file to process
```

## Schema Format

### RPC Service

```flatbuffers
namespace benchmark;

table AddRequest {
    a: int32;
    b: int32;
}

table AddResponse {
    result: int32;
}

rpc_service BenchmarkService {
    Add(AddRequest):AddResponse;
}
```

### Broadcast Service

```flatbuffers
namespace broadcast;

table NewsMessage {
    title: string;
    content: string;
}

rpc_service BroadcastService {
    PublishNews(NewsRequest):NewsResponse;
}
```

### Oneway Methods

Oneway methods are RPC calls that don't require a response. They are useful for fire-and-forget operations like logging, notifications, or events where the client doesn't need to wait for a result.

**Schema Format:**
Define an oneway method by setting its return type to `EmptyResponse`:

```flatbuffers
namespace benchmark;

table LogRequest {
    level: int32;
    message: string;
    timestamp: int64;
}

/* Empty response for oneway methods */
table EmptyResponse {}

rpc_service BenchmarkService {
    Add(AddRequest):AddResponse;
    Log(LogRequest):EmptyResponse;  /* oneway method */
}
```

**Generated Client Code:**
For oneway methods, the generator creates a function that takes a struct parameter:

```c
/* Oneway method - no response expected */
uvrpc_error_t uvrpc_benchmarkservice_log(uvrpc_client_t* client,
                                          const benchmark_LogRequest_t* request);
```

**Usage Example:**
```c
/* Create log entry struct */
benchmark_LogRequest_t log_req = {
    .level = 1,
    .message = "Application started",
    .timestamp = time(NULL)
};

/* Call oneway method - no callback needed */
uvrpc_error_t ret = uvrpc_benchmarkservice_log(client, &log_req);
if (ret == UVRPC_OK) {
    printf("Log sent successfully (fire-and-forget)\n");
}
```

**Key Differences:**
- Oneway methods use `uvrpc_client_call_oneway()` internally
- No callback parameter in the generated function
- No `*_sync()` variant is generated for oneway methods
- Client doesn't wait for a response from the server

## Generated Code Example

### Client Code

```c
#include "benchmark_benchmarkservice_api.h"

void call_add(benchmark_BenchmarkService_client_t* client, int a, int b) {
    benchmark_AddRequest_start_as_root(&builder);
    benchmark_AddRequest_a_add(&builder, a);
    benchmark_AddRequest_b_add(&builder, b);
    benchmark_AddRequest_end_as_root(&builder);
    
    uint8_t* data;
    size_t size;
    data = flatcc_builder_finalize_buffer(&builder, &size);
    
    benchmark_BenchmarkService_Add(client, data, size, callback, ctx);
    
    flatcc_builder_clear(&builder);
    free(data);
}
```

### Server Stub

```c
#include "benchmark_benchmarkservice_server_stub.h"

void handle_add(uvrpc_server_t* server, 
                 const uint8_t* params, size_t params_size,
                 uvrpc_callback_t callback, void* ctx) {
    benchmark_AddRequest_t* request = benchmark_AddRequest_as_root(params);
    
    int32_t result = request->a + request->b;
    
    benchmark_AddResponse_start_as_root(&builder);
    benchmark_AddResponse_result_add(&builder, result);
    benchmark_AddResponse_end_as_root(&builder);
    
    uint8_t* response_data;
    size_t response_size;
    response_data = flatcc_builder_finalize_buffer(&builder, &response_size);
    
    callback(response_data, response_size, UVRPC_OK, ctx);
    
    flatcc_builder_clear(&builder);
    free(response_data);
}
```

## Templates

The generator uses Jinja2 templates located in `tools/templates/`:

### RPC Templates
- `api.h.j2` - API header template
- `rpc_common.h.j2` - Common RPC header template
- `rpc_common.c.j2` - Common RPC implementation template
- `server_stub.c.j2` - Server stub template
- `client.c.j2` - Client code template

### Broadcast Templates
- `broadcast_api.h.j2` - Broadcast API header template
- `broadcast_publisher.c.j2` - Publisher code template
- `broadcast_subscriber.c.j2` - Subscriber code template

## Building Standalone Generator

```bash
# Install dependencies
pip install jinja2 pyinstaller

# Build with PyInstaller
cd tools
pyinstaller uvrpcc.spec --onefile

# Output: dist/uvrpcc
```

## Design Philosophy

- **Type Safety**: Generated code is strongly typed based on schema
- **Zero Overhead**: Minimal runtime overhead for type safety
- **Standard Syntax**: Uses standard FlatBuffers `rpc_service` syntax
- **Modular**: Each service generates independent code
- **Flexible**: Supports both RPC and broadcast patterns

## Integration with Build System

### CMake Integration

```cmake
# Generate RPC code
add_custom_target(generate_rpc
    COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/uvrpcc.py
        ${CMAKE_SOURCE_DIR}/schema/benchmark_server.fbs
        -o ${CMAKE_SOURCE_DIR}/generated
    DEPENDS ${CMAKE_SOURCE_DIR}/tools/uvrpcc.py
    COMMENT "Generating RPC code"
)

# Add generated files to library
add_library(uvrpc_generated
    ${CMAKE_SOURCE_DIR}/generated/benchmark_benchmarkservice_server_stub.c
    ${CMAKE_SOURCE_DIR}/generated/benchmark_benchmarkservice_client.c
)
```

## Oneway Support

Methods with `EmptyResponse` type are generated to use `uvrpc_client_call_oneway()`:

```flatbuffers
rpc_service LogService {
    Log(LogRequest):EmptyResponse;  /* oneway method */
}
```

Generated code:
```c
void LogService_Log(uvrpc_client_t* client, 
                    benchmark_LogRequest_t* request) {
    uvrpc_client_call_oneway(client, "Log", data, size);
}
```

## Error Handling

The generator reports errors for:
- Invalid schema syntax
- Missing FlatCC compiler
- Template rendering errors
- File write errors

## Performance

Generated code is optimized for:
- **Zero-copy**: Direct buffer access via FlatBuffers
- **Type safety**: Compile-time type checking
- **Minimal overhead**: No runtime reflection
- **Efficient serialization**: FlatCC optimized code

## License

Part of UVRPC project. See main project LICENSE file.

## Contributing

To modify or extend the generator:

1. Edit templates in `tools/templates/`
2. Update `RPCParser` for new syntax support
3. Update `RPCGenerator` for new code patterns
4. Rebuild with PyInstaller

## See Also

- [FlatBuffers Schema Documentation](https://flatbuffers.dev/schema/)
- [FlatBuffers Grammar](https://flatbuffers.dev/grammar/)
- [UVRPC Main Documentation](../README.md)