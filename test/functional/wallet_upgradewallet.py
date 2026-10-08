#!/usr/bin/env python3
# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test current-wallet upgrade behavior without historical release binaries.

Restores the current-wallet cases from Bitcoin Core v26.0's upgrade test.
Historical non-HD/HD-split migrations still require authentic BTQ fixtures.
"""

from test_framework.test_framework import BTQTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error


class UpgradeWalletTest(BTQTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser)

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.wallet_names = []

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        node = self.nodes[0]
        for private_keys in (True, False):
            name = f"upgrade_{private_keys}"
            node.createwallet(name, descriptors=self.options.descriptors,
                              disable_private_keys=not private_keys)
            wallet = node.get_wallet_rpc(name)
            address = wallet.getnewaddress() if private_keys else None
            before = wallet.getwalletinfo()
            assert_equal(before["walletversion"], 169900)
            expected = {"wallet_name": name, "previous_version": 169900,
                        "current_version": 169900,
                        "result": "Already at latest version. Wallet version unchanged."}
            for args in ((), (0,), (169900,)):
                assert_equal(wallet.upgradewallet(*args), expected)
            for version in (60000, 169899):
                assert_equal(wallet.upgradewallet(version), {
                    "wallet_name": name, "previous_version": 169900,
                    "current_version": 169900,
                    "error": f"Cannot downgrade wallet from version 169900 to version {version}. Wallet version unchanged.",
                })
            assert_raises_rpc_error(-3, "not of expected type number", wallet.upgradewallet, "169900")
            after = wallet.getwalletinfo()
            for field in ("walletversion", "private_keys_enabled", "descriptors", "keypoolsize"):
                assert_equal(after[field], before[field])
            node.unloadwallet(name)
            node.loadwallet(name)
            wallet = node.get_wallet_rpc(name)
            assert_equal(wallet.upgradewallet(), expected)
            if address is not None:
                assert_equal(wallet.getaddressinfo(address)["ismine"], True)
            assert_equal(wallet.getwalletinfo()["private_keys_enabled"], private_keys)
            node.unloadwallet(name)


if __name__ == '__main__':
    UpgradeWalletTest().main()
