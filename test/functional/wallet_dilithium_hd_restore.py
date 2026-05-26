#!/usr/bin/env python3
# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test Dilithium HD wallet: same seed must yield the same Dilithium addresses."""

from test_framework.test_framework import BTQTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error
from test_framework.wallet_util import get_generate_key


class WalletDilithiumHDRestoreTest(BTQTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser, descriptors=False)

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()
        self.skip_if_no_bdb()

    def run_test(self):
        node = self.nodes[0]
        self.generate(node, 101)

        seed = get_generate_key()
        seed_wif = seed.privkey

        self.log.info("Create legacy HD wallet w1 and set a fixed seed")
        node.createwallet("w1", descriptors=False, blank=True)
        w1 = node.get_wallet_rpc("w1")
        w1.sethdseed(False, seed_wif)

        addr1 = w1.getnewdilithiumaddress()
        addr2 = w1.getnewdilithiumaddress()
        assert addr1 != addr2

        msg = "dilithium hd restore test"
        sig = w1.signmessagewithdilithium(addr1, msg)
        assert w1.verifydilithiumsignature(msg, addr1, sig)

        self.log.info("Create w2 with the same seed; first Dilithium address must match w1")
        node.createwallet("w2", descriptors=False, blank=True)
        w2 = node.get_wallet_rpc("w2")
        w2.sethdseed(False, seed_wif)

        addr1_restored = w2.getnewdilithiumaddress()
        assert_equal(addr1, addr1_restored)

        assert w2.verifydilithiumsignature(msg, addr1_restored, sig)

        self.log.info("getnewdilithiumaddress on blank wallet without HD seed must fail")
        node.createwallet("blank", descriptors=False, blank=True)
        blank = node.get_wallet_rpc("blank")
        assert_raises_rpc_error(-4, "Error: Keypool ran out", blank.getnewdilithiumaddress)


if __name__ == "__main__":
    WalletDilithiumHDRestoreTest().main()
