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

        self.log.info("Refuse OP_TRUE trees unless allow_trivial_leaves is set")
        assert_raises_rpc_error(-4, "trivial anyone-can-spend", wallet.getnewp2mraddress, tree, "rpc-p2mr")
        assert_raises_rpc_error(-4, "trivial anyone-can-spend", wallet.sendtop2mr, tree, Decimal("1.0"), "rpc-p2mr-fund")

        self.log.info("Create, persist, and list a P2MR address")
        created = wallet.getnewp2mraddress(tree, "rpc-p2mr", True)
        assert created["address"]
        assert created["p2mr_id"]
        duplicate = wallet.getnewp2mraddress(tree, "rpc-p2mr-duplicate", True)
        assert_equal(duplicate["address"], created["address"])
        assert_equal(duplicate["p2mr_id"], created["p2mr_id"])
        listed = wallet.listp2mr()
        assert_equal(sum(1 for e in listed if e["address"] == created["address"]), 1)
        assert any(e["id"] == created["p2mr_id"] for e in listed)
        assert_equal(wallet.getp2mrinfo(created["p2mr_id"])["id"], created["p2mr_id"])
        assert_raises_rpc_error(-8, "unknown p2mr_id", wallet.getp2mrinfo, "does-not-exist")

        self.log.info("Fund through convenience RPC")
        funded = wallet.sendtop2mr(tree, Decimal("1.0"), "rpc-p2mr-fund", allow_trivial_leaves=True)
        assert funded["txid"]
        assert_equal(funded["p2mr_id"], created["p2mr_id"])
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

        self.log.info("Create, sign, broadcast, and mine a Dilithium P2MR receive spend")
        dilithium_created = wallet.getnewdilithiumaddress("rpc-dilithium-receive")
        assert isinstance(dilithium_created, dict), dilithium_created
        dilithium_address = dilithium_created["address"]
        dilithium_info = wallet.getaddressinfo(dilithium_address)
        assert dilithium_info["solvable"]
        assert dilithium_info["isdilithium"]
        assert_equal(dilithium_info["scriptPubKey"], dilithium_created["scriptPubKey"])
        # Fund the wallet-created Dilithium P2MR destination directly.
        wallet.sendtoaddress(dilithium_address, Decimal("0.2"))
        self.generate(node, 1)
        dilithium_spend = wallet.createp2mrspend(dilithium_created["p2mr_id"], destination, Decimal("0.1"))
        dilithium_signed = wallet.signp2mrtransaction(dilithium_spend["hex"], dilithium_created["p2mr_id"])
        assert dilithium_signed["complete"], dilithium_signed
        dilithium_accept = wallet.testp2mrtransaction(dilithium_signed["hex"])
        assert dilithium_accept[0]["allowed"], dilithium_accept[0].get("reject-reason", "")
        dilithium_txid = wallet.sendrawtransaction(dilithium_signed["hex"])
        self.generate(node, 1)
        dilithium_tx = wallet.gettransaction(dilithium_txid, True)
        assert dilithium_tx["confirmations"] > 0

        self.log.info("Do not mark a single-leaf P2MR script complete without required witness data")
        needs_stack_tree = [{
            "depth": 0,
            "leaf_version": LEAF_VERSION_TAPSCRIPT,
            "script": "7551",  # OP_DROP OP_TRUE
        }]
        needs_stack = wallet.sendtop2mr(needs_stack_tree, Decimal("0.1"), "rpc-p2mr-needs-stack")
        self.generate(node, 1)
        needs_stack_spend = wallet.createp2mrspend(needs_stack["p2mr_id"], destination, Decimal("0.05"))
        incomplete = wallet.signp2mrtransaction(needs_stack_spend["hex"], needs_stack["p2mr_id"])
        assert_equal(incomplete["complete"], False)
        rejected = wallet.testp2mrtransaction(incomplete["hex"])
        assert_equal(rejected[0]["allowed"], False)


if __name__ == "__main__":
    P2MRRPCTest().main()
