#!/usr/bin/env python3
# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Reject an obsolete chainstate schema and recover from local regtest blocks.

The fixture contains only the old DB_COINS key prefix. It tests the schema
guard, not historical release compatibility or deserialization of old coins.
See data/obsolete_chainstate.md for its provenance and regeneration.
"""

import json
from pathlib import Path
import shutil

from test_framework.test_framework import BTQTestFramework
from test_framework.util import assert_equal


class UnsupportedUtxoDbTest(BTQTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def run_test(self):
        node = self.nodes[0]
        self.generatetoaddress(node, 3, node.get_deterministic_priv_key().address)
        tip = node.getbestblockhash()
        before = node.gettxoutsetinfo()
        self.stop_node(0)
        chainstate = node.chain_path / "chainstate"
        shutil.rmtree(chainstate)
        chainstate.mkdir()
        fixture = Path(__file__).parent / "data" / "obsolete_chainstate.json"
        for name, data in json.loads(fixture.read_text(encoding="utf-8")).items():
            assert Path(name).name == name
            (chainstate / name).write_bytes(bytes.fromhex(data))
        node.assert_start_raises_init_error(expected_msg=(
            "Error: Unsupported chainstate database format found. "
            "Please restart with -reindex-chainstate. This will rebuild the chainstate database."
        ))
        self.start_node(0, extra_args=["-reindex-chainstate"])
        assert_equal(node.getbestblockhash(), tip)
        after = node.gettxoutsetinfo()
        for field in ("height", "bestblock", "txouts", "total_amount", "hash_serialized_3"):
            assert_equal(after[field], before[field])
        # Recovery must persist, rather than depend on reindexing every startup.
        self.restart_node(0)
        assert_equal(node.gettxoutsetinfo()["hash_serialized_3"], before["hash_serialized_3"])


if __name__ == '__main__':
    UnsupportedUtxoDbTest().main()
