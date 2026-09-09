#!/usr/bin/env python3
"""Synthetic process classifier tests. These do NOT run TT-UMD or ttsim."""
import json
import contextlib
import importlib.util
import io
from pathlib import Path
import subprocess
import sys
import tempfile
from unittest import mock

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


def process_exit_during_timeout_signal(directory, after_sigterm):
    """Deterministically model exit between wait timeout and group signalling."""
    spec = importlib.util.spec_from_file_location("umd_runner_under_test", ROOT / "scripts/run_umd.py")
    runner = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(runner)
    destination = directory / ("kill_race" if after_sigterm else "term_race")

    class ExitedProcess:
        pid = 123456789  # mock only: no operating-system process is signalled
        returncode = None
        waits = 0

        def wait(self, timeout=None):
            self.waits += 1
            if self.waits <= (2 if after_sigterm else 1):
                raise subprocess.TimeoutExpired("synthetic-child", timeout)
            self.returncode = 0
            return self.returncode

    process = ExitedProcess()
    signals = [None, ProcessLookupError()] if after_sigterm else [ProcessLookupError()]
    argv = ["run_umd.py", str(directory / "mock-binary"), str(directory / "mock.so"),
            "--output", str(destination), "--timeout", "1", "--repeats", "1"]
    with mock.patch.object(sys, "argv", argv), \
            mock.patch.object(runner, "metadata"), \
            mock.patch.object(runner.subprocess, "Popen", return_value=process), \
            mock.patch.object(runner.os, "killpg", side_effect=signals) as killpg, \
            contextlib.redirect_stdout(io.StringIO()):
        try:
            runner.main()
        except SystemExit as error:
            if error.code != 1:
                raise AssertionError(f"Expected failed timeout run, got {error.code}") from error
        else:
            raise AssertionError("Runner did not return a failing timeout status")
    record = json.loads((destination / "sample-00.json").read_text())
    if record["classification"] != "timeout" or record["exit_code"] != 0:
        raise AssertionError(f"Timeout race lost its outcome or reaped exit code: {record}")
    if killpg.call_count != (2 if after_sigterm else 1):
        raise AssertionError("Expected signal race was not exercised")
    print(f"PASS synthetic_timeout_exit_before={'SIGKILL' if after_sigterm else 'SIGTERM'}")


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
        process_exit_during_timeout_signal(directory, after_sigterm=False)
        process_exit_during_timeout_signal(directory, after_sigterm=True)
    print("synthetic_classifier_cases_passed=9; actual_umd_executions=0")


if __name__ == "__main__":
    main()
