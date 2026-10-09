#!/usr/bin/env python3
# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Regression checks for failures that must not be reported as green CI."""

from contextlib import redirect_stdout
import importlib.util
import io
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

import check_known_failures as checker


class FunctionalHarnessTest(unittest.TestCase):
    def check_log(self, rows, *, footer=True, completed=True, coverage=False, known=""):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "run.log"
            failures = Path(directory) / "known.txt"
            log.write_text("TEST | STATUS | DURATION\n" + rows + ("ALL | Failed | 1 s\n" if footer else "") + ("Functional test runner completed.\n" if completed else ""))
            failures.write_text(known)
            argv = ["checker", str(log), str(failures)] + (["--coverage"] if coverage else [])
            with patch.object(sys, "argv", argv), patch.object(checker, "expected_base_tests", return_value={"a.py", "b.py"}), redirect_stdout(io.StringIO()):
                return checker.main()

    def test_completed_known_failure(self):
        self.assertEqual(self.check_log("a.py | Passed | 1 s\nb.py | Failed | 1 s\n", known="b.py\n"), 0)

    def test_new_failure(self):
        self.assertEqual(self.check_log("a.py | Passed | 1 s\nb.py | Failed | 1 s\n"), 1)

    def test_truncated_table(self):
        self.assertEqual(self.check_log("a.py | Passed | 1 s\nb.py | Passed | 1 s\n", footer=False), 1)

    def test_missing_variant(self):
        self.assertEqual(self.check_log("a.py | Passed | 1 s\n"), 1)

    def test_duplicate_variant(self):
        self.assertEqual(self.check_log("a.py | Passed | 1 s\na.py | Passed | 1 s\nb.py | Passed | 1 s\n"), 1)

    def test_all_skipped(self):
        self.assertEqual(self.check_log("a.py | Skipped | 1 s\nb.py | Skipped | 1 s\n"), 1)

    def test_exception_after_table(self):
        self.assertEqual(self.check_log("a.py | Passed | 1 s\nb.py | Passed | 1 s\n", completed=False), 1)

    def test_missing_rpc_coverage(self):
        self.assertEqual(self.check_log("a.py | Passed | 1 s\nb.py | Passed | 1 s\n", coverage=True), 1)

    def test_framework_failure_exits_nonzero(self):
        path = Path(__file__).resolve().parents[2] / "test/functional/test_runner.py"
        spec = importlib.util.spec_from_file_location("functional_runner", path)
        runner = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(runner)

        class BrokenFrameworkTest(unittest.TestCase):
            def runTest(self):
                self.fail("Injected framework failure")

        text_runner = unittest.TextTestRunner(stream=io.StringIO())
        with patch.object(runner, "TEST_FRAMEWORK_MODULES", ["injected"]), patch.object(runner.subprocess, "run"), patch.object(unittest.TestLoader, "loadTestsFromName", return_value=BrokenFrameworkTest()), patch.object(unittest, "TextTestRunner", return_value=text_runner), patch.object(sys, "path", list(sys.path)), redirect_stdout(io.StringIO()):
            with self.assertRaises(SystemExit) as exited:
                runner.run_tests(test_list=[], src_dir=str(path.parents[2]), build_dir="/nonexistent", tmpdir="/nonexistent", use_term_control=False)
        self.assertEqual(exited.exception.code, 1)


if __name__ == "__main__":
    unittest.main()
