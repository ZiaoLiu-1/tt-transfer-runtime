# Independent implementation review

2026-09-09 UTC. A separate reviewer inspected the runtime contract, core concurrency/lifecycle implementation, tests, CPU measurement script, and UMD adapter/examples without modifying them.

The first pass found three provenance defects in the CPU measurement driver: it attributed an arbitrary PATH compiler to the binary, depended on invocation working directory for source hashes, and lost metadata on timeout. All three were fixed. The second pass verified actual CMake compiler/build metadata, repository-root anchoring and freshness checks, and metadata persistence through timeout.

No actionable normal-path core defect was found. Acceptance order, Q queued plus one executing, blocked submitter wakeup, close/join outside the queue mutex, terminal exception completion, and backend lifetime were inspected. Tests independently exercise all eight required behaviors and three additional cases. The test author strengthened the final version to wake two blocked producers and compare overlapping cross-producer read/write operations against an acceptance-sequence oracle. `results/host-final` binds the final test file to successful Release/ASan+UBSan/TSan output.

Source review of the optional adapter confirmed one live Cluster, descriptor-derived TENSIX coordinates, scratch-bound validation, sequential direct-to-FIFO ownership handoff, real barrier-hook invocation, and owner join before backend destruction. Allocator examples call the real upstream API. This source review does not establish that UMD compiled or ttsim ran; those require the separately recorded remote results.

The repository is C++/CMake and Python; the `fix` skill's Yarn/Prettier/linc commands do not exist here. Appropriate checks are strict host compilation (`-Wall -Wextra -Wpedantic -Werror`), the three host test configurations, Python syntax checks, and `git diff --check`. No CI badge or cloud run is claimed.
