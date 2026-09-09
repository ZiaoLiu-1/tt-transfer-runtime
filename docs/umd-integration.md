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
Disable unrelated UMD tools, tests, Python, RDMA, JTAG, Tracy, and emulation.
Each `run_umd.py` output directory must be new to preserve previous raw evidence.
The coordinated server also requires the shared build lock before downloads,
builds, or timing samples; private connection details are kept outside this repo.

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
