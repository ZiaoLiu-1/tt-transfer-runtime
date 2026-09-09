# Tenstorrent dependencies

`upstream-source/` preserves the unmodified Tenstorrent allocator source and
header, the upstream Apache-2.0 license and the pinned Wormhole SoC descriptor.
Exact paths and SHA-256 values are in [`provenance.json`](../provenance.json).
These files and the allocator algorithm are Tenstorrent's work.

The optional `allocator_examples` target links `umd::tt-umd` from the pinned
checkout. It exercises the existing allocate/free behavior with mixed-size
release and fallback sequences, plus a nonzero Blackhole BAR4 base address.

[`scripts/fetch_upstream.py`](../scripts/fetch_upstream.py) creates ignored
`tt-umd/` and `ttsim/` directories. It refuses to alter a mismatched existing
checkout or binary. The simulator release asset is checksum verified and is not
committed. Only a descriptor is needed from TT-Metal; this project does not fetch
or build full Metal.

The UMD child build fetches dependencies specified by that upstream revision.
Their names, versions, and licenses are recorded in their repositories and
UMD's `third_party/CMakeLists.txt`. Keep the configure log when reproducing a
build; the recorded run's dependency identities and license references are in
[`results/umd-build/`](../results/umd-build/).
