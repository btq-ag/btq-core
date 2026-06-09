#!/usr/bin/env python3
# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""BTQ-AUDIT-006: descriptor wallet spends p2sh-segwit UTXOs to other types."""

from decimal import Decimal

from test_framework.test_framework import BTQTestFramework
from test_framework.util import assert_greater_than


class WalletP2shSegwitSpendTest(BTQTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser, descriptors=True)

    def set_test_params(self):
        self.num_nodes = 2
        self.extra_args = [
            ["-whitelist=noban@127.0.0.1"],
            ["-addresstype=bech32", "-whitelist=noban@127.0.0.1"],
        ]
        self.setup_clean_chain = True

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()
        self.skip_if_no_sqlite()

    def setup_network(self):
        self.setup_nodes()
        self.connect_nodes(0, 1)
        self.sync_all()

    def run_test(self):
        miner = self.nodes[0]
        self.generate(miner, 101)

        self.nodes[1].createwallet("p2sh_spend", descriptors=True)
        w = self.nodes[1].get_wallet_rpc("p2sh_spend")

        p2sh_addr = w.getnewaddress("", "p2sh-segwit")
        legacy_addr = w.getnewaddress("", "legacy")
        bech32_addr = w.getnewaddress("", "bech32")

        self.log.info("Fund p2sh-segwit address from miner")
        miner.sendtoaddress(p2sh_addr, Decimal("1.0"))
        self.generate(miner, 1)
        self.sync_all()

        self.log.info("Spend p2sh-segwit UTXO to legacy (fee estimation regression)")
        txid = w.sendtoaddress(legacy_addr, Decimal("0.4"))
        assert txid
        self.generate(miner, 1)
        self.sync_all()

        self.log.info("Spend remaining balance to bech32")
        balance = w.getbalance()
        assert_greater_than(balance, Decimal("0.5"))
        txid2 = w.sendtoaddress(bech32_addr, Decimal("0.1"))
        assert txid2
        self.generate(miner, 1)
        self.sync_all()


if __name__ == "__main__":
    WalletP2shSegwitSpendTest().main()
