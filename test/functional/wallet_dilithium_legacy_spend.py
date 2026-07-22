#!/usr/bin/env python3
# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Legacy (BDB) wallets must be able to spend Dilithium coins (issue #74).

Covers the repro matrix from the issue:
  - legacy, plain:     receive OK, spend must work
  - legacy, encrypted: receive OK, spend must work
  - ECDSA coin in a separate legacy wallet still spends (control)
"""

from decimal import Decimal

from test_framework.test_framework import BTQTestFramework
from test_framework.util import assert_equal, assert_greater_than, assert_raises_rpc_error


class WalletDilithiumLegacySpendTest(BTQTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser, descriptors=False)

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()
        self.skip_if_no_bdb()

    def fund_address(self, address, amount):
        txid = self.funding.sendtoaddress(address, amount)
        self.generate(self.nodes[0], 1)
        return txid

    def assert_spend_roundtrip(self, wallet, wallet_name):
        """The wallet holds exactly one UTXO; spend part of it back to the funding wallet."""
        node = self.nodes[0]

        utxos = wallet.listunspent()
        assert_equal(len(utxos), 1)
        utxo = utxos[0]
        assert utxo["spendable"], f"{wallet_name}: Dilithium UTXO not spendable"
        assert utxo["solvable"], f"{wallet_name}: Dilithium UTXO not solvable"

        dest = self.funding.getnewaddress()
        send_amount = Decimal("3")
        txid = wallet.sendtoaddress(dest, send_amount)

        # Transaction must be accepted to the mempool.
        assert txid in node.getrawmempool()

        # The Dilithium input must actually have been consumed.
        raw = node.getrawtransaction(txid, True)
        spent_inputs = {(vin["txid"], vin["vout"]) for vin in raw["vin"]}
        assert (utxo["txid"], utxo["vout"]) in spent_inputs

        # And it must confirm.
        self.generate(node, 1)
        tx = wallet.gettransaction(txid)
        assert_equal(tx["confirmations"], 1)

        # Funds arrive at the destination.
        received = self.funding.getreceivedbyaddress(dest)
        assert_equal(received, send_amount)

    def run_test(self):
        node = self.nodes[0]

        self.log.info("Set up descriptor funding wallet")
        node.createwallet(wallet_name="funding", descriptors=True)
        self.funding = node.get_wallet_rpc("funding")
        self.generatetoaddress(node, 110, self.funding.getnewaddress())

        self.log.info("Legacy plain wallet: receive Dilithium coins")
        node.createwallet(wallet_name="legacy_plain", descriptors=False)
        plain = node.get_wallet_rpc("legacy_plain")
        addr = plain.getnewdilithiumaddress()
        info = plain.getaddressinfo(addr)
        assert info["ismine"], "legacy_plain: dilithium address not ismine"
        assert info["solvable"], "legacy_plain: dilithium address not solvable"
        self.fund_address(addr, Decimal("10"))
        assert_equal(plain.getbalance(), Decimal("10"))

        self.log.info("Legacy plain wallet: spend Dilithium coins (issue #74)")
        self.assert_spend_roundtrip(plain, "legacy_plain")

        self.log.info("Legacy encrypted wallet: receive Dilithium coins")
        node.createwallet(wallet_name="legacy_enc", descriptors=False)
        enc = node.get_wallet_rpc("legacy_enc")
        enc.encryptwallet("pass")
        enc.walletpassphrase("pass", 600)
        enc_addr = enc.getnewdilithiumaddress()
        self.fund_address(enc_addr, Decimal("10"))
        assert_equal(enc.getbalance(), Decimal("10"))

        self.log.info("Legacy encrypted wallet: spend Dilithium coins (issue #74)")
        self.assert_spend_roundtrip(enc, "legacy_enc")

        self.log.info("Legacy encrypted wallet: locked wallet cannot spend Dilithium")
        enc.walletlock()
        locked_dest = self.funding.getnewaddress()
        assert_raises_rpc_error(
            -13,
            "Please enter the wallet passphrase with walletpassphrase first",
            enc.sendtoaddress,
            locked_dest,
            Decimal("1"),
        )
        enc.walletpassphrase("pass", 600)

        self.log.info("Control: ECDSA coin in a separate legacy wallet still spends")
        node.createwallet(wallet_name="legacy_ecdsa", descriptors=False)
        ecdsa = node.get_wallet_rpc("legacy_ecdsa")
        self.fund_address(ecdsa.getnewaddress(), Decimal("10"))
        self.assert_spend_roundtrip(ecdsa, "legacy_ecdsa")

        self.log.info("Legacy plain wallet: Dilithium spend survives restart")
        self.restart_node(0)
        node.loadwallet("funding")
        self.funding = node.get_wallet_rpc("funding")
        node.loadwallet("legacy_plain")
        plain = node.get_wallet_rpc("legacy_plain")
        addr2 = plain.getnewdilithiumaddress()
        self.fund_address(addr2, Decimal("5"))
        balance = plain.getbalance()
        assert_greater_than(balance, Decimal("0"))
        utxos = [u for u in plain.listunspent() if u["address"] == addr2]
        assert_equal(len(utxos), 1)
        dest = self.funding.getnewaddress()
        txid = plain.sendtoaddress(dest, Decimal("2"))
        assert txid in node.getrawmempool()
        self.generate(node, 1)
        assert_equal(plain.gettransaction(txid)["confirmations"], 1)


if __name__ == "__main__":
    WalletDilithiumLegacySpendTest().main()
