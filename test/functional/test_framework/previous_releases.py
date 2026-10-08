# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Pinned, source-built BTQ releases for disposable compatibility tests."""

import hashlib
import json
import os
from pathlib import Path

from .test_framework import SkipTest


RELEASES = {
    "v0.4.3-testnet": ("41ed614252380520d57f70b8265beeedd965eaaa", 403),
    "v0.5.0-testnet": ("14752190a5e53ea62b28272aec8df6831dd3e03e", 500),
}


def release_binaries():
    directory = os.getenv("BTQ_PREVIOUS_RELEASES")
    if not directory:
        raise SkipTest("set BTQ_PREVIOUS_RELEASES to pinned source builds from test/build_previous_releases.py")
    root = Path(directory).resolve()
    manifest = json.loads((root / "manifest.json").read_text(encoding="utf-8"))
    result = []
    for tag, (commit, version) in RELEASES.items():
        record = manifest[tag]
        assert record["source_commit"] == commit, f"unexpected source commit for {tag}"
        paths = {}
        for name in ("btqd", "btq-cli"):
            path = root / tag / "bin" / name
            assert hashlib.sha256(path.read_bytes()).hexdigest() == record["sha256"][name], f"binary checksum mismatch: {path}"
            assert os.access(path, os.X_OK), f"binary is not executable: {path}"
            paths[name] = str(path)
        result.append({"tag": tag, "version": version, **paths})
    return result
