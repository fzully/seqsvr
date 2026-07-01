# SeqSvr

A high-performance distributed sequence number service for IM backends. Provides monotonically increasing, non-decreasing sequence numbers per user, session, or global key — suitable for message ordering in large-scale chat systems.

## Architecture

```
Client SDK  →  AllocSvr cluster  →  StoreSvr cluster (RocksDB, NRW quorum)
```

- **StoreSvr** — persists `max_seq` per section and manages leases. Three nodes form an NRW quorum (N=3, W=2, R=2). Backed by RocksDB.
- **AllocSvr** — serves allocations entirely from memory. Writes to StoreSvr only when its in-memory batch is exhausted (every `--step` allocations, default 10,000). Holds a 30s lease per section to prevent split-brain.
- **Client SDK** — brpc channel pool with automatic retry and REDIRECT following.

The uid space is partitioned into **sections** of 100,000 UIDs (`section_id = uid / 100_000`). Each AllocSvr node owns a fixed set of sections declared at startup.

## Building

Requires libraries pre-installed to `~/devlibs/` (see `setup.sh` for the one-time setup). `LD_LIBRARY_PATH` must be set for all build and run commands.

```bash
export LD_LIBRARY_PATH=~/devlibs/usr/local/lib:~/devlibs/usr/lib/x86_64-linux-gnu:$LD_LIBRARY_PATH
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

## Running

Start all three StoreSvr nodes first, then AllocSvr:

```bash
./src/storesvr/storesvr --port=8200 --db_path=/tmp/store0
./src/storesvr/storesvr --port=8201 --db_path=/tmp/store1
./src/storesvr/storesvr --port=8202 --db_path=/tmp/store2

./src/allocsvr/allocsvr --port=9100 \
    --store_addrs=127.0.0.1:8200,127.0.0.1:8201,127.0.0.1:8202 \
    --sections=0,1,2 --my_addr=127.0.0.1:9100
```

## Testing

```bash
cd build
ctest --timeout 120        # all suites (unit + integration)
./tests/test_full_flow     # integration test (~50s, starts real brpc servers)
```

## Performance

Benchmark on loopback with 8 threads, 30s duration:

| Metric | Value |
|--------|-------|
| QPS | 9,000 |
| Avg latency | 0.07 ms |
| P50 | 0.06 ms |
| P95 | 0.09 ms |
| P99 | 0.20 ms |
| P99.9 | 0.33 ms |

Run your own benchmark:

```bash
./tests/bench_client --allocsvr=127.0.0.1:9100 --threads=8 --duration=30
```

## Dependencies

- [brpc](https://github.com/apache/brpc) — RPC framework (built from source)
- [RocksDB](https://rocksdb.org/) — embedded storage for StoreSvr
- [protobuf](https://protobuf.dev/) 3.21 — protocol definition
- [gflags](https://gflags.github.io/gflags/) — command-line flags
- [GTest](https://github.com/google/googletest) 1.14 — unit and integration tests
