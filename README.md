# TT Transfer Runtime

A compact C++20 host runtime that serializes transfers from multiple producers through a bounded FIFO and one backend owner. It demonstrates ownership, backpressure, completion, shutdown, and error handling around a byte-addressed scratch region.

The CPU path is self-contained. All **11 host contract tests pass** in Release, ASan/UBSan, and TSan. The pinned TT-UMD adapter also built and passed **five real ttsim process runs**, each checking direct, FIFO, and two-producer 1 KiB readbacks. The three examples linking the real upstream allocator passed. This project has no Tenstorrent hardware results.

```text
Two host producers → mutex/CV FIFO → one owner → CPU byte array
                                              └→ TT-UMD → ttsim
TT-Metal: higher-level device programming/runtime; outside this build
```

## Build and run

Requires a C++20 compiler, CMake ≥3.25, and threads. The CPU path has no vendor dependency.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
./build/transfer_demo
```

If CMake is missing, install tools into an isolated, ignored project environment:

```sh
python3 -m venv .tools
.tools/bin/pip install cmake==3.31.6 ninja==1.11.1.4
export PATH="$PWD/.tools/bin:$PATH"
```

## API and contract

```cpp
using namespace tt_transfer;
Runtime runtime(std::make_unique<CpuBackend>(8192), 4);
Bytes input(1024, std::byte{0x5a});
auto write = runtime.write(0, std::move(input));
auto fence = runtime.fence();
auto read = runtime.read(0, 1024);
write.completion.get();
fence.completion.get();
Bytes result = read.completion.get();
runtime.close_and_drain();
```

| Operation | Observable contract |
| --- | --- |
| `write(offset, Bytes)` | Blocking admission; accepted payload ownership moves to the request. Completion returns an empty byte vector. |
| `read(offset, count)` | Returns an independent byte vector through a future. It may outlive the runtime. |
| `fence()` | Completes after earlier accepted requests and the backend barrier hook. It does not make a write/read pair a transaction. |
| `close_and_drain()` | Rejects new requests, wakes blocked producers, drains accepted requests, and joins without holding the queue mutex. Concurrent calls are safe. |

Sequence numbers describe successful acceptance under one mutex. The owner executes that FIFO order. Up to **Q queued + 1 executing** requests are retained, each payload at most **4 KiB**. Caller results/futures, unaccepted arguments, backend memory, and third-party allocations are outside this bound. Reads/writes are checked against immutable backend extent before I/O, using subtraction to avoid address overflow. Zero-byte operations at the end of the region are valid.

An ordinary backend exception rejects subsequent submissions and finishes the current and pending futures with that exception. Already successful work stays successful. The backend must return or throw for shutdown to finish; destruction must not race callers still using the runtime. Backend callbacks must not call `close_and_drain` on their own owner thread.

## Verification and measurements

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DTT_TRANSFER_SANITIZER=address
cmake --build build-asan --parallel 2
ctest --test-dir build-asan --output-on-failure
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug -DTT_TRANSFER_SANITIZER=thread
cmake --build build-tsan --parallel 2
ctest --test-dir build-tsan --output-on-failure
```

The contract tests exercise intermediate byte order, two producers, actual blocked admission, drain, failure, range rejection, buffer lifetime, and the single owner. The test runner prints individual cases; CTest groups them in one executable. Sanitizers cover this project's host code, not an uninstrumented vendor stack.

`cpu_benchmark [roundtrips=20000] [repeats=7]` emits raw CSV. Both modes use one producer and identical 1 KiB write/fence/read cycles, checking every byte. Construction and teardown are excluded; FIFO timing includes payload copy, scheduling, future completion, result allocation, and verification. Execution order alternates across repeats. The FIFO may cost more than direct synchronous copies.

Use the shared measurement lock when other projects are active:

```sh
python3 scripts/measure_cpu.py --binary build/cpu_benchmark \
  --lock /path/to/shared/local-measure.lock --output results/my-cpu-run
```

On Apple M2, seven samples of 20,000 checked round trips had median total wall times of **13.30 ms synchronous** and **182.06 ms FIFO**. The additional handoff, allocation, and future overhead dominates these tiny host copies. Normal desktop apps were active; [raw samples and environment](results/cpu-final/) preserve that context. These are host wall-time observations, not PCIe/DRAM/NoC bandwidth or device latency.

## Upstream scope

TT-UMD is pinned to `1a513b2ca8a8955db1fe306b6d65dee2ffe5c9bc`; ttsim reference source is `89bdc5eb726c4f1ebbe597e03b1b9cdf7622c779`, release `v1.10.6`. Upstream allocator behavior is reference work, not an original allocation algorithm. The integration uses descriptor-derived TENSIX coordinates and a bounded scratch region starting at `0x1000`. Direct smoke and FIFO share one live Cluster, with backend destruction after owner join.

The Linux ARM64 build used Ubuntu 22.04.3, Clang 20.1.8 and GCC 12 headers/runtime. [Final simulator samples](results/umd-final/) bind source, binary, library and descriptor hashes; [build logs](results/umd-build/) retain the initial adapter compile error and its correction. The simulator warns that board n150 harvesting metadata is inconsistent. Verification is limited to L1 byte transfers on the selected `t1-1` core; it does not establish full topology correctness.

The simulator API requires serialized calls and can terminate its process on fatal errors. Its fixed-version barrier hooks do not validate silicon barriers. No kernel launch, DMA engine, multi-chip scheduler, or full Metal runtime is implemented here. See [UMD build and source details](docs/umd-integration.md), the [Chinese code tour](docs/explain.md), and [evidence boundaries](docs/resume-evidence.md).

## Troubleshooting

| Symptom | Next step |
| --- | --- |
| CMake missing or too old | Use the isolated `.tools` installation above. |
| `runtime closed` | Create a new runtime; close is terminal. Inspect futures for an earlier backend error. |
| Payload or range error | Limit each operation to 4096 bytes and keep offset/count inside the backend's scratch size. |
| Measurement lock already exists | The slot belongs to another project. Retry after it releases the lock; do not delete it. |
| Simulator crash, timeout, or missing output | Preserve the external runner's result. A linked library or partial stdout is not a successful readback. |

This is a private learning/portfolio repository. Verified engineering behavior and personal understanding are tracked separately.
