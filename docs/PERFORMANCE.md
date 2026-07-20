# Performance baseline

Performance is tracked at two levels because a protocol microbenchmark cannot
predict Steam relay throughput.

## Protocol baseline

The repository benchmark was run on 2026-07-17 with an Apple M3 Pro, macOS
15.5, Clang 21.1.8, `-O3`, and a 1,200-byte payload. The table reports the
median of three consecutive five-million-iteration runs:

| Operation | Operations/second |
| --- | ---: |
| VPN envelope encode | 27,589,321 |
| VPN envelope decode | 1,559,596,227 |
| TCP multiplex encode | 25,827,041 |
| TCP multiplex decode | 311,804,928 |

These numbers are a comparison baseline, not a CI threshold. Run the benchmark
on the same machine before and after changes:

```sh
cmake -S . -B build/perf -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCONNECTTOOL_BUILD_APPLICATION=OFF \
  -DCONNECTTOOL_BUILD_BENCHMARKS=ON \
  -DBUILD_TESTING=OFF
cmake --build build/perf --target connecttool_protocol_benchmark
./build/perf/connecttool_protocol_benchmark
```

Decode is intentionally allocation-free and returns payload spans into the
input frame, which explains the large difference between encode and decode.

## End-to-end baseline

The earlier 1.5.x README recorded 7.02 Mbit/s sender and 6.85 Mbit/s receiver in
one `iperf3` TUN session. Its peer hardware and relay route were not recorded,
so it is historical context rather than a controlled before/after result.

For a useful comparison, record both peers, whether the connection is direct or
relayed, the selected POP, and run at least three 30-second samples in each
direction. The current implementation also removes two avoidable costs that the
old measurement included:

- a local TCP socket is read only by `MultiplexSession`, eliminating duplicate
  asynchronous reads and duplicate forwarding;
- per-client read buffers are 64 KiB instead of 1 MiB, reducing retained memory
  and cache pressure while preserving stream throughput.
