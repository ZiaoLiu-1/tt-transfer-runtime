#!/usr/bin/env python3
"""Run each simulator smoke in a separate process with an external deadline.

Stores unedited stdout/stderr, classification JSON, and host wall-time rows.
Simulator fatal exits cannot be caught by the in-process C++ exception handler.
"""
import argparse
import csv
import datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import signal
import subprocess
import time

EXPECTED = ["PASS direct_readback bytes=1024", "PASS fifo_readback bytes=1024",
            "PASS two_producer_readback bytes_each=1024 disjoint_regions=2"]
METRIC = re.compile(r"METRIC constructor_us=([\d.e+-]+) direct_roundtrip_us=([\d.e+-]+) fifo_roundtrip_us=([\d.e+-]+)")
ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def command_output(command, cwd=None):
    result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=cwd)
    return result.stdout.strip() if result.returncode == 0 else None


def metadata(args):
    build = args.binary.resolve().parent
    cache = build / "CMakeCache.txt"
    compile_commands = build / "compile_commands.json"
    cache_text = cache.read_text() if cache.exists() else ""
    compiler_match = re.search(r"^CMAKE_CXX_COMPILER:[^=]+=(.+)$", cache_text, re.MULTILINE)
    compiler = compiler_match.group(1) if compiler_match else None
    source_files = sorted(path for folder in ["src", "include", "examples", "cmake", "scripts"]
                          for path in (ROOT / folder).rglob("*") if path.is_file()
                          and path.suffix in [".cpp", ".hpp", ".cmake", ".py", ".sh"])
    source_files.extend([ROOT / "CMakeLists.txt", ROOT / "provenance.json"])
    own_compile_commands = []
    if compile_commands.exists():
        own_compile_commands = [entry for entry in json.loads(compile_commands.read_text())
                                if any(str(entry.get("file", "")).endswith(str(p.relative_to(ROOT)))
                                       for p in source_files)]
    data = {
        "utc_recorded": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "system": {"os": platform.system(), "release": platform.release(), "machine": platform.machine(),
                   "logical_cpus": os.cpu_count(), "load_average": os.getloadavg(),
                   "python": platform.python_version()},
        "project_commit": command_output(["git", "rev-parse", "HEAD"], ROOT),
        "project_status": command_output(["git", "status", "--porcelain"], ROOT),
        "umd_checkout_commit": command_output(["git", "rev-parse", "HEAD"], ROOT / "third_party/tt-umd")
                               if (ROOT / "third_party/tt-umd/.git").exists() else None,
        "umd_checkout_status": command_output(["git", "status", "--porcelain"], ROOT / "third_party/tt-umd")
                               if (ROOT / "third_party/tt-umd/.git").exists() else None,
        "project_source_sha256": {str(p.relative_to(ROOT)): digest(p) for p in source_files},
        "provenance": json.loads((ROOT / "provenance.json").read_text()),
        "binary_name": args.binary.name,
        "binary_sha256": digest(args.binary) if args.binary.is_file() else None,
        "umd_built_library_sha256": {str(p.relative_to(build)): digest(p)
                                     for p in build.rglob("libtt-umd.so*") if p.is_file()},
        "library_name": args.library.name,
        "library_sha256": digest(args.library) if args.library.is_file() else None,
        "descriptor_sha256": digest(args.library.parent / "soc_descriptor.yaml")
                             if (args.library.parent / "soc_descriptor.yaml").is_file() else None,
        "compiler": compiler,
        "compiler_version": command_output([compiler, "--version"]) if compiler else None,
        "cmake_cache_sha256": digest(cache) if cache.exists() else None,
        "compile_commands_sha256": digest(compile_commands) if compile_commands.exists() else None,
        "project_compile_commands": own_compile_commands,
        "build_cache_flags": [line for line in cache_text.splitlines()
                              if re.match(r"(?:CMAKE_BUILD_TYPE|CMAKE_CXX_FLAGS|TT_TRANSFER_|TT_UMD_)", line)],
        "repeats": args.repeats, "timeout_seconds": args.timeout,
        "metric_boundary": "Host software wall time only; simulator barrier is a no-op; no silicon timing",
        "redaction": "Local project and home path prefixes replaced by $PROJECT and $HOME; no routing recorded",
    }
    # Preserve technical logs and flag values, but do not publish user routing or
    # identifying home prefixes as part of the reproduction metadata.
    encoded = json.dumps(data, indent=2).replace(str(ROOT), "$PROJECT").replace(str(Path.home()), "$HOME")
    (args.output / "run-metadata.json").write_text(encoded + "\n")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=Path)
    parser.add_argument("library", type=Path)
    parser.add_argument("--output", type=Path, default=Path("results/umd"))
    parser.add_argument("--repeats", type=int, default=5)
    parser.add_argument("--timeout", type=float, default=60)
    args = parser.parse_args()
    if not 1 <= args.repeats <= 10 or args.timeout <= 0:
        parser.error("repeats must be 1..10 and timeout must be positive")
    args.output.mkdir(parents=True, exist_ok=False)
    metadata(args)
    records = []
    for sample in range(args.repeats):
        started = datetime.datetime.now(datetime.timezone.utc).isoformat()
        prefix = args.output / f"sample-{sample:02d}"
        start = time.monotonic()
        timed_out = False
        startup_error = None
        with prefix.with_suffix(".stdout.log").open("wb") as stdout, prefix.with_suffix(".stderr.log").open("wb") as stderr:
            try:
                process = subprocess.Popen([str(args.binary.resolve()), str(args.library.resolve())],
                                           stdout=stdout, stderr=stderr, start_new_session=True)
                try:
                    code = process.wait(timeout=args.timeout)
                except subprocess.TimeoutExpired:
                    timed_out = True
                    try:
                        os.killpg(process.pid, signal.SIGTERM)
                    except ProcessLookupError:
                        # The child may exit between wait's deadline and killpg.
                        # Still reap it and preserve the timeout classification.
                        pass
                    try:
                        process.wait(timeout=2)
                    except subprocess.TimeoutExpired:
                        try:
                            os.killpg(process.pid, signal.SIGKILL)
                        except ProcessLookupError:
                            pass
                        process.wait()
                    code = process.returncode
            except OSError as error:
                startup_error = str(error)
                stderr.write((startup_error + "\n").encode())
                code = None
        elapsed = time.monotonic() - start
        output = prefix.with_suffix(".stdout.log").read_text(errors="replace")
        errors = prefix.with_suffix(".stderr.log").read_text(errors="replace")
        metrics = METRIC.search(output)
        if startup_error:
            classification = "startup_error"
        elif timed_out:
            classification = "timeout"
        elif code < 0:
            classification = "signal_crash"
        elif "byte mismatch" in errors:
            classification = "mismatch"
        elif code != 0:
            classification = "nonzero_exit"
        elif not all(marker in output for marker in EXPECTED) or not metrics:
            classification = "incomplete_output"
        else:
            classification = "pass"
        record = {"sample": sample, "utc_start": started, "exit_code": code,
                  "classification": classification, "process_wall_seconds": elapsed,
                  "constructor_us": metrics.group(1) if metrics else "",
                  "direct_roundtrip_us": metrics.group(2) if metrics else "",
                  "fifo_roundtrip_us": metrics.group(3) if metrics else ""}
        records.append(record)
        prefix.with_suffix(".json").write_text(json.dumps(record, indent=2) + "\n")
        print(json.dumps(record), flush=True)
        if classification != "pass":
            break
    with (args.output / "samples.csv").open("w", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=list(records[0]))
        writer.writeheader()
        writer.writerows(records)
    raise SystemExit(0 if len(records) == args.repeats and all(r["classification"] == "pass" for r in records) else 1)


if __name__ == "__main__":
    main()
