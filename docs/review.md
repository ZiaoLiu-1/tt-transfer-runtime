# Independent implementation review

2026-09-09 UTC. A separate reviewer inspected the runtime contract, core concurrency/lifecycle implementation, tests, CPU measurement script, and UMD adapter/examples without modifying them.

The first pass found three provenance defects in the CPU measurement driver: it attributed an arbitrary PATH compiler to the binary, depended on invocation working directory for source hashes, and lost metadata on timeout. All three were fixed. The second pass verified actual CMake compiler/build metadata, repository-root anchoring and freshness checks, and metadata persistence through timeout.

No actionable normal-path core defect was found. Acceptance order, Q queued plus one executing, blocked submitter wakeup, close/join outside the queue mutex, terminal exception completion, and backend lifetime were inspected. Tests independently exercise all eight required behaviors and three additional cases. The test author strengthened the final version to wake two blocked producers and compare overlapping cross-producer read/write operations against an acceptance-sequence oracle. `results/host-final` binds the final test file to successful Release/ASan+UBSan/TSan output.

Source review of the optional adapter confirmed one live Cluster, descriptor-derived TENSIX coordinates, scratch-bound validation, sequential direct-to-FIFO ownership handoff, real barrier-hook invocation, and owner join before backend destruction. Allocator examples call the real upstream API. The later actual build and readback evidence is separate from this source review and is recorded in `results/umd-build/` and `results/umd-final/`.

The repository is C++/CMake and Python; the `fix` skill's Yarn/Prettier/linc commands do not exist here. Appropriate checks are strict host compilation (`-Wall -Wextra -Wpedantic -Werror`), the three host test configurations, Python syntax checks, and `git diff --check`. No CI badge or cloud run is claimed.

The real Linux build exposed a project enum namespace error. It was corrected to `tt::CoreType`; the failed phase-01 log and successful phase-02 log are retained. Upstream source remained unchanged. A separate integration reviewer checked the actual allocator output, five original simulator runs and their source/artifact/import identities.

That reviewer also found a race in the external runner: the process could exit between a wait deadline and signalling, causing `ProcessLookupError` to escape into startup-error handling and lose the exit code. The runner now handles that race locally at both SIGTERM and SIGKILL, still waits/reaps, and preserves timeout plus actual return code. Two deterministic mock regressions cover these branches; the full synthetic suite passes 9/9 and executes no UMD. `results/synthetic-runner-before-fix.log` shows the two regressions reject the old runner. The reviewed revised runner was then used for five fresh actual processes in `results/umd-final/`, with before/after source and artifact hashes. No unchanged host matrix was rerun.

The harvesting warning is retained in all raw simulator output. Claims are limited to the selected `t1-1` core's L1 readback and do not extend to topology, silicon barriers or device timing. Authorship and learner mastery are explicitly separate from engineering verification.

Final independent review found no blocking gap: all five final processes passed, all 17 measured local source hashes and 45 imported file hashes matched, and all 23 post-run source/artifact checks passed. CPU and simulator medians were independently recalculated from raw CSV. The final repository settings readback records PRIVATE visibility, disabled Actions and zero workflow runs.
