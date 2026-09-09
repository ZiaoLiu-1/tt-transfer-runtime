# Runtime implementation state

As of 2026-09-09 UTC (2026-09-08 Toronto), the approved runtime implementation, real integration, final documentation and independent review are complete. The delivery commit and final remote readback are recorded in the coordinator handoff. Root coordination owns workspace STATUS/CHANGELOG, resume evidence and learning mastery; this task changes only its project.

## Verified scope

| Area | Result | Evidence |
| --- | --- | --- |
| Host runtime | Bounded mutex/CV FIFO; one backend owner; write/read/fence; backpressure; draining close and terminal failure propagation | `src/`, `include/` |
| Host contract | 11/11 cases in Release, ASan/UBSan and TSan; all eight required behaviors plus three additional cases | `results/host-final/` |
| CPU observation | Seven samples per mode, 20,000 checked 1 KiB round trips; median total 13.295458 ms synchronous and 182.063166 ms FIFO | `results/cpu-final/` |
| Actual UMD build | Unmodified pinned TT-UMD linked on Ubuntu 22.04.3 ARM64 using Clang 20.1.8/GCC 12; three upstream allocator examples passed | `results/umd-build/` |
| Actual ttsim readback | Five final processes each passed direct 1 KiB, FIFO 1 KiB, and two-producer disjoint 1 KiB per producer readbacks | `results/umd-final/` |
| Runner regression | Nine synthetic classification cases, including deterministic exit-before-SIGTERM/SIGKILL races; zero UMD executions in this test | `results/synthetic-runner-final.log` |
| Repository settings | PRIVATE, Actions disabled; zero workflow runs at readback | `results/repository-settings.json` |

UMD uses TT-UMD `1a513b2ca8a8955db1fe306b6d65dee2ffe5c9bc` and the official checksum-verified ttsim v1.10.6 ARM64 release asset. The binary is downloaded, not claimed to have been built from the reference ttsim source. Allocator source and license remain unmodified; provenance is in `provenance.json`. Actual build commands, dependency identities, resource footprint, raw logs and import hash mappings are retained in `docs/umd-integration.md` and `results/umd-build/`.

The first real build caught a project enum qualification error (`tt::umd::CoreType` to `tt::CoreType`). Its failed log is preserved; incremental phase 02 built successfully without vendor patches. The host matrix did not compile this optional adapter, so its earlier adapter hash is historical; all exercised host source and test hashes remain unchanged.

The original five successful real process runs in `results/umd-20260909/` used the earlier runner. Independent review then found a timeout/signalling race that could lose the reaped exit code or classify an already-exited process as a startup error. Local `ProcessLookupError` handling and deterministic regressions fixed it. The final five samples use the revised runner and the same already-built UMD executable; run-start and post-run identities are checked. No original results were overwritten, and the unchanged host code did not need a repeat matrix.

## Limits and retained history

The simulator emits an n150 harvesting-information warning (one expected harvested unit versus a zero mask). Raw output retains it. The verified result covers L1 byte transfers on descriptor-selected `t1-1` only; it is not a full topology validation. At the pinned UMD commit the simulation `l1_membar` hook is empty. No Tenstorrent hardware, silicon barrier correctness, device performance, kernel execution, DMA engine, or Metal scheduler result is claimed. CPU and simulator durations are host wall-time observations, with their environment/load recorded.

`results/host-validation/` is the earlier test snapshot, before final test strengthening. `results/cpu-20260909/` is exploratory because another compiler was observed before it. Final CPU measurement held the shared local lock during a coordinated no-compile interval; ordinary desktop applications remained active. All remote builds and final samples used the shared atomic lock; mutable Runtime checkouts/builds/cache were isolated, with authorized read-only reuse of a verified toolchain prefix. No privileged installation or paid cloud CI was used.

Private repository: https://github.com/ZiaoLiu-1/tt-transfer-runtime. Host checkpoint `4f2c85195c75696493d75725f5338fe1d3799794` was pushed and independently read back. Remote integration started from `c5475021af8886c71b469a4fb452c8ddf240f744` plus recorded adapter/runner patches; exact measured file hashes are authoritative. Final delivery commit is reported in the coordinator handoff rather than self-referenced inside this file.

This project was implemented with Codex assistance. Personal explanation, independent modification and reproduction remain **not assessed**. Verified engineering output does not automatically create a resume claim or learning-mastery record.

## Goal lifecycle

A real goal was created on 2026-09-09 UTC without a token budget for implementation, verification, attribution, private push, review and handoff (task `01a083bb-556a-7de1-a33e-f04d1b73ad26`). During a platform model-capacity failure its state became `blocked` without this task calling `update_goal(blocked)`. The goal API has no resume operation. Under the coordinator's explicit resume instruction engineering continued without creating a duplicate goal; no UI recovery is being attempted. Completion is marked only after the verified private push and handoff.
