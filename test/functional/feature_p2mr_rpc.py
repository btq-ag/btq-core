#!/usr/bin/env python3
# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""RPC-level P2MR wallet flow tests."""

from decimal import Decimal

from test_framework.script import LEAF_VERSION_TAPSCRIPT
from test_framework.test_framework import BTQTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error


class P2MRRPCTest(BTQTestFramework):
    def add_options(self, parser):
        # Force descriptor wallets so the test runs on SQLite-only builds.
        self.add_wallet_options(parser, descriptors=True, legacy=False)

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [["-acceptnonstdtxn=1"]]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        node = self.nodes[0]
        node.createwallet(wallet_name="p2mr", descriptors=True)
        wallet = node.get_wallet_rpc("p2mr")
        self.generatetoaddress(node, 110, wallet.getnewaddress())

        tree = [{
            "depth": 0,
            "leaf_version": LEAF_VERSION_TAPSCRIPT,
            "script": "51",  # OP_TRUE
        }]

        self.log.info("Create, persist, and list a P2MR address")
        created = wallet.getnewp2mraddress(tree, "rpc-p2mr")
        assert created["address"]
        assert created["p2mr_id"]
        listed = wallet.listp2mr()
        assert any(e["id"] == created["p2mr_id"] for e in listed)
        assert_equal(wallet.getp2mrinfo(created["p2mr_id"])["id"], created["p2mr_id"])
        assert_raises_rpc_error(-8, "unknown p2mr_id", wallet.getp2mrinfo, "does-not-exist")

        self.log.info("Fund through convenience RPC")
        funded = wallet.sendtop2mr(tree, Decimal("1.0"), "rpc-p2mr-fund")
        assert funded["txid"]
        self.generate(node, 1)

        p2mr_id = funded["p2mr_id"]
        destination = wallet.getnewaddress()

        self.log.info("Create unsigned spend and sign via P2MR metadata")
        spend = wallet.createp2mrspend(p2mr_id, destination, Decimal("0.5"))
        signed = wallet.signp2mrtransaction(spend["hex"], p2mr_id)
        assert signed["complete"]

        self.log.info("Mempool dry-run + broadcast + mine")
        accept = wallet.testp2mrtransaction(signed["hex"])
        assert accept[0]["allowed"], accept[0].get("reject-reason", "")
        txid = accept[0]["txid"]
        wallet.sendrawtransaction(signed["hex"])
        self.generate(node, 1)
        tx = wallet.gettransaction(txid, True)
        assert tx["confirmations"] > 0


if __name__ == "__main__":
    P2MRRPCTest().main()
