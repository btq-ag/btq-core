#!/usr/bin/env python3
# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Encrypted legacy HD Dilithium wallet: restart, unlock, sign (BTQ-AUDIT-001/002/009)."""

from test_framework.test_framework import BTQTestFramework
from test_framework.util import assert_raises_rpc_error
from test_framework.wallet_util import get_generate_key


class WalletDilithiumEncryptedRestartTest(BTQTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser, descriptors=False)

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()
        self.skip_if_no_bdb()

    def dilithium_address(self, wallet):
        created = wallet.getnewdilithiumaddress()
        assert isinstance(created, dict), created
        return created["address"]

    def run_test(self):
        node = self.nodes[0]
        self.generate(node, 101)

        passphrase = "audit-encrypted-dilithium"
        seed_wif = get_generate_key().privkey
        msg = "encrypted dilithium restart test"

        self.log.info("Create legacy HD wallet, derive Dilithium P2MR key, sign")
        node.createwallet("enc_dil", descriptors=False, blank=True)
        w = node.get_wallet_rpc("enc_dil")
        w.sethdseed(False, seed_wif)
        addr = self.dilithium_address(w)
        sig_before = w.signmessagewithdilithium(addr, msg)
        assert w.verifydilithiumsignature(msg, addr, sig_before)

        self.log.info("Encrypt wallet; unlock is required before signing (encryptwallet ends locked)")
        w.encryptwallet(passphrase)
        w.walletpassphrase(passphrase, 60)
        sig_unlocked = w.signmessagewithdilithium(addr, msg)
        assert w.verifydilithiumsignature(msg, addr, sig_unlocked)

        self.log.info("Lock wallet; signing must require unlock")
        w.walletlock()
        assert_raises_rpc_error(
            -13,
            "Please enter the wallet passphrase with walletpassphrase first",
            w.signmessagewithdilithium,
            addr,
            msg,
        )

        self.log.info("Restart node; encrypted Dilithium key must persist")
        self.restart_node(0)
        node.loadwallet("enc_dil")
        w = node.get_wallet_rpc("enc_dil")

        assert_raises_rpc_error(
            -13,
            "Please enter the wallet passphrase with walletpassphrase first",
            w.signmessagewithdilithium,
            addr,
            msg,
        )

        w.walletpassphrase(passphrase, 60)
        sig_after = w.signmessagewithdilithium(addr, msg)
        assert w.verifydilithiumsignature(msg, addr, sig_after)

        self.log.info("Second Dilithium P2MR address from same HD seed after restart")
        addr2 = self.dilithium_address(w)
        assert addr2 != addr
        sig2 = w.signmessagewithdilithium(addr2, msg)
        assert w.verifydilithiumsignature(msg, addr2, sig2)


if __name__ == "__main__":
    WalletDilithiumEncryptedRestartTest().main()
