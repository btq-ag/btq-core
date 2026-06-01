#!/usr/bin/env python3
# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Regression tests for wallet sends/funding with Dilithium descriptor UTXOs."""

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

    def run_test(self):
        node = self.nodes[0]

        node.createwallet(wallet_name="funding", descriptors=True)
        funding = node.get_wallet_rpc("funding")
        self.generatetoaddress(node, 110, funding.getnewaddress())

        node.createwallet(wallet_name="repro", descriptors=True)
        repro = node.get_wallet_rpc("repro")

        self.log.info("Descriptor Dilithium keys remain usable after wallet encryption")
        dilithium_address = repro.getnewdilithiumaddress()
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

        self.log.info("Fund repro wallet with confirmed Dilithium and bech32m UTXOs")
        taproot_address = repro.getnewaddress(address_type="bech32m")
        funding.sendtoaddress(dilithium_address, Decimal("10"))
        funding.sendtoaddress(taproot_address, Decimal("10"))
        self.generate(node, 1)

        utxos = repro.listunspent()
        assert_equal(len(utxos), 2)

        dilithium_utxo = next(utxo for utxo in utxos if utxo["address"] == dilithium_address)
        taproot_utxo = next(utxo for utxo in utxos if utxo["address"] == taproot_address)

        raw = repro.createrawtransaction(
            [{"txid": dilithium_utxo["txid"], "vout": dilithium_utxo["vout"]}],
            [{repro.getnewaddress(): Decimal("0.01")}],
        )
        funded = repro.fundrawtransaction(raw, {"add_inputs": False})
        assert funded["hex"]
        assert funded["fee"] > 0

        self.log.info("sendtoaddress must return success while spending the Dilithium UTXO")
        repro.lockunspent(False, [{"txid": taproot_utxo["txid"], "vout": taproot_utxo["vout"]}])
        destination = repro.getnewaddress()
        txid = repro.sendtoaddress(destination, Decimal("0.01"))
        assert txid
        assert txid in node.getrawmempool()


if __name__ == "__main__":
    WalletDilithiumSendTest().main()
