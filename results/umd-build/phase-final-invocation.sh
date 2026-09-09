set -euo pipefail
runtime_base="${PROJECT_BASE:?set the user-owned project base}"
runtime_lock="$runtime_base/.heavy-build.lock"
runtime_token="tt-transfer-runtime:$$:$(date -u +%Y%m%dT%H%M%SZ)"
if ! mkdir "$runtime_lock"; then
  echo 'Shared build lock occupied; no final Runtime samples started'
  exit 75
fi
printf '%s\npurpose=final real UMD five-process readback samples\n' "$runtime_token" > "$runtime_lock/owner"
runtime_cleanup() {
  if [ -f "$runtime_lock/owner" ] && [ "$(head -n1 "$runtime_lock/owner")" = "$runtime_token" ]; then
    rm "$runtime_lock/owner"
    rmdir "$runtime_lock"
    echo 'Runtime shared build lock released'
  fi
}
trap runtime_cleanup EXIT
trap 'exit 130' INT TERM HUP
cd "$runtime_base/tt-transfer-runtime/source"
exec > >(tee results/umd-build/phase-final.log) 2>&1
set -x
date -u
runtime_prefix="${TOOLCHAIN_PREFIX:?set the read-only Clang 20 and GCC 12 usr prefix}"
runtime_venv="${TOOLCHAIN_VENV:?set the read-only CMake and Ninja virtualenv}"
export PATH="$runtime_venv/bin:$runtime_prefix/bin:$PATH"
export LD_LIBRARY_PATH="$runtime_prefix/lib/aarch64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
sha256sum scripts/run_umd.py src/umd_backend.cpp
sha256sum "$runtime_prefix/lib/aarch64-linux-gnu/libstdc++.so.6" "$runtime_prefix/lib/aarch64-linux-gnu/libhwloc.so.15"
git diff --binary HEAD > results/umd-build/final-source-changes.patch
python3 scripts/run_umd.py build-umd/umd_demo third_party/ttsim/libttsim_wh_aarch64.so --output results/umd-final --repeats 5 --timeout 60
python3 - <<'PY'
from pathlib import Path
import datetime,hashlib,json,subprocess
root=Path.cwd()
meta=json.loads((root/'results/umd-final/run-metadata.json').read_text())
old=json.loads((root/'results/umd-20260909/run-metadata.json').read_text())
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
checks={}
for name,expected in meta['project_source_sha256'].items():
    actual=sha(root/name)
    checks[name]={'expected':expected,'actual':actual,'match':expected==actual}
artifacts={'build-umd/umd_demo':meta['binary_sha256'],'third_party/ttsim/libttsim_wh_aarch64.so':meta['library_sha256'],'third_party/ttsim/soc_descriptor.yaml':meta['descriptor_sha256'],**{'build-umd/'+name:expected for name,expected in meta['umd_built_library_sha256'].items()}}
for name,expected in artifacts.items():
    actual=sha(root/name)
    checks[name]={'expected':expected,'actual':actual,'match':expected==actual}
head=subprocess.check_output(['git','-C',str(root/'third_party/tt-umd'),'rev-parse','HEAD'],text=True).strip()
dirty=subprocess.check_output(['git','-C',str(root/'third_party/tt-umd'),'status','--porcelain'],text=True).strip()
record={'utc_verified':datetime.datetime.now(datetime.timezone.utc).isoformat(),'against_run':'umd-final','checks':checks,'umd_commit':head,'umd_status':dirty,'all_match':all(x['match'] for x in checks.values()) and head==meta['umd_checkout_commit'] and not dirty,'same_binary_as_initial_run':meta['binary_sha256']==old['binary_sha256'],'same_umd_library_as_initial_run':meta['umd_built_library_sha256']==old['umd_built_library_sha256'],'runner_sha256':meta['project_source_sha256']['scripts/run_umd.py']}
(root/'results/umd-build/post-run-identity-final.json').write_text(json.dumps(record,indent=2)+'\n')
assert record['all_match'] and record['same_binary_as_initial_run'] and record['same_umd_library_as_initial_run']
assert record['runner_sha256']=='8983f3e410ed755573b5ca1d2873521255345b3a5fbaf21a2b4c25eaf6895a04'
print('PASS final post-run identity:',len(checks),'checks; binary and UMD library unchanged; runner matches approved SHA')
PY
sha256sum "$runtime_prefix/lib/aarch64-linux-gnu/libstdc++.so.6" "$runtime_prefix/lib/aarch64-linux-gnu/libhwloc.so.15"
date -u
