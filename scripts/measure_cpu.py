#!/usr/bin/env python3
"""Run the fixed CPU experiment while holding an atomic mkdir lock."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("--binary", type=Path, required=True)
parser.add_argument("--lock", type=Path, required=True, help="shared local measurement lock directory")
parser.add_argument("--output", type=Path, required=True, help="new results directory")
args = parser.parse_args()
args.lock.mkdir()  # FileExistsError means another project owns measurement slot.
owner = f"tt-transfer-runtime pid={os.getpid()}"
try:
    (args.lock / "owner").write_text(owner + "\n")
    args.output.mkdir(parents=True, exist_ok=False)
    binary = args.binary.resolve()
    repo = Path(__file__).resolve().parents[1]
    sources = [repo / name for name in (
        "src/runtime.cpp", "src/cpu_backend.cpp", "benchmarks/cpu_benchmark.cpp",
        "include/tt_transfer/runtime.hpp", "include/tt_transfer/cpu_backend.hpp", "CMakeLists.txt")]
    cache_path = binary.parent / "CMakeCache.txt"
    cache = {}
    for line in cache_path.read_text().splitlines():
        if line and not line.startswith(("//", "#")) and "=" in line:
            key_type, value = line.split("=", 1)
            cache[key_type.split(":", 1)[0]] = value
    compiler = cache["CMAKE_CXX_COMPILER"]
    if Path(cache["CMAKE_HOME_DIRECTORY"]).resolve() != repo:
        raise ValueError("binary build directory belongs to another source tree")
    if any(p.stat().st_mtime_ns > binary.stat().st_mtime_ns for p in sources):
        raise ValueError("measured sources changed after binary build; rebuild first")
    if cache_path.stat().st_mtime_ns > binary.stat().st_mtime_ns:
        raise ValueError("CMake configuration is newer than binary; rebuild cpu_benchmark with --clean-first")
    metadata = {
        "utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "system": {"os": platform.system(), "release": platform.release(), "machine": platform.machine()},
        "cpu_count": os.cpu_count(), "load_before": os.getloadavg(),
        "compiler": subprocess.check_output([compiler, "--version"], text=True).splitlines()[0],
        "cmake": {key: value for key, value in cache.items()
                  if key in ("CMAKE_BUILD_TYPE", "CMAKE_CXX_COMPILER_ID", "TT_TRANSFER_SANITIZER")
                  or key.startswith("CMAKE_CXX_FLAGS")},
        "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
        "source_sha256": {str(p.relative_to(repo)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},
        "command": ["cpu_benchmark", "20000", "7"],
        "claim_scope": "CPU host wall time only; no device/PCIe/DRAM/NoC performance",
        "status": "started",
    }
    if platform.system() == "Darwin":
        metadata["cpu_model"] = subprocess.check_output(["sysctl", "-n", "machdep.cpu.brand_string"], text=True).strip()
    (args.output / "environment.json").write_text(json.dumps(metadata, indent=2) + "\n")
    try:
        with (args.output / "cpu.csv").open("w") as out, (args.output / "stderr.log").open("w") as err:
            result = subprocess.run([str(binary), "20000", "7"], stdout=out, stderr=err, timeout=120)
        metadata["returncode"] = result.returncode
        metadata["status"] = "passed" if result.returncode == 0 else "failed"
    except subprocess.TimeoutExpired:
        metadata["status"] = "timeout"
        metadata["timeout_seconds"] = 120
        raise
    finally:
        metadata["load_after"] = os.getloadavg()
        (args.output / "environment.json").write_text(json.dumps(metadata, indent=2) + "\n")
    if result.returncode:
        raise SystemExit(result.returncode)
    print(f"Saved 7 samples per mode to {args.output}")
finally:
    if (args.lock / "owner").exists() and (args.lock / "owner").read_text().strip() == owner:
        (args.lock / "owner").unlink()
        args.lock.rmdir()
