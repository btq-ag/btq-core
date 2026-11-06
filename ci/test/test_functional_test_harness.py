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


class FunctionalHarnessTest(unittest.TestCase):
    def test_authoritative_rpc_reference_includes_wallet_commands(self):
        path = Path(__file__).resolve().parents[2] / "test/functional/test_runner.py"
        sys.path.insert(0, str(path.parent))
        self.addCleanup(sys.path.remove, str(path.parent))
        from test_framework.coverage import write_all_rpc_commands
        spec = importlib.util.spec_from_file_location("coverage_runner", path)
        runner = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(runner)

        class Interface:
            def __init__(self, commands):
                self.commands = commands

            def help(self):
                return self.commands

        with tempfile.TemporaryDirectory() as directory:
            core = Interface("getblockcount\n")
            current = Interface("getblockcount\nupgradewallet\nwalletpassphrase\n")
            historical = Interface("getblockcount\nobsolete_rpc\n")
            write_all_rpc_commands(directory, core)
            write_all_rpc_commands(directory, current, authoritative=True)
            self.assertFalse(write_all_rpc_commands(directory, core))
            self.assertFalse(write_all_rpc_commands(directory, historical))
            calls = Path(directory) / "coverage.test.txt"
            calls.write_text("getblockcount\nupgradewallet\n")
            coverage = runner.RPCCoverage.__new__(runner.RPCCoverage)
            coverage.dir = directory
            self.assertEqual(coverage._get_uncovered_rpc_commands(), {"walletpassphrase"})
            with redirect_stdout(io.StringIO()):
                self.assertFalse(coverage.report_rpc_coverage())
                calls.write_text("getblockcount\nupgradewallet\nwalletpassphrase\n")
                self.assertTrue(coverage.report_rpc_coverage())

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

    def test_rpc_rejections_count_but_transport_failures_do_not(self):
        path = Path(__file__).resolve().parents[2] / "test/functional"
        sys.path.insert(0, str(path))
        self.addCleanup(sys.path.remove, str(path))
        from test_framework.authproxy import JSONRPCException
        from test_framework.coverage import AuthServiceProxyWrapper

        class ErrorProxy:
            _service_name = "signtransactionwithdilithium"

            def __init__(self, code):
                self.code = code

            def __call__(self):
                raise JSONRPCException({"code": self.code, "message": "injected error"})

        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "coverage.test.txt"
            for code in (-32700, -32600, -32601, -342, -343, -344):
                proxy = AuthServiceProxyWrapper(ErrorProxy(code), "unused", str(log))
                with self.assertRaises(JSONRPCException):
                    proxy()
                self.assertFalse(log.exists())
            proxy = AuthServiceProxyWrapper(ErrorProxy(-32), "unused", str(log))
            with self.assertRaises(JSONRPCException):
                proxy()
            self.assertEqual(log.read_text(), "signtransactionwithdilithium\n")


if __name__ == "__main__":
    unittest.main()
