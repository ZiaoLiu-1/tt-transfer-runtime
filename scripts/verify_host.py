#!/usr/bin/env python3
"""Build and run host contract tests, retaining stage outcomes and compiler logs."""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("--cmake", default="cmake")
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
repo = Path(__file__).resolve().parents[1]
cmake = Path(shutil.which(args.cmake) or args.cmake).resolve()
ctest = cmake.with_name("ctest")
args.output.mkdir(parents=True, exist_ok=False)
records = []
for name, build_type, sanitizer in (("release", "Release", ""), ("asan", "Debug", "address"), ("tsan", "Debug", "thread")):
    build = repo / ("build" if name == "release" else f"build-{name}")
    commands = [
        [str(cmake), "-S", str(repo), "-B", str(build), f"-DCMAKE_BUILD_TYPE={build_type}",
         f"-DTT_TRANSFER_SANITIZER={sanitizer}"],
        [str(cmake), "--build", str(build), "--parallel", "2"],
        [str(ctest), "--test-dir", str(build), "--verbose"],
    ]
    for stage, command in zip(("configure", "build", "test"), commands):
        path = args.output / f"{name}-{stage}.log"
        record = {"mode": name, "stage": stage, "utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
                  "command": [part.replace(str(repo), "$PROJECT") for part in command]}
        with path.open("w") as log:
            try:
                result = subprocess.run(command, cwd=repo, stdout=log, stderr=subprocess.STDOUT, timeout=120)
                record["exit_code"] = result.returncode
                record["status"] = "pass" if result.returncode == 0 else "fail"
            except subprocess.TimeoutExpired:
                record["status"] = "timeout"
        records.append(record)
        print(json.dumps(record), flush=True)
        if record["status"] != "pass":
            break
    (args.output / "outcomes.json").write_text(json.dumps(records, indent=2) + "\n")
sources = [repo / "CMakeLists.txt"]
sources += [p for folder in ("src", "include", "tests", "benchmarks") for p in (repo / folder).rglob("*") if p.is_file()]
(args.output / "source-sha256.json").write_text(json.dumps({str(p.relative_to(repo)): hashlib.sha256(p.read_bytes()).hexdigest()
                                                         for p in sorted(sources)}, indent=2) + "\n")
raise SystemExit(0 if len(records) == 9 and all(r["status"] == "pass" for r in records) else 1)
