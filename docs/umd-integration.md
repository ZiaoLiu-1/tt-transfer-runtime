# Real UMD integration

The optional Linux target uses unmodified TT-UMD commit
`1a513b2ca8a8955db1fe306b6d65dee2ffe5c9bc` and the checksum-verified Linux ARM64
Wormhole simulator release `v1.10.6`. `provenance.json` records all source and asset
identities. The simulator binary is downloaded from the official release; the
referenced ttsim source commit documents the ABI and is not a claim that we built
the release binary ourselves.

```sh
python3 scripts/fetch_upstream.py
cmake -S . -B build-umd -G Ninja -DCMAKE_BUILD_TYPE=Release -DTT_TRANSFER_WITH_UMD=ON
cmake --build build-umd --target umd_demo allocator_examples --parallel 2
./build-umd/allocator_examples
python3 scripts/run_umd.py build-umd/umd_demo third_party/ttsim/libttsim_wh_aarch64.so --output results/umd-new --repeats 5
```

Requires Linux ARM64, CMake >=3.25, a C++20 compiler, Ninja, and libhwloc headers
and library. UMD's dependencies are fetched by its pinned CMake configuration.
The actual successful build used **Ubuntu 22.04.3, Clang 20.1.8, GCC 12 C++
headers/runtime, CMake 4.0.2 and Ninja 1.11.1** from a pre-existing user prefix
that was reused read-only. The host has four ARM Neoverse-N1 logical CPUs and
glibc 2.35. See `results/umd-build/native-environment.log` for observed details.
`scripts/build_umd_linux.sh` is an optional Ubuntu bootstrap with pinned
CMake/Ninja wheels, extracted hwloc packages and the system GCC 11 compiler;
it passed shell syntax checking but **was not executed end-to-end**.
The multiarch hwloc generated header directory (`include/aarch64-linux-gnu`)
must be included alongside `include` when using an extracted user prefix.
Disable unrelated UMD tools, tests, Python, RDMA, JTAG, Tracy, and emulation.
Each `run_umd.py` output directory must be new to preserve previous raw evidence.
The coordinated server also requires the shared build lock before downloads,
builds, or timing samples; private connection details are kept outside this repo.

For the validated user-prefix build, set `TOOLCHAIN_PREFIX` to the prefix's
`usr` directory and `TOOLCHAIN_VENV` to the CMake/Ninja environment. The effective
configure command was:

```sh
export LD_LIBRARY_PATH="$TOOLCHAIN_PREFIX/lib/aarch64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
"$TOOLCHAIN_VENV/bin/cmake" -S . -B build-umd -G Ninja \
  -DCMAKE_MAKE_PROGRAM="$TOOLCHAIN_VENV/bin/ninja" \
  -DCMAKE_C_COMPILER="$TOOLCHAIN_PREFIX/bin/clang-20" \
  -DCMAKE_CXX_COMPILER="$TOOLCHAIN_PREFIX/bin/clang++-20" \
  -DCMAKE_BUILD_TYPE=Release -DTT_TRANSFER_WITH_UMD=ON \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DCMAKE_C_FLAGS="--gcc-toolchain=$TOOLCHAIN_PREFIX -isystem $TOOLCHAIN_PREFIX/include -isystem $TOOLCHAIN_PREFIX/include/aarch64-linux-gnu" \
  -DCMAKE_CXX_FLAGS="--gcc-toolchain=$TOOLCHAIN_PREFIX -isystem $TOOLCHAIN_PREFIX/include -isystem $TOOLCHAIN_PREFIX/include/aarch64-linux-gnu" \
  -DCMAKE_SHARED_LINKER_FLAGS="-L$TOOLCHAIN_PREFIX/lib/aarch64-linux-gnu -Wl,-rpath,$TOOLCHAIN_PREFIX/lib/aarch64-linux-gnu" \
  -DCMAKE_EXE_LINKER_FLAGS="-L$TOOLCHAIN_PREFIX/lib/aarch64-linux-gnu -Wl,-rpath,$TOOLCHAIN_PREFIX/lib/aarch64-linux-gnu"
"$TOOLCHAIN_VENV/bin/cmake" --build build-umd --target umd_demo allocator_examples --parallel 2
```

The archived `results/umd-build/phase-02-invocation.sh` preserves the executed
phase with three environment path definitions generalized. Its sidecar records
the original and archived script hashes. The compiled C++ source was checkpoint
`c5475021af8886c71b469a4fb452c8ddf240f744` plus the one-line adapter fix in
`results/umd-build/source-changes.patch`: the real public enum is `tt::CoreType`.
The first failed build is retained in `phase-01.log`; `phase-02.log` contains the
successful build and all three real allocator example passes. No upstream UMD
source patch was needed.

`results/umd-build/dependencies.json` records the actual SHA, repository and
license-file references of all 12 cached repositories, including unbuilt
dependencies and nested submodules. The Asio license supplement is alongside it.
The observed Runtime footprint was 125 MiB of CPM sources, 13 MiB of UMD checkout,
61 MiB of build files and 204 KiB of simulator/descriptor files. The shared
toolchain footprint is outside those numbers. All imported logs retain numerical
results and technical output; path/routing redactions and original/imported
file hashes are recorded in `import-manifest.json`.

## Verified result and boundary

On 2026-09-09 at 02:24:52 UTC, the final runner completed **five independent
real UMD/ttsim processes, all exit 0**. Every process passed a full-byte 1 KiB
direct readback, a full-byte 1 KiB FIFO readback and two producer readbacks in
disjoint 1 KiB regions, using one live Cluster. The chosen core was `t1-1 (NOC0)`.
The three allocator examples separately linked and passed against the real UMD
library: baseline allocate/free, mixed-size release/reuse/escalation, and
nonzero Blackhole BAR4 address calculation.

Final samples are in `results/umd-final/samples.csv`, with each process's stdout,
stderr and classification JSON alongside its source/environment/binary manifest.
`results/umd-build/post-run-identity-final.json` verifies 23 source and artifact
hashes after execution, including the UMD library, ttsim and descriptor. The
final runner SHA is
`8983f3e410ed755573b5ca1d2873521255345b3a5fbaf21a2b4c25eaf6895a04`.
The complete source changes from the checkpoint are recorded in
`results/umd-build/final-source-changes.patch`.

| Host wall-time measurement, 5 samples | Median (us) | Observed range (us) |
| --- | ---: | ---: |
| UMD backend construction | 4887.46 | 4814.66–5122.62 |
| Direct write/fence/read, 1 KiB | 28.24 | 27.84–32.00 |
| FIFO write/fence/read, 1 KiB | 67.48 | 63.88–82.04 |

The shared build lock excluded competing project builds/measurements. The
recorded load averages at the final run start were 0.043, 0.398 and 0.984.
These are five initial shared-host observations; the extra FIFO handoff cost is
visible for this small operation, but these rows establish no silicon latency,
bandwidth, general speedup, or tail-latency conclusion.

`results/umd-20260909/` retains the five earlier successful real processes.
Independent review then fixed an external timeout-signal race in the Python
runner and added deterministic TERM/KILL regression cases. The final samples
used that revised runner in a new directory. The binary and UMD library hashes
remained identical between the two runs; no rebuild or host test rerun was
necessary. Both the initial failure log and earlier samples remain intact.

UMD emitted a warning that its n150 board description expected one harvested
Tensix unit while the mask indicated zero. The warning is retained in every
stdout log. This exercise validates the selected descriptor core's scratch
readback and does not establish full-board harvesting or topology correctness.
The pinned simulation L1 barrier remains a no-op, as described below.

`UmdBackend` builds public `ClusterOptions` with simulation chip type, chip 0,
zero host memory channels and the `.so` path. It chooses a real TENSIX CoreCoord
from the SoC descriptor and verifies that its 8 KiB scratch region from `0x1000`
fits within worker L1. The adjacent descriptor is named `soc_descriptor.yaml`,
as required by ttsim's documented setup.

The demo creates exactly one Cluster. It writes a nonuniform 1 KiB byte pattern,
calls `l1_membar`, reads all bytes and checks equality. It then transfers the same
backend into the FIFO runtime and repeats with another pattern. Two producer
threads also write/fence/read disjoint 1 KiB ranges. Their interleavings are legal
without pretending a fence turns several requests into a transaction. Runtime
joins its owner before backend destruction; no second Cluster is constructed.

`run_umd.py` launches a fresh process per sample and records stdout, stderr, exit
status, timeout, and required completion markers. It classifies mismatch,
incomplete output, nonzero exit and signal crash separately. A process-level
ttsim fatal exit is outside the runtime's C++ exception recovery contract.

The reported constructor and round-trip durations are **host wall time**.
They include software and thread costs. They are not PCIe, NoC, DRAM bandwidth,
Tensix execution latency or silicon performance. At this exact UMD commit,
`SimulationChip::l1_membar` is empty: the adapter calls the API but simulator
readback cannot establish hardware memory-barrier semantics.

The runner's process-classifier regression cases are independently runnable
with `python3 tests/test_umd_runner.py`. They deliberately launch synthetic small
programs to check successful marker parsing, startup errors, nonzero exits,
signals, timeouts, mismatch and incomplete output, plus a deterministic timeout
signal race regression. The latest log is `results/synthetic-runner-final.log`;
**actual UMD executions in that log: zero**.

## Allocator reading exercise（中文）

`SimulationTlbAllocator::allocate_tlb_index` 按窗口从小到大扫描。Wormhole
有 1 MiB、2 MiB、16 MiB 三个池；请求先找到能装下的最小池，如果该池满了，
再尝试更大的池。一个 mutex 保护选择与标记，避免两个调用拿到同一个空槽。
`deallocate_tlb_index` 在同一个 mutex 下把标记清掉；布局在构造后不再变化。

`get_tlb_address_from_index` 把索引翻译成映射窗口地址。普通池以 BAR0 为基址；
Blackhole 的 4 GiB 池以 BAR4 为基址，每个槽位再加一个 4 GiB 偏移。
这一步只做整数地址计算与少量分配标记，**没有申请 4 GiB 的 host RAM**。
`allocator_examples` 的第三例特意用非零 BAR4，防止把“相对 offset”误读为
“完整地址”。这三个例子运行真实 UMD 实现，不是 CPU fake 或重新实现的算法。

个人复述尚待验收：解释为什么 mutex 覆盖“找空槽＋标记”，为什么释放后可以
重用旧索引，为什么 source-level allocator 成功不表示完成了设备 readback。
