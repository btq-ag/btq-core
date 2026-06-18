#!/usr/bin/env python3
#
# Copyright (c) 2018-2022 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Check the test suite naming conventions
"""

import ast
from pathlib import Path
import re
import subprocess
import sys


def grep_boost_fixture_test_suite():
    command = [
        "git",
        "grep",
        "-E",
        r"^BOOST_FIXTURE_TEST_SUITE\(",
        "--",
        "src/test/**.cpp",
        "src/wallet/test/**.cpp",
    ]
    return subprocess.check_output(command, text=True, encoding="utf8")


def check_matching_test_names(test_suite_list):
    not_matching = [
        x
        for x in test_suite_list
        if re.search(r"/(.*?)\.cpp:BOOST_FIXTURE_TEST_SUITE\(\1, .*\)", x) is None
    ]
    if len(not_matching) > 0:
        not_matching = "\n".join(not_matching)
        error_msg = (
            "The test suite in file src/test/foo_tests.cpp should be named\n"
            '"foo_tests". Please make sure the following test suites follow\n'
            "that convention:\n\n"
            f"{not_matching}\n"
        )
        print(error_msg)
        return 1
    return 0


def get_duplicates(input_list):
    """
    From https://stackoverflow.com/a/9835819
    """
    seen = set()
    dupes = set()
    for x in input_list:
        if x in seen:
            dupes.add(x)
        else:
            seen.add(x)
    return dupes


def check_unique_test_names(test_suite_list):
    output = [re.search(r"\((.*?),", x) for x in test_suite_list]
    output = [x.group(1) for x in output if x is not None]
    output = get_duplicates(output)
    output = sorted(list(output))

    if len(output) > 0:
        output = "\n".join(output)
        error_msg = (
            "Test suite names must be unique. The following test suite names\n"
            f"appear to be used more than once:\n\n{output}"
        )
        print(error_msg)
        return 1
    return 0


def check_unit_tests_are_registered():
    test_makefile = Path("src/Makefile.test.include").read_text(encoding="utf8")
    unit_test_files = subprocess.check_output(
        [
            "git",
            "ls-files",
            "src/test/*_tests.cpp",
            "src/wallet/test/*_tests.cpp",
        ],
        text=True,
        encoding="utf8",
    ).splitlines()

    missing = []
    for test_file in unit_test_files:
        makefile_path = test_file.removeprefix("src/")
        if makefile_path not in test_makefile:
            missing.append(test_file)

    if missing:
        print(
            "The following unit test source files are not registered in "
            "src/Makefile.test.include:\n\n" + "\n".join(sorted(missing)) + "\n"
        )
        return 1
    return 0


def check_functional_tests_exist():
    runner = Path("test/functional/test_runner.py")
    tree = ast.parse(runner.read_text(encoding="utf8"))
    registered = []
    for node in tree.body:
        if not isinstance(node, ast.Assign):
            continue
        for target in node.targets:
            if not isinstance(target, ast.Name):
                continue
            if target.id not in {"BASE_SCRIPTS", "EXTENDED_SCRIPTS"}:
                continue
            for entry in node.value.elts:
                if isinstance(entry, ast.Constant) and isinstance(entry.value, str):
                    registered.append((target.id, entry.value))

    missing = []
    for group, entry in registered:
        script = entry.split()[0]
        if not Path("test/functional", script).exists():
            missing.append(f"{group}: {entry}")

    if missing:
        print(
            "The following functional tests are registered in "
            "test/functional/test_runner.py but do not exist:\n\n"
            + "\n".join(sorted(missing)) + "\n"
        )
        return 1
    return 0


def main():
    test_suite_list = grep_boost_fixture_test_suite().splitlines()
    exit_code = check_matching_test_names(test_suite_list)
    exit_code |= check_unique_test_names(test_suite_list)
    exit_code |= check_unit_tests_are_registered()
    exit_code |= check_functional_tests_exist()
    sys.exit(exit_code)


if __name__ == "__main__":
    main()
