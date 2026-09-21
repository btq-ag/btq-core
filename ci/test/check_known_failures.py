#!/usr/bin/env python3
# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Compare a functional test run against the list of known failures.

Usage: check_known_failures.py <test_runner output> <known failures file>

test_runner.py exits non-zero if any test fails. While many functional
tests still fail on master (#104), that exit code tells a PR author
nothing. This script reads the results table that test_runner.py prints
at the end and fails only when a test fails that is not on the known
list. Then a PR cannot break a test that passes today.

It also warns about listed tests that now pass, were skipped or did not
run, so the list gets shorter as tests are fixed.
"""

import re
import sys

HEADER = re.compile(r"^TEST\s*\| STATUS\s*\| DURATION")
# One row per test: "<name padded> | <glyph> <status> | <seconds> s".
# The glyph is a Unicode symbol, or P/x/o if stdout is not UTF-8.
ROW = re.compile(r"^(?P<name>\S.*?)\s*\| (?:\S )?(?P<status>Passed|Failed|Skipped)\s*\| ")


def read_results(path):
    with open(path, encoding="utf8", errors="replace") as f:
        lines = f.read().splitlines()
    # Failed tests print their own output before the table, so start
    # from the last header line.
    starts = [i for i, line in enumerate(lines) if HEADER.match(line)]
    if not starts:
        return None
    results = {}
    for line in lines[starts[-1] + 1:]:
        m = ROW.match(line)
        if m and m.group("name") != "ALL":
            results[m.group("name")] = m.group("status")
    return results


def read_known(path):
    known = []
    with open(path, encoding="utf8") as f:
        for line in f:
            name = line.split("#", 1)[0].strip()
            if name:
                known.append(name)
    return known


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    results = read_results(sys.argv[1])
    if not results:
        print("::error::No results table found in the test_runner.py output. "
              "The runner did not finish; see the log above.")
        return 1
    known = read_known(sys.argv[2])
    known_set = set(known)

    failed = sorted(name for name, status in results.items() if status == "Failed")
    new = [name for name in failed if name not in known_set]
    fixed = [name for name in known if results.get(name) == "Passed"]
    skipped = [name for name in known if results.get(name) == "Skipped"]
    missing = [name for name in known if name not in results]

    counts = {s: sum(1 for v in results.values() if v == s) for s in ("Passed", "Failed", "Skipped")}
    print(f"{len(results)} tests: {counts['Passed']} passed, {counts['Failed']} failed "
          f"({len(failed) - len(new)} known, {len(new)} new), {counts['Skipped']} skipped.")

    for name in fixed:
        print(f"::warning::{name} passes but is on the known-failures list. Remove it from the list.")
    for name in skipped:
        print(f"::warning::{name} is on the known-failures list but was skipped.")
    for name in missing:
        print(f"::warning::{name} is on the known-failures list but did not run. Was it renamed or removed?")
    for name in new:
        print(f"::error::{name} failed and is not on the known-failures list.")

    print("::group::All failing tests, in known-failures list format")
    print("\n".join(failed))
    print("::endgroup::")

    return 1 if new else 0


if __name__ == "__main__":
    sys.exit(main())
