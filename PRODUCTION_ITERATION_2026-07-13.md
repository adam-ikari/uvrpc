# Production Iteration Milestone — 2026-07-13

This iteration hardened UVRPC to production quality across the library, tests,
CI, documentation, and tooling. All changes are verified; the summary below is
the durable evidence record.

## Library quality

- **Design philosophy conformance (Zero threads / Zero locks / Zero globals):**
  - `src/` has zero file-scope mutable globals in system/mimalloc builds. The
    only one, `g_custom_allocator` (`src/uvrpc_allocator.c:49`), is `#if`-guarded
    and compiled out in system builds (`nm` confirms 0 symbols in the .o).
  - INPROC transport has no `pthread`/`rwlock`/`atomic` (grep count: 0). The
    global endpoint hash + rwlock were replaced with a per-loop registry on
    `loop->data` (`src/uvbus_loop_registry.h`); SAMELOOP's global registry
    likewise. Both are lock-free (single-threaded by design).
  - Allocator dispatches at compile time (`-DUVRPC_DEFAULT_ALLOCATOR`); no
    runtime `g_allocator_type` global.
- **C99 conformance:** every C target (library, 28 examples, all C tests, 2
  benchmarks) compiles with `-std=gnu99`. No C11/C17 anywhere. No C11 features
  (`_Static_assert`/`_Generic`/`_Atomic`/`<stdatomic.h>`) in source.
- **Warning-clean:** builds with `-Wall -Wextra -Wpedantic` with zero warnings
  from `src/` (the only link-time notes are the pre-existing libuv glibc
  static-link NSS warnings, from libuv's `core.c`, not our code).
- **Zero debug spam:** Release builds emit zero library debug output.

## Tests

- **107/107 tests pass** (`ctest`, ~5s).
- All 5 transports end-to-end: TCP, UDP, IPC, INPROC, SAMELOOP
  (`test_tcp`, `test_udp`, `test_ipc`, `test_inproc`, `test_stream_e2e`) + the
  transport-lifetime regression test + broadcast (8/8).
- **ASAN-clean:** server-freed-before-client teardown (the UAF/double-free
  scenario) verified under AddressSanitizer for both INPROC and SAMELOOP — no
  leaks, no UAF, no double-free.
- Bug fixes from the iteration's code reviews: INPROC UAF/double-free (detach
  clients instead of freeing), SAMELOOP dangling back-pointer, `inproc_broadcast`
  OOM return, SAMELOOP transport leak, UDP transport recv_ctx dispatch bug + UAF.

## Performance (sequential ping-pong, 8-byte payload, Release, single thread)

| Transport | Round-trip latency | Throughput (1/latency) |
|-----------|--------------------|------------------------|
| SAMELOOP  | ~4.1 µs | ~246,000 req/s |
| INPROC    | ~4.1 µs | ~243,000 req/s |
| IPC       | ~31 µs  | ~33,000 req/s |
| UDP       | ~38 µs  | ~26,000 req/s |
| TCP       | ~35 µs  | ~28,000 req/s |

Measured with `benchmark/perf_benchmark` (correct `UV_RUN_ONCE` methodology).

## CI (`.github/workflows/`)

- `ci.yml` — 5 jobs: build-and-test (matrix, warning-gate, ctest),
  debug-logging-build, memory-check (ASan), performance (perf_benchmark),
  static-analysis (cppcheck). All valid YAML.
- `benchmark.yml` — perf_benchmark, results to GitHub Step Summary, weekly cron.
- `deploy-docs.yml` — builds VitePress site, deploys to GitHub Pages.

## Documentation & website

- VitePress site builds clean (`build complete in 9.32s`); no broken symlinks.
- README, `docs/index.md`, `docs/zh/index.md` updated with current numbers and
  correct links.
- Cleaned: deleted stale root reports, ~21 orphaned duplicate docs, the dead
  `uvrpc_benchmark.c` + 11 stale scripts + 6 stale reports + FORK_EXEC doc;
  fixed 6 broken `docs/zh/` symlinks; untracked `docs/node_modules` (1703 files).
- Doc structure: EN root + guide/development/architecture/api subdirs + zh locale.

## Git

All changes are uncommitted working-tree modifications (the iteration's
deliverables): 1833 files changed (dominated by the node_modules untrack),
+1852 / −324714 lines, 5 new untracked source files (perf_benchmark, the
transport-lifetime test, etc.). Ready to commit.
