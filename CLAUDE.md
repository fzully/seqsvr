# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build

All binaries depend on libraries in `~/devlibs/` (no system-wide install). **`LD_LIBRARY_PATH` must be set for every build and run command.**

```bash
export LD_LIBRARY_PATH=~/devlibs/usr/local/lib:~/devlibs/usr/lib/x86_64-linux-gnu:$LD_LIBRARY_PATH
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release   # or Debug
make -j$(nproc)
```

## Tests

```bash
export LD_LIBRARY_PATH=~/devlibs/usr/local/lib:~/devlibs/usr/lib/x86_64-linux-gnu:$LD_LIBRARY_PATH
cd build
ctest --timeout 120           # all suites
ctest -R test_section_state   # single unit suite
./tests/test_full_flow        # integration test (takes ~50s, starts real brpc servers)
```

Unit suites: `test_section_state`, `test_nrw_coordinator`, `test_seq_client`.
Integration suite: `test_full_flow` — 3 StoreSvr on ports 18200-18202, 1 AllocSvr on 19100.

## Running manually

StoreSvr must be started first (all 3), then AllocSvr:

```bash
./src/storesvr/storesvr --port=8200 --db_path=/tmp/store0
./src/storesvr/storesvr --port=8201 --db_path=/tmp/store1
./src/storesvr/storesvr --port=8202 --db_path=/tmp/store2
./src/allocsvr/allocsvr --port=9100 \
    --store_addrs=127.0.0.1:8200,127.0.0.1:8201,127.0.0.1:8202 \
    --sections=0,1,2 --my_addr=127.0.0.1:9100
```

Performance benchmark (outputs `perf_report.md`):
```bash
./tests/bench_client --allocsvr=127.0.0.1:9100 --threads=8 --duration=30
```

## Architecture

Three-layer system: **Client SDK → AllocSvr cluster → StoreSvr cluster**

**Section** is the shard unit: `section_id = uid / 100_000`. Each AllocSvr owns a fixed set of sections declared at startup via `--sections`.

**AllocSvr** (`src/allocsvr/`):
- `SectionState` — per-section seqno allocator. Holds `cur_seq` and `max_seq` in memory. `TryAlloc` increments `cur_seq` and calls `fetch_fn` (which writes to StoreSvr via NRW) only when `cur_seq > max_seq`. The lock is held through the `fetch_fn` call — this is intentional to prevent concurrent over-allocation.
- `NRWCoordinator` — AllocSvr IS the NRW coordinator (not StoreSvr). Sends to all 3 StoreSvr peers via `std::thread`s (joined, not detached), succeeds on W≥2 confirmations.
- `LeaseHolder` — acquires a 30s lease per section at startup (waits up to 35s for contested leases), renews every 10s in a background thread, releases all leases on `Stop()`.
- `AllocServiceImpl` — maps `uid→section_id`, `session_id→section_id`, `global_key→section_id` via hash. Returns REDIRECT if section not owned.

**StoreSvr** (`src/storesvr/`):
- RocksDB-backed. Key schema: `"maxseq:{id}"` (raw uint64), `"lease:{id}"` (protobuf `LeaseRecord`), `"route_table"` (protobuf `RouteTable`).
- `LeaseManager` — enforces one holder per section. CONFLICT if a different valid holder requests. LEASE_EXPIRED if the caller is not the current holder on renew.

**Client SDK** (`src/client/`):
- `SeqClient` — mutex-protected brpc channel pool. 4 attempts (1 + 3 retries) with 50/100/200ms backoff. Follows REDIRECT responses.

## Critical non-obvious facts

**`option cc_generic_services = true`** in `proto/seqsvr.proto` is mandatory. Without it, protobuf's protoc (proto3 mode) does not generate C++ abstract service base classes, and brpc fails to build.

**protoc is wrapped** in a cmake `add_custom_command` with `env "LD_LIBRARY_PATH=..."` because protoc itself is dynamically linked against devlibs' libprotoc and cannot find it otherwise.

**`SectionState::Reset(max_seq)`** sets `cur_seq_ = max_seq` (not 0). This means the first allocation after startup will trigger a `fetch_fn` call, ensuring StoreSvr is always written before serving clients.

**NRW `RequestLease` response aggregation**: `max_seq` and `expire_time_ms` from StoreSvr nodes are tracked independently when scanning responses. The condition `max_seq > 0` cannot be used to gate `expire_time_ms` because a fresh DB has `max_seq = 0` — both fields must be updated from the winning response regardless.

**`ReleaseLease` uses joined threads** (not detached). Detached threads with raw `this` pointers into `peers_` cause use-after-free on `NRWCoordinator` destruction.

**brpc location**: built from source at `~/devlibs/usr/local/`. All other devlibs deps at `~/devlibs/usr/`. `BRPC_LINK_LIBS` in `CMakeLists.txt` lists the full transitive link set (leveldb, gflags, openssl, protobuf, threads, dl, z).
