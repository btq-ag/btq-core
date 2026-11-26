#!/usr/bin/env python3
# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Validate the summary printed by a complete functional test run."""

import argparse
import ast
from pathlib import Path
import re
import sys

HEADER = re.compile(r"^TEST\s*\| STATUS\s*\| DURATION")
# One row per test: "<name padded> | <glyph> <status> | <seconds> s".
# The glyph is a Unicode symbol, or P/x/o if stdout is not UTF-8.
ROW = re.compile(r"^(?P<name>\S.*?)\s*\| (?:\S )?(?P<status>Passed|Failed|Skipped)\s*\| ")


def read_results(lines):
    # Failed tests print their own output before the table, so start
    # from the last header line.
    starts = [i for i, line in enumerate(lines) if HEADER.match(line)]
    if not starts or lines[-1] != "Functional test runner completed.":
        return None
    results = {}
    for index, line in enumerate(lines[starts[-1] + 1:], starts[-1] + 1):
        m = ROW.match(line)
        if m:
            name = m.group("name")
            if name == "ALL":
                return results, m.group("status"), lines[index + 1:]
            if name in results:
                return None
            results[name] = m.group("status")
    return None


def expected_base_tests(extended=False):
    """Read the runner's inventory without importing or executing the runner."""
    runner = Path(__file__).resolve().parents[2] / "test/functional/test_runner.py"
    names = {"BASE_SCRIPTS", "EXTENDED_SCRIPTS"} if extended else {"BASE_SCRIPTS"}
    expected = set()
    for statement in ast.parse(runner.read_text(encoding="utf8")).body:
        if isinstance(statement, ast.Assign) and any(
            isinstance(target, ast.Name) and target.id in names
            for target in statement.targets
        ):
            expected.update(ast.literal_eval(statement.value))
            names.difference_update(target.id for target in statement.targets if isinstance(target, ast.Name))
    if names:
        raise ValueError("Missing runner inventory: " + ", ".join(sorted(names)))
    return expected


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--extended", action="store_true")
    parser.add_argument("--coverage", action="store_true")
    args = parser.parse_args()

    lines = args.log.read_text(encoding="utf8", errors="replace").splitlines()
    summary = read_results(lines)
    if summary is None or not summary[0]:
        print("::error::No complete, unique results table found in the test_runner.py output.")
        return 1

    results, aggregate_status, summary_footer = summary
    expected = expected_base_tests(args.extended)
    missing_tests = sorted(expected - results.keys())
    unexpected_tests = sorted(results.keys() - expected)
    if missing_tests:
        print("::error::Incomplete suite: " + ", ".join(missing_tests))
    if unexpected_tests:
        print("::error::Unexpected tests: " + ", ".join(unexpected_tests))
    if missing_tests or unexpected_tests:
        return 1

    if all(status == "Skipped" for status in results.values()):
        print("::error::All functional tests were skipped.")
        return 1
    if args.coverage and "All RPC commands covered." not in summary_footer:
        print("::error::RPC coverage did not complete successfully.")
        return 1

    counts = {status: sum(value == status for value in results.values()) for status in ("Passed", "Failed", "Skipped")}
    expected_status = "Failed" if counts["Failed"] else "Passed"
    if aggregate_status != expected_status:
        print("::error::The aggregate test status does not match the individual results.")
        return 1

    print(f"{len(results)} tests: {counts['Passed']} passed, {counts['Failed']} failed, {counts['Skipped']} skipped.")
    for name, status in sorted(results.items()):
        if status == "Failed":
            print(f"::error::{name} failed.")
    return 1 if counts["Failed"] else 0


if __name__ == "__main__":
    sys.exit(main())
