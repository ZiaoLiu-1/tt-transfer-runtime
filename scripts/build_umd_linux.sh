#!/usr/bin/env bash
# User-local Ubuntu 22.04 ARM64 bootstrap and narrow build. On the coordinated
# server, the caller MUST hold the shared .heavy-build.lock for this whole script.
set -euo pipefail
cd "$(dirname "$0")/.."
if [[ "$(uname -s)" != Linux || "$(uname -m)" != aarch64 ]]; then
  echo 'This bootstrap is pinned to Ubuntu 22.04 ARM64' >&2
  exit 2
fi
runtime_project_root="$PWD"
mkdir -p .tools/python .tools/packages .tools/prefix results/umd-build
if [[ ! -x .tools/python/cmake/data/bin/cmake || ! -x .tools/python/ninja/data/bin/ninja ]]; then
  python3 -m pip install --no-cache-dir --target .tools/python cmake==3.31.6 ninja==1.11.1.4
fi
if [[ ! -f .tools/prefix/usr/include/hwloc.h ]]; then
  (
    cd .tools/packages
    # Only downloads from configured signed indexes. Extraction into this prefix
    # does not install packages, run scripts, or change any system files.
    apt-get download libhwloc-dev=2.7.0-2ubuntu1 libhwloc15=2.7.0-2ubuntu1
    for package in ./*.deb; do
      dpkg-deb -x "$package" ../prefix
    done
    sha256sum ./*.deb
  )
fi
runtime_cmake="$runtime_project_root/.tools/python/cmake/data/bin/cmake"
runtime_ninja="$runtime_project_root/.tools/python/ninja/data/bin/ninja"
runtime_prefix="$runtime_project_root/.tools/prefix/usr"
"$runtime_cmake" --version
"$runtime_ninja" --version
g++ --version
python3 scripts/fetch_upstream.py
"$runtime_cmake" -S . -B build-umd -G Ninja \
  -DCMAKE_MAKE_PROGRAM="$runtime_ninja" \
  -DCMAKE_BUILD_TYPE=Release -DTT_TRANSFER_WITH_UMD=ON \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DCMAKE_CXX_FLAGS="-isystem $runtime_prefix/include" \
  -DCMAKE_SHARED_LINKER_FLAGS="-L$runtime_prefix/lib/aarch64-linux-gnu -Wl,-rpath,$runtime_prefix/lib/aarch64-linux-gnu" \
  -DCMAKE_EXE_LINKER_FLAGS="-L$runtime_prefix/lib/aarch64-linux-gnu -Wl,-rpath,$runtime_prefix/lib/aarch64-linux-gnu"
"$runtime_cmake" --build build-umd --target umd_demo allocator_examples --parallel 2
./build-umd/allocator_examples
du -sh .tools third_party/tt-umd third_party/ttsim build-umd
