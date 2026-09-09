#!/usr/bin/env python3
"""Synthetic process classifier tests. These do NOT run TT-UMD or ttsim."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
CASES = {
    "incomplete_output": "pass",
    "nonzero_exit": "raise SystemExit(7)",
    "mismatch": 'import sys; print("ERROR byte mismatch",file=sys.stderr); raise SystemExit(1)',
    "timeout": "import time; time.sleep(10)",
    "signal_crash": "import os,signal; os.kill(os.getpid(),signal.SIGTERM)",
    "pass": '''print("PASS direct_readback bytes=1024")
print("PASS fifo_readback bytes=1024")
print("PASS two_producer_readback bytes_each=1024 disjoint_regions=2")
print("METRIC constructor_us=12.3 direct_roundtrip_us=4.5 fifo_roundtrip_us=6.7")''',
}


def main():
    with tempfile.TemporaryDirectory(prefix="tt-runner-synthetic-") as temporary:
        directory = Path(temporary)
        for expected, body in {**CASES, "startup_error": None}.items():
            binary = directory / expected
            if body is not None:
                binary.write_text(f"#!{sys.executable}\n{body}\n")
                binary.chmod(0o755)
            destination = directory / (expected + "-result")
            command = [sys.executable, str(ROOT / "scripts/run_umd.py"), str(binary),
                       str(directory / "missing.so"), "--output", str(destination),
                       "--timeout", "1" if expected == "timeout" else "5", "--repeats", "1"]
            result = subprocess.run(command, capture_output=True, text=True)
            record = json.loads((destination / "sample-00.json").read_text())
            metadata = json.loads((destination / "run-metadata.json").read_text())
            assert result.returncode == (0 if expected == "pass" else 1), result
            assert record["classification"] == expected, record
            assert metadata["project_source_sha256"]["scripts/run_umd.py"], metadata
            assert metadata["library_sha256"] is None, metadata
            if expected == "pass":
                assert record["constructor_us"] == "12.3", record
            print(f"PASS synthetic_runner_classification={expected}")
    print("synthetic_classifier_cases_passed=7; actual_umd_executions=0")


if __name__ == "__main__":
    main()
