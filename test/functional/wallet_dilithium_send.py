#!/usr/bin/env python3
# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Regression tests for wallet sends/funding with Dilithium P2MR UTXOs."""

from decimal import Decimal

from test_framework.test_framework import BTQTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error


class WalletDilithiumSendTest(BTQTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser, descriptors=True, legacy=False)

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def dilithium_address(self, wallet, label=""):
        created = wallet.getnewdilithiumaddress(label)
        assert isinstance(created, dict), created
        return created["address"]

    def run_test(self):
        node = self.nodes[0]

        node.createwallet(wallet_name="funding", descriptors=True)
        funding = node.get_wallet_rpc("funding")
        self.generatetoaddress(node, 110, funding.getnewaddress())

        node.createwallet(wallet_name="repro", descriptors=True)
        repro = node.get_wallet_rpc("repro")

        self.log.info("Descriptor Dilithium P2MR keys remain usable after wallet encryption")
        dilithium_address = self.dilithium_address(repro)
        msg = "descriptor encrypted dilithium signing"
        sig = repro.signmessagewithdilithium(dilithium_address, msg)
        assert repro.verifydilithiumsignature(msg, dilithium_address, sig)
        repro.encryptwallet("pass")
        assert_raises_rpc_error(
            -13,
            "Please enter the wallet passphrase with walletpassphrase first",
            repro.signmessagewithdilithium,
            dilithium_address,
            msg,
        )
        repro.walletpassphrase("pass", 100000)
        sig = repro.signmessagewithdilithium(dilithium_address, msg)
        assert repro.verifydilithiumsignature(msg, dilithium_address, sig)

        self.log.info("Fund repro wallet with confirmed Dilithium P2MR and bech32m UTXOs")
        taproot_address = repro.getnewaddress(address_type="bech32m")
        # A second Dilithium UTXO so sendmany can be exercised independently.
        dilithium_address2 = self.dilithium_address(repro)
        funding.sendtoaddress(dilithium_address, Decimal("10"))
        funding.sendtoaddress(dilithium_address2, Decimal("10"))
        funding.sendtoaddress(taproot_address, Decimal("10"))
        self.generate(node, 1)

        utxos = repro.listunspent()
        assert_equal(len(utxos), 3)

        dilithium_utxo = next(utxo for utxo in utxos if utxo["address"] == dilithium_address)
        dilithium_utxo2 = next(utxo for utxo in utxos if utxo["address"] == dilithium_address2)
        taproot_utxo = next(utxo for utxo in utxos if utxo["address"] == taproot_address)

        info = repro.getaddressinfo(dilithium_address)
        assert info["solvable"]
        assert info["isdilithium"]
        assert info["ismine"]

        raw = repro.createrawtransaction(
            [{"txid": dilithium_utxo["txid"], "vout": dilithium_utxo["vout"]}],
            [{repro.getnewaddress(): Decimal("0.01")}],
        )
        funded = repro.fundrawtransaction(raw, {"add_inputs": False})
        assert funded["hex"]
        assert funded["fee"] > 0

        self.log.info("sendtoaddress must return a valid, broadcastable tx spending the Dilithium P2MR UTXO")
        # Lock every other input so coin selection is forced to spend the Dilithium UTXO,
        # exercising the OP_CHECKSIGDILITHIUM P2MR signing path (regression for issue #41).
        repro.lockunspent(False, [
            {"txid": taproot_utxo["txid"], "vout": taproot_utxo["vout"]},
            {"txid": dilithium_utxo2["txid"], "vout": dilithium_utxo2["vout"]},
        ])
        destination = repro.getnewaddress()
        txid = repro.sendtoaddress(destination, Decimal("0.01"))
        assert txid
        assert txid in node.getrawmempool()

        # The Dilithium UTXO must actually be the input that was spent.
        spent_outpoints = {(vin["txid"], vin["vout"]) for vin in node.getrawtransaction(txid, True)["vin"]}
        assert (dilithium_utxo["txid"], dilithium_utxo["vout"]) in spent_outpoints

        # Crux of issue #41: the RPC reported success AND the tx is consensus-valid.
        # Mining it proves the Dilithium signature verifies, not merely that it relayed.
        self.generate(node, 1)
        assert_equal(repro.gettransaction(txid)["confirmations"], 1)
        unspent_after = {(u["txid"], u["vout"]) for u in repro.listunspent(0)}
        assert (dilithium_utxo["txid"], dilithium_utxo["vout"]) not in unspent_after

        self.log.info("sendmany must also spend a Dilithium P2MR UTXO and broadcast a valid tx")
        repro.lockunspent(True)
        # Leave only the second Dilithium UTXO spendable.
        others = [
            {"txid": u["txid"], "vout": u["vout"]}
            for u in repro.listunspent(0)
            if (u["txid"], u["vout"]) != (dilithium_utxo2["txid"], dilithium_utxo2["vout"])
        ]
        repro.lockunspent(False, others)
        many_txid = repro.sendmany("", {
            repro.getnewaddress(): Decimal("0.2"),
            self.dilithium_address(repro): Decimal("0.3"),
        })
        assert many_txid in node.getrawmempool()
        many_spent = {(vin["txid"], vin["vout"]) for vin in node.getrawtransaction(many_txid, True)["vin"]}
        assert (dilithium_utxo2["txid"], dilithium_utxo2["vout"]) in many_spent
        self.generate(node, 1)
        assert_equal(repro.gettransaction(many_txid)["confirmations"], 1)


if __name__ == "__main__":
    WalletDilithiumSendTest().main()
