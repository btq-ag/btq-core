#!/usr/bin/env python3
# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""End-to-end checks for sendmany/sendrawtransaction after BIP-360 changes.

This test validates three transaction paths on regtest:
1) wallet sendmany to multiple recipients
2) raw transaction built/signed by wallet and submitted via sendrawtransaction
3) BIP-360 P2MR script-path spend submitted via sendrawtransaction
"""

from decimal import Decimal

from test_framework.blocktools import COINBASE_MATURITY
from test_framework.messages import (
    COutPoint,
    CTransaction,
    CTxIn,
    CTxInWitness,
    CTxOut,
    SEQUENCE_FINAL,
)
from test_framework.script import (
    CScript,
    OP_TRUE,
    p2mr_construct,
)
from test_framework.test_framework import BTQTestFramework
from test_framework.util import assert_equal
from test_framework.wallet import MiniWallet


class WalletBip360SendPathsTest(BTQTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser)

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        # Keep policy permissive for custom script-path spend checks.
        self.extra_args = [["-acceptnonstdtxn=1"]]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        node = self.nodes[0]
        wallet = node.get_wallet_rpc(self.default_wallet_name)
        miniwallet = MiniWallet(node)

        self.log.info("Funding RPC wallet and MiniWallet")
        self.generate(wallet, COINBASE_MATURITY + 5)
        self.generate(miniwallet, COINBASE_MATURITY + 5)

        self.log.info("1/3: sendmany should succeed and confirm")
        recipients = {
            node.getnewaddress(): Decimal("1.00000000"),
            node.getnewaddress(): Decimal("1.25000000"),
        }
        sendmany_txid = wallet.sendmany("", recipients, 1, "bip360-sendmany-e2e")
        assert sendmany_txid in node.getrawmempool()
        self.generate(node, 1)
        assert_equal(wallet.gettransaction(sendmany_txid)["confirmations"], 1)

        self.log.info("2/3: createrawtransaction + signrawtransactionwithwallet + sendrawtransaction should succeed")
        utxo = wallet.listunspent(1, 9999999)[0]
        fee = Decimal("0.00010000")
        amount = Decimal(str(utxo["amount"])) - fee
        raw_tx = node.createrawtransaction(
            [{"txid": utxo["txid"], "vout": utxo["vout"]}],
            {node.getnewaddress(): amount},
        )
        signed = node.signrawtransactionwithwallet(raw_tx)
        assert_equal(signed["complete"], True)
        raw_send_txid = node.sendrawtransaction(signed["hex"])
        assert raw_send_txid in node.getrawmempool()
        self.generate(node, 1)
        assert_equal(wallet.gettransaction(raw_send_txid)["confirmations"], 1)

        self.log.info("3/3: BIP-360 P2MR script-path spend via sendrawtransaction should succeed")
        p2mr = p2mr_construct([("leaf", CScript([OP_TRUE]))])
        funded = miniwallet.send_to(from_node=node, scriptPubKey=p2mr.scriptPubKey, amount=50_000)
        self.generate(node, 1)

        tx = CTransaction()
        tx.nVersion = 2
        tx.vin = [CTxIn(COutPoint(int(funded["txid"], 16), funded["sent_vout"]), b"", SEQUENCE_FINAL)]
        tx.vout = [CTxOut(40_000, p2mr.scriptPubKey)]

        leaf = p2mr.leaves["leaf"]
        control_block = bytes([leaf.version | 1]) + leaf.merklebranch
        witness = CTxInWitness()
        witness.scriptWitness.stack = [bytes(leaf.script), control_block]
        tx.wit.vtxinwit = [witness]
        tx.rehash()

        p2mr_spend_txid = node.sendrawtransaction(tx.serialize().hex())
        assert p2mr_spend_txid in node.getrawmempool()
        self.generate(node, 1)
        block_hash = node.getbestblockhash()
        block = node.getblock(block_hash)
        assert p2mr_spend_txid in block["tx"]


if __name__ == "__main__":
    WalletBip360SendPathsTest().main()
