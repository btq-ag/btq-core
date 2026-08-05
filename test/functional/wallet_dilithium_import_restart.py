#!/usr/bin/env python3
# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Imported Dilithium keys must survive a restart.

An HD-derived Dilithium key is recoverable from the seed, so losing its database
record costs nothing. An imported key has no seed behind it: the wallet record is
the only copy, and losing it loses the coins. That makes this the one Dilithium
persistence path where the database round-trip is load-bearing, and it was the
one with no coverage.

Regression test for the loader failing an entire wallet as corrupt when a
Dilithium record arrived before any script pub key manager existed.
"""

from test_framework.test_framework import BTQTestFramework
from test_framework.util import assert_equal


class WalletDilithiumImportRestartTest(BTQTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser, descriptors=False)

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [["-deprecatedrpc=create_bdb", "-fallbackfee=0.0002"]]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()
        self.skip_if_no_bdb()

    def dilithium_secret_from_dump(self, wallet, path):
        """Pull the Dilithium secret out of a dumpwallet file.

        Dilithium secrets are far longer than any WIF, which is enough to pick
        them out without depending on the dump's column layout."""
        wallet.dumpwallet(path)
        with open(path, encoding="utf8") as f:
            for line in f:
                first = line.split(" ")[0]
                if len(first) > 1000:
                    return first
        raise AssertionError("no Dilithium secret found in dumpwallet output")

    def run_test(self):
        node = self.nodes[0]
        msg = "imported dilithium restart test"

        self.log.info("Create a source wallet and give it a Dilithium P2MR address")
        node.createwallet(wallet_name="source", descriptors=False)
        source = node.get_wallet_rpc("source")
        self.generatetoaddress(node, 110, source.getnewaddress())
        source_addr = source.getnewdilithiumaddress()["address"]

        secret = self.dilithium_secret_from_dump(
            source, self.nodes[0].datadir_path / "dump.txt")

        self.log.info("Import that key into a blank wallet, which has no seed to re-derive it from")
        node.createwallet(wallet_name="target", descriptors=False, blank=True)
        target = node.get_wallet_rpc("target")
        imported = target.importdilithiumkey(secret, "imported")

        # Same key in, same P2MR address out.
        assert_equal(imported["address"], source_addr)
        addr = imported["address"]

        sig_before = target.signmessagewithdilithium(addr, msg)
        assert target.verifydilithiumsignature(msg, addr, sig_before)

        self.log.info("Restart; the wallet must still load and the key must still be there")
        self.restart_node(0, extra_args=["-deprecatedrpc=create_bdb", "-fallbackfee=0.0002"])
        node = self.nodes[0]
        node.loadwallet("source")
        node.loadwallet("target")
        source = node.get_wallet_rpc("source")
        target = node.get_wallet_rpc("target")

        # Before the fix the wallet did not get this far: the load aborted with
        # "Wallet corrupted", stranding every other key in the file as well.
        info = target.getaddressinfo(addr)
        assert_equal(info["ismine"], True)
        assert_equal(info["iswatchonly"], False)

        sig_after = target.signmessagewithdilithium(addr, msg)
        assert target.verifydilithiumsignature(msg, addr, sig_after)

        self.log.info("The reloaded key can still spend, not just sign")
        source.sendtoaddress(addr, 1)
        self.generate(node, 1)
        balance = target.getbalance()
        assert balance > 0

        # Sweep the whole balance: the wallet is deliberately blank, so it has no
        # keypool to draw a change address from.
        txid = target.sendtoaddress(address=source.getnewaddress(), amount=balance,
                                    subtractfeefromamount=True)
        assert txid in node.getrawmempool()
        self.generate(node, 1)
        assert_equal(target.gettransaction(txid)["confirmations"], 1)


if __name__ == "__main__":
    WalletDilithiumImportRestartTest().main()
