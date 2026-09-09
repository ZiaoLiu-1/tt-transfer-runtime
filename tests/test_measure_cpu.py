#!/usr/bin/env python3
"""Check measurement freshness in temporary trees without running a benchmark."""
import contextlib
import io
import json
import os
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]


class MeasurementFreshnessTests(unittest.TestCase):
    def exercise(self, stale_input=None):
        with tempfile.TemporaryDirectory(prefix="tt-measure-synthetic-") as temporary:
            repo = Path(temporary).resolve()
            sources = [repo / name for name in (
                "src/runtime.cpp", "src/cpu_backend.cpp", "benchmarks/cpu_benchmark.cpp",
                "include/tt_transfer/runtime.hpp", "include/tt_transfer/cpu_backend.hpp", "CMakeLists.txt")]
            for source in sources:
                source.parent.mkdir(parents=True, exist_ok=True)
                source.write_text("synthetic source\n")
                os.utime(source, (1700000000, 1700000000))
            script = repo / "scripts/measure_cpu.py"
            script.parent.mkdir()
            script.write_text((ROOT / "scripts/measure_cpu.py").read_text())
            binary = repo / "build/cpu_benchmark"
            binary.parent.mkdir()
            binary.write_text("synthetic binary: never executed\n")
            os.utime(binary, (1700000020, 1700000020))
            cache = binary.parent / "CMakeCache.txt"
            cache.write_text(f"CMAKE_HOME_DIRECTORY:INTERNAL={repo}\n"
                             "CMAKE_CXX_COMPILER:FILEPATH=/synthetic/compiler\n"
                             "CMAKE_BUILD_TYPE:STRING=Debug\n")
            os.utime(cache, (1700000010, 1700000010))
            if stale_input:
                changed = cache if stale_input == "cache" else sources[0]
                os.utime(changed, (1700000030, 1700000030))
            output = repo / "result"
            lock = repo / "measure.lock"
            argv = [str(script), "--binary", str(binary), "--lock", str(lock), "--output", str(output)]
            with mock.patch.object(sys, "argv", argv), \
                    mock.patch.object(subprocess, "run", return_value=subprocess.CompletedProcess([], 0)) as launch, \
                    mock.patch.object(subprocess, "check_output", return_value="synthetic compiler\n"), \
                    contextlib.redirect_stdout(io.StringIO()):
                if stale_input:
                    message = "CMake configuration is newer" if stale_input == "cache" else "measured sources changed"
                    with self.assertRaisesRegex(ValueError, message):
                        runpy.run_path(str(script), run_name="__main__")
                    launch.assert_not_called()
                    self.assertFalse((output / "environment.json").exists())
                else:
                    runpy.run_path(str(script), run_name="__main__")
                    self.assertEqual(launch.call_count, 1)
                    self.assertEqual(launch.call_args.args[0], [str(binary), "20000", "7"])
                    metadata = json.loads((output / "environment.json").read_text())
                    self.assertEqual(metadata["status"], "passed")
                    self.assertEqual(metadata["cmake"]["CMAKE_BUILD_TYPE"], "Debug")
            self.assertFalse(lock.exists(), "Measurement lock was not released")

    def test_fresh_inputs_reach_measurement(self):
        self.exercise()

    def test_source_changed_after_build_is_rejected(self):
        self.exercise("source")

    def test_reconfigured_cache_without_rebuild_is_rejected(self):
        self.exercise("cache")


if __name__ == "__main__":
    unittest.main()
