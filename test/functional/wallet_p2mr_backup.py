#!/usr/bin/env python3
# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Restore custom P2MR trees on descriptor wallets via listp2mr / importp2mr."""

from decimal import Decimal

from test_framework.script import LEAF_VERSION_TAPSCRIPT
from test_framework.test_framework import BTQTestFramework
from test_framework.util import assert_equal


class WalletP2MRBackupTest(BTQTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser, descriptors=True, legacy=False)

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [["-acceptnonstdtxn=1"]]

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
        imported = restored.importp2mr(exported)
        assert_equal(len(imported), 1)
        assert_equal(imported[0]["address"], funded["address"])

        spend = restored.createp2mrspend(imported[0]["p2mr_id"], source.getnewaddress(), Decimal("0.4"))
        signed = restored.signp2mrtransaction(spend["hex"], imported[0]["p2mr_id"])
        assert signed["complete"]
        txid = restored.sendrawtransaction(signed["hex"])
        self.generate(node, 1)
        assert_equal(restored.gettransaction(txid, True)["confirmations"], 1)


if __name__ == "__main__":
    WalletP2MRBackupTest().main()
