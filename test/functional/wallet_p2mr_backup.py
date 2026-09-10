#!/usr/bin/env python3
# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Restore custom P2MR trees on descriptor wallets via listp2mr / importp2mr."""

import copy
from decimal import Decimal

from test_framework.script import LEAF_VERSION_TAPSCRIPT
from test_framework.test_framework import BTQTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error


class WalletP2MRBackupTest(BTQTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser, descriptors=True, legacy=False)

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [["-acceptnonstdtxn=1", "-blockfilterindex=1"]]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        node = self.nodes[0]
        node.createwallet(wallet_name="source", descriptors=True)
        source = node.get_wallet_rpc("source")
        self.generatetoaddress(node, 110, source.getnewaddress())

        tree = [{
            "depth": 0,
            "leaf_version": LEAF_VERSION_TAPSCRIPT,
            "script": "51",
        }]
        funded = source.sendtop2mr(tree, Decimal("1.0"), "custom-vault", allow_trivial_leaves=True)
        self.generate(node, 1)
        exported = source.listp2mr()
        assert any(e["id"] == funded["p2mr_id"] for e in exported)

        self.log.info("Restore the custom tree on a fresh descriptor wallet")
        node.createwallet(wallet_name="restored", descriptors=True)
        restored = node.get_wallet_rpc("restored")
        restore_entries = copy.deepcopy(exported)
        del restore_entries[0]["created_at"]
        later_entry = {
            "tree": [{
                "depth": 0,
                "leaf_version": LEAF_VERSION_TAPSCRIPT,
                "script": "52",
            }],
            "label": "later-entry",
            "created_at": node.getblockheader(node.getbestblockhash())["time"] + 1000,
        }
        imported = restored.importp2mr([restore_entries[0], later_entry])
        assert_equal(len(imported), 2)
        restored_funded = next(row for row in imported if row["address"] == funded["address"])

        spend = restored.createp2mrspend(restored_funded["p2mr_id"], source.getnewaddress(), Decimal("0.4"))
        signed = restored.signp2mrtransaction(spend["hex"], restored_funded["p2mr_id"])
        assert signed["complete"]
        txid = restored.sendrawtransaction(signed["hex"])
        self.generate(node, 1)
        assert_equal(restored.gettransaction(txid, True)["confirmations"], 1)

        self.log.info("Reject the whole batch when a later entry is invalid")
        node.createwallet(wallet_name="atomic", descriptors=True)
        atomic = node.get_wallet_rpc("atomic")
        bad_entry = copy.deepcopy(later_entry)
        bad_entry["merkle_root"] = 1
        assert_raises_rpc_error(-8, "merkle_root must be a string", atomic.importp2mr, [later_entry, bad_entry])
        assert_equal(atomic.listp2mr(), [])

        self.log.info("Preserve an explicit zero creation timestamp")
        node.createwallet(wallet_name="zero_time", descriptors=True)
        zero_time = node.get_wallet_rpc("zero_time")
        zero_entry = copy.deepcopy(later_entry)
        zero_entry["created_at"] = 0
        zero_imported = zero_time.importp2mr([zero_entry])
        zero_metadata = zero_time.getp2mrinfo(zero_imported[0]["p2mr_id"])
        assert_equal(zero_metadata["created_at"], 0)


if __name__ == "__main__":
    WalletP2MRBackupTest().main()
