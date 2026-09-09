# TT Transfer Runtime

A C++20 host runtime that queues memory reads and writes from multiple threads
and sends them to a simulated Tenstorrent device through TT-UMD. A bounded FIFO
feeds one worker thread, so callers can share a backend that requires serialized
access. Each accepted request returns a future for its result or error.

The default build uses a CPU byte array and needs no vendor dependencies. The
optional Linux ARM64 build connects the same queue to TT-UMD and ttsim for L1
readback on one Wormhole core.

```text
Producer threads → bounded FIFO → backend owner → CPU byte array
                                              └→ TT-UMD → ttsim
```

## Build and run

Requires a C++20 compiler, CMake 3.25 or newer, and threads. Run from the repository
root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
./build/transfer_demo
```

The demo starts two producers, each writing and reading a distinct 1 KiB region,
and checks every returned byte. For the vendor build, see
[UMD setup and integration](docs/umd-integration.md).

## Using the queue

```cpp
#include "tt_transfer/cpu_backend.hpp"
#include <utility>

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

Submission blocks when the queue is full. Successful admission assigns a sequence
number under the queue mutex; the worker executes that order. A queue of capacity
Q holds at most Q waiting requests and one executing request, with each transfer
limited to 4096 bytes. Caller-owned arguments, completed results and backend memory
are outside this bound.

Writes move their byte buffer into the request, copying just the used bytes if
the vector has reserved more than 4096 bytes. Request bookkeeping and allocator
overhead are outside the payload limit. Reads return an independent
buffer that may outlive the runtime. Write and fence futures return an empty
buffer. A fence calls the backend's barrier after earlier requests have run;
several producers can still interleave their write/fence/read sequences. Use
disjoint regions, as the demo does, when those sequences must not overwrite one
another.

`close_and_drain()` rejects new submissions, wakes blocked producers, finishes
accepted work and joins the worker. Concurrent close calls are safe. A backend
exception closes admission and reaches the current and pending futures; earlier
successful work stays successful. Backend operations must return or throw for
shutdown to finish. Stop callers before destroying the runtime, and do not call
close from a backend callback.

## Tests and measurements

The host tests cover byte order, concurrent producers, blocked admission, draining
shutdown, failure propagation, range checks and buffer lifetime. Release,
ASan/UBSan and TSan runs are recorded in [validation notes](docs/validation.md),
along with commands for repeating them.

The CPU benchmark compares synchronous copies with queued write/fence/read
round trips. On Apple M2, seven samples of 20,000 checked 1 KiB round trips had
median total times of 13.30 ms synchronous and 182.06 ms queued. Thread handoff,
allocation and futures add substantial overhead for these small copies. These
are host wall-time measurements with ordinary desktop apps running; see the
[raw samples and environment](results/cpu-final/).

The recorded adapter build passed five ttsim process runs with direct and queued L1
readbacks. There are no hardware results. The pinned simulator's L1 barrier hook
is empty, and its n150 harvesting warning limits the result to the selected core.
This runtime handles byte transfers; kernel execution, DMA scheduling and
multi-chip coordination are outside its scope.

## Troubleshooting and further reading

- **CMake missing or too old:** the [validation notes](docs/validation.md#local-tools)
  include an isolated tools setup.
- **`runtime closed`:** inspect earlier futures for a backend error; create a new
  runtime to accept more work.
- **Payload or range error:** keep each operation within 4096 bytes and the
  backend's scratch extent. A zero-byte operation at the end is valid.
- **Simulator failure:** inspect the sample's JSON, stdout and stderr in the
  runner output directory; a partial readback does not count as a pass.

The [Chinese code tour](docs/explain.md) follows admission, ownership and shutdown
through the implementation. [Upstream sources](third_party/README.md) and
[provenance.json](provenance.json) identify the pinned Tenstorrent dependencies
and retained Apache-2.0 source files.
