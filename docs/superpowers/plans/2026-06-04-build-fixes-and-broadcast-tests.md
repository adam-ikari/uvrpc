# UVRPC Build Fixes and Test Coverage Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix all build issues (shared library, GTest tests) and add missing broadcast test coverage for UDP, IPC, and SAMELOOP transports.

**Architecture:**
- Rebuild libuv with `-fPIC` flag to enable shared library linking
- Fix FlatBuffers field name mismatch (`params` → `data`) in GTest unit tests
- Remove debug printf statements from IPC transport
- Extend broadcast test suite to cover all 5 transports

**Tech Stack:** C99, CMake, libuv, FlatCC (FlatBuffers), GTest (C++)

---

## File Structure

| File | Action | Purpose |
|------|--------|---------|
| `deps/libuv/CMakeLists.txt` | Modify | Add `-fPIC` for static library |
| `tests/unit/test_flatbuffers.cpp` | Modify | Fix `params` → `data` field name |
| `src/uvbus_transport_ipc.c` | Modify | Remove debug printf calls |
| `tests/uvbus_broadcast_test.c` | Modify | Add UDP, IPC, SAMELOOP tests |
| `docs/superpowers/plans/` | Create | This plan file |

---

### Task 1: Rebuild libuv with -fPIC for Shared Library Support

**Files:**
- Modify: `deps/libuv/CMakeLists.txt`
- Test: Build `uvrpc_shared` target

- [ ] **Step 1: Add -fPIC flag to libuv static library**

Edit `deps/libuv/CMakeLists.txt` to add position-independent code for the static library target. Find the `uv_a` target definition and add:

```cmake
# After the uv_a static library is defined, add:
set_target_properties(uv_a PROPERTIES POSITION_INDEPENDENT_CODE ON)
```

If the target is defined with `add_library(uv_a STATIC ...)`, add the line right after that definition.

- [ ] **Step 2: Clean and rebuild libuv**

```bash
cd /home/gem/project/uvrpc/deps/libuv/build
rm -rf *
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

Expected: libuv.a rebuilt with -fPIC

- [ ] **Step 3: Rebuild uvrpc_shared and verify it links**

```bash
cd /home/gem/project/uvrpc/build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc) uvrpc_shared
```

Expected: `dist/lib/libuvrpc.so.0.1.0` created without link errors

- [ ] **Step 4: Commit**

```bash
git add deps/libuv/CMakeLists.txt
git commit -m "fix(deps): build libuv with -fPIC for shared library support"
```

---

### Task 2: Fix GTest FlatBuffers Field Name Mismatch

**Files:**
- Modify: `tests/unit/test_flatbuffers.cpp` (lines 48, 67, 85, 114, 141, 177, 188, 222)
- Test: Build and run `uvrpc_tests`

- [ ] **Step 1: Replace all `uvrpc_RpcFrame_params` with `uvrpc_RpcFrame_data`**

In `tests/unit/test_flatbuffers.cpp`, replace all 8 occurrences:

```bash
sed -i 's/uvrpc_RpcFrame_params/uvrpc_RpcFrame_data/g' tests/unit/test_flatbuffers.cpp
```

Manual verification - the changes should be:
- Line 48: `uvrpc_RpcFrame_data(frame)` instead of `uvrpc_RpcFrame_params(frame)`
- Line 67: `uvrpc_RpcFrame_data(frame)` instead of `uvrpc_RpcFrame_params(frame)`
- Line 85: `uvrpc_RpcFrame_data(frame)` instead of `uvrpc_RpcFrame_params(frame)`
- Line 114: `uvrpc_RpcFrame_data(frame)` instead of `uvrpc_RpcFrame_params(frame)`
- Line 141: `uvrpc_RpcFrame_data(frame)` instead of `uvrpc_RpcFrame_params(frame)`
- Line 177: `uvrpc_RpcFrame_data(frame)` instead of `uvrpc_RpcFrame_params(frame)`
- Line 188: `uvrpc_RpcFrame_data(frame)` instead of `uvrpc_RpcFrame_params(frame)`
- Line 222: `uvrpc_RpcFrame_data(frame)` instead of `uvrpc_RpcFrame_params(frame)`

- [ ] **Step 2: Build GTest unit tests**

```bash
cd /home/gem/project/uvrpc/build
cmake .. -DUVRPC_BUILD_TESTS=ON
make -j$(nproc) uvrpc_tests
```

Expected: Compilation succeeds without `uvrpc_RpcFrame_params` undeclared errors

- [ ] **Step 3: Run GTest unit tests**

```bash
./dist/bin/uvrpc_tests
```

Expected: All flatbuffers tests pass (may have other unrelated test failures)

- [ ] **Step 4: Commit**

```bash
git add tests/unit/test_flatbuffers.cpp
git commit -m "fix(test): use correct FlatBuffers field name 'data' instead of 'params'"
```

---

### Task 3: Remove Debug Printf from IPC Transport

**Files:**
- Modify: `src/uvbus_transport_ipc.c` (lines 231, 252, 256)
- Test: Build and verify no printf in binary

- [ ] **Step 1: Remove the three debug printf calls**

In `src/uvbus_transport_ipc.c`, delete these lines:

Line 231:
```c
    printf("[IPC_CB] ENTER: status=%d, connect_cb=%p, callback_ctx=%p\n",
           status, (void*)transport->connect_cb, (void*)transport->callback_ctx);
```

Line 252:
```c
        printf("[IPC_CB] Calling connect_cb with UVBUS_OK\n");
```

Line 256:
```c
        printf("[IPC_CB] connect_cb returned\n");
```

- [ ] **Step 2: Rebuild and verify**

```bash
cd /home/gem/project/uvrpc/build
make -j$(nproc)
```

Expected: Clean build

- [ ] **Step 3: Verify no debug strings in binary**

```bash
strings dist/lib/libuvrpc.a | grep -c "\[IPC_CB\]"
```

Expected: Output is `0` (no matches)

- [ ] **Step 4: Commit**

```bash
git add src/uvbus_transport_ipc.c
git commit -m "fix(ipc): remove debug printf statements from production code"
```

---

### Task 4: Add UDP Broadcast Test

**Files:**
- Modify: `tests/uvbus_broadcast_test.c`
- Test: Run `uvbus_broadcast_test`

- [ ] **Step 1: Add UDP broadcast test function**

Add after `test_broadcast_tcp_multi_client` (around line 230):

```c
/**
 * Test 5: UDP multi-client broadcast - point-to-point send to each client
 */
void test_broadcast_udp_multi_client(void) {
    run_multi_client_broadcast_test(
        "test_broadcast_udp_multi_client",
        UVBUS_TRANSPORT_UDP,
        "udp://127.0.0.1:15562"
    );
}
```

- [ ] **Step 2: Add test to main()**

In `main()`, add the new test after the TCP test:

```c
    test_broadcast_udp_multi_client();
```

- [ ] **Step 3: Rebuild and run**

```bash
cd /home/gem/project/uvrpc/build
make -j$(nproc) uvbus_broadcast_test
./dist/bin/uvbus_broadcast_test
```

Expected: 6 tests total, all pass (including new UDP test)

- [ ] **Step 4: Commit**

```bash
git add tests/uvbus_broadcast_test.c
git commit -m "test(broadcast): add UDP multi-client broadcast test"
```

---

### Task 5: Add IPC Broadcast Test

**Files:**
- Modify: `tests/uvbus_broadcast_test.c`
- Test: Run `uvbus_broadcast_test`

- [ ] **Step 1: Add IPC broadcast test function**

Add after `test_broadcast_udp_multi_client`:

```c
/**
 * Test 6: IPC multi-client broadcast - shared buffer with refcount
 */
void test_broadcast_ipc_multi_client(void) {
    run_multi_client_broadcast_test(
        "test_broadcast_ipc_multi_client",
        UVBUS_TRANSPORT_IPC,
        "ipc://@uvrpc_broadcast_ipc_test"
    );
}
```

- [ ] **Step 2: Add test to main()**

In `main()`, add after UDP test:

```c
    test_broadcast_ipc_multi_client();
```

- [ ] **Step 3: Rebuild and run**

```bash
cd /home/gem/project/uvrpc/build
make -j$(nproc) uvbus_broadcast_test
./dist/bin/uvbus_broadcast_test
```

Expected: 7 tests total, all pass (including new IPC test)

- [ ] **Step 4: Commit**

```bash
git add tests/uvbus_broadcast_test.c
git commit -m "test(broadcast): add IPC multi-client broadcast test"
```

---

### Task 6: Add SAMELOOP Broadcast Test

**Files:**
- Modify: `tests/uvbus_broadcast_test.c`
- Test: Run `uvbus_broadcast_test`

- [ ] **Step 1: Add SAMELOOP broadcast test function**

Add after `test_broadcast_ipc_multi_client`:

```c
/**
 * Test 7: SAMELOOP multi-client broadcast - direct callback, no serialization
 */
void test_broadcast_sameloop_multi_client(void) {
    run_multi_client_broadcast_test(
        "test_broadcast_sameloop_multi_client",
        UVBUS_TRANSPORT_SAMELOOP,
        "sameloop://broadcast_test"
    );
}
```

- [ ] **Step 2: Add test to main()**

In `main()`, add after IPC test:

```c
    test_broadcast_sameloop_multi_client();
```

- [ ] **Step 3: Rebuild and run**

```bash
cd /home/gem/project/uvrpc/build
make -j$(nproc) uvbus_broadcast_test
./dist/bin/uvbus_broadcast_test
```

Expected: 8 tests total, all pass (including new SAMELOOP test)

- [ ] **Step 4: Commit**

```bash
git add tests/uvbus_broadcast_test.c
git commit -m "test(broadcast): add SAMELOOP multi-client broadcast test"
```

---

### Task 7: Final Verification

**Files:**
- None (verification only)

- [ ] **Step 1: Run full test suite**

```bash
cd /home/gem/project/uvrpc/build
cmake .. -DUVRPC_BUILD_TESTS=ON
make -j$(nproc)
./dist/bin/uvbus_broadcast_test
./dist/bin/uvbus_test
./dist/bin/uvrpc_tests
```

Expected: All tests pass

- [ ] **Step 2: Verify shared library exists**

```bash
ls -la dist/lib/libuvrpc.so*
```

Expected: `libuvrpc.so.0.1.0` and symlinks exist

- [ ] **Step 3: Final commit (if any remaining changes)**

```bash
git status
git add -A
git commit -m "chore: final cleanup after build fixes and test coverage"
```

---

## Self-Review Checklist

**1. Spec coverage:**
- [x] Shared library build fix - Task 1
- [x] GTest compilation fix - Task 2
- [x] Debug printf removal - Task 3
- [x] UDP broadcast test - Task 4
- [x] IPC broadcast test - Task 5
- [x] SAMELOOP broadcast test - Task 6
- [x] Final verification - Task 7

**2. Placeholder scan:**
- [x] No TBD/TODO placeholders
- [x] All code blocks contain actual code
- [x] All file paths are exact
- [x] All commands have expected output

**3. Type consistency:**
- [x] `uvbus_transport_type_t` enum values match header (UVBUS_TRANSPORT_TCP, etc.)
- [x] `uvbus_recv_callback_t` signature matches header (4 params: data, size, client_ctx, server_ctx)
- [x] Test function names follow existing pattern (`test_broadcast_*_multi_client`)
