# Upstream provenance

`upstream-source/` preserves two **unmodified Tenstorrent source files** for the
allocator reading exercise, plus the upstream Apache-2.0 license and the pinned
Wormhole SoC descriptor. Exact paths and SHA-256 values are in `provenance.json`.
These files and the allocator algorithm are Tenstorrent's work.

The optional `allocator_examples` target links `umd::tt-umd` from the real pinned
checkout, rather than compiling a rewritten allocator. There is no portability
patch or source adaptation in this path. The baseline allocate/free behavior
already exists upstream. The project adds the compact mixed-size and nonzero
BAR4 exercise sequences; those sequences do not constitute a new allocator.

`scripts/fetch_upstream.py` creates ignored `tt-umd/` and `ttsim/` directories.
It refuses to alter a mismatched existing checkout or binary. The simulator
release asset is checksum verified and is not committed. Only a descriptor is
needed from TT-Metal; this project does not fetch or build full Metal.

The UMD child build fetches dependencies specified by that upstream revision.
Their names, versions, and licenses remain governed by their repositories and
the upstream `third_party/CMakeLists.txt`; record the actual configure log for a
reproduction. This repository does not vendor those dependency implementations.
