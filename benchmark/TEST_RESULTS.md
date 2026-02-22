# UVRPC Benchmark Test Results

## Summary

已完成 benchmark 对各种传输层的测试工作，并移除了 pump timer 机制以简化设计。

## 性能测试结果

### TCP Transport (tcp://127.0.0.1:20000)

#### Regular RPC
- **Throughput**: 149K - 152K ops/sec
- **Error Rate**: 0%
- **Status**: ✅ Stable

#### Oneway RPC
- **Throughput**: ~10K ops/sec
- **Status**: ⚠️ Limited by 1s stop timer

### IPC Transport (ipc:///tmp/uvrpc_test)

#### Issues Found
- Socket cleanup issue: `address already in use` error
- Client connection hangs after connecting
- Status: ⚠️ **Needs debugging**

### UDP Transport (udp://127.0.0.1:20000)

#### Status
- Server receives data successfully
- Client connection detection not working (TCP socket check)
- Status: ⚠️ Connection detection issue

### INPROC Transport (inproc://uvrpc_test)

#### Issues Found
- Client connection detection not working (TCP socket check)
- Status: ⚠️ Connection detection issue

## Code Changes

### Removed Pump Timer Mechanism

Following the "less is more" principle, the pump timer mechanism has been removed from the transport layer:

**Files Modified:**
1. `src/uvbus_transport_tcp.c`
   - Removed `pump_timer` field from `uvbus_tcp_client_t`
   - Removed `on_pump_timer()` callback function
   - Removed timer initialization and cleanup code
   - Removed timer start in `tcp_send()`

2. `include/uvbus.h`
   - Removed `pump_interval` field from `uvbus_transport_t`
   - Removed `uvbus_config_set_pump_interval()` declaration

3. `include/uvbus_config.h`
   - Removed `UVBUS_DEFAULT_PUMP_INTERVAL_MS` constant

4. `src/uvbus.c`
   - Removed `uvbus_config_set_pump_interval()` implementation

5. `benchmark/benchmark.c`
   - Removed `uvrpc_config_set_pump_interval()` call from oneway test
   - Modified server ready check to skip TCP socket check for non-TCP transports

### IPC Transport Fix

**File Modified:**
- `src/uvbus_transport_ipc.c`
  - Added `unlink(socket_path)` before binding to remove stale socket files

## Known Issues

### 1. IPC Client Connection Hang
**Symptoms:**
- Server binds successfully
- Client connects but hangs
- No throughput results

**Possible Causes:**
- Read callback not being triggered
- Pipe handle data pointer issue
- Event loop not running properly

**Next Steps:**
- Add debug logging to trace connection flow
- Verify pipe_handle.data is set correctly
- Check if uv_read_start is called after connection

### 2. Non-TCP Connection Detection
**Symptoms:**
- Benchmark uses TCP socket to check server readiness
- Doesn't work for IPC/UDP/INPROC
- Test starts too early or fails

**Fix Applied:**
- Modified benchmark to skip TCP socket check for non-TCP transports
- Still may have timing issues

### 3. UDP Transport
**Symptoms:**
- Server receives data (visible in logs)
- No throughput results
- Connection detection issue

**Note:** UDP is connectionless, may need different testing approach

### 4. INPROC Transport
**Symptoms:**
- Similar to IPC - client hangs
- Connection detection issue

**Note:** INPROC uses function calls, may need different testing approach

## Design Decisions

### Pump Timer Removal
**Rationale:**
- Added complexity to transport layer
- Violated "less is more" principle
- Pumping should be handled at RPC layer, not transport layer
- Simpler design is more maintainable

### TCP Socket Check Skip
**Rationale:**
- Non-TCP transports don't use TCP sockets
- Socket check would always fail
- May need transport-specific detection methods

## Next Steps

1. **Fix IPC Transport**
   - Debug client connection hang
   - Verify pipe handle lifecycle
   - Add comprehensive logging

2. **Implement Transport-Specific Server Detection**
   - IPC: Check socket file existence
   - UDP: Send probe packet
   - INPROC: Check endpoint registration

3. **Performance Optimization**
   - Investigate why Oneway is slower than Regular
   - Remove artificial 1s stop timer
   - Measure true throughput

4. **Add UDP/INPROC Testing**
   - Create appropriate test methods
   - Verify message delivery
   - Measure performance

## Conclusion

TCP transport works well with high throughput (150K+ ops/sec). IPC, UDP, and IN transports have connection detection and client hang issues that need debugging. The pump timer removal simplifies the design as requested.