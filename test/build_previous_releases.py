#!/usr/bin/env python3
# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Build pinned BTQ release sources already present in this Git repository.

No Bitcoin binaries, public-network nodes, or downloads are used. Requires the
normal BTQ autotools, compiler, Boost, libevent, BDB and SQLite dependencies.
"""

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile

sys.path.insert(0, str(Path(__file__).parent / "functional"))
from test_framework.previous_releases import RELEASES


def install_binaries(output, tag, source, *, commit):
    expected, _ = RELEASES[tag]
    assert commit == expected
    destination = output / tag / "bin"
    destination.mkdir(parents=True, exist_ok=True)
    hashes = {}
    for name in ("btqd", "btq-cli"):
        binary = source / "src" / name
        assert binary.is_file() and binary.read_bytes()[:4] == b"\x7fELF", f"not a built Linux executable: {binary}"
        shutil.copy2(binary, destination / name)
        hashes[name] = hashlib.sha256((destination / name).read_bytes()).hexdigest()
    return {"source_commit": commit, "sha256": hashes}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--configure-arg", action="append", default=[])
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    repo = Path(__file__).resolve().parents[1]
    manifest = {}
    for tag, (commit, _) in RELEASES.items():
        actual = subprocess.check_output(["git", "rev-parse", f"{tag}^{{commit}}"], cwd=repo, text=True).strip()
        assert actual == commit, f"fetch and verify the official pinned {tag} tag first"
        source = output / tag / "source"
        source.mkdir(parents=True, exist_ok=False)
        with tempfile.TemporaryFile() as archive:
            subprocess.run(["git", "archive", commit], cwd=repo, stdout=archive, check=True)
            archive.seek(0)
            with tarfile.open(fileobj=archive) as files:
                files.extractall(source, filter="data")
        subprocess.run(["./autogen.sh"], cwd=source, check=True)
        configure = ["./configure", "--without-gui", "--disable-tests", "--disable-bench",
                     "--disable-fuzz", "--disable-fuzz-binary", "--without-zmq",
                     "--with-incompatible-bdb", *args.configure_arg]
        subprocess.run(configure, cwd=source, check=True)
        subprocess.run(["make", f"-j{args.jobs}", "-C", "src", "btqd", "btq-cli"], cwd=source, check=True)
        manifest[tag] = install_binaries(output, tag, source, commit=commit)
        manifest[tag]["configure"] = configure
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
