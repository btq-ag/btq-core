#!/usr/bin/env python3
# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""End-to-end wallet send paths for BIP360/P2MR destinations."""

from decimal import Decimal

from test_framework.script import LEAF_VERSION_TAPSCRIPT
from test_framework.test_framework import BTQTestFramework
from test_framework.util import assert_equal


class WalletBIP360SendPathsTest(BTQTestFramework):
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

        node.createwallet(wallet_name="sender", descriptors=True)
        sender = node.get_wallet_rpc("sender")
        self.generatetoaddress(node, 110, sender.getnewaddress())

        node.createwallet(wallet_name="receiver", descriptors=True)
        receiver = node.get_wallet_rpc("receiver")

        tree = [{
            "depth": 0,
            "leaf_version": LEAF_VERSION_TAPSCRIPT,
            "script": "51",  # OP_TRUE
        }]

        self.log.info("Receiver creates wallet-tracked P2MR metadata")
        created = receiver.getnewp2mraddress(tree, "plain-wallet-send")
        assert created["address"].startswith("qcrt1z")
        assert created["scriptPubKey"].startswith("5220")
        assert_equal(receiver.getp2mrinfo(created["p2mr_id"])["address"], created["address"])

        self.log.info("Plain sendtoaddress can fund a wallet-tracked P2MR address")
        txid = sender.sendtoaddress(created["address"], Decimal("1.0"))
        assert txid in node.getrawmempool()
        self.generate(node, 1)

        spendable = receiver.createp2mrspend(created["p2mr_id"], sender.getnewaddress(), Decimal("0.5"))
        assert_equal(spendable["p2mr_id"], created["p2mr_id"])
        assert spendable["input_txid"]

        self.log.info("Wallet metadata signs and broadcasts the P2MR spend")
        signed = receiver.signp2mrtransaction(spendable["hex"], created["p2mr_id"])
        assert signed["complete"]
        accepted = receiver.testp2mrtransaction(signed["hex"])
        assert_equal(accepted[0]["allowed"], True)
        spent_txid = receiver.sendrawtransaction(signed["hex"])
        self.generate(node, 1)
        assert_equal(receiver.gettransaction(spent_txid, True)["confirmations"], 1)

        self.log.info("sendtop2mr convenience path remains idempotent for an identical tree")
        funded = sender.sendtop2mr(tree, Decimal("0.25"), "sender-owned-p2mr")
        duplicate = sender.getnewp2mraddress(tree, "sender-owned-p2mr-duplicate")
        assert_equal(funded["address"], duplicate["address"])
        assert_equal(funded["p2mr_id"], duplicate["p2mr_id"])


if __name__ == "__main__":
    WalletBIP360SendPathsTest().main()
