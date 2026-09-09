set -euo pipefail
runtime_base="${PROJECT_BASE:?set the user-owned project base}"
runtime_lock="$runtime_base/.heavy-build.lock"
runtime_token="tt-transfer-runtime:$$:$(date -u +%Y%m%dT%H%M%SZ)"
if ! mkdir "$runtime_lock"; then
  echo 'Shared build lock occupied; no Runtime work started'
  exit 75
fi
printf '%s\npurpose=real UMD build allocator and simulator readback\n' "$runtime_token" > "$runtime_lock/owner"
runtime_cleanup() {
  if [ -f "$runtime_lock/owner" ] && [ "$(head -n1 "$runtime_lock/owner")" = "$runtime_token" ]; then
    rm "$runtime_lock/owner"
    rmdir "$runtime_lock"
    echo 'Runtime shared build lock released'
  fi
}
trap runtime_cleanup EXIT
trap 'exit 130' INT TERM HUP
runtime_root="$runtime_base/tt-transfer-runtime"
if [ ! -e "$runtime_root/source" ]; then
  git clone "$runtime_root/checkpoint.bundle" "$runtime_root/source"
fi
cd "$runtime_root/source"
test "$(git rev-parse HEAD)" = c5475021af8886c71b469a4fb452c8ddf240f744
mkdir -p results/umd-build
exec > >(tee results/umd-build/phase-02.log) 2>&1
set -x
date -u
uname -srm
df -h .
free -m
runtime_prefix="${TOOLCHAIN_PREFIX:?set the read-only Clang 20 and GCC 12 usr prefix}"
runtime_venv="${TOOLCHAIN_VENV:?set the read-only CMake and Ninja virtualenv}"
export PATH="$runtime_venv/bin:$runtime_prefix/bin:$PATH"
export LD_LIBRARY_PATH="$runtime_prefix/lib/aarch64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
"$runtime_prefix/bin/clang++-20" --version
"$runtime_venv/bin/cmake" --version
"$runtime_venv/bin/ninja" --version
sha256sum "$runtime_prefix/bin/clang++-20"
git diff -- src/umd_backend.cpp
sha256sum src/umd_backend.cpp
git diff --binary HEAD > results/umd-build/source-changes.patch
python3 scripts/fetch_upstream.py
timeout 1200 "$runtime_venv/bin/cmake" -S . -B build-umd -G Ninja \
  -DCMAKE_MAKE_PROGRAM="$runtime_venv/bin/ninja" \
  -DCMAKE_C_COMPILER="$runtime_prefix/bin/clang-20" \
  -DCMAKE_CXX_COMPILER="$runtime_prefix/bin/clang++-20" \
  -DCMAKE_BUILD_TYPE=Release -DTT_TRANSFER_WITH_UMD=ON \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DCMAKE_C_FLAGS="--gcc-toolchain=$runtime_prefix -isystem $runtime_prefix/include -isystem $runtime_prefix/include/aarch64-linux-gnu" \
  -DCMAKE_CXX_FLAGS="--gcc-toolchain=$runtime_prefix -isystem $runtime_prefix/include -isystem $runtime_prefix/include/aarch64-linux-gnu" \
  -DCMAKE_SHARED_LINKER_FLAGS="-L$runtime_prefix/lib/aarch64-linux-gnu -Wl,-rpath,$runtime_prefix/lib/aarch64-linux-gnu" \
  -DCMAKE_EXE_LINKER_FLAGS="-L$runtime_prefix/lib/aarch64-linux-gnu -Wl,-rpath,$runtime_prefix/lib/aarch64-linux-gnu"
timeout 1800 "$runtime_venv/bin/cmake" --build build-umd --target umd_demo allocator_examples --parallel 2
timeout 30 ./build-umd/allocator_examples
python3 scripts/run_umd.py build-umd/umd_demo third_party/ttsim/libttsim_wh_aarch64.so --output results/umd-20260909 --repeats 5 --timeout 60
du -sh third_party/tt-umd third_party/ttsim build-umd
find build-umd/_deps -maxdepth 2 -name .git -type d -print | while read -r metadata; do
  git -C "$(dirname "$metadata")" remote get-url origin
  git -C "$(dirname "$metadata")" rev-parse HEAD
done
df -h .
date -u
