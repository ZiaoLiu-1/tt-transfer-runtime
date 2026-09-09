# Project evidence and authorship boundary

Private repository: https://github.com/ZiaoLiu-1/tt-transfer-runtime (created and read back as PRIVATE; initial remote empty). The final delivery commit and private push readback are reported in the coordinator handoff; measured source identities are recorded with the raw results.

## Verified host behavior

- C++20 mutex/CV bounded FIFO with one backend owner, acceptance sequences, owned write buffers, independent read results, fence hook, terminal failure propagation, and draining close.
- 11 contract cases, including all eight required cases, passed in Release, AddressSanitizer/UndefinedBehaviorSanitizer, and ThreadSanitizer on Apple M2 / Apple Clang 17. Raw final logs and source hashes: `results/host-final/`. CTest has one aggregate host target; the executable prints 11 individual cases. Sanitizers cover project host code.
- Two-producer CPU demo passed with full 1 KiB pattern equality in two disjoint regions.
- `results/cpu-final/` contains seven raw samples per mode for 20,000 identical 1 KiB write/fence/read round trips, 60,000 requests and 40,960,000 transferred bytes per sample. Measured median total wall time: synchronous CPU **13.295458 ms**, FIFO CPU **182.063166 ms**. These are desktop host observations with ordinary apps running; all bytes are checked. Queue high-water and environment are in raw CSV/JSON. No device performance claim.
- Earlier `results/cpu-20260909/` is retained as exploratory only: another compiler was observed before that run. It is not the headline result. `results/host-validation/` precedes the final test-strengthening change; use `host-final` for the current tests.

Reproduction:

```sh
python3 scripts/verify_host.py --cmake .tools/bin/cmake --output results/new-host-validation
python3 scripts/measure_cpu.py --binary build/cpu_benchmark \
  --lock /path/to/shared/local-measure.lock --output results/new-cpu-measurement
```

Actual local commands used the workspace's shared lock path outside this repository. It is intentionally parameterized here. The verification driver stores each exact build/test command with `$PROJECT` representing this repository.

## Vendor identity and current boundary

`provenance.json` fixes TT-UMD source, ttsim release asset digest, descriptor source, and retained allocator hashes. `third_party/upstream-source` holds the original allocator `.cpp/.hpp`, descriptor and Apache-2.0 license. The allocation algorithm and synchronization originate upstream. The project adds a direct-link executable with one reference baseline and two small project examples: mixed-size release/fallback and nonzero Blackhole BAR4 address calculation. A 4 GiB mapping window is not 4 GiB RAM allocation.

Actual TT-UMD linking passed on Ubuntu 22.04.3 ARM64 using Clang 20.1.8 with GCC 12 headers/runtime. All three allocator examples passed against the real unmodified library. Five final real ttsim processes each passed direct 1 KiB, FIFO 1 KiB, and two-producer 1 KiB per producer readbacks in disjoint ranges. `results/umd-final/` and `results/umd-build/` retain raw logs, exact source/binary/library/descriptor hashes, post-run identity checks, build failures and corrections, and import mappings. The earlier five successful processes are preserved separately and are not pooled into the final five.

The simulator warns that n150 harvesting metadata is inconsistent. Readback supports only the selected `t1-1` core's L1 byte-transfer result, not full topology correctness. The pinned simulation barrier hook is empty; calling it does not prove silicon memory ordering. Durations in these runs are host software wall time on a shared ARM64 host, with no performance or hardware claim. Nine synthetic runner tests exercise process classification only and count as zero actual UMD executions.

## Authorship and understanding

This new project was implemented with Codex engineering assistance under Ziao's approved scope; independent agent review and test work are recorded in `docs/review.md`. It is not evidence that Ziao independently wrote every function or has personally mastered the code. The pinned allocator is upstream code and must not be described as an original algorithm. The byte-array backend is an independent functional host model, not a fake UMD success path.

Personal explanation, modification, and reproduction are **not yet assessed**. The short [Chinese code tour](explain.md) identifies the key functions and follow-up questions. No root evidence ledger, learning mastery, or application record was changed by this task.

Conservative candidate sentence for coordinator review: “Implemented and tested a C++20 bounded host transfer queue with backpressure, draining shutdown and failure propagation; integrated pinned TT-UMD and verified 1 KiB direct and queued L1 readbacks in ttsim.” Attribution remains project-scoped and Codex-assisted; personal understanding still requires assessment. No Metal device scheduler, hardware DMA, silicon barrier, or hardware performance claim is supported.
