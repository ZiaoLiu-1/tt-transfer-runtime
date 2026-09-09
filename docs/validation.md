# Validation and measurement

The host suite runs against the CPU backend and controlled test backends. It
checks intermediate byte order, overlapping operations from two producers against
an acceptance-sequence oracle, actual blocked submission, close with pending work,
backend failure, bounds, buffer lifetime and single-threaded backend access.
CTest runs one executable, which prints the individual cases.

## Reproduce host checks

From the repository root:

```sh
python3 scripts/verify_host.py --output results/my-host-check
python3 tests/test_umd_runner.py
python3 tests/test_measure_cpu.py
```

`verify_host.py` configures, builds and tests Release, ASan/UBSan and TSan in
`build`, `build-asan` and `build-tsan`. It requires a new output directory and
saves commands, stage logs, outcomes and source hashes. The sanitizer builds
instrument this project's host code; they do not cover an uninstrumented vendor
library.

The Python runner tests use synthetic processes and mocked timeout races. They
check parsing, mismatch, incomplete output, startup errors, nonzero exits,
signals and timeouts. They do not execute UMD or ttsim.

The measurement-script tests use temporary files and a mocked benchmark launch
to check source/configuration freshness and lock release.

### Local tools

If CMake is unavailable, install it in an ignored project environment:

```sh
python3 -m venv .tools
.tools/bin/pip install cmake==3.31.6 ninja==1.11.1.4
export PATH="$PWD/.tools/bin:$PATH"
```

## CPU benchmark

`cpu_benchmark [roundtrips=20000] [repeats=7]` emits CSV for two modes. Both use
one producer and identical 1 KiB write/fence/read cycles, checking every byte.
Construction and teardown are excluded. Queue timing includes payload copying,
thread scheduling, future completion, result allocation and verification.
Execution order alternates across repeats.

To collect the default workload with build and environment metadata:

```sh
python3 scripts/measure_cpu.py --binary build/cpu_benchmark \
  --lock /path/to/shared/local-measure.lock --output results/my-cpu-run
```

Choose a lock path whose parent exists, and use the same path for competing
measurements. The script creates the lock directory atomically and releases it
after the run. If it already exists, wait for its owner; do not remove another
process's lock. Rebuild after changing measured source files, and use a new output
directory for every run.

If CMake configuration is newer than the binary, the script rejects the run.
Rebuild with `cmake --build build --target cpu_benchmark --clean-first` before
measuring so the recorded flags describe the executable being timed.

The retained Apple M2 run used Apple Clang 17 and seven samples per mode, with
20,000 round trips per sample. Median total times were 13.295458 ms synchronous
and 182.063166 ms queued. Ordinary desktop applications remained active. These
numbers describe this host workload and do not measure device bandwidth or
silicon latency.

## Retained results

| Directory | Contents |
| --- | --- |
| [`host-20260909-cleanup/`](../results/host-20260909-cleanup/) | Current payload-capacity fix: 12 host cases in each of Release, ASan/UBSan and TSan; 21 runner and 3 measurement-script cases. Negative regressions reject the earlier implementations. |
| [`host-final/`](../results/host-final/) | Original Release, ASan/UBSan and TSan logs: 11 cases in each configuration, with source hashes. |
| [`cpu-final/`](../results/cpu-final/) | Raw CSV and environment for the Apple M2 measurement above. |
| [`umd-build/`](../results/umd-build/) | Linux build logs, dependency licenses, artifact identities and import mappings. The initial adapter compile error and correction are both retained. |
| [`umd-final/`](../results/umd-final/) | Five ttsim processes, each checking direct, queued and two-producer readbacks. |

The earlier `host-validation/` logs precede test strengthening. `cpu-20260909/`
is exploratory: another compiler was observed before that run. `umd-20260909/`
holds five earlier successful processes that used the runner before its
timeout-signalling race fix. These samples are kept separately from the final
runs.

Historical logs and manifests retain their original bytes, including metadata
and source hashes. They describe the revisions measured at the time. Source
changes and new checks belong in new result directories; they do not retroactively
change old measurements. See [UMD integration](umd-integration.md) for the
simulator's barrier and topology limits.

The capacity fix and stricter runner checks were validated locally after the
recorded CPU and simulator measurements. Those older measurements were not rerun
for this revision.
